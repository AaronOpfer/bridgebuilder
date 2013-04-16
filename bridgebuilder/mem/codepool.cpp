#include "codepool.h"
#include <stdlib.h>
#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #include <Windows.h>
#else
 #include <unistd.h>
 #include <sys/mman.h>
#endif


struct pagedata_t {
	void* page;
	// bit sequence goes
	// dfdfdfdf
	// where d is whether it's double and f is whether it is free.
	unsigned long bitfield[];
};

static size_t numPages = 0;
static size_t pageSize = 0;
static size_t numPageSlices = 0;
static size_t pageDataUnitSize = 0;



// allocated off heap memory. Technically a pointer to an array of
// pagedata_t's, but because pagedata_t's size depends on pageSize
// and cannot be determined at compile time (even though it's almost
// always 4KB),  it's probably easier on everybody we just make this
// a void ptr, since we won't be able to use an index anyway, and will
// be typecasting the hell out of this.
static void* pageMetaDataArray; 


__inline void* codepool_align_pointer (void* ptr, size_t alignment) {
	return (void*)(size_t(ptr) & ~(alignment-1));
}

__inline bool pointer_to_sub (void* ptr, unsigned long** word,unsigned char* bitNum) {
	size_t pageNum,ptrDistance;
	
	pagedata_t* pageMetaData;

	void* pagePtr = codepool_align_pointer(ptr,pageSize);

	ptrDistance = size_t(ptr) - size_t(pagePtr);

	for (pageNum = numPages-1; pageNum >= 0; pageNum--) {

		// Setting up pointer using maths, because we can't use an
		// array index with variably-sized data
		pageMetaData = (pagedata_t*)((char*)pageMetaDataArray 
	                                         + pageDataUnitSize*pageNum);
		
		// we have found the page!
		if (pagePtr == pageMetaData->page) {
			*bitNum = (ptrDistance&64)/16;
			*word = &pageMetaData->bitfield[ptrDistance/64];
			return true;
		}
	}
	// didn't find the page metadata.
	return false;
}

__inline void* sub_to_pointer (pagedata_t* pageMetaData, size_t wordNum, size_t bitNum) {

	return (void*)((size_t)pageMetaData->page 
	               + (wordNum*sizeof(unsigned long)+bitNum)*16);
}
	

__inline void sub_set_allocated (unsigned long *word, unsigned char index, bool isDouble) {
	index *= 2;

	// clear out the free bits
	*word &= ~(3<<index);
	// add the double bit if necessary
	if(isDouble) {
		*word |= 2 << index;
	}
}

__inline void sub_set_free (unsigned long *word, unsigned char index) {
	index *= 2;

	// clear out the double bit
	*word &= ~(3 << index);
	// set the free bit
	*word |= 1 << index;
}

__inline bool sub_is_free (unsigned long word, unsigned char index) {
	return ((word&(1<<(index*2))) != 0);
}

__inline bool sub_is_dbl (unsigned long word, unsigned char index) {
	return ((word&(2<<(index*2))) != 0);
}


bool codepool_addpage (void) {
	pagedata_t* pageMetaData;
	// calculate new size of the allocator meta-data
	size_t newSize = pageDataUnitSize * (numPages+1);

	// allocate or resize our memory
	if (numPages == 0) {
		pageMetaDataArray = (void*)malloc(newSize);
	} else {
		pageMetaDataArray = (void*)realloc(pageMetaDataArray, newSize);
	}

	// increment the page count
	numPages++;

	// initialize our added data and allocate a virtual memory page
	// for it (I really hate typecasting sometimes.)
	pageMetaData = (pagedata_t*)((char*)pageMetaDataArray 
	                              + pageDataUnitSize*(numPages-1));

	#ifdef _WIN32
		pageMetaData->page = VirtualAlloc(NULL,
		                                  pageSize,
		                                  MEM_COMMIT | MEM_RESERVE,
		                                  PAGE_EXECUTE_READWRITE);
	#else
		pageMetaData->page = mmap(NULL,
		                          pageSize,
		                          PROT_READ | PROT_WRITE | PROT_EXEC,
		                          MAP_ANONYMOUS,
		                          -1, 0)
	#endif

	// NULL is for WIN32 and -1 is for *nix, Although thanks to page
	// boundaries -1 isn't valid anywhere. more ifdefs would disrupt
	// code clarity, so I'm leaving it as it is here.
	if (pageMetaData == NULL || pageMetaData == (void*) -1 ) {
		return false;
	}

	// Initialize the memory page with 0xCC, the INT3 instruction. A
	// good trap for errant execution.
	memset(pageMetaData->page, 0xCC, pageSize);

	// initialize the bitfield to free
	memset(pageMetaData->bitfield, 0x55, 
	       pageDataUnitSize - sizeof(pagedata_t));

	// remove write permission from the page
	codepool_lock(pageMetaData->page);

	// That ought to do it!
	return true;
}

