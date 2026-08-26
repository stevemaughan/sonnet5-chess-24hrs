#pragma once
#include "board.hpp"
#include "movegen.hpp"
#include "tt.hpp"
#include <atomic>
#include <cstdint>

struct SearchLimits {
    int64_t wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = 0;
    int64_t movetime = -1;
    int depth = -1;
    bool infinite = false;
};

// Thin public wrapper; all search state lives as file-scope globals in search.cpp
// (proven more robust under this toolchain than storing large arrays as class members).
class Search {
public:
    void newGame();
    void setHashMB(size_t mb);
    void setMoveOverhead(int ms);

    void resetGameHistory();
    void pushGameHistory(uint64_t h);

    void go(Board board, const SearchLimits& limits);
    void requestStop();
    bool isSearching() const;
};

extern Search g_search;
