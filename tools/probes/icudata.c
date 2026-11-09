/* Checksums ICU's built-in data as this process sees it, then calls
   setlocale() (which crashes intermittently with the full data). */
#include <dlfcn.h>
#include <locale.h>
#include <stdio.h>
#include <stdint.h>

int
main()
{
	void* data = dlopen("libicudata.so.74", RTLD_NOW);
	const uint8_t* p = data != NULL ? dlsym(data, "icudt74_dat") : NULL;
	if (p == NULL) {
		printf("no icudt74_dat\n");
		return 2;
	}
	uint64_t hash = 1469598103934665603ull;
	for (uint32_t i = 0; i < 30783664; i++)
		hash = (hash ^ p[i]) * 1099511628211ull;
	printf("data %016llx\n", (unsigned long long)hash);
	fflush(stdout);
	const char* locale = setlocale(LC_ALL, "en_US.UTF-8");
	printf("setlocale %s\n", locale != NULL ? locale : "(null)");
	return 0;
}
