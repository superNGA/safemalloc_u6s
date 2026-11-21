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
* Custom allocator, if want
*/
#define malloc_fn   malloc
#define calloc_fn   calloc
#define realloc_fn  realloc
#define free_fn     free

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

/*
* Transparent usage.
*
* Enables transparent usage of safemalloc in the project.
* This flag can be enabled either by a define statement
* or by a 'CFLAGS', with -DSF_TRANSPARENT.
*/
#ifdef SF_TRANSPARENT
	#define malloc  sf_malloc
	#define calloc  sf_calloc
	#define realloc sf_realloc
	#define free    sf_free
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
#define VERBOSE_MODE_1  1 /* Suppress all messages.      */
#define VERBOSE_MODE_2  2 /* Only errors messages.       */
#define VERBOSE_MODE_3  (VERBOSE_MODE_1 & VERBOSE_MODE_2) /* Full verbose.               */
#define ON_ERROR_ABORT  1 /* Abort if any error.         */
#define ON_ERROR_EXIT   2 /* Exit if any error.          */
#define ON_ERROR_NULL   4 /* Just returns NULL if error. */


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
	};

	/* External functions. */
	extern void sf_free_all(void);
	extern int sf_init(int verbose_mode, int on_error);

#ifndef SF_DISABLE_SIGNAL_HANDLERS
	extern void sf_reg_sigint(void (*handler)(int));
	extern void sf_reg_sigterm(void (*handler)(int));
#endif


	/**
	 * Macros that expands to the real functions
	 *
	 * These macros help to obtain allocation info at compile time
	 * like: filename, function name and line number.
	 */
#ifndef SFMALLOC_SAVE_SPACE
	#define sf_malloc(size)        _sf_malloc((size), __FILE__, __func__, __LINE__)
	#define sf_xmalloc(size)        _sf_xmalloc((size), __FILE__, __func__, __LINE__)

	#define sf_calloc(nmemb, size) _sf_calloc((nmemb), (size), __FILE__, __func__, __LINE__)
	#define sf_xcalloc(nmemb, size) _sf_xcalloc((nmemb), (size), __FILE__, __func__, __LINE__)
	
	#define sf_realloc(ptr, size)  _sf_realloc((ptr), (size), __FILE__, __func__, __LINE__)
	#define sf_xrealloc(ptr, size)  _sf_xrealloc((ptr), (size), __FILE__, __func__, __LINE__)

	#define sf_free(ptr)           _sf_free((ptr), __FILE__, __func__, __LINE__)
#else
	#define sf_malloc(size) _sf_malloc((size), 0, 0, 0)
	#define sf_xmalloc(size) _sf_xmalloc((size), 0, 0, 0)

	#define sf_calloc(nmemb, size) _sf_calloc((nmemb), (size), 0, 0, 0)
	#define sf_xcalloc(nmemb, size) _sf_xcalloc((nmemb), (size), 0, 0, 0)

	#define sf_realloc(ptr, size) _sf_realloc((ptr), (size), 0, 0, 0)
	#define sf_xrealloc(ptr, size) _sf_xrealloc((ptr), (size), 0, 0, 0)

	#define sf_free(ptr) _sf_free((ptr), 0, 0, 0)
#endif
extern void *_sf_malloc(size_t size, const char *file, const char *func, int line);
extern void *_sf_xmalloc(size_t size, const char *file, const char *func, int line);

extern void *_sf_calloc(size_t nmemb, size_t size, const char *file, const char *func, int line);
extern void *_sf_xcalloc(size_t nmemb, size_t size, const char *file, const char *func, int line);

extern void *_sf_realloc(void *ptr, size_t size, const char *file, const char *func, int line);
extern void *_sf_xrealloc(void *ptr, size_t size, const char *file, const char *func, int line);

extern void _sf_free(void *ptr, const char *file, const char *func, int line);



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
