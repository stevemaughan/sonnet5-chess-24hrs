#include "bitboard.hpp"
#include <vector>

Bitboard PawnAttacks[COLOR_NB][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];

static Bitboard RookMask[64];
static Bitboard BishopMask[64];
static int RookOffset[64];
static int BishopOffset[64];
static std::vector<Bitboard> RookTable;
static std::vector<Bitboard> BishopTable;

static Bitboard rayCast(int sq, Bitboard occ, const int dx[4], const int dy[4]) {
    Bitboard attacks = 0;
    int f0 = fileOf(sq), r0 = rankOf(sq);
    for (int d = 0; d < 4; d++) {
        int f = f0 + dx[d], r = r0 + dy[d];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            int s = r * 8 + f;
            attacks |= squareBB(s);
            if (occ & squareBB(s)) break;
            f += dx[d]; r += dy[d];
        }
    }
    return attacks;
}

static const int RookDX[4] = { 1, -1, 0, 0 };
static const int RookDY[4] = { 0, 0, 1, -1 };
static const int BishopDX[4] = { 1, 1, -1, -1 };
static const int BishopDY[4] = { 1, -1, 1, -1 };

static Bitboard rookMaskFor(int sq) {
    Bitboard mask = 0;
    int f0 = fileOf(sq), r0 = rankOf(sq);
    for (int f = f0 + 1; f <= 6; f++) mask |= squareBB(r0 * 8 + f);
    for (int f = f0 - 1; f >= 1; f--) mask |= squareBB(r0 * 8 + f);
    for (int r = r0 + 1; r <= 6; r++) mask |= squareBB(r * 8 + f0);
    for (int r = r0 - 1; r >= 1; r--) mask |= squareBB(r * 8 + f0);
    return mask;
}

static Bitboard bishopMaskFor(int sq) {
    Bitboard mask = 0;
    int f0 = fileOf(sq), r0 = rankOf(sq);
    for (int f = f0 + 1, r = r0 + 1; f <= 6 && r <= 6; f++, r++) mask |= squareBB(r * 8 + f);
    for (int f = f0 + 1, r = r0 - 1; f <= 6 && r >= 1; f++, r--) mask |= squareBB(r * 8 + f);
    for (int f = f0 - 1, r = r0 + 1; f >= 1 && r <= 6; f--, r++) mask |= squareBB(r * 8 + f);
    for (int f = f0 - 1, r = r0 - 1; f >= 1 && r >= 1; f--, r--) mask |= squareBB(r * 8 + f);
    return mask;
}

Bitboard rookAttacks(int sq, Bitboard occ) {
    Bitboard sub = occ & RookMask[sq];
    return RookTable[RookOffset[sq] + (int)pext(sub, RookMask[sq])];
}

Bitboard bishopAttacks(int sq, Bitboard occ) {
    Bitboard sub = occ & BishopMask[sq];
    return BishopTable[BishopOffset[sq] + (int)pext(sub, BishopMask[sq])];
}

static void initSlider(bool rook, std::vector<Bitboard>& table, int* offset, Bitboard* maskArr) {
    int total = 0;
    for (int sq = 0; sq < 64; sq++) {
        Bitboard mask = rook ? rookMaskFor(sq) : bishopMaskFor(sq);
        maskArr[sq] = mask;
        offset[sq] = total;
        total += 1 << popcount(mask);
    }
    table.assign(total, 0ULL);
    for (int sq = 0; sq < 64; sq++) {
        Bitboard mask = maskArr[sq];
        Bitboard sub = 0;
        do {
            Bitboard att = rook ? rayCast(sq, sub, RookDX, RookDY) : rayCast(sq, sub, BishopDX, BishopDY);
            int idx = offset[sq] + (int)pext(sub, mask);
            table[idx] = att;
            sub = (sub - mask) & mask;
        } while (sub != 0);
        // also fill the empty-subset case (loop above's do-while covers sub==0 on first iter already)
    }
}

static void initBetweenAndLine() {
    for (int a = 0; a < 64; a++) {
        for (int b = 0; b < 64; b++) {
            BetweenBB[a][b] = 0;
            LineBB[a][b] = 0;
            if (a == b) continue;
            int fa = fileOf(a), ra = rankOf(a), fb = fileOf(b), rb = rankOf(b);
            int df = fb - fa, dr = rb - ra;
            bool aligned = (df == 0) || (dr == 0) || (df == dr) || (df == -dr);
            if (!aligned) continue;
            int stepF = (df > 0) - (df < 0);
            int stepR = (dr > 0) - (dr < 0);
            Bitboard between = 0;
            int f = fa + stepF, r = ra + stepR;
            while (f != fb || r != rb) {
                between |= squareBB(r * 8 + f);
                f += stepF; r += stepR;
            }
            BetweenBB[a][b] = between;
            // full line: extend both directions across whole board
            Bitboard line = squareBB(a) | squareBB(b) | between;
            int f2 = fa - stepF, r2 = ra - stepR;
            while (f2 >= 0 && f2 < 8 && r2 >= 0 && r2 < 8) { line |= squareBB(r2 * 8 + f2); f2 -= stepF; r2 -= stepR; }
            f2 = fb + stepF; r2 = rb + stepR;
            while (f2 >= 0 && f2 < 8 && r2 >= 0 && r2 < 8) { line |= squareBB(r2 * 8 + f2); f2 += stepF; r2 += stepR; }
            LineBB[a][b] = line;
        }
    }
}

void initAttackTables() {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard b = squareBB(sq);
        PawnAttacks[WHITE][sq] = shift<NORTH_EAST>(b) | shift<NORTH_WEST>(b);
        PawnAttacks[BLACK][sq] = shift<SOUTH_EAST>(b) | shift<SOUTH_WEST>(b);

        Bitboard k = 0;
        int f0 = fileOf(sq), r0 = rankOf(sq);
        static const int kdf[8] = { 1,2,2,1,-1,-2,-2,-1 };
        static const int kdr[8] = { 2,1,-1,-2,-2,-1,1,2 };
        for (int i = 0; i < 8; i++) {
            int f = f0 + kdf[i], r = r0 + kdr[i];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) k |= squareBB(r * 8 + f);
        }
        KnightAttacks[sq] = k;

        Bitboard kg = 0;
        for (int df = -1; df <= 1; df++) for (int dr = -1; dr <= 1; dr++) {
            if (df == 0 && dr == 0) continue;
            int f = f0 + df, r = r0 + dr;
            if (f >= 0 && f < 8 && r >= 0 && r < 8) kg |= squareBB(r * 8 + f);
        }
        KingAttacks[sq] = kg;
    }
    initSlider(true, RookTable, RookOffset, RookMask);
    initSlider(false, BishopTable, BishopOffset, BishopMask);
    initBetweenAndLine();
}
