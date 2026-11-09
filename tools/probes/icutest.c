/* Opens an ICU hash table like XLikelySubtagsData::load does and prints its
   resize fields: length (+52), highWaterMark (+56), lowWaterMark (+60),
   the water ratios (+64, +68); then puts 400 keys and prints them again. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef void* (*open_size_t)(void*, void*, void*, int32_t, int*);
typedef void* (*put_t)(void*, void*, void*, int*);

static void
show(const char* when, const uint8_t* h)
{
	float ratios[2];
	memcpy(ratios, h + 64, sizeof(ratios));
	printf("%s: count %d length %d high %d low %d ratios %g %g\n", when,
		*(int32_t*)(h + 48), *(int32_t*)(h + 52), *(int32_t*)(h + 56),
		*(int32_t*)(h + 60), ratios[0], ratios[1]);
}

int
main()
{
	void* uc = dlopen("libicuuc.so.74", RTLD_NOW);
	if (uc == NULL) {
		printf("dlopen: %s\n", dlerror());
		return 2;
	}
	open_size_t openSize = (open_size_t)dlsym(uc, "uhash_openSize_74");
	put_t put = (put_t)dlsym(uc, "uhash_put_74");
	void* hashChars = dlsym(uc, "uhash_hashChars_74");
	void* compareChars = dlsym(uc, "uhash_compareChars_74");
	int status = 0;
	uint8_t* h = openSize(hashChars, compareChars, NULL, 100, &status);
	if (h == NULL) {
		printf("openSize failed: %d\n", status);
		return 3;
	}
	show("open", h);
	static char keys[400][8];
	for (int i = 0; i < 400; i++) {
		snprintf(keys[i], sizeof(keys[i]), "k%d", i);
		put(h, keys[i], keys[i], &status);
		if (status > 0) {
			printf("put %d failed: %d\n", i, status);
			show("fail", h);
			return 1;
		}
	}
	show("400 puts", h);
	return 0;
}
