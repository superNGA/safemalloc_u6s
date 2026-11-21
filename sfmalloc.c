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

/* Undef SF_TRANSPARENT to ourselves. */
#undef SF_TRANSPARENT

#include "sfmalloc.h"
#include <assert.h>
#include <stdarg.h>


// colorful terminal text
#define LOGCLR_RESET   "\033[0m"
#define LOGCLR_BOLD    "\033[1m"
#define LOGCLR_CYAN    "\033[36m"
#define LOGCLR_GREEN   "\033[32m"
#define LOGCLR_YELLOW  "\033[33m"
#define LOGCLR_RED     "\033[31m"
#define LOG(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)


#define ASSERT_STATE() assert(!sf.initialized)



/*
 * SafeMalloc main context structure.
 */
static struct safemalloc sf = {
	0, 0, 0, 0, 0,
#ifndef SFMALLOC_SAVE_SPACE
	0, 0,
#endif
	0, 0, 0, 0
};


// Safe Malloc mutex.
#ifdef _WIN32
static HANDLE ghMutex = NULL;
#else
static pthread_mutex_t sf_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int sfmtx_held = 0;

#ifdef _WIN32
    #define SF_MUTEX_LOCK_IMPL() WaitForSingleObject(ghMutex, INFINITE)
    #define SF_MUTEX_UNLOCK_IMPL() ReleaseMutex(ghMutex)
#else
    #define SF_MUTEX_LOCK_IMPL() pthread_mutex_lock(&sf_mutex) 
    #define SF_MUTEX_UNLOCK_IMPL() pthread_mutex_unlock(&sf_mutex)
#endif

#define SF_MUTEX_LOCK()   do{ SF_MUTEX_LOCK_IMPL();   sfmtx_held = 1; } while (0)
#define SF_MUTEX_UNLOCK() do{ SF_MUTEX_UNLOCK_IMPL(); sfmtx_held = 0; } while (0)
#define SFMTX_ASSERT_LOCK()   assert(sfmtx_held)
#define SFMTX_ASSERT_UNLOCK() assert(!sfmtx_held)



/* Forward declarations. */
static int hashtable_init(struct hashtable **ht);
static int hashtable_finish(struct hashtable *ht);
static int hashtable_add(struct hashtable **ht, struct address *key);
static void* hashtable_remove(struct hashtable **ht, uintptr_t address);
static void* hashtable_get(struct hashtable **ht, uintptr_t key);
#if HASHTABLE_ENABLE_STATS == 1
static void hashtable_print_stats(struct hashtable **ht);
#endif

/* Hash stuffs */
static int hashtable_cmp_ptr(uintptr_t key1, uintptr_t key2);
static uint64_t hashtable_splitmix64_hash(uintptr_t key, size_t size);



void sf_log_leaks()
{
	SFMTX_ASSERT_LOCK();
	LOG(LOGCLR_RED);
	struct hashtable *h = sf.ht;
	if (!h)
		return;

	for (struct address **lpp = h->bucket; lpp < h->bucket + h->capacity; ++lpp) {
		struct address *lp = *lpp;
		for (; lp; lp = lp->next) {
			LOG("memory leak of %zu bytes", lp->bytes);
#ifndef SFMALLOC_SAVE_SPACE
			LOG("file : %s, [line: %d]", lp->file, lp->line);
			LOG("func : %s", lp->func);
#endif // SFMALLOC_SAVE_SPACE
		}
	}
	LOG(LOGCLR_RESET);
}

/**
 * Deallocates all the memory at the process termination, warns
 * the user for possible memory leaks (if VERBOSE_MODE_2) and
 * shows the data gathere at the moment.
 */
static void sf_uninit(void)
{
	SF_MUTEX_LOCK();
	if (sf.verbose_mode == VERBOSE_MODE_3) {
		if (sf.ht->elements) {
			LOG(LOGCLR_RED);
			LOG("LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(");
			LOG("LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(");
			LOG("LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(LEAK-DETECTED:(");
		}
		else {
			LOG(LOGCLR_GREEN);
			LOG("NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:");
			LOG(")!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAK");
			LOG("S:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LEAKS:)!NO-LE");
		}
		LOG(LOGCLR_RESET);
		LOG("SafeMalloc");
		LOG("");
		LOG("total memory allocated : %zu", sf.total_allocated);
		LOG("peak memory consumption : %zu", sf.peak_memory);
		LOG("");
		LOG("malloc calls  : %zu", sf.malloc_calls);
		LOG("calloc calls  : %zu", sf.calloc_calls);
		LOG("realloc calls : %zu", sf.realloc_calls);
		LOG("free calls    : %zu", sf.free_calls);
	}
	if (sf.verbose_mode & VERBOSE_MODE_2)
		sf_log_leaks();

	hashtable_finish(sf.ht);
	sf.ht = NULL;
	SF_MUTEX_UNLOCK();

#ifdef _WIN32
	CloseHandle(ghMutex);
#else
	pthread_mutex_destroy(&sf_mutex);
#endif
}


