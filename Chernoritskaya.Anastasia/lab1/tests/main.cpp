#include "test_support.hpp"

int main() {
    int failedTests = 0;
    for (const auto& tc : registry()) {
        int before = g_fail;
        std::cout << "[ RUN  ] " << tc.name << std::endl;
        tc.fn();
        if (g_fail == before) {
            std::cout << "[  OK  ] " << tc.name << std::endl;
        } else {
            std::cout << "[ FAIL ] " << tc.name << std::endl;
            ++failedTests;
        }
    }
    std::cout << "\n" << registry().size() - failedTests << "/" << registry().size()
              << " tests passed" << std::endl;
    return failedTests ? 1 : 0;
}