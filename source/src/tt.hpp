#pragma once
#include "types.hpp"
#include <vector>
#include <cstddef>

enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT = 1, TT_ALPHA = 2, TT_BETA = 3 };

struct TTEntry {
    uint64_t key = 0;
    int16_t score = 0;
    int16_t eval = 0;
    Move move = NO_MOVE;
    uint8_t depth = 0;
    uint8_t flag = TT_NONE;
    uint8_t age = 0;
};

class TT {
public:
    void resize(size_t mb);
    void clear();
    void newSearch() { generation++; }
    bool probe(uint64_t key, TTEntry& out) const;
    void store(uint64_t key, int depth, int score, int eval, int flag, Move move, int ply);
    int hashfull() const;

    static int scoreToTT(int score, int ply);
    static int scoreFromTT(int score, int ply);

private:
    std::vector<TTEntry> table;
    size_t mask = 0;
    uint8_t generation = 0;
};

extern TT g_tt;
