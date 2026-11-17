#ifndef _DIAG_H
#define _DIAG_H

#ifdef GB6_NO_PRINTF
#define DIAG(...) ((void) 0)
#else
#include <stdio.h>
#define DIAG(...) fprintf(stderr, __VA_ARGS__)
#endif

#endif
