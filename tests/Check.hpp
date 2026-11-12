#pragma once

#include <cstdio>

// Shared check harness for the standalone test executables. CHECK reports a failed expression and keeps going;
// main() returns g_failures == 0 ? 0 : 1.
inline int g_failures = 0;

inline void report(bool ok, const char *expression, const char *file, int line) {
    if (ok)
        return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
