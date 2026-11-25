#include "../../sfmalloc.h"


int main(int argc, char **argv) 
{
	sf_init(VERBOSE_FULL);
	void *m = NULL;
	// m = sf_malloc(10);
	// for (int i = 1; i <= 10; ++i) {
	// 	m = sf_realloc(m, i * 64);
	// }
	// m = sf_calloc(16, 1);
	// m = sf_malloc(10);
	// sf_free(m);
	
	m = sf_mmap(0, 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	printf("mmap @ %p\n", m);
	// sf_munmap(m, 1000);
	sf_uninit();
	return 0;
}
