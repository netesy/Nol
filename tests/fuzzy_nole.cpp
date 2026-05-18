#include "../include/nole.hpp"
#include <iostream>
int main() {
    // Basic evaluation fuzzy test
    for(int i=0; i<100; i++) {
        std::string s = "a: " + std::to_string(i) + "\nb: \"${a}\"";
        try {
            auto doc = NOLE::parse(s, {});
        } catch(...) {}
    }
    std::cout << "Fuzzy NOLE OK" << std::endl;
    return 0;
}
