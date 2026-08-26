#pragma once
#include "types.hpp"

extern uint64_t ZobristPiece[12][64];
extern uint64_t ZobristSide;
extern uint64_t ZobristCastling[16];
extern uint64_t ZobristEpFile[8];

void initZobrist();
