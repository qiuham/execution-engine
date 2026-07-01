#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace exec::test {

struct TestContext {
    bool ok{true};

    bool expect(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            ok = false;
        }
        return condition;
    }

    template <typename Actual, typename Expected>
    bool expect_eq(const Actual& actual, const Expected& expected, const char* message) {
        if (!(actual == expected)) {
            std::cerr << message << " actual=" << actual << " expected=" << expected << '\n';
            ok = false;
            return false;
        }
        return true;
    }

    int result() const {
        return ok ? 0 : 1;
    }
};

inline bool contains_text(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // 命名空间 exec::test
