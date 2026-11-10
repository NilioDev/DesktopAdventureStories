// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include <cstdio>

namespace das::test {

namespace {
int failures = 0;
}

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void reportFailure(const char* file, int line, const char* expression) {
    ++failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

} // namespace das::test

int main() {
    using namespace das::test;
    int failedCases = 0;
    for (const auto& testCase : registry()) {
        const int before = failures;
        testCase.body();
        const bool ok = failures == before;
        failedCases += ok ? 0 : 1;
        std::printf("[%s] %.*s\n", ok ? " ok " : "FAIL", static_cast<int>(testCase.name.size()),
                    testCase.name.data());
    }
    std::printf("%zu tests, %d failed\n", registry().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
