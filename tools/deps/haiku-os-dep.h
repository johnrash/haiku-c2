/* OpenSSL's os-dep/haiku.h, but not for assembler sources (AArch64 has
   preprocessed .S files). */
#ifndef __ASSEMBLER__
#include <sys/select.h>
#include <sys/time.h>
#endif
