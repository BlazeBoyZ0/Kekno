#include <iostream>
#include <string>
#include "compiler.h"
#include "vm.h"

int main() {
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

    return 0;
}
