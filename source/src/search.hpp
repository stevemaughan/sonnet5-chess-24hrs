#pragma once
#include "board.hpp"
#include "movegen.hpp"
#include "tt.hpp"
#include <atomic>
#include <cstdint>

struct SearchLimits {
    // NOT_SET means the GUI didn't send this field at all. A GUI can legitimately
    // send a zero or negative wtime/btime/movetime (clock essentially expired) —
    // that must NOT be confused with "no time control given" (which means search
    // unbounded, governed only by depth/infinite/stop). Keep these distinct.
    static constexpr int64_t NOT_SET = INT64_MIN;
    int64_t wtime = NOT_SET, btime = NOT_SET, winc = 0, binc = 0;
    int movestogo = 0;
    int64_t movetime = NOT_SET;
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
