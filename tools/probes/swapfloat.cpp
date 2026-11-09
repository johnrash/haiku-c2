/* Checks libroot's __swap_float/__swap_double (B_SWAP_FLOAT etc.) against a
   byte-wise swap, for a few values. */
#include <ByteOrder.h>
#include <stdio.h>
#include <string.h>

int
main()
{
	int bad = 0;
	const float floats[] = { 539.0f, -1.5f, 0.0f, 1e-10f };
	for (float value : floats) {
		float swapped = B_SWAP_FLOAT(value);
		uint8 in[4], out[4];
		memcpy(in, &value, 4);
		memcpy(out, &swapped, 4);
		bool ok = out[0] == in[3] && out[1] == in[2] && out[2] == in[1]
			&& out[3] == in[0] && B_SWAP_FLOAT(swapped) == value;
		printf("float %g: %s\n", value, ok ? "ok" : "WRONG");
		bad += !ok;
	}
	const double doubles[] = { 539.0, -1.5, 1e-100 };
	for (double value : doubles) {
		double swapped = B_SWAP_DOUBLE(value);
		uint8 in[8], out[8];
		memcpy(in, &value, 8);
		memcpy(out, &swapped, 8);
		bool ok = B_SWAP_DOUBLE(swapped) == value;
		for (int i = 0; i < 8; i++)
			ok = ok && out[i] == in[7 - i];
		printf("double %g: %s\n", value, ok ? "ok" : "WRONG");
		bad += !ok;
	}
	return bad != 0;
}
