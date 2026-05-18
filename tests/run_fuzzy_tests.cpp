#include "../include/nol.hpp"
#include "../include/nole.hpp"
#include <iostream>
#include <chrono>

int main() {
    std::cout << "🧪 NOL & NOLE BASIC VERIFICATION" << std::endl;
    
    // Test NOL basic parsing
    std::string nolTest = "[server]\nhost: \"localhost\"\nport: 8080\n";
    try {
        NOL::Document doc = NOL::parse(nolTest);
        if (doc.get("server.host")->asString() == "localhost") {
            std::cout << "✅ NOL Document API works" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ NOL test exception: " << e.what() << std::endl;
    }
    
    // Test NOLE basic parsing
    std::string noleTest = "base: \"http://localhost\"\nurl: \"\${base}:8080\"\n";
    try {
        NOLE::Document doc = NOLE::parse(noleTest);
        if (doc.get("url")->asString() == "http://localhost:8080") {
            std::cout << "✅ NOLE Document API works" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ NOLE test exception: " << e.what() << std::endl;
    }

    // Test Builders
    NOL::Builder nb;
    nb.set("a.b", 1);
    if (nb.build().get("a.b")->asInt() == 1) {
        std::cout << "✅ NOL Builder API works" << std::endl;
    }

    NOLE::Builder neb;
    neb.set("x.y", "z");
    if (neb.build().get("x.y")->asString() == "z") {
        std::cout << "✅ NOLE Builder API works" << std::endl;
    }
    
    return 0;
}