static void sfm_fail_die(const char *msg, const char *file, const char *func, int line)
{
	static const char nullstr[] = "null";
	printf(LOGCLR_RED);
	{
		printf("=============== FATAL ERROR ===============");
		printf("%s", msg);
		printf("file : %s", file ? file : nullstr);
		printf("func : %s", func ? func : nullstr);
		printf("line : %d", line);
	}
	printf(LOGCLR_RESET);
	exit(SFMALLOC_EXIT_FAILURE);
}


static void sfm_warn(const char *msg, const char *file, const char *func, int line)
{
	static const char nullstr[] = "null";
	printf(LOGCLR_YELLOW);
	{
		printf("%s", msg);
		printf("file : %s", file ? file : nullstr);
		printf("func : %s", func ? func : nullstr);
		printf("line : %d", line);
	}
	printf(LOGCLR_RESET);
}


int sf_init(int vlvl, int er_rout)
{
#ifdef _WIN32
	if (!ghMutex) {
		ghMutex = CreateMutex(NULL, FALSE, NULL);
		if (!ghMutex) {
			fprintf(stderr, "CreateMutex error: %d\n", GetLastError());
			exit(SFMALLOC_EXIT_FAILURE);
		}
	}
#endif

	SF_MUTEX_LOCK();
	assert(!sf.initialized && "initializing sf twice!");
	assert(vlvl == VERBOSE_MODE_1 || vlvl == VERBOSE_MODE_2 || vlvl == VERBOSE_MODE_3);
	assert(er_rout == ON_ERROR_ABORT || er_rout == ON_ERROR_EXIT || er_rout == ON_ERROR_NULL);

	if (hashtable_init(&sf.ht) < 0)
		sfm_fail_die("sf_init: an error has ocurred while allocating hashtable.", NULL, NULL, NULL);

	sf.verbose_mode = vlvl;
	sf.on_error     = er_rout;
	sf.initialized  = 1;
	SF_MUTEX_UNLOCK();
	return 0;
}


static int record_alloc(struct address *p, size_t s, const char *file, const char *func, int line)
{
	p->address = (uintptr_t)(p+1);
	p->next    = NULL;
	p->bytes   = s;
#ifndef SFMALLOC_SAVE_SPACE
	p->file    = file;
	p->func    = func;
	p->line    = line;
#endif

	int ret = 0;
	SF_MUTEX_LOCK();
	{
		sf.total_allocated += s;
		sf.current_memory  += s;
		if (sf.current_memory > sf.peak_memory)
			sf.peak_memory = sf.current_memory;

		if (hashtable_add(&sf.ht, p) < 0)
			ret = 1;
	}
	SF_MUTEX_UNLOCK();
	return ret;
}


void *_sf_malloc(size_t size, const char *file, const char *func, int line)
{
	ASSERT_STATE();
	if (!size) {
		sfm_warn("[malloc] trying to zero bytes", file, func, line);
		return NULL;
	}

	struct address *p = malloc_fn(sizeof(struct address) + size);
	if (!p)
		return NULL;

	if (record_alloc(p, size, file, func, line) != 0) {
		free_fn(p);
		sfm_fail_die("[malloc] failed to record allocation", file, func, line);
	}

	SF_MUTEX_LOCK();
	++sf.malloc_calls;
	SF_MUTEX_UNLOCK();

	return p + 1;
}


void *_sf_xmalloc(size_t size ,const char *file, const char *func, int line)
{
	void *m = _sf_malloc(size, file, func, line);
	if (!m)
		exit(SFMALLOC_EXIT_FAILURE);
	return m;
}


