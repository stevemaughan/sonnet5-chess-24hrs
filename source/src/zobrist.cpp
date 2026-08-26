#include "zobrist.hpp"

uint64_t ZobristPiece[12][64];
uint64_t ZobristSide;
uint64_t ZobristCastling[16];
uint64_t ZobristEpFile[8];

static uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void initZobrist() {
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (int p = 0; p < 12; p++)
        for (int s = 0; s < 64; s++)
            ZobristPiece[p][s] = splitmix64(state);
    ZobristSide = splitmix64(state);
    for (int i = 0; i < 16; i++) ZobristCastling[i] = splitmix64(state);
    for (int i = 0; i < 8; i++) ZobristEpFile[i] = splitmix64(state);
}
