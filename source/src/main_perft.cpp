#include "init.hpp"
#include "perft.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    engineInit();
    std::string epdPath = "resources/perft/perft.epd";
    int maxDepth = 5;
    if (argc > 1) epdPath = argv[1];
    if (argc > 2) maxDepth = std::stoi(argv[2]);
    runPerftSuite(epdPath, maxDepth);
    return 0;
}
