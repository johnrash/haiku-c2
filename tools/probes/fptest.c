/* Fills all 32 SIMD registers with known values, spins, and checks that
   they are unchanged; counts mismatches. Usage: fptest <seconds> */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static int
round_trip(uint64_t seed)
{
	uint64_t in[64], out[64];
	for (int i = 0; i < 64; i++)
		in[i] = seed * 0x9e3779b97f4a7c15ull + i;
	__asm__ volatile(
		"ldp q0, q1, [%0, #0]\n ldp q2, q3, [%0, #32]\n"
		"ldp q4, q5, [%0, #64]\n ldp q6, q7, [%0, #96]\n"
		"ldp q8, q9, [%0, #128]\n ldp q10, q11, [%0, #160]\n"
		"ldp q12, q13, [%0, #192]\n ldp q14, q15, [%0, #224]\n"
		"ldp q16, q17, [%0, #256]\n ldp q18, q19, [%0, #288]\n"
		"ldp q20, q21, [%0, #320]\n ldp q22, q23, [%0, #352]\n"
		"ldp q24, q25, [%0, #384]\n ldp q26, q27, [%0, #416]\n"
		"ldp q28, q29, [%0, #448]\n ldp q30, q31, [%0, #480]\n"
		"mov x9, #0x30000\n1: subs x9, x9, #1\n b.ne 1b\n"
		"stp q0, q1, [%1, #0]\n stp q2, q3, [%1, #32]\n"
		"stp q4, q5, [%1, #64]\n stp q6, q7, [%1, #96]\n"
		"stp q8, q9, [%1, #128]\n stp q10, q11, [%1, #160]\n"
		"stp q12, q13, [%1, #192]\n stp q14, q15, [%1, #224]\n"
		"stp q16, q17, [%1, #256]\n stp q18, q19, [%1, #288]\n"
		"stp q20, q21, [%1, #320]\n stp q22, q23, [%1, #352]\n"
		"stp q24, q25, [%1, #384]\n stp q26, q27, [%1, #416]\n"
		"stp q28, q29, [%1, #448]\n stp q30, q31, [%1, #480]\n"
		: : "r"(in), "r"(out)
		: "x9", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
		  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17",
		  "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26",
		  "v27", "v28", "v29", "v30", "v31");
	int bad = 0;
	for (int i = 0; i < 64; i++) {
		if (in[i] != out[i]) {
			if (bad == 0)
				printf("mismatch v%d: %016llx != %016llx\n", i / 2,
					(unsigned long long)out[i], (unsigned long long)in[i]);
			bad++;
		}
	}
	return bad != 0;
}

int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 10;
	time_t end = time(NULL) + seconds;
	long rounds = 0, bad = 0;
	while (time(NULL) < end) {
		bad += round_trip(rounds);
		rounds++;
	}
	printf("fptest: %ld rounds, %ld with corrupted registers\n", rounds, bad);
	return bad != 0;
}
