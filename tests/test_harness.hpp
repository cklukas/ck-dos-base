// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dosbase::bootstrap_test {
struct TestCase { const char* name; void (*function)(); };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> tests; return tests; }
struct Registrar {
    Registrar(const char* name, void (*function)()) { registry().push_back({name, function}); }
};

template <typename Actual, typename Expected>
void expect_equal(const Actual& actual, const Expected& expected, const char* expression,
                  const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << file << ':' << line << ": expected " << expression;
        throw std::runtime_error(message.str());
    }
}
}  // namespace dosbase::bootstrap_test

#define DOSBASE_TEST_JOIN2(a, b) a##b
#define DOSBASE_TEST_JOIN(a, b) DOSBASE_TEST_JOIN2(a, b)
#define DOSBASE_TEST(name) \
    static void name(); \
    static ::dosbase::bootstrap_test::Registrar DOSBASE_TEST_JOIN(registrar_, __LINE__)(#name, &name); \
    static void name()
#define DOSBASE_TEST_EXPECT(expression) do { if (!(expression)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": expected " #expression); } while (false)
#define DOSBASE_TEST_EXPECT_EQ(actual, expected) ::dosbase::bootstrap_test::expect_equal((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
