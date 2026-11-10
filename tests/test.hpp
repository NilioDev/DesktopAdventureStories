// SPDX-License-Identifier: GPL-3.0-or-later
//
// A deliberately tiny test harness, so the core can be tested without
// third-party dependencies.
//
//   DAS_TEST(name) { DAS_CHECK(1 + 1 == 2); }
#pragma once

#include <functional>
#include <string_view>
#include <vector>

namespace das::test {

struct Case {
    std::string_view name;
    std::function<void()> body;
};

std::vector<Case>& registry();
void reportFailure(const char* file, int line, const char* expression);
void reportSkip(const char* reason);

struct Registrar {
    Registrar(std::string_view name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

} // namespace das::test

#define DAS_TEST_CONCAT_INNER(a, b) a##b
#define DAS_TEST_CONCAT(a, b) DAS_TEST_CONCAT_INNER(a, b)

#define DAS_TEST(name)                                                                     \
    static void DAS_TEST_CONCAT(das_test_fn_, __LINE__)();                                 \
    static const ::das::test::Registrar DAS_TEST_CONCAT(das_test_reg_, __LINE__){          \
        name, &DAS_TEST_CONCAT(das_test_fn_, __LINE__)};                                   \
    static void DAS_TEST_CONCAT(das_test_fn_, __LINE__)()

#define DAS_CHECK(...)                                                              \
    do {                                                                                   \
        if (!(__VA_ARGS__)) {                                                               \
            ::das::test::reportFailure(__FILE__, __LINE__, #__VA_ARGS__);                   \
        }                                                                                  \
    } while (false)

// Ends the current test early and reports it as skipped, for tests that need
// files which are not always present (such as the original game data).
#define DAS_SKIP(reason)                                                                   \
    do {                                                                                   \
        ::das::test::reportSkip(reason);                                                   \
        return;                                                                            \
    } while (false)
