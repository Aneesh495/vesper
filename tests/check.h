#ifndef VS_TEST_CHECK_H
#define VS_TEST_CHECK_H
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)
#endif
