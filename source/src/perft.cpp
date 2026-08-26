#include "perft.hpp"
#include "movegen.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <chrono>

uint64_t perft(Board& b, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generateLegal(b, list);
    if (depth == 1) return (uint64_t)list.count;
    uint64_t nodes = 0;
    for (int i = 0; i < list.count; i++) {
        Undo u;
        b.makeMove(list.moves[i], u);
        nodes += perft(b, depth - 1);
        b.unmakeMove(list.moves[i], u);
    }
    return nodes;
}

void runPerftSuite(const std::string& epdPath, int maxDepth) {
    std::ifstream in(epdPath);
    if (!in) { std::cerr << "cannot open " << epdPath << "\n"; return; }
    std::string line;
    int lineNo = 0, passLines = 0, failLines = 0;
    auto t0 = std::chrono::steady_clock::now();
    uint64_t totalNodes = 0;
    while (std::getline(in, line)) {
        lineNo++;
        if (line.empty()) continue;
        // split by ';'
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ';')) parts.push_back(tok);
        if (parts.empty()) continue;
        std::string fen = parts[0];
        // trim trailing space
        while (!fen.empty() && fen.back() == ' ') fen.pop_back();

        Board b;
        b.setFromFEN(fen);
        bool lineOk = true;
        for (size_t i = 1; i < parts.size(); i++) {
            std::stringstream ds(parts[i]);
            std::string dtok; uint64_t expected;
            ds >> dtok >> expected; // dtok like "D1"
            if (dtok.empty() || dtok[0] != 'D') continue;
            int depth = std::stoi(dtok.substr(1));
            if (depth > maxDepth) continue;
            uint64_t got = perft(b, depth);
            totalNodes += got;
            if (got != expected) {
                std::cout << "FAIL line " << lineNo << " depth " << depth
                          << " fen=" << fen << " expected=" << expected << " got=" << got << "\n";
                lineOk = false;
            }
        }
        if (lineOk) passLines++; else failLines++;
    }
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "Perft suite done: " << passLines << " pass, " << failLines << " fail, "
              << totalNodes << " nodes in " << secs << "s ("
              << (secs > 0 ? (uint64_t)(totalNodes / secs) : 0) << " nps)\n";
}
