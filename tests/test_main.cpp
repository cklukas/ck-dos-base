// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <exception>
#include <iostream>

int main() {
    std::size_t failures = 0;
    for (const auto& test : dosbase::bootstrap_test::registry()) {
        try {
            test.function();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << (dosbase::bootstrap_test::registry().size() - failures) << '/'
              << dosbase::bootstrap_test::registry().size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
