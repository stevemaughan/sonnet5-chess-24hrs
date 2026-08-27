#include "tt.hpp"
#include <algorithm>

TT g_tt;

void TT::resize(size_t mb) {
    size_t bytes = mb * 1024ULL * 1024ULL;
    size_t count = bytes / sizeof(TTEntry);
    size_t pow2 = 1;
    while (pow2 * 2 <= count && pow2 * 2 != 0) pow2 *= 2;
    if (pow2 == 0) pow2 = 1;
    table.assign(pow2, TTEntry());
    mask = pow2 - 1;
}

void TT::clear() {
    std::fill(table.begin(), table.end(), TTEntry());
    generation = 0;
}

int TT::scoreToTT(int score, int ply) {
    if (score >= MATE_IN_MAX) return score + ply;
    if (score <= -MATE_IN_MAX) return score - ply;
    return score;
}

int TT::scoreFromTT(int score, int ply) {
    if (score >= MATE_IN_MAX) return score - ply;
    if (score <= -MATE_IN_MAX) return score + ply;
    return score;
}

bool TT::probe(uint64_t key, TTEntry& out) const {
    if (table.empty()) return false;
    const TTEntry& e = table[key & mask];
    if (e.key == key && e.flag != TT_NONE) { out = e; return true; }
    return false;
}

void TT::store(uint64_t key, int depth, int score, int eval, int flag, Move move, int ply) {
    if (table.empty()) return;
    TTEntry& e = table[key & mask];
    // Replacement: prefer empty/different-key slots, or same-key with >= depth, or aged-out entries.
    // (A shallow EXACT result must not unconditionally evict a much deeper
    // entry at the same slot just because of its flag — depth is a better
    // proxy for how valuable an entry is to keep.)
    if (e.key != key || depth >= e.depth || generation != e.age) {
        if (move == NO_MOVE && e.key == key) move = e.move; // keep previous move if none given (e.g. all-node re-store)
        e.key = key;
        e.score = (int16_t)scoreToTT(score, ply);
        e.eval = (int16_t)eval;
        e.move = move;
        e.depth = (uint8_t)depth;
        e.flag = (uint8_t)flag;
        e.age = generation;
    }
}

int TT::hashfull() const {
    if (table.empty()) return 0;
    int count = 0;
    for (int i = 0; i < 1000 && i < (int)table.size(); i++)
        if (table[i].flag != TT_NONE && table[i].age == generation) count++;
    return count;
}