bool codepool_init (void) {
	// determine the size of a single page of memory
	#ifdef WIN32
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		pageSize = sysInfo.dwPageSize;
	#else
		pageSize = sysconf(_SC_PAGESIZE);
	#endif

	// calculate the size of the pageData struct
	pageDataUnitSize = sizeof(pagedata_t) + pageSize/64;
	numPageSlices = pageSize/128;

	// add the first page
	return codepool_addpage();
}
// FIXME: We don't allocate pages over word boundaries in the bit
// fields. Is that okay?
void* codepool_alloc (size_t newCodeSize) {

	bool doublePage = false;

	pagedata_t* pageMetaData;
	size_t pageNum,wordNum;
	unsigned char bitNum;
	unsigned long *bits;

	// ensure parameter is in acceptable range
	if (newCodeSize > 32) {
		return false;
	}
	// is this a double page?
	if (newCodeSize > 16) {
		doublePage = true;
	}	

	// initialize codepool if it has not been already
	if (pageDataUnitSize == 0) {
		// some memory failed to init or something?!?
		if (codepool_init() == false) {
			return 0;
		}
	}
	
	// we're going to iterate through all the page metadata until we
	// find a large enough free section. Let's start from the most
	// recently allocated page. This is probably fastest. maybe.
	// (who's got time for testing?)
	for (pageNum = numPages-1; pageNum >= 0; pageNum--) {
		// Setting up pointer using maths, because we can't use an
		// array index with variably-sized data

		pageMetaData = (pagedata_t*)((char*)pageMetaDataArray 
	                                         + pageDataUnitSize*pageNum);

		// now we are going to scan for a free index. We scan one word a
		// time for a little extra speed.
		for (wordNum = 0; wordNum < numPageSlices/4; wordNum++) {
			bits = &pageMetaData->bitfield[wordNum];

			// if none of these bits are marked free, move on
			if ((*bits&0x55) == 0) {
				continue;
			}

			// There is at least one free page in here, let's look for it.
			// the weird math in the expression below is so we take into
			// account a double page's need for two consecutive pages.
			for (bitNum = 0; bitNum < (4 - doublePage); bitNum++) {
				if (sub_is_free(*bits,bitNum) == false) {
					continue;
				}

				if (doublePage == true) {
					if (sub_is_free(*bits,bitNum+1) == false) {
						continue;
					}
					// set second pagelet to free=false,dbl=false
					sub_set_allocated(bits,bitNum+1,false);
				}
				sub_set_allocated(bits,bitNum,doublePage);

				// return its address
				return sub_to_pointer(pageMetaData,wordNum,bitNum);
			}
		}
	}
	// There are no free subpages suitable for our purposes,
	// So let's make a new one!
	if (codepool_addpage() == false) {
		return 0;
	}

	// Let's try it all over again. Since we scan the most recently
	// created pages first, and the new page is entirely free, this
	// call is O(1)
	return codepool_alloc(newCodeSize);
}

void codepool_free (void* codeMemory) {
	unsigned long* bits;
	unsigned char bitNum;

	// it's possible this address isn't actually one of our bridges.
	// This is because we are smart sometimes and detect when a
	// real bridge isn't necessary.
	if (pointer_to_sub(codeMemory,&bits,&bitNum) == false) {
		return;
	}

	// double-free! really bad!
	if (sub_is_free(*bits,bitNum) == true) {
		return;
	}

	codepool_unlock(codeMemory);

	// clear the memory
	memset(codeMemory, 0xCC, sub_is_dbl(*bits,bitNum) ? 32 : 16);

	codepool_lock(codeMemory);
	
	sub_set_free(bits,bitNum);
}

void codepool_can_write (void* codeMemory, bool canWrite) {
	#ifdef _WIN32
		DWORD oldProtect;

		VirtualProtect(codepool_align_pointer(codeMemory,pageSize),
		               pageSize,
		               (canWrite==true) ?
                              PAGE_EXECUTE_READWRITE :
                              PAGE_EXECUTE_READ,
					&oldProtect
		);
	#else
		mprotect(codepool_align_pointer(codeMemory,pageSize),
		         pageSize,
		         (canWrite==true) ?
		              PROT_READ | PROT_WRITE | PROT_EXEC :
		              PROT_READ | PROT_EXEC
		);
	#endif
}

void codepool_lock (void* codeMemory) {
	codepool_can_write(codeMemory,false);
}

void codepool_unlock (void* codeMemory) {
	codepool_can_write(codeMemory,true);
}