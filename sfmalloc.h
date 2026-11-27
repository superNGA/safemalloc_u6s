/*
 * MIT License
 *
 * Copyright (c) 2020 Davidson Francis <davidsondfgl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SFMALLOC_H

	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <inttypes.h>
#ifdef _WIN32
    #include <windows.h>
#else
	#include <unistd.h>
	#include <pthread.h>
	#include <sys/mman.h> // mmap() support
#endif
	#include <math.h>
	#include <signal.h>
	#include <sys/types.h>


/*
* Define the define below (or before including this header)
* in order to reduce the memory footprint of safemalloc
* internal structures.
*
* #define SFMALLOC_SAVE_SPACE
*/



/**
* Custom exit error code.
*
* Change if you want to another code other than
* 'EXIT_FAILURE'. Useful if you do not want
* to mix with return codes already used
* by the application.
*/
#ifndef SFMALLOC_EXIT_FAILURE
	#define SFMALLOC_EXIT_FAILURE (EXIT_FAILURE)
#endif



/**
* Structure that holds per-allocation infos required
* in order to retrieve the address and print
* useful information in case of error.
*
* Each [m,c,re]alloc call will allocate a new structure.
*/
struct address
{
	/* Relevant info. */
	uintptr_t address;
	struct address *next;

	size_t bytes;

	/* Additional info. */
#ifndef SFMALLOC_SAVE_SPACE
	const char *file;
	const char *func;
	int line;
#endif
};


/* Behaviour macros. */
#define VERBOSE_NONE      (0) // No messages
#define VERBOSE_DESC      (1 << 0) // Only memory usages metrics
#define VERBOSE_LEAK_DUMP (1 << 1) // Dump all detected leaks
#define VERBOSE_FULL      (VERBOSE_DESC | VERBOSE_LEAK_DUMP) // Max verbose


/**
* SafeMalloc main structure
*
* This struct holds the main data for the library
* be able to work. Note that this is not dynamically
* allocated and its declaration belongs to sfmalloc.c
* as a global variable.
*/
struct safemalloc
{
	/* Hashtable. */
	struct hashtable *ht;

	int initialized;

	/* Behaviour. */
	int verbose_mode;
	int on_error;

	size_t total_allocated;
	size_t peak_memory;
	size_t current_memory;

	// NOTE : Only successful calls are recorded.
	size_t malloc_calls;
	size_t calloc_calls;
	size_t realloc_calls;
	size_t free_calls;
	size_t mmap_calls;
	size_t munmap_calls;
};

/* External functions. */
extern int sf_init(int verbose_mode);
extern void sf_uninit(void);


/**
* Macros that expands to the real functions
*
* These macros help to obtain allocation info at compile time
* like: filename, function name and line number.
*/
#ifndef SFMALLOC_SAVE_SPACE
	#define sf_malloc(size) _sf_malloc((size), __FILE__, __func__, __LINE__)
	#define sf_xmalloc(size) _sf_xmalloc((size), __FILE__, __func__, __LINE__)

	#define sf_calloc(nmemb, size) _sf_calloc((nmemb), (size), __FILE__, __func__, __LINE__)
	#define sf_xcalloc(nmemb, size) _sf_xcalloc((nmemb), (size), __FILE__, __func__, __LINE__)
	
	#define sf_realloc(ptr, size) _sf_realloc((ptr), (size), __FILE__, __func__, __LINE__)
	#define sf_xrealloc(ptr, size) _sf_xrealloc((ptr), (size), __FILE__, __func__, __LINE__)

	#define sf_free(ptr) _sf_free((ptr), __FILE__, __func__, __LINE__)

	#define sf_mmap(addr, len, prot, flags, fd, offset) _sf_mmap(addr, len, \
			prot, flags, fd, offset, __FILE__, __func__, __LINE__)
	#define sf_xmmap(addr, len, prot, flags, fd, offset) _sf_mmap(addr, len, \
			prot, flags, fd, offset, __FILE__, __func__, __LINE__)
	#define sf_munmap(addr, len) _sf_munmap(addr, len, __FILE__, __func__, __LINE__)
#else
	#define sf_malloc(size) _sf_malloc((size), 0, 0, 0)
	#define sf_xmalloc(size) _sf_xmalloc((size), 0, 0, 0)

	#define sf_calloc(nmemb, size) _sf_calloc((nmemb), (size), 0, 0, 0)
	#define sf_xcalloc(nmemb, size) _sf_xcalloc((nmemb), (size), 0, 0, 0)

	#define sf_realloc(ptr, size) _sf_realloc((ptr), (size), 0, 0, 0)
	#define sf_xrealloc(ptr, size) _sf_xrealloc((ptr), (size), 0, 0, 0)

	#define sf_free(ptr) _sf_free((ptr), 0, 0, 0)

	#define sf_mmap(addr, len, prot, flags, fd, offset) _sf_mmap(addr, len, prot, flags, fd, offset, 0, 0, 0)
	#define sf_xmmap(addr, len, prot, flags, fd, offset) _sf_mmap(addr, len, prot, flags, fd, offset, 0, 0, 0)
	#define sf_munmap(addr, len) _sf_munmap(addr, len, 0, 0, 0)
#endif
void *_sf_malloc(size_t size, const char *file, const char *func, int line);
void *_sf_xmalloc(size_t size, const char *file, const char *func, int line);

void *_sf_calloc(size_t nmemb, size_t size, const char *file, const char *func, int line);
void *_sf_xcalloc(size_t nmemb, size_t size, const char *file, const char *func, int line);

void *_sf_realloc(void *ptr, size_t size, const char *file, const char *func, int line);
void *_sf_xrealloc(void *ptr, size_t size, const char *file, const char *func, int line);

void _sf_free(void *ptr, const char *file, const char *func, int line);

void *_sf_mmap(void *__addr, size_t __len, int __prot, int __flags, int __fd, __off_t __offset,
		const char *file, const char *func, int line);
void *_sf_xmmap(void *__addr, size_t __len, int __prot, int __flags, int __fd, __off_t __offset,
		const char *file, const char *func, int line);
void _sf_munmap(void *__addr, size_t __len, const char *file, const char *func, int line);

// View into the internal variables.
size_t sf_memusg();         // get total memory allocated right now (via safe malloc)
size_t sf_peak_memusg();    // get highest memory usage at any point till this point
size_t sf_total_memusg();   // total memory allocated till this point
size_t sf_ncalls_malloc();  // number of sf_malloc calls made 
size_t sf_ncalls_calloc();  // number of sf_calloc calls made
size_t sf_ncalls_realloc(); // number of sf_realloc calls made
size_t sf_ncalls_free();    // number of sf_free calls made
size_t sf_ncalls_mmap();    // number of sf_mmap calls made
size_t sf_ncalls_munmap();  // number of sf_munmap calls made



/*===========================================================================*
 *                                                                           *
 *                          HASHTABLE DEFINITIONS                            *
 *                                                                           *
 * From this point below all the macros, functions, structs belongs to the   *
 * hashtable definition.                                                     *
 *===========================================================================*/

	/**
	 * @brief Hashtable initial size.
	 */
	#define HASHTABLE_DEFAULT_SIZE 16

	/**
	 * @brief Load factor
	 */
	#define HASHTABLE_LOAD_FACTOR 0.6f

	/**
	 * @brief Enable additional debug data, like collision counting.
	 */
#ifndef SFMALLOC_SAVE_SPACE
	#define HASHTABLE_DEBUG 1
#endif

	/**
	 * @brief Enable (or not) hashtable status usage
	 */
	#define HASHTABLE_ENABLE_STATS 0

	/**
	 * @brief Hashtable structure.
	 */
	struct hashtable
	{
		struct address **bucket; /* Bucket list.      */
		size_t capacity;         /* Current capacity. */
		size_t elements;         /* Current elements. */
		ssize_t key_size;        /* Hash key size.    */

#if HASHTABLE_DEBUG
		size_t collisions;       /* Collisions count. */
#endif
	};

#endif /* SAFEMALLOC_H */
