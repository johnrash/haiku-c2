/* LD_PRELOAD library: wraps ICU's uprv_malloc/uprv_realloc and
   uhash_openSize/uhash_put (libicuuc calls them through its PLT) and logs
   anomalies to /dev/dprintf (serial and syslog): allocations of 4 MB or
   more with their caller, and a put that fails, with the table's fields. */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int sLog = -1;


/* the real function, from libicuuc itself (Haiku's RTLD_NEXT gives NULL here) */
static void*
real_symbol(const char* name)
{
	static void* sICU;
	if (sICU == NULL)
		sICU = dlopen("libicuuc.so.74", RTLD_NOW);
	return dlsym(sICU, name);
}

static void
trace(const char* format, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, format);
	int length = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (sLog < 0)
		sLog = open("/dev/dprintf", O_WRONLY);
	if (sLog >= 0 && length > 0)
		write(sLog, buffer, length < (int)sizeof(buffer) ? length : sizeof(buffer) - 1);
}

static void
dump(const char* what, const uint8_t* h, int status)
{
	float ratios[2];
	memcpy(ratios, h + 64, sizeof(ratios));
	trace("icutrace %d: %s status %d: count %d length %d high %d low %d "
		"ratios %g %g prime %d\n", getpid(), what, status, *(int32_t*)(h + 48),
		*(int32_t*)(h + 52), *(int32_t*)(h + 56), *(int32_t*)(h + 60),
		ratios[0], ratios[1], *(int8_t*)(h + 72));
}

static uint8_t* sLastTable;
	// the last 80-byte allocation: uhash_openSize's UHashtable


void*
uprv_malloc_74(size_t size)
{
	static void* (*real)(size_t);
	if (real == NULL)
		real = (void* (*)(size_t))real_symbol("uprv_malloc_74");
	// the ratios uhash_openSize stored before _uhash_allocate allocates
	// the elements: already wrong, or changed by this allocation?
	uint64_t before = 0;
	uint8_t* table = sLastTable;
	if (table != NULL)
		memcpy(&before, table + 64, sizeof(before));
	void* result = real(size);
	if (table != NULL) {
		uint64_t after;
		memcpy(&after, table + 64, sizeof(after));
		if (before != 0x3f000000 || after != before) {
			trace("icutrace %d: table %p ratios before malloc(%zu) %016llx, "
				"after %016llx\n", getpid(), table, size,
				(unsigned long long)before, (unsigned long long)after);
		}
		sLastTable = NULL;
	}
	if (size == 80)
		sLastTable = (uint8_t*)result;
	if (size >= 4 << 20 || result == NULL) {
		trace("icutrace %d: uprv_malloc %zu -> %p from %p\n", getpid(), size,
			result, __builtin_return_address(0));
	}
	return result;
}

void*
uprv_realloc_74(void* buffer, size_t size)
{
	static void* (*real)(void*, size_t);
	if (real == NULL)
		real = (void* (*)(void*, size_t))real_symbol("uprv_realloc_74");
	void* result = real(buffer, size);
	if (size >= 4 << 20 || result == NULL) {
		trace("icutrace %d: uprv_realloc %zu -> %p from %p\n", getpid(), size,
			result, __builtin_return_address(0));
	}
	return result;
}

void*
uhash_openSize_74(void* hash, void* compare, void* valueCompare, int32_t size,
	int* status)
{
	static void* (*real)(void*, void*, void*, int32_t, int*);
	if (real == NULL) {
		real = (void* (*)(void*, void*, void*, int32_t, int*))
			real_symbol("uhash_openSize_74");
	}
	int before = *status;
	void* result = real(hash, compare, valueCompare, size, status);
	if (result == NULL || size > 100000) {
		trace("icutrace %d: openSize %d status %d->%d -> %p from %p\n", getpid(),
			size, before, *status, result, __builtin_return_address(0));
	} else if (*(int32_t*)((uint8_t*)result + 56) <= 0) {
		dump("openSize high <= 0", result, *status);
		// the ratio table (RESIZE_POLICY_RATIO_TABLE, libicuuc 0x188e48 in
		// the arm64 bootstrap build) as mapped, and where the table and its
		// elements are
		Dl_info info;
		if (dladdr((void*)real, &info) != 0) {
			const uint32_t* table
				= (const uint32_t*)((uint8_t*)info.dli_fbase + 0x188e48);
			trace("icutrace %d: ratio table %08x %08x %08x %08x; hash %p "
				"elements %p\n", getpid(), table[0], table[1], table[2], table[3],
				result, *(void**)result);
		}
	}
	return result;
}

void*
uhash_put_74(void* hash, void* key, void* value, int* status)
{
	static void* (*real)(void*, void*, void*, int*);
	if (real == NULL)
		real = (void* (*)(void*, void*, void*, int*))real_symbol("uhash_put_74");
	int before = *status;
	if (hash == NULL) {
		trace("icutrace %d: put on NULL, status %d, from %p\n", getpid(), before,
			__builtin_return_address(0));
		return NULL;
	}
	void* result = real(hash, key, value, status);
	if (before <= 0 && *status > 0) {
		dump("put failed", hash, *status);
		trace("icutrace %d: key '%.40s' from %p\n", getpid(), (const char*)key,
			__builtin_return_address(0));
	}
	return result;
}
