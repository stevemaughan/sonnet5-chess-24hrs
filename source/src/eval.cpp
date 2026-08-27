#include "eval.hpp"
#include "bitboard.hpp"
#include <cstring>
#include <algorithm>

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
static Bitboard PassedMask[COLOR_NB][64];
static Bitboard AdjFiles[8];

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

static void initEvalMasks() {
    for (int f = 0; f < 8; f++) {
        Bitboard m = 0;
        if (f > 0) m |= fileBB(f - 1);
        if (f < 7) m |= fileBB(f + 1);
        AdjFiles[f] = m;
    }
    for (int sq = 0; sq < 64; sq++) {
        int f = fileOf(sq), r = rankOf(sq);
        Bitboard files = fileBB(f) | AdjFiles[f];
        Bitboard whiteMask = 0, blackMask = 0;
        for (int rr = r + 1; rr < 8; rr++) whiteMask |= (files & rankBB(rr));
        for (int rr = r - 1; rr >= 0; rr--) blackMask |= (files & rankBB(rr));
        PassedMask[WHITE][sq] = whiteMask;
        PassedMask[BLACK][sq] = blackMask;
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
    initEvalMasks();
}

static int chebyshevDist(int sq1, int sq2) {
    int df = fileOf(sq1) - fileOf(sq2), dr = rankOf(sq1) - rankOf(sq2);
    return std::max(std::abs(df), std::abs(dr));
}

static void pawnStructure(const Board& b, Color us, int& mg, int& eg) {
    Color them = ~us;
    Bitboard ownPawns = b.pieceBB[makePiece(us, PAWN)];
    Bitboard enemyPawns = b.pieceBB[makePiece(them, PAWN)];
    Bitboard ownRooks = b.pieceBB[makePiece(us, ROOK)];
    int ourKing = lsb(b.pieceBB[makePiece(us, KING)]);
    int theirKing = lsb(b.pieceBB[makePiece(them, KING)]);

    Bitboard bb = ownPawns;
    while (bb) {
        int sq = popLsb(bb);
        int f = fileOf(sq);

        if (!(PassedMask[us][sq] & enemyPawns)) {
            int rank = rankOf(sq);
            int advance = (us == WHITE) ? rank : 7 - rank;
            static const int passedMG[8] = { 0, 5, 10, 20, 35, 55, 80, 0 };
            static const int passedEG[8] = { 0, 10, 20, 40, 65, 100, 150, 0 };
            mg += passedMG[advance];
            eg += passedEG[advance];

            // King proximity to the passed pawn's promotion square matters a
            // lot in the endgame: reward our king being close (can escort),
            // penalize the enemy king being close (can blockade/win it).
            int promoSq = makeSquare(f, us == WHITE ? 7 : 0);
            eg += (chebyshevDist(theirKing, promoSq) - chebyshevDist(ourKing, promoSq)) * 5;

            // Rook behind a passed pawn (Tarrasch rule) supports its advance.
            Bitboard rooksOnFile = ownRooks & fileBB(f);
            while (rooksOnFile) {
                int rsq = popLsb(rooksOnFile);
                bool behind = (us == WHITE) ? (rankOf(rsq) < rank) : (rankOf(rsq) > rank);
                if (behind) { mg += 10; eg += 20; }
            }
        }
        if (!(AdjFiles[f] & ownPawns)) { mg -= 12; eg -= 18; }
    }
    for (int f = 0; f < 8; f++) {
        int cnt = popcount(ownPawns & fileBB(f));
        if (cnt > 1) { mg -= 10 * (cnt - 1); eg -= 20 * (cnt - 1); }
    }
}

static Bitboard pawnAttacksBB(Bitboard pawns, Color c) {
    return (c == WHITE) ? (shift<NORTH_EAST>(pawns) | shift<NORTH_WEST>(pawns))
                         : (shift<SOUTH_EAST>(pawns) | shift<SOUTH_WEST>(pawns));
}

static void pieceBonuses(const Board& b, Color us, int& mg, int& eg) {
    Color them = ~us;
    Bitboard ownPawns = b.pieceBB[makePiece(us, PAWN)];
    Bitboard enemyPawns = b.pieceBB[makePiece(them, PAWN)];

    if (popcount(b.pieceBB[makePiece(us, BISHOP)]) >= 2) { mg += 30; eg += 45; }

    // Knight outposts: a knight defended by a pawn, on a square no enemy
    // pawn can ever challenge, deep in enemy territory, is a long-term asset.
    {
        Bitboard ownPawnAttacks = pawnAttacksBB(ownPawns, us);
        Bitboard enemyPawnAttacks = pawnAttacksBB(enemyPawns, them);
        Bitboard knights = b.pieceBB[makePiece(us, KNIGHT)];
        while (knights) {
            int sq = popLsb(knights);
            int rank = rankOf(sq);
            bool advanced = (us == WHITE) ? (rank >= 3 && rank <= 5) : (rank >= 2 && rank <= 4);
            if (advanced && (ownPawnAttacks & squareBB(sq)) && !(enemyPawnAttacks & squareBB(sq))) {
                mg += 18; eg += 10;
            }
        }
        Bitboard bishopsForOutpost = b.pieceBB[makePiece(us, BISHOP)];
        while (bishopsForOutpost) {
            int sq = popLsb(bishopsForOutpost);
            int rank = rankOf(sq);
            bool advanced = (us == WHITE) ? (rank >= 3 && rank <= 5) : (rank >= 2 && rank <= 4);
            if (advanced && (ownPawnAttacks & squareBB(sq)) && !(enemyPawnAttacks & squareBB(sq))) {
                mg += 12; eg += 6;
            }
        }
    }

    Bitboard rooks = b.pieceBB[makePiece(us, ROOK)];
    while (rooks) {
        int sq = popLsb(rooks);
        int f = fileOf(sq);
        bool ownOnFile = (ownPawns & fileBB(f)) != 0;
        bool enemyOnFile = (enemyPawns & fileBB(f)) != 0;
        if (!ownOnFile && !enemyOnFile) { mg += 20; eg += 10; }
        else if (!ownOnFile) { mg += 10; eg += 5; }
    }

    int ksq = lsb(b.pieceBB[makePiece(us, KING)]);
    int kf = fileOf(ksq), kr = rankOf(ksq);
    int shieldRank = (us == WHITE) ? kr + 1 : kr - 1;
    if (shieldRank >= 0 && shieldRank < 8) {
        int shieldCount = 0;
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++)
            if (ownPawns & squareBB(shieldRank * 8 + f)) shieldCount++;
        mg += (shieldCount - 3) * 10;
    }

    // Open/semi-open files next to our own king are highways for enemy
    // rooks and queens, independent of whether the immediate shield square
    // itself is missing a pawn.
    for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
        bool ownOnFile = (ownPawns & fileBB(f)) != 0;
        bool enemyOnFile = (enemyPawns & fileBB(f)) != 0;
        if (!ownOnFile && !enemyOnFile) mg -= 20;
        else if (!ownOnFile) mg -= 10;
    }

    // King attack pressure: weight enemy pieces that bear on the squares
    // around our king; a lone attacker is largely harmless, so the penalty
    // only kicks in once at least two pieces are involved (classic "attack
    // units" idea from CPW's King Safety article).
    Bitboard zone = KingAttacks[ksq] | squareBB(ksq);
    Bitboard occ = b.occAll;
    int attackUnits = 0, attackerCount = 0;

    Bitboard n = b.pieceBB[makePiece(them, KNIGHT)];
    while (n) { int s = popLsb(n); if (KnightAttacks[s] & zone) { attackUnits += 2; attackerCount++; } }
    Bitboard bi = b.pieceBB[makePiece(them, BISHOP)];
    while (bi) { int s = popLsb(bi); if (bishopAttacks(s, occ) & zone) { attackUnits += 2; attackerCount++; } }
    Bitboard r = b.pieceBB[makePiece(them, ROOK)];
    while (r) { int s = popLsb(r); if (rookAttacks(s, occ) & zone) { attackUnits += 3; attackerCount++; } }
    Bitboard q = b.pieceBB[makePiece(them, QUEEN)];
    while (q) { int s = popLsb(q); if (queenAttacks(s, occ) & zone) { attackUnits += 5; attackerCount++; } }

    if (attackerCount >= 2) {
        int penalty = std::min(400, attackUnits * attackUnits / 2);
        mg -= penalty;
    }
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

    int wmg = 0, weg = 0, bmg = 0, beg = 0;
    pawnStructure(b, WHITE, wmg, weg);
    pieceBonuses(b, WHITE, wmg, weg);
    pawnStructure(b, BLACK, bmg, beg);
    pieceBonuses(b, BLACK, bmg, beg);
    mg += wmg - bmg;
    eg += weg - beg;

    if (phase > MAX_PHASE) phase = MAX_PHASE;
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    // Small bonus for the side to move (having the tempo is a real, if
    // modest, advantage).
    return ((b.sideToMove == WHITE) ? score : -score) + 10;
}