void *_sf_calloc(size_t nmemb, size_t size, const char *file, const char *func, int line)
{
	ASSERT_STATE();
	if (size + 5 >= SIZE_MAX / nmemb) { // +5 cause off-by-one errors paranoia me.
		sfm_warn("[calloc] nmemb * size overflowed", file, func, line);
		return NULL;
	}

	size_t s = size * nmemb; // TODO : to size_t overflow check
	if (!s) {
		sfm_warn("[calloc] trying to zero bytes", file, func, line);
		return NULL;
	}

	struct address *p = calloc_fn(1, s + sizeof(struct address));
	if (!p)
		return NULL;

	if (record_alloc(p, s, file, func, line) != 0) {
		free_fn(p);
		sfm_fail_die("[calloc] failed to record allocation", file, func, line);
	}

	SF_MUTEX_LOCK();
	++sf.calloc_calls;
	SF_MUTEX_UNLOCK();

	return p + 1;
}


void *_sf_xcalloc(size_t nmemb, size_t size, const char *file, const char *func, int line)
{
	void *m = _sf_calloc(nmemb, size, file, func, line);
	if (!m)
		return NULL;
	return m;
}


void *_sf_realloc(void *ptr, size_t size, const char *file, const char *func, int line)
{
	ASSERT_STATE();
	if (!size) {
		sfm_warn("[realloc] trying to zero bytes", file, func, line);
		return NULL;
	}
	
	SF_MUTEX_LOCK();
	if (ptr) {
		struct address *p_old = hashtable_remove(&sf.ht, (uintptr_t)ptr);
		if (!p_old)
			sfm_fail_die("[realloc] failed to unregister old memory", file, func, line);
		sf.current_memory -= p_old->bytes;
	}
	SF_MUTEX_UNLOCK();

	struct address *p_new = realloc_fn(ptr, size);
	if (!p_new)
		return NULL;

	if (record_alloc(p_new, size, file, func, line) != 0) {
		free_fn(p_new);
		sfm_fail_die("[realloc] failed to record allocation", file, func, line);
	}

	SF_MUTEX_LOCK();
	++sf.realloc_calls;
	SF_MUTEX_UNLOCK();

	return p_new + 1;
}


void *_sf_xrealloc(void *ptr, size_t size, const char *file, const char *func, int line)
{
	void *m = _sf_realloc(ptr, size, file, func, line);
	if (!m)
		return NULL;
	return m;
}


void _sf_free(void *ptr, const char *file, const char *func, int line)
{
	ASSERT_STATE();
	if (ptr == NULL)
		sfm_fail_die("trying to free nullptr.", file, func, line);

	SF_MUTEX_LOCK();
	struct address *p = hashtable_remove(&sf.ht, (uintptr_t)ptr);
	++sf.free_calls;
#ifndef SFMALLOC_SAVE_SPACE
	sf.current_memory -= p->bytes;
#endif
	SF_MUTEX_UNLOCK();

	free_fn(p);
}

/*===========================================================================*
 *                                                                           *
 *                          HASHTABLE FUNCTIONS                              *
 *                                                                           *
 * From this point below all the macros, functions, structs belongs to the   *
 * hashtable.                                                                *
 *===========================================================================*/

/**
 * @brief Initializes the hashtable.
 *
 * @param ht Hashtable structure pointer to be
 * initialized.
 *
 * @return Returns 0 if success and a negative number
 * otherwise.
 */
static int hashtable_init(struct hashtable **ht)
{
	struct hashtable *out;
	out = calloc_fn(1, sizeof(struct hashtable));
	if (out == NULL)
		return (-1);

	out->capacity = HASHTABLE_DEFAULT_SIZE;
	out->elements = 0;
	out->bucket   = calloc_fn(out->capacity, sizeof(void *));
	if (out->bucket == NULL)
	{
		free_fn(out);
		return (-1);
	}

	*ht = out;
	return (0);
}

/**
 * @brief Deallocates the hashtable.
 *
 * @param ht Hashtable pointer.
 *
 * @return Returns 0 if success.
 */
static int hashtable_finish(struct hashtable *h)
{
	struct address *l_ptr;  /* List pointer. */
	size_t i;               /* Loop index.   */

	/* Invalid array. */
	if (h == NULL)
		return (-1);

	/*
	 * For each bucket, deallocates each list.
	 */
	for (i = 0; i < h->capacity; i++)
	{
		struct address *l_ptr_next;

		l_ptr = h->bucket[i];
		while (l_ptr != NULL)
		{
			l_ptr_next = l_ptr->next;
			free_fn(l_ptr);
			l_ptr = l_ptr_next;
		}
	}

	free_fn(h->bucket);
	free_fn(h);
	return (0);
}

