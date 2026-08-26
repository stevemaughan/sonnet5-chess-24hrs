#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

using Bitboard = uint64_t;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_TYPE_NB = 6, NO_PIECE_TYPE = 6 };

// Piece = color*6 + type, plus NO_PIECE sentinel
enum Piece : int {
    WP = 0, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK,
    NO_PIECE = 12
};

inline Piece makePiece(Color c, PieceType pt) { return Piece(c * 6 + pt); }
inline PieceType typeOf(Piece p) { return PieceType(p % 6); }
inline Color colorOf(Piece p) { return Color(p / 6); }

enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SQ_NONE = 64
};

inline int fileOf(int sq) { return sq & 7; }
inline int rankOf(int sq) { return sq >> 3; }
inline Square makeSquare(int file, int rank) { return Square(rank * 8 + file); }

enum : int { NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
             NORTH_EAST = 9, NORTH_WEST = 7, SOUTH_EAST = -7, SOUTH_WEST = -9 };

enum CastlingRight : int {
    WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8,
    ANY_CASTLING = 15
};

// Move encoding: 16 bits: from(6) to(6) flags(4)
// flags: 0=quiet 1=double-push 2=OO 3=OOO 4=capture 5=ep-capture
// 8=N-promo 9=B-promo 10=R-promo 11=Q-promo
// 12=N-promo-capture 13=B-promo-capture 14=R-promo-capture 15=Q-promo-capture
using Move = uint16_t;

enum MoveFlag : int {
    MF_QUIET = 0, MF_DOUBLE = 1, MF_OO = 2, MF_OOO = 3,
    MF_CAPTURE = 4, MF_EP = 5,
    MF_PROMO_N = 8, MF_PROMO_B = 9, MF_PROMO_R = 10, MF_PROMO_Q = 11,
    MF_PROMO_N_CAP = 12, MF_PROMO_B_CAP = 13, MF_PROMO_R_CAP = 14, MF_PROMO_Q_CAP = 15
};

constexpr Move NO_MOVE = 0;

inline Move makeMove(int from, int to, int flag = MF_QUIET) {
    return Move(from | (to << 6) | (flag << 12));
}
inline int moveFrom(Move m) { return m & 0x3F; }
inline int moveTo(Move m) { return (m >> 6) & 0x3F; }
inline int moveFlags(Move m) { return (m >> 12) & 0xF; }
inline bool isCapture(Move m) { return (moveFlags(m) & 4) != 0; }
inline bool isPromo(Move m) { return (moveFlags(m) & 8) != 0; }
inline bool isEnPassant(Move m) { return moveFlags(m) == MF_EP; }
inline bool isCastle(Move m) { return moveFlags(m) == MF_OO || moveFlags(m) == MF_OOO; }
inline PieceType promoType(Move m) {
    // maps flag 8/12->N, 9/13->B, 10/14->R, 11/15->Q
    static const PieceType t[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
    return t[moveFlags(m) & 3];
}

inline std::string squareToStr(int sq) {
    if (sq == SQ_NONE) return "-";
    std::string s;
    s += char('a' + fileOf(sq));
    s += char('1' + rankOf(sq));
    return s;
}

inline std::string moveToUCI(Move m) {
    std::string s = squareToStr(moveFrom(m)) + squareToStr(moveTo(m));
    if (isPromo(m)) {
        const char pc[4] = { 'n', 'b', 'r', 'q' };
        s += pc[moveFlags(m) & 3];
    }
    return s;
}

constexpr int MAX_PLY = 128;
constexpr int INF_SCORE = 32000;
constexpr int MATE_SCORE = 31000;
constexpr int MATE_IN_MAX = MATE_SCORE - MAX_PLY;
