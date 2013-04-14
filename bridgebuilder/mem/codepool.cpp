#include "codepool.h"
#include <stdlib.h>
#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #include <Windows.h>
#else
 #include <unistd.h>
 #include <sys/mman.h>
#endif

// this macro subtracts from ptr so that it is aligned with alignment,
// assuming alignment is a power of two
#define ALIGN_PTR(ptr,alignment) (void*)(((size_t)ptr) & ~((alignment)-1))

#define SUB_IS_FREE(word,index) word&(1<<(index)*2)
#define SUB_SET_ALLOC(word,index,dbl) word=word&(~(3<<((index)*2)));if(dbl)word=word|((dbl*2)<<((index)*2))
#define SUB_SET_FREE(word,index) word=word&(~(3<<((index)*2)))|1<<((index)*2))

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

void* codepool_alloc (size_t newCodeSize) {

	bool doublePage = false;

	pagedata_t* pageMetaData;
	size_t j,k;
	unsigned char b;

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
	// find a large enough free section
	for (j = 0; j < numPages; j++) {
		pageMetaData = (pagedata_t*)((char*)pageMetaDataArray 
	                         + pageDataUnitSize*j);

		// now we are going to scan for a free index
		k = 0;
		while (k < numPageSlices/4) {
			// we can scan one word at a time for a little extra speed
			if ((pageMetaData->bitfield[k]&0x55) != 0) {
				// There is at least one free page in here
				for (b = 0; b < (4 - doublePage); b++) {
					if (SUB_IS_FREE(pageMetaData->bitfield[k],b) && (doublePage == false || SUB_IS_FREE(pageMetaData->bitfield[k],b+1))) {
						// we have found a free spot!
						SUB_SET_ALLOC(pageMetaData->bitfield[k],b,doublePage);

						// return its address
						return (void*)((size_t)pageMetaData->page +
						               (k*sizeof(unsigned long)+b)*16);
					}
				}
			}
			k++;
		}
	}
	return NULL;
}

void codepool_free (void* codeMemory);

void codepool_can_write (void* codeMemory, bool canWrite) {
	#ifdef _WIN32
		DWORD oldProtect;

		VirtualProtect(ALIGN_PTR(codeMemory,pageSize),
		               pageSize,
		               (canWrite==true) ?
                              PAGE_EXECUTE_READWRITE :
                              PAGE_EXECUTE_READ,
					&oldProtect
		);
	#else
		mprotect(ALIGN_PTR(codeMemory,pageSize),
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