#pragma once
#include <stdint.h>

/* User-space AppleSMC reads. Returns 0 on success. */
int smc_open(void);
void smc_close(void);
int smc_read_f(const char *key, float *out);
