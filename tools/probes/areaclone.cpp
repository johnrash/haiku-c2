/* Does resize_area() grow the clones of an area? Mirrors app_server's
   ClientMemoryAllocator (a 128 KB B_NO_LOCK cloneable area, grown by
   resize_area) and the client's ServerMemoryAllocator (the clone made in a
   reserved address range so it can grow in place). Prints both sizes and
   touches the clone's grown part. */
#include <OS.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" status_t _kern_reserve_address_range(addr_t* _address,
	uint32 addressSpec, addr_t size);


static void
segv(int, siginfo_t* info, void*)
{
	fprintf(stderr, "areaclone: segment violation at %p\n", info->si_addr);
	_exit(2);
}


static size_t
area_size(area_id area)
{
	area_info info;
	return get_area_info(area, &info) == B_OK ? info.size : 0;
}


int
main()
{
	struct sigaction action = {};
	action.sa_sigaction = segv;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);

	void* address;
	area_id server = create_area("server chunk", &address, B_ANY_ADDRESS,
		128 * 1024, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	void* base = (void*)0x60000000;
	status_t reserved = _kern_reserve_address_range((addr_t*)&base,
		B_BASE_ADDRESS, 128 * 1024 * 1024);
	area_id clone = clone_area("client clone", &base,
		reserved == B_OK ? B_EXACT_ADDRESS : B_BASE_ADDRESS,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA, server);
	printf("server %d at %p, clone %d at %p (reserve: %s)\n", (int)server,
		address, (int)clone, base, strerror(reserved));
	status_t status = resize_area(server, 2 * 1024 * 1024 + 128 * 1024);
	printf("resize_area: %s; server %zu bytes, clone %zu bytes\n",
		strerror(status), area_size(server), area_size(clone));
	fflush(stdout);
	memset((uint8*)base + 128 * 1024, 0x55, 2 * 1024 * 1024);
	printf("clone's grown part written\n");
	return 0;
}
