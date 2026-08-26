#include "eval.hpp"
#include "bitboard.hpp"
#include <cstring>

// Tables below are written in "printed" order: row 0 = rank 8 ... row 7 = rank 1,
// each row a1-file..h-file left to right, matching common chess-board PST layouts
// (in the style of the well-known Michniewski "Simplified Evaluation Function" /
// PeSTO piece-square tables published on chessprogramming.org).

static const int PawnMG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};
static const int PawnEG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    80, 80, 80, 80, 80, 80, 80, 80,
    50, 50, 50, 50, 50, 50, 50, 50,
    30, 30, 30, 30, 30, 30, 30, 30,
    15, 15, 15, 15, 15, 15, 15, 15,
     5,  5,  5,  5,  5,  5,  5,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
};
static const int KnightMG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};
static const int BishopMG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};
static const int RookMG[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0,
};
static const int QueenMG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};
static const int KingMG[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20,
};
static const int KingEG[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50,
};

static int PST_MG[6][64];
static int PST_EG[6][64];

static const int MaterialMG[6] = { 100, 320, 330, 500, 900, 0 };
static const int MaterialEG[6] = { 120, 320, 330, 530, 950, 0 };
static const int PhaseWeight[6] = { 0, 1, 1, 2, 4, 0 };
constexpr int MAX_PHASE = 24;

static void flatten(const int printedTable[64], int out[64]) {
    for (int sq = 0; sq < 64; sq++) {
        int r = rankOf(sq), f = fileOf(sq);
        out[sq] = printedTable[(7 - r) * 8 + f];
    }
}

void initEval() {
    flatten(PawnMG, PST_MG[PAWN]);
    flatten(KnightMG, PST_MG[KNIGHT]);
    flatten(BishopMG, PST_MG[BISHOP]);
    flatten(RookMG, PST_MG[ROOK]);
    flatten(QueenMG, PST_MG[QUEEN]);
    flatten(KingMG, PST_MG[KING]);
    flatten(PawnEG, PST_EG[PAWN]);
    flatten(KnightMG, PST_EG[KNIGHT]);
    flatten(BishopMG, PST_EG[BISHOP]);
    flatten(RookMG, PST_EG[ROOK]);
    flatten(QueenMG, PST_EG[QUEEN]);
    flatten(KingEG, PST_EG[KING]);
}

static int mobilityScore(const Board& b, Color us) {
    Bitboard occ = b.occAll;
    int mob = 0;
    Bitboard bb = b.pieceBB[makePiece(us, KNIGHT)];
    while (bb) { int s = popLsb(bb); mob += popcount(KnightAttacks[s] & ~b.occ[us]); }
    bb = b.pieceBB[makePiece(us, BISHOP)];
    while (bb) { int s = popLsb(bb); mob += popcount(bishopAttacks(s, occ) & ~b.occ[us]); }
    bb = b.pieceBB[makePiece(us, ROOK)];
    while (bb) { int s = popLsb(bb); mob += popcount(rookAttacks(s, occ) & ~b.occ[us]); }
    bb = b.pieceBB[makePiece(us, QUEEN)];
    while (bb) { int s = popLsb(bb); mob += popcount(queenAttacks(s, occ) & ~b.occ[us]); }
    return mob;
}

int evaluate(const Board& b) {
    int mg = 0, eg = 0, phase = 0;

    for (int c = 0; c < 2; c++) {
        int sign = (c == WHITE) ? 1 : -1;
        for (int pt = PAWN; pt <= KING; pt++) {
            Bitboard bb = b.pieceBB[makePiece((Color)c, (PieceType)pt)];
            phase += popcount(bb) * PhaseWeight[pt];
            while (bb) {
                int sq = popLsb(bb);
                int psq = (c == WHITE) ? sq : (sq ^ 56);
                mg += sign * (MaterialMG[pt] + PST_MG[pt][psq]);
                eg += sign * (MaterialEG[pt] + PST_EG[pt][psq]);
            }
        }
    }

    int mob = mobilityScore(b, WHITE) - mobilityScore(b, BLACK);
    mg += mob * 3;
    eg += mob * 2;

    if (phase > MAX_PHASE) phase = MAX_PHASE;
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    return (b.sideToMove == WHITE) ? score : -score;
}
