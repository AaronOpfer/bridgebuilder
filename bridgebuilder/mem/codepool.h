/**
 * codepool.h
 *
 * Memory page sharing for dynamically created code
 *
 * The purpose of this code comes as a result of the limited granularity
 * of memory allocating functionality for runtime-generated code.
 * Permissions (read/write/exec) for memory pages is typically set on
 * a per-page basis. That means that heap functions are innappropriate
 * for allocating runtime-generated code. In addition, the naive
 * implementation would use a memory allocator (VirtualAlloc on win,
 * mmap on *nix) separately for each hook. However, these functions will
 * always allocate at least 4KB of memory at a time (one page). This means
 * that creating 10 bridges of 25 bytes a piece (a worst-case estimate) 
 * would instead cost 40KB, 163 times more memory! It's not hard to imagine
 * an application setting a hook on every exported function of a system 
 * library, would could result in 100s of hooks. If this application also
 * injected a library in every process on the system, we could then 
 * multiply this consumed memory figure by 50 or 100, and then it becomes
 * clear this is a real issue.
 *
 * This code is NOT thread-safe.
 *
 **/

#pragma once
#include <stddef.h> // size_t

/**
 * codepool_alloc
 *
 * Returns a pointer to a block of memory newCodeSize bytes in length
 * appropriate for dynamically generated code to be written to.
 *
 * Currently, due to how the virtual memory pool is sliced, newCodeSize
 * must be less than 32 bytes.
 *
 * Note that this function does not remove write-protection from the
 * memory page this memory was sliced out of, if it was present. Also,
 * if a new memory page had been allocated the returned memory, it will
 * default to being read-only. Therefore, it is required to use the
 * codepool_unlock and codepool_lock functions before and after writing
 * to this pointer.
 *
 * @param newCodeSize  the size of writeable code to be allocated.
 *
 * @return  A pointer to locked memory from the code pool, appropriate
 *          for dynamically generated code.
 *
 **/

void* codepool_alloc (size_t newCodeSize);

/**
 * codepool_free
 *
 * Returns the memory used by codeMemory to the pool.
 *
 * @param codeMemory  The memory to return to the pool.
 */

void codepool_free (void* codeMemory);

/**
 * codepool_lock
 *
 * Write-protects the memory page codeMemory is located in. This function
 * should be called after the dynamically generated code has been written
 * and no more modification is necessary. 
 *
 * Write-protecting code is generally a good idea. It allows bugs to be
 * much more quickly noticed (esp. w/ execute disable CPU feature), and
 * it generally makes the pages look normal to antivirus heuristics and
 * rootkit detectors, which tend to pay special attention to RWE pages.
 */

void codepool_lock (void* codeMemory);

/**
 * codepool_unlock
 *
 * Allows writes to codeMemory location. This should be called before
 * writing to codeMemory, and then once writing is complete, one should
 * call codepool_lock to again write-protect the memory (the benefits of
 * write-protecting code memory are discussed in codepool_lock's comment).
 *
 */

void codepool_unlock (void* codeMemory);