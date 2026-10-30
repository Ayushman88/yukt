#pragma once

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace yukt::test {

inline int g_failed = 0;
inline int g_passed = 0;

inline void check(bool condition, const char* expression, const char* file, int line,
                  const std::string& message = {}) {
    if (condition) {
        ++g_passed;
        return;
    }

    ++g_failed;
    std::cerr << "FAIL " << file << ':' << line << "  " << expression;
    if (!message.empty()) {
        std::cerr << " — " << message;
    }
    std::cerr << '\n';
}

inline int summary(const char* suite) {
    std::cout << suite << ": " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace yukt::test

#define REQUIRE(condition) ::yukt::test::check(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define REQUIRE_MSG(condition, message) \
    ::yukt::test::check(static_cast<bool>(condition), #condition, __FILE__, __LINE__, (message))
