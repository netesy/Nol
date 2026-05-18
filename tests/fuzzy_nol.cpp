#include "../include/nol.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>

class NOLFuzzer {
private:
    std::vector<std::string> validKeys = {"name", "host", "port", "debug", "timeout", "retry", "enabled", "path", "version", "mode"};
    std::vector<std::string> validStrings = {"localhost", "127.0.0.1", "example.com", "app", "server", "client", "v1.0", "production", "development"};
    std::vector<int> validNumbers = {0, 1, 8080, 3000, 443, 22, 3306, 5432, 6379, 27017};
    std::vector<bool> validBooleans = {true, false};
public:
    std::mt19937 rng;
    NOLFuzzer() : rng(std::chrono::steady_clock::now().time_since_epoch().count()) {}
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
    std::string generateNestedObject() {
        std::string obj = "{";
        int numPairs = 2 + (rng() % 4);
        for (int i = 0; i < numPairs; i++) { if (i > 0) obj += ","; std::string key = generateRandomKey(); if (rng() % 3 == 0) obj += key + ": " + generateSimpleObject(); else obj += key + ": " + generateRandomValue(); }
        obj += "}"; return obj;
    }
    std::string generateArray() {
        std::string arr = "[";
        int numElements = 1 + (rng() % 4);
        for (int i = 0; i < numElements; i++) { if (i > 0) arr += ","; arr += generateRandomValue(); }
        arr += "]"; return arr;
    }
    std::string generateSectionedConfig() {
        std::string config;
        int numSections = 1 + (rng() % 3);
        for (int i = 0; i < numSections; i++) { if (i > 0) config += "\n\n"; config += "[" + generateRandomKey() + "]\n"; int numPairs = 1 + (rng() % 4); for (int j = 0; j < numPairs; j++) config += generateRandomKey() + ": " + generateRandomValue() + "\n"; }
        return config;
    }
    std::string generateMalformedInput() {
        int choice = rng() % 8;
        switch (choice) {
            case 0: return "{incomplete: object";
            case 1: return "unclosed: \"string";
            case 2: return "invalid: [1,2,3";
            case 3: return "duplicate: \"value1\"\nduplicate: \"value2\"";
            case 4: return "invalid: true false";
            case 5: return "{: \"missing_key\"}";
            case 6: return "key: value: extra";
            case 7: return "invalid: ${interpolation}";
            default: return "";
        }
    }
};

void testValidNOLInputs() {
    NOLFuzzer fuzzer;
    int successCount = 0;
    int totalTests = 100;
    for (int i = 0; i < totalTests; i++) {
        std::string input;
        int testType = fuzzer.rng() % 4;
        switch (testType) {
            case 0: input = fuzzer.generateSimpleObject(); break;
            case 1: input = fuzzer.generateNestedObject(); break;
            case 2: input = fuzzer.generateSectionedConfig(); break;
            case 3: input = fuzzer.generateRandomKey() + ": " + fuzzer.generateRandomValue(); break;
        }
        try {
            NOL::Document doc = NOL::parse(input);
            successCount++;
        } catch (...) {}
    }
    std::cout << "✅ Valid NOL inputs: " << successCount << "/" << totalTests << " passed" << std::endl;
}

void testInvalidNOLInputs() {
    NOLFuzzer fuzzer;
    int failureCount = 0;
    int totalTests = 50;
    for (int i = 0; i < totalTests; i++) {
        std::string input = fuzzer.generateMalformedInput();
        try {
            NOL::parse(input);
        } catch (...) {
            failureCount++;
        }
    }
    std::cout << "✅ Invalid NOL inputs: " << failureCount << "/" << totalTests << " correctly rejected" << std::endl;
}

int main() {
    testValidNOLInputs();
    testInvalidNOLInputs();
    return 0;
}
