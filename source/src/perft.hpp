#pragma once
#include "board.hpp"
#include <cstdint>
#include <string>

uint64_t perft(Board& b, int depth);
void runPerftSuite(const std::string& epdPath, int maxDepth);
