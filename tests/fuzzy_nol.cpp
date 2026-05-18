#include "../include/nol.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <random>

std::string gen_val(int d);
std::string gen_obj(int d) {
    if (d > 5) return "1";
    std::string s = "{";
    int n = rand() % 5;
    for(int i=0; i<n; i++) {
        s += "k" + std::to_string(i) + ": " + gen_val(d+1);
        if (i+1 < n) s += ", ";
    }
    return s + "}";
}
std::string gen_val(int d) {
    int t = rand() % 4;
    if (t == 0) return std::to_string(rand() % 100);
    if (t == 1) return "\"s" + std::to_string(rand() % 100) + "\"";
    if (t == 2) return (rand() % 2) ? "true" : "false";
    return gen_obj(d);
}

int main() {
    srand(42);
    for(int i=0; i<100; i++) {
        std::string s = gen_obj(0);
        try {
            auto doc = NOL::parse(s);
        } catch(...) {}
    }
    std::cout << "Fuzzy NOL OK" << std::endl;
    return 0;
}