/**
 * @brief Calculates the bucket size accordingly with the
 * current hash function set and the current hashtable
 * capacity.
 *
 * @param ht Hashtable pointer.
 * @param key Key to be hashed.
 *
 * @return Returns a value between [0,h->capacity] that
 * should be used to address the target bucket for the
 * hash table.
 */
static size_t hashtable_bucket_index(struct hashtable **ht, uintptr_t key)
{
	struct hashtable *h;  /* Hashtable.    */
	uint64_t hash;        /* Hash value.   */
	size_t index;         /* Bucket index. */

	/*
	 * Its important to note that the hashtable->capacity
	 * will always be power of 2, thus, allowing the &-1
	 * as a modulus operation.
	 */
	h     = *ht;
	hash  = hashtable_splitmix64_hash(key, h->key_size);
	index = hash & (h->capacity - 1);

	return (index);
}

/**
 * @brief Resize the hashtable @p ht for the new given size
 * @p new_size.
 *
 * @param ht Hashtable to be resized.
 * @param new_size New hashtable size, must be power of two.
 *
 * @return Returns 0 if success, negative number otherwise.
 */
static int hashtable_resize(struct hashtable **ht, size_t new_size)
{
	struct hashtable *h;       /* Hashtable.    */
	struct address *l_ptr;     /* List pointer. */
	struct address *l_ptr_nxt; /* List pointer. */
	size_t i;                  /* Loop index.   */
	size_t hash;               /* Hash value.   */
	size_t old_capacity;
	struct address **new_buckets;

	h = *ht;

	/* Hashtable exists?. */
	if (h == NULL)
		return (-1);

	/* Is size power of two?. */
	if (new_size & (new_size - 1))
		return (-1);

	/* Allocate new buckets. */
	new_buckets = calloc_fn(new_size, sizeof(void *));
	if (new_buckets == NULL)
		return (-1);

	old_capacity  = h->capacity;
	h->capacity   = new_size;

#if HASHTABLE_DEBUG
	h->collisions = 0;
#endif

	/* Move elements to it. */
	for (i = 0; i < old_capacity; i++)
	{
		l_ptr = h->bucket[i];
		while (l_ptr != NULL)
		{
			l_ptr_nxt = l_ptr->next;

			if (l_ptr->address != 0)
			{
				/* Recalculates the hash for each key inside the bucket. */
				hash = hashtable_bucket_index(ht, l_ptr->address);
				l_ptr->next = new_buckets[hash];
#if HASHTABLE_DEBUG
				if(new_buckets[hash] != NULL)
					h->collisions++;
#endif
				new_buckets[hash] = l_ptr;
			}

			l_ptr = l_ptr_nxt;
		}
	}

	free_fn(h->bucket);
	h->bucket = new_buckets;
	return (0);
}

/**
 * @brief Adds the current @p key and @p value into the hashtable.
 *
 * @param ht Hashtable pointer.
 * @param key Key to be added.
 * @param value Value to be added.
 *
 * The hash table will grows twice whenever it reaches
 * its threshold of 60% percent.
 *
 * @return Returns 0 if success and a negative number otherwise.
 */
static int hashtable_add(struct hashtable **ht, struct address *key)
{
	struct hashtable *h;    /* Hashtable.    */
	struct address *l_ptr;  /* List pointer. */
	size_t hash;            /* Hash value.   */

	h = *ht;

	/* Hashtable exists?. */
	if (h == NULL)
		return (-1);

	/* Asserts if we have space enough. */
	if (h->elements >= h->capacity*HASHTABLE_LOAD_FACTOR)
		if (hashtable_resize(ht, h->capacity << 1) < 0)
			return (-1);

	/* Hash it. */
	hash = hashtable_bucket_index(ht, key->address);

	/*
	 * Loops through the list in order to see if the key already exists,
	 * if so, do nothing, just returns. However, this should not occur.
	 */
	l_ptr = h->bucket[hash];
	while (l_ptr != NULL)
	{
		if (l_ptr->address != 0 && hashtable_cmp_ptr(l_ptr->address, key->address) == 0)
			return (0);

		l_ptr = l_ptr->next;
	}

	/* Fill the entry. */
	key->next = h->bucket[hash];

	/* Add into the top of the buckets list. */
#if HASHTABLE_DEBUG
	if (h->bucket[hash] != NULL)
		h->collisions++;
#endif

	h->bucket[hash] = key;
	h->elements++;

	return (0);
}

