#ifndef TESTS_TEST_MACROS_H
#define TESTS_TEST_MACROS_H

#include <stdio.h>
#include <stdlib.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "\033[1;31m[FAIL]\033[0m %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define ASSERT_MSG(cond, msg, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "\033[1;31m[FAIL]\033[0m %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, msg, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
        exit(1); \
    } \
} while (0)

#define RUN_TEST(test_func) do { \
    printf("[RUN]  %s\n", #test_func); \
    test_func(); \
    printf("\033[1;32m[PASS]\033[0m %s\n", #test_func); \
} while (0)

#endif // TESTS_TEST_MACROS_H

