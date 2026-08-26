#pragma once
#include "types.hpp"
#include <immintrin.h>

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << 8;
constexpr Bitboard RANK_4_BB = RANK_1_BB << 24;
constexpr Bitboard RANK_5_BB = RANK_1_BB << 32;
constexpr Bitboard RANK_7_BB = RANK_1_BB << 48;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;

inline Bitboard fileBB(int f) { return FILE_A_BB << f; }
inline Bitboard rankBB(int r) { return RANK_1_BB << (r * 8); }
inline Bitboard squareBB(int sq) { return 1ULL << sq; }

inline int popcount(Bitboard b) { return (int)__builtin_popcountll(b); }
inline int lsb(Bitboard b) { return __builtin_ctzll(b); }
inline int popLsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }
inline bool moreThanOne(Bitboard b) { return (b & (b - 1)) != 0; }

inline Bitboard pext(Bitboard b, Bitboard mask) { return _pext_u64(b, mask); }

// Shifts that clip at board edges
template<int Dir> inline Bitboard shift(Bitboard b) {
    if constexpr (Dir == NORTH) return b << 8;
    else if constexpr (Dir == SOUTH) return b >> 8;
    else if constexpr (Dir == EAST) return (b & ~FILE_H_BB) << 1;
    else if constexpr (Dir == WEST) return (b & ~FILE_A_BB) >> 1;
    else if constexpr (Dir == NORTH_EAST) return (b & ~FILE_H_BB) << 9;
    else if constexpr (Dir == NORTH_WEST) return (b & ~FILE_A_BB) << 7;
    else if constexpr (Dir == SOUTH_EAST) return (b & ~FILE_H_BB) >> 7;
    else if constexpr (Dir == SOUTH_WEST) return (b & ~FILE_A_BB) >> 9;
    return 0;
}

void initAttackTables();

extern Bitboard PawnAttacks[COLOR_NB][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard BetweenBB[64][64];   // squares strictly between a and b (exclusive), 0 if not aligned
extern Bitboard LineBB[64][64];      // full line through a and b (if aligned), else 0

Bitboard bishopAttacks(int sq, Bitboard occ);
Bitboard rookAttacks(int sq, Bitboard occ);
inline Bitboard queenAttacks(int sq, Bitboard occ) { return bishopAttacks(sq, occ) | rookAttacks(sq, occ); }