/**
 * @brief Removes the address @p address from the hashtable.
 *
 * @param ht Hashtable pointer.
 * @param address Address to be removed.
 *
 * The hash table will shrinks twice whenever it reaches down
 * its threshold of 60% percent.
 *
 * @return If success, returns a valid pointer to 'struct address',
 * otherwise, returns NULL.
 */
static void *hashtable_remove(struct hashtable **ht, uintptr_t address)
{
	struct hashtable *h;    /* Hashtable.    */
	struct address *l_ptr;  /* List pointer. */
	struct address **prev;  /* Prev pointer. */
	size_t hash;            /* Hash value.   */

	h = *ht;

	/* Hashtable exists?. */
	if (h == NULL)
		return (NULL);

	/* Shall we contract the hashtable?. */
	if (h->elements <= ((h->capacity >> 1)*HASHTABLE_LOAD_FACTOR) &&
		h->capacity > HASHTABLE_DEFAULT_SIZE)
	{
		if (hashtable_resize(ht, h->capacity >> 1) < 0)
			return (NULL);
	}

	/* Hash it. */
	hash = hashtable_bucket_index(ht, address);

	/*
	 * Loops through the list in order to see if the key exists,
	 * if so, removes.
	 */
	prev  = &h->bucket[hash];
	l_ptr = h->bucket[hash];
	while (l_ptr != NULL)
	{
		if (l_ptr->address != 0 && hashtable_cmp_ptr(l_ptr->address, address) == 0)
		{
			*prev = l_ptr->next;

			h->elements--;
#if HASHTABLE_DEBUG
			if (l_ptr != h->bucket[hash])
				if (h->collisions)
					h->collisions--;
#endif
			return (l_ptr);
		}

		prev  = &l_ptr->next;
		l_ptr = l_ptr->next;
	}

	return (NULL);
}

/**
 * @brief Retrieves the value belonging to the parameter @p key.
 *
 * @param ht Hashtable pointer.
 * @param key Corresponding key to be retrieved.
 *
 * @return Returns the value belonging to the @p key, or NULL
 * if not found.
 */
static void* hashtable_get(struct hashtable **ht, uintptr_t key)
{
	struct hashtable *h;    /* Hashtable.    */
	struct address *l_ptr;  /* List pointer. */
	uint64_t hash;          /* Hash value.   */

	h = *ht;

	/* Hashtable exists and has at least one element?. */
	if (h == NULL || h->elements < 1)
		return (NULL);

	/* Hash it. */
	hash = hashtable_bucket_index(ht, key);

	/*
	 * Loops through the list in order to see if the key exists,
	 * if so, gets the value.
	 */
	l_ptr = h->bucket[hash];
	while (l_ptr != NULL)
	{
		if (l_ptr->address != 0 && hashtable_cmp_ptr(l_ptr->address, key) == 0)
			return (l_ptr);

		l_ptr = l_ptr->next;
	}

	/* If not found, return NULL. */
	return (NULL);
}

/*===========================================================================*
 *                         -.- Hash functions -.-                            *
 *===========================================================================*/

/**
 * @brief Generic key comparator.
 *
 * Compares two given pointers and returns a negative, 0 or positive
 * number if less than, equal or greater for the keys specified.
 *
 * @param key1 First key to be compared.
 * @param key2 Second key to be compared.
 *
 * @returns Returns a negative, 0 or positive number if @p key1
 * is less than, equal or greater than @p key2.
 */
static int hashtable_cmp_ptr(uintptr_t key1, uintptr_t key2)
{
	return ( (int)( key1 - key2 ) );
}

/*----------------------------------------------------------------------------*
 * Based in splitmix64, from Better Bit Mixing - Improving on MurmurHash3's   *
 * 64-bit Finalizer                                                           *
 *                                                                            *
 * Link:http://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html*
 * ----                                                                       *
 * I've found this particular interesting and the results showed that the     *
 * resulting hash is very close to MurMur3 ;-).                               *
 *----------------------------------------------------------------------------*/

/**
 * @brief splitmix64 based algorithm.
 *
 * This algorithm seems to be the 'Mix13' for the link above.
 *
 * @param key Key to be hashed.
 * @param size Pointer size (unused here).
 *
 * @return Returns a hashed number for the @p key argument.
 */
static uint64_t hashtable_splitmix64_hash(uintptr_t key, size_t size)
{
	uint64_t x; /* Hash key. */
	((void)size);

	x = (uint64_t)key;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
	x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
	x = x ^ (x >> 31);
	return (x);
}
