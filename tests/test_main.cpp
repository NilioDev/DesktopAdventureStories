// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include <cstdio>
#include <exception>

namespace das::test {

namespace {
int failures = 0;
const char* skipReason = nullptr;
}

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void reportFailure(const char* file, int line, const char* expression) {
    ++failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

void reportSkip(const char* reason) { skipReason = reason; }

} // namespace das::test

int main() {
    using namespace das::test;
    int failedCases = 0;
    int skippedCases = 0;
    for (const auto& testCase : registry()) {
        const int before = failures;
        skipReason = nullptr;
        try {
            testCase.body();
        } catch (const std::exception& e) {
            reportFailure(__FILE__, __LINE__, e.what());
        }
        const bool ok = failures == before;
        failedCases += ok ? 0 : 1;
        const char* status = !ok ? "FAIL" : skipReason ? "skip" : " ok ";
        skippedCases += ok && skipReason ? 1 : 0;
        std::printf("[%s] %.*s%s%s\n", status, static_cast<int>(testCase.name.size()), testCase.name.data(),
                    ok && skipReason ? ": " : "", ok && skipReason ? skipReason : "");
    }
    std::printf("%zu tests, %d failed, %d skipped\n", registry().size(), failedCases, skippedCases);
    return failedCases == 0 ? 0 : 1;
}
