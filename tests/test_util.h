#pragma once

#include <iostream>

namespace biem::test {
inline int& failureCount() {
    static int n = 0;
    return n;
}
} // namespace biem::test

#define BIEM_CHECK(cond)                                                                              \
    do {                                                                                              \
        if (!(cond)) {                                                                                \
            std::cerr << "CHECK FAILED: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++biem::test::failureCount();                                                             \
        }                                                                                              \
    } while (0)

#define BIEM_TEST_MAIN_RETURN()                                       \
    do {                                                              \
        if (biem::test::failureCount() == 0) {                        \
            std::cout << "OK: all checks passed\n";                   \
            return 0;                                                 \
        }                                                              \
        std::cerr << biem::test::failureCount() << " check(s) failed\n"; \
        return 1;                                                     \
    } while (0)
