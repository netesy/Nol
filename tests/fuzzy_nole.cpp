#include "../include/nole.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>

class NOLEFuzzer {
private:
    std::vector<std::string> validKeys = {"name", "host", "port", "debug", "timeout", "retry", "enabled", "path", "version", "mode"};
    std::vector<std::string> validStrings = {"localhost", "127.0.0.1", "example.com", "app", "server", "client", "v1.0", "production", "development"};
    std::vector<int> validNumbers = {0, 1, 8080, 3000, 443, 22, 3306, 5432, 6379, 27017};
    std::vector<bool> validBooleans = {true, false};
public:
    std::mt19937 rng;
    NOLEFuzzer() : rng(std::chrono::steady_clock::now().time_since_epoch().count()) {}
    std::string generateRandomKey() { return validKeys[rng() % validKeys.size()]; }
    std::string generateRandomString() { return validStrings[rng() % validStrings.size()]; }
    int generateRandomNumber() { return validNumbers[rng() % validNumbers.size()]; }
    bool generateRandomBoolean() { return validBooleans[rng() % validBooleans.size()]; }
    std::string generateRandomValue() {
        int choice = rng() % 4;
        switch (choice) {
            case 0: return "\"" + generateRandomString() + "\"";
            case 1: return std::to_string(generateRandomNumber());
            case 2: return generateRandomBoolean() ? "true" : "false";
            case 3: return "null";
            default: return "\"default\"";
        }
    }
    std::string generateSimpleObject() {
        std::string obj = "{";
        int numPairs = 1 + (rng() % 5);
        for (int i = 0; i < numPairs; i++) { if (i > 0) obj += ","; obj += generateRandomKey() + ": " + generateRandomValue(); }
        obj += "}"; return obj;
    }
    std::string generateMalformedExtendedInput() {
        int choice = rng() % 10;
        switch (choice) {
            case 0: return "{incomplete: object";
            case 1: return "unclosed: \"string";
            case 2: return "invalid: [1,2,3";
            case 3: return "duplicate: \"value1\"\nduplicate: \"value2\"";
            case 4: return "invalid: true false";
            case 5: return "{: \"missing_key\"}";
            case 6: return "key: value: extra";
            case 7: return "&anchor { incomplete";
            case 8: return "<<:*nonexistent";
            case 9: return "${unclosed.interpolation";
            default: return "";
        }
    }
};

void testValidNOLEInputs() {
    NOLEFuzzer fuzzer;
    int successCount = 0;
    int totalTests = 100;
    for (int i = 0; i < totalTests; i++) {
        std::string input = fuzzer.generateRandomKey() + ": " + fuzzer.generateRandomValue();
        try {
            NOLE::parse(input);
            successCount++;
        } catch (...) {}
    }
    std::cout << "✅ Valid NOLE inputs: " << successCount << "/" << totalTests << " passed" << std::endl;
}

void testInvalidNOLEInputs() {
    NOLEFuzzer fuzzer;
    int failureCount = 0;
    int totalTests = 50;
    for (int i = 0; i < totalTests; i++) {
        std::string input = fuzzer.generateMalformedExtendedInput();
        try {
            NOLE::parse(input);
        } catch (...) {
            failureCount++;
        }
    }
    std::cout << "✅ Invalid NOLE inputs: " << failureCount << "/" << totalTests << " correctly rejected" << std::endl;
}

int main() {
    testValidNOLEInputs();
    testInvalidNOLEInputs();
    return 0;
}
