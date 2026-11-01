#pragma once
#include <cstdio>

inline int g_failures = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,     \
                         __LINE__, #expr);                                  \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

inline int test_result(const char* name) {
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all %s tests passed\n", name);
    return 0;
}
