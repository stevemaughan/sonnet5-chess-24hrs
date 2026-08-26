#include "movegen.hpp"

static void addPromotions(MoveList& list, int from, int to, bool capture) {
    if (capture) {
        list.add(makeMove(from, to, MF_PROMO_Q_CAP));
        list.add(makeMove(from, to, MF_PROMO_R_CAP));
        list.add(makeMove(from, to, MF_PROMO_B_CAP));
        list.add(makeMove(from, to, MF_PROMO_N_CAP));
    } else {
        list.add(makeMove(from, to, MF_PROMO_Q));
        list.add(makeMove(from, to, MF_PROMO_R));
        list.add(makeMove(from, to, MF_PROMO_B));
        list.add(makeMove(from, to, MF_PROMO_N));
    }
}

template<bool CapturesOnly>
static void genPawns(const Board& b, MoveList& list) {
    Color us = b.sideToMove, them = ~us;
    Bitboard pawns = b.pieceBB[makePiece(us, PAWN)];
    int fwd = (us == WHITE) ? NORTH : SOUTH;
    int startRank = (us == WHITE) ? 1 : 6;
    int promoRank = (us == WHITE) ? 7 : 0;

    Bitboard bb = pawns;
    while (bb) {
        int sq = popLsb(bb);
        int to = sq + fwd;
        if (!CapturesOnly && to >= 0 && to < 64 && !(b.occAll & squareBB(to))) {
            if (rankOf(to) == promoRank) {
                addPromotions(list, sq, to, false);
            } else {
                list.add(makeMove(sq, to, MF_QUIET));
                if (rankOf(sq) == startRank) {
                    int to2 = to + fwd;
                    if (!(b.occAll & squareBB(to2))) list.add(makeMove(sq, to2, MF_DOUBLE));
                }
            }
        }
        Bitboard caps = PawnAttacks[us][sq] & b.occ[them];
        while (caps) {
            int t = popLsb(caps);
            if (rankOf(t) == promoRank) addPromotions(list, sq, t, true);
            else list.add(makeMove(sq, t, MF_CAPTURE));
        }
        if (b.epSquare != SQ_NONE && (PawnAttacks[us][sq] & squareBB(b.epSquare)))
            list.add(makeMove(sq, b.epSquare, MF_EP));
        // capture promotions already covered above; if CapturesOnly, also include promo pushes (they're forcing)
        if (CapturesOnly) {
            if (to >= 0 && to < 64 && rankOf(to) == promoRank && !(b.occAll & squareBB(to))) {
                addPromotions(list, sq, to, false);
            }
        }
    }
}

static void genKnights(const Board& b, MoveList& list, bool capturesOnly) {
    Color us = b.sideToMove;
    Bitboard bb = b.pieceBB[makePiece(us, KNIGHT)];
    while (bb) {
        int sq = popLsb(bb);
        Bitboard att = KnightAttacks[sq] & ~b.occ[us];
        if (capturesOnly) att &= b.occ[~us];
        while (att) {
            int to = popLsb(att);
            list.add(makeMove(sq, to, (b.occ[~us] & squareBB(to)) ? MF_CAPTURE : MF_QUIET));
        }
    }
}

static void genSliders(const Board& b, MoveList& list, PieceType pt, bool capturesOnly) {
    Color us = b.sideToMove;
    Bitboard bb = b.pieceBB[makePiece(us, pt)];
    while (bb) {
        int sq = popLsb(bb);
        Bitboard att = (pt == BISHOP) ? bishopAttacks(sq, b.occAll)
                     : (pt == ROOK)   ? rookAttacks(sq, b.occAll)
                                      : queenAttacks(sq, b.occAll);
        att &= ~b.occ[us];
        if (capturesOnly) att &= b.occ[~us];
        while (att) {
            int to = popLsb(att);
            list.add(makeMove(sq, to, (b.occ[~us] & squareBB(to)) ? MF_CAPTURE : MF_QUIET));
        }
    }
}

static void genKing(const Board& b, MoveList& list, bool capturesOnly) {
    Color us = b.sideToMove;
    int sq = lsb(b.pieceBB[makePiece(us, KING)]);
    Bitboard att = KingAttacks[sq] & ~b.occ[us];
    if (capturesOnly) att &= b.occ[~us];
    while (att) {
        int to = popLsb(att);
        list.add(makeMove(sq, to, (b.occ[~us] & squareBB(to)) ? MF_CAPTURE : MF_QUIET));
    }
    if (capturesOnly) return;

    if (us == WHITE) {
        if ((b.castlingRights & WHITE_OO) &&
            !(b.occAll & (squareBB(F1) | squareBB(G1))) &&
            !b.squareAttacked(E1, BLACK) && !b.squareAttacked(F1, BLACK) && !b.squareAttacked(G1, BLACK))
            list.add(makeMove(E1, G1, MF_OO));
        if ((b.castlingRights & WHITE_OOO) &&
            !(b.occAll & (squareBB(B1) | squareBB(C1) | squareBB(D1))) &&
            !b.squareAttacked(E1, BLACK) && !b.squareAttacked(D1, BLACK) && !b.squareAttacked(C1, BLACK))
            list.add(makeMove(E1, C1, MF_OOO));
    } else {
        if ((b.castlingRights & BLACK_OO) &&
            !(b.occAll & (squareBB(F8) | squareBB(G8))) &&
            !b.squareAttacked(E8, WHITE) && !b.squareAttacked(F8, WHITE) && !b.squareAttacked(G8, WHITE))
            list.add(makeMove(E8, G8, MF_OO));
        if ((b.castlingRights & BLACK_OOO) &&
            !(b.occAll & (squareBB(B8) | squareBB(C8) | squareBB(D8))) &&
            !b.squareAttacked(E8, WHITE) && !b.squareAttacked(D8, WHITE) && !b.squareAttacked(C8, WHITE))
            list.add(makeMove(E8, C8, MF_OOO));
    }
}

void generatePseudoLegal(const Board& b, MoveList& list) {
    genPawns<false>(b, list);
    genKnights(b, list, false);
    genSliders(b, list, BISHOP, false);
    genSliders(b, list, ROOK, false);
    genSliders(b, list, QUEEN, false);
    genKing(b, list, false);
}

void generateCaptures(const Board& b, MoveList& list) {
    genPawns<true>(b, list);
    genKnights(b, list, true);
    genSliders(b, list, BISHOP, true);
    genSliders(b, list, ROOK, true);
    genSliders(b, list, QUEEN, true);
    genKing(b, list, true);
}

bool isPseudoLegalMoveLegal(Board& b, Move m) {
    Color us = b.sideToMove;
    Undo u;
    b.makeMove(m, u);
    bool legal = !b.inCheck(us);
    b.unmakeMove(m, u);
    return legal;
}

void generateLegal(Board& b, MoveList& list) {
    MoveList pseudo;
    generatePseudoLegal(b, pseudo);
    for (int i = 0; i < pseudo.count; i++) {
        if (isPseudoLegalMoveLegal(b, pseudo.moves[i]))
            list.add(pseudo.moves[i]);
    }
}
