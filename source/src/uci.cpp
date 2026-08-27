#include "init.hpp"
#include "board.hpp"
#include "movegen.hpp"
#include "search.hpp"
#include "eval.hpp"
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static const char* ENGINE_NAME = "Sonnet 5 chess 24hrs";
static const char* ENGINE_AUTHOR = "Sonnet 5";

static Board g_board;

static std::mutex g_queueMutex;
static std::condition_variable g_queueCv;
static std::deque<std::string> g_queue;
static bool g_quitRequested = false;

static Move parseUCIMove(Board& b, const std::string& s) {
    MoveList list;
    generateLegal(b, list);
    for (int i = 0; i < list.count; i++)
        if (moveToUCI(list.moves[i]) == s) return list.moves[i];
    return NO_MOVE;
}

static void cmdPosition(std::istringstream& iss) {
    std::string tok;
    iss >> tok;
    std::string fen;
    if (tok == "startpos") {
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        iss >> tok; // may be "moves" or nothing
    } else if (tok == "fen") {
        std::vector<std::string> parts;
        while (iss >> tok && tok != "moves") parts.push_back(tok);
        for (size_t i = 0; i < parts.size(); i++) fen += (i ? " " : "") + parts[i];
    } else {
        return;
    }

    g_board.setFromFEN(fen);
    g_search.resetGameHistory();
    g_search.pushGameHistory(g_board.hash);

    if (tok == "moves") {
        std::string mv;
        while (iss >> mv) {
            Move m = parseUCIMove(g_board, mv);
            if (m == NO_MOVE) break;
            Undo u;
            g_board.makeMove(m, u);
            g_search.pushGameHistory(g_board.hash);
        }
    }
}

static void cmdGo(std::istringstream& iss) {
    SearchLimits limits;
    std::string tok;
    while (iss >> tok) {
        if (tok == "wtime") iss >> limits.wtime;
        else if (tok == "btime") iss >> limits.btime;
        else if (tok == "winc") iss >> limits.winc;
        else if (tok == "binc") iss >> limits.binc;
        else if (tok == "movestogo") iss >> limits.movestogo;
        else if (tok == "movetime") iss >> limits.movetime;
        else if (tok == "depth") iss >> limits.depth;
        else if (tok == "infinite") limits.infinite = true;
        else if (tok == "ponder") { /* not supported, ignore */ }
        // nodes, mate, searchmoves: not required, ignored
    }
    g_search.go(g_board, limits); // synchronous, runs on this (main) thread
}

static void cmdSetOption(std::istringstream& iss) {
    std::string tok;
    iss >> tok; // "name"
    std::string name;
    while (iss >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
    std::string value;
    while (iss >> tok) value += (value.empty() ? "" : " ") + tok;

    if (name == "Hash") {
        try { g_search.setHashMB((size_t)std::max(1, std::stoi(value))); } catch (...) {}
    } else if (name == "Move Overhead") {
        try { g_search.setMoveOverhead(std::max(0, std::stoi(value))); } catch (...) {}
    }
    // Unknown options are silently ignored, per UCI spec.
}

static void printUCI() {
    std::lock_guard<std::mutex> lk(g_coutMutex);
    std::cout << "id name " << ENGINE_NAME << "\n";
    std::cout << "id author " << ENGINE_AUTHOR << "\n";
    std::cout << "option name Hash type spin default 256 min 1 max 16384\n";
    std::cout << "option name Move Overhead type spin default 40 min 0 max 5000\n";
    std::cout << "uciok\n" << std::flush;
}

// Reader thread: handles stdin. "stop"/"quit"/"isready" are acted on immediately
// (so they work while a search is running on the main thread); everything else
// is queued for the main thread to process sequentially.
static void readerThreadFunc() {
    std::string line;
    bool quitSent = false;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "stop") {
            g_search.requestStop();
        } else if (cmd == "isready") {
            std::lock_guard<std::mutex> lk(g_coutMutex);
            std::cout << "readyok\n" << std::flush;
        } else if (cmd == "quit") {
            g_search.requestStop();
            {
                std::lock_guard<std::mutex> lk(g_queueMutex);
                g_quitRequested = true;
                g_queue.push_back("quit");
            }
            g_queueCv.notify_all();
            quitSent = true;
            break;
        } else {
            std::lock_guard<std::mutex> lk(g_queueMutex);
            g_queue.push_back(line);
            g_queueCv.notify_all();
        }
    }
    if (!quitSent) {
        // EOF with no explicit quit: make sure the main thread wakes up and exits.
        g_search.requestStop();
        std::lock_guard<std::mutex> lk(g_queueMutex);
        g_quitRequested = true;
        g_queue.push_back("quit");
        g_queueCv.notify_all();
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    engineInit();
    initEval();
    g_search.setHashMB(256);
    g_board.setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    g_search.resetGameHistory();
    g_search.pushGameHistory(g_board.hash);

    std::thread reader(readerThreadFunc);

    while (true) {
        std::string line;
        {
            std::unique_lock<std::mutex> lk(g_queueMutex);
            g_queueCv.wait(lk, [] { return !g_queue.empty(); });
            line = g_queue.front();
            g_queue.pop_front();
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "uci") {
            printUCI();
        } else if (cmd == "ucinewgame") {
            g_search.newGame();
            g_board.setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            g_search.resetGameHistory();
            g_search.pushGameHistory(g_board.hash);
        } else if (cmd == "position") {
            cmdPosition(iss);
        } else if (cmd == "setoption") {
            cmdSetOption(iss);
        } else if (cmd == "go") {
            cmdGo(iss);
        } else if (cmd == "quit") {
            break;
        }
        // unknown/empty commands and "ponderhit": ignore
    }

    reader.join();
    return 0;
}
