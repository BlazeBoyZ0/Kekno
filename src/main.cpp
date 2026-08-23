#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "compiler.h"
#include "vm.h"

static void runFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open file \"" << path << "\"." << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    VM vm;
    Chunk bytecode;
    Compiler compiler(source, bytecode);

    if (compiler.compile()) {
        vm.run(bytecode);
    }
}

static void runRepl() {
    std::cout << "===============================" << std::endl;
    std::cout << "            Kekno              " << std::endl;
    std::cout << "===============================" << std::endl;

    VM vm;
    std::string line;

    while (true) {
        std::cout << "kekno> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        Chunk bytecode;
        Compiler compiler(line, bytecode);

        if (compiler.compile()) {
            vm.run(bytecode);
        }
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        runRepl();
    } else if (argc == 2) {
        runFile(argv[1]);
    } else {
        std::cerr << "Usage: kekno [path]" << std::endl;
        return 64;
    }

    return 0;
}
