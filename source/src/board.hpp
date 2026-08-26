#pragma once
#include "types.hpp"
#include "bitboard.hpp"
#include <string>

struct Undo {
    Piece captured;
    int castlingRights;
    int epSquare;
    int halfmoveClock;
    uint64_t hash;
};

struct Board {
    Bitboard pieceBB[12];
    Piece pieceOn[64];
    Bitboard occ[2];
    Bitboard occAll;
    Color sideToMove;
    int castlingRights;
    int epSquare;
    int halfmoveClock;
    int fullmoveNumber;
    uint64_t hash;

    void clear();
    void setFromFEN(const std::string& fen);
    std::string toFEN() const;

    bool squareAttacked(int sq, Color by) const;
    bool inCheck(Color c) const { return squareAttacked(lsb(pieceBB[makePiece(c, KING)]), ~c); }
    Bitboard attackersTo(int sq, Bitboard occ) const;

    void makeMove(Move m, Undo& u);
    void unmakeMove(Move m, const Undo& u);
    void makeNullMove(Undo& u);
    void unmakeNullMove(const Undo& u);

    uint64_t computeHash() const;

    void addPiece(Piece p, int sq);
    void removePiece(Piece p, int sq);
    void movePiece(Piece p, int from, int to);
};
