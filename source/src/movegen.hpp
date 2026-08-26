#pragma once
#include "types.hpp"
#include "board.hpp"

struct MoveList {
    Move moves[256];
    int count = 0;
    void add(Move m) { moves[count++] = m; }
};

void generatePseudoLegal(const Board& b, MoveList& list);
void generateCaptures(const Board& b, MoveList& list); // captures + promotions, for quiescence
void generateLegal(Board& b, MoveList& list);
bool isPseudoLegalMoveLegal(Board& b, Move m); // makes/unmakes to test check
