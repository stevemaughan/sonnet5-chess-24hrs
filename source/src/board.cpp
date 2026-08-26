#include "board.hpp"
#include "zobrist.hpp"
#include <sstream>
#include <cctype>

static int CastlingRightsMask[64];

static void initCastlingMasks() {
    for (int i = 0; i < 64; i++) CastlingRightsMask[i] = ANY_CASTLING;
    CastlingRightsMask[E1] = ANY_CASTLING & ~(WHITE_OO | WHITE_OOO);
    CastlingRightsMask[A1] = ANY_CASTLING & ~WHITE_OOO;
    CastlingRightsMask[H1] = ANY_CASTLING & ~WHITE_OO;
    CastlingRightsMask[E8] = ANY_CASTLING & ~(BLACK_OO | BLACK_OOO);
    CastlingRightsMask[A8] = ANY_CASTLING & ~BLACK_OOO;
    CastlingRightsMask[H8] = ANY_CASTLING & ~BLACK_OO;
}
struct CastlingMaskInit { CastlingMaskInit() { initCastlingMasks(); } };
static CastlingMaskInit _castlingMaskInit;

void Board::addPiece(Piece p, int sq) {
    pieceBB[p] |= squareBB(sq);
    pieceOn[sq] = p;
    occ[colorOf(p)] |= squareBB(sq);
    occAll |= squareBB(sq);
    hash ^= ZobristPiece[p][sq];
}

void Board::removePiece(Piece p, int sq) {
    pieceBB[p] &= ~squareBB(sq);
    pieceOn[sq] = NO_PIECE;
    occ[colorOf(p)] &= ~squareBB(sq);
    occAll &= ~squareBB(sq);
    hash ^= ZobristPiece[p][sq];
}

void Board::movePiece(Piece p, int from, int to) {
    Bitboard fromTo = squareBB(from) | squareBB(to);
    pieceBB[p] ^= fromTo;
    occ[colorOf(p)] ^= fromTo;
    occAll ^= fromTo;
    pieceOn[from] = NO_PIECE;
    pieceOn[to] = p;
    hash ^= ZobristPiece[p][from] ^ ZobristPiece[p][to];
}

void Board::clear() {
    for (int i = 0; i < 12; i++) pieceBB[i] = 0;
    for (int i = 0; i < 64; i++) pieceOn[i] = NO_PIECE;
    occ[WHITE] = occ[BLACK] = occAll = 0;
    sideToMove = WHITE;
    castlingRights = 0;
    epSquare = SQ_NONE;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    hash = 0;
}

uint64_t Board::computeHash() const {
    uint64_t h = 0;
    for (int p = 0; p < 12; p++) {
        Bitboard b = pieceBB[p];
        while (b) { int s = popLsb(b); h ^= ZobristPiece[p][s]; }
    }
    if (sideToMove == BLACK) h ^= ZobristSide;
    h ^= ZobristCastling[castlingRights];
    if (epSquare != SQ_NONE) h ^= ZobristEpFile[fileOf(epSquare)];
    return h;
}

static Piece charToPiece(char c) {
    bool black = islower((unsigned char)c);
    char u = toupper((unsigned char)c);
    PieceType pt;
    switch (u) {
        case 'P': pt = PAWN; break;
        case 'N': pt = KNIGHT; break;
        case 'B': pt = BISHOP; break;
        case 'R': pt = ROOK; break;
        case 'Q': pt = QUEEN; break;
        case 'K': pt = KING; break;
        default: return NO_PIECE;
    }
    return makePiece(black ? BLACK : WHITE, pt);
}

static char pieceToChar(Piece p) {
    static const char w[6] = { 'P','N','B','R','Q','K' };
    static const char b[6] = { 'p','n','b','r','q','k' };
    return colorOf(p) == WHITE ? w[typeOf(p)] : b[typeOf(p)];
}

