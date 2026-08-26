#include "init.hpp"
#include "bitboard.hpp"
#include "zobrist.hpp"

void engineInit() {
    initAttackTables();
    initZobrist();
}