void Board::setFromFEN(const std::string& fen) {
    clear();
    std::istringstream iss(fen);
    std::string boardPart, sidePart, castlePart, epPart;
    int halfmove = 0, fullmove = 1;
    iss >> boardPart >> sidePart >> castlePart >> epPart;
    if (!(iss >> halfmove)) halfmove = 0;
    if (!(iss >> fullmove)) fullmove = 1;

    int sq = A8;
    for (char c : boardPart) {
        if (c == '/') { sq -= 16; }
        else if (isdigit((unsigned char)c)) { sq += (c - '0'); }
        else {
            Piece p = charToPiece(c);
            if (p != NO_PIECE) addPiece(p, sq);
            sq++;
        }
    }

    sideToMove = (sidePart == "b") ? BLACK : WHITE;

    for (char c : castlePart) {
        switch (c) {
            case 'K': castlingRights |= WHITE_OO; break;
            case 'Q': castlingRights |= WHITE_OOO; break;
            case 'k': castlingRights |= BLACK_OO; break;
            case 'q': castlingRights |= BLACK_OOO; break;
            default: break;
        }
    }

    if (epPart != "-" && epPart.size() == 2) {
        int f = epPart[0] - 'a';
        int r = epPart[1] - '1';
        epSquare = makeSquare(f, r);
    } else {
        epSquare = SQ_NONE;
    }

    halfmoveClock = halfmove;
    fullmoveNumber = fullmove;
    hash = computeHash();
}

std::string Board::toFEN() const {
    std::ostringstream oss;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int s = r * 8 + f;
            Piece p = pieceOn[s];
            if (p == NO_PIECE) empty++;
            else {
                if (empty) { oss << empty; empty = 0; }
                oss << pieceToChar(p);
            }
        }
        if (empty) oss << empty;
        if (r > 0) oss << '/';
    }
    oss << (sideToMove == WHITE ? " w " : " b ");
    std::string cr;
    if (castlingRights & WHITE_OO) cr += 'K';
    if (castlingRights & WHITE_OOO) cr += 'Q';
    if (castlingRights & BLACK_OO) cr += 'k';
    if (castlingRights & BLACK_OOO) cr += 'q';
    oss << (cr.empty() ? "-" : cr) << ' ';
    oss << squareToStr(epSquare) << ' ' << halfmoveClock << ' ' << fullmoveNumber;
    return oss.str();
}

bool Board::squareAttacked(int sq, Color by) const {
    if (PawnAttacks[~by][sq] & pieceBB[makePiece(by, PAWN)]) return true;
    if (KnightAttacks[sq] & pieceBB[makePiece(by, KNIGHT)]) return true;
    if (KingAttacks[sq] & pieceBB[makePiece(by, KING)]) return true;
    Bitboard bq = pieceBB[makePiece(by, BISHOP)] | pieceBB[makePiece(by, QUEEN)];
    if (bishopAttacks(sq, occAll) & bq) return true;
    Bitboard rq = pieceBB[makePiece(by, ROOK)] | pieceBB[makePiece(by, QUEEN)];
    if (rookAttacks(sq, occAll) & rq) return true;
    return false;
}

Bitboard Board::attackersTo(int sq, Bitboard occAllArg) const {
    Bitboard att = 0;
    att |= PawnAttacks[BLACK][sq] & pieceBB[WP];
    att |= PawnAttacks[WHITE][sq] & pieceBB[BP];
    att |= KnightAttacks[sq] & (pieceBB[WN] | pieceBB[BN]);
    att |= KingAttacks[sq] & (pieceBB[WK] | pieceBB[BK]);
    Bitboard b = bishopAttacks(sq, occAllArg);
    att |= b & (pieceBB[WB] | pieceBB[BB] | pieceBB[WQ] | pieceBB[BQ]);
    Bitboard r = rookAttacks(sq, occAllArg);
    att |= r & (pieceBB[WR] | pieceBB[BR] | pieceBB[WQ] | pieceBB[BQ]);
    return att;
}

void Board::makeMove(Move m, Undo& u) {
    int from = moveFrom(m), to = moveTo(m);
    int flag = moveFlags(m);
    Color us = sideToMove, them = ~us;
    Piece moved = pieceOn[from];

    u.captured = NO_PIECE;
    u.castlingRights = castlingRights;
    u.epSquare = epSquare;
    u.halfmoveClock = halfmoveClock;
    u.hash = hash;

    hash ^= ZobristCastling[castlingRights];
    if (epSquare != SQ_NONE) hash ^= ZobristEpFile[fileOf(epSquare)];

    halfmoveClock++;
    if (typeOf(moved) == PAWN) halfmoveClock = 0;

    if (flag == MF_EP) {
        int capSq = to + (us == WHITE ? SOUTH : NORTH);
        u.captured = pieceOn[capSq];
        removePiece(u.captured, capSq);
        halfmoveClock = 0;
    } else if (isCapture(m)) {
        u.captured = pieceOn[to];
        removePiece(u.captured, to);
        halfmoveClock = 0;
    }

    movePiece(moved, from, to);

    if (isPromo(m)) {
        removePiece(moved, to);
        addPiece(makePiece(us, promoType(m)), to);
    }

    if (flag == MF_OO) {
        int rFrom = (us == WHITE ? H1 : H8), rTo = (us == WHITE ? F1 : F8);
        movePiece(makePiece(us, ROOK), rFrom, rTo);
    } else if (flag == MF_OOO) {
        int rFrom = (us == WHITE ? A1 : A8), rTo = (us == WHITE ? D1 : D8);
        movePiece(makePiece(us, ROOK), rFrom, rTo);
    }

    castlingRights &= CastlingRightsMask[from] & CastlingRightsMask[to];
    hash ^= ZobristCastling[castlingRights];

    epSquare = SQ_NONE;
    if (flag == MF_DOUBLE) {
        epSquare = from + (us == WHITE ? NORTH : SOUTH);
        hash ^= ZobristEpFile[fileOf(epSquare)];
    }

    hash ^= ZobristSide;
    sideToMove = them;
    if (us == BLACK) fullmoveNumber++;
}

void Board::unmakeMove(Move m, const Undo& u) {
    sideToMove = ~sideToMove;
    Color us = sideToMove;
    int from = moveFrom(m), to = moveTo(m);
    int flag = moveFlags(m);
    if (us == BLACK) fullmoveNumber--;

    if (isPromo(m)) {
        Piece promoPiece = pieceOn[to];
        removePiece(promoPiece, to);
        addPiece(makePiece(us, PAWN), from);
    } else {
        Piece moved = pieceOn[to];
        movePiece(moved, to, from);
    }

    if (flag == MF_OO) {
        int rFrom = (us == WHITE ? H1 : H8), rTo = (us == WHITE ? F1 : F8);
        movePiece(makePiece(us, ROOK), rTo, rFrom);
    } else if (flag == MF_OOO) {
        int rFrom = (us == WHITE ? A1 : A8), rTo = (us == WHITE ? D1 : D8);
        movePiece(makePiece(us, ROOK), rTo, rFrom);
    }

    if (flag == MF_EP) {
        int capSq = to + (us == WHITE ? SOUTH : NORTH);
        addPiece(u.captured, capSq);
    } else if (u.captured != NO_PIECE) {
        addPiece(u.captured, to);
    }

    castlingRights = u.castlingRights;
    epSquare = u.epSquare;
    halfmoveClock = u.halfmoveClock;
    hash = u.hash;
}

void Board::makeNullMove(Undo& u) {
    u.epSquare = epSquare;
    u.hash = hash;
    u.castlingRights = castlingRights;
    u.halfmoveClock = halfmoveClock;
    u.captured = NO_PIECE;
    if (epSquare != SQ_NONE) { hash ^= ZobristEpFile[fileOf(epSquare)]; epSquare = SQ_NONE; }
    hash ^= ZobristSide;
    sideToMove = ~sideToMove;
    halfmoveClock++;
}

void Board::unmakeNullMove(const Undo& u) {
    sideToMove = ~sideToMove;
    epSquare = u.epSquare;
    hash = u.hash;
    halfmoveClock = u.halfmoveClock;
}
