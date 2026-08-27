#include "search.hpp"
#include "eval.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>

Search g_search;
std::mutex g_coutMutex;

using clock_t_ = std::chrono::steady_clock;

static const int PieceVal[6] = { 100, 320, 330, 500, 900, 20000 };

// Small contempt: a draw (repetition/50-move) is scored as slightly worse
// than dead-even for the side to move at that node, rather than exactly 0.
// This discourages steering into a draw when an alternative exists, without
// materially affecting play in genuinely balanced or lost positions (the
// margin is far below normal eval noise). Standard, low-risk technique.
static const int CONTEMPT = 10;

static const int MAX_HIST = 1400;
static uint64_t gameHistoryStack[MAX_HIST];
static int gameHistoryLen = 0;

static uint64_t historyStack[MAX_HIST];
static int historyTop = 0;

static std::atomic<bool> stopFlag{false};

// Closes a race between the out-of-band reader thread (handles stop/quit
// immediately) and the queued "go" the main thread executes: if stop/quit is
// processed for a "go" that's still sitting in the queue (not yet started),
// requestStop() sets stopFlag=true, but go() used to unconditionally reset it
// to false at entry — silently losing the stop request. For a time-bounded
// search this only delayed the response; for "go infinite" (no time bound at
// all) it meant the search would run forever with nothing left to ever stop
// it, since the reader thread has already exited after handling "quit".
// Fix: track how many "go" lines have been queued (bumped by the reader
// thread via noteGoQueued(), called before the line is pushed) versus how
// many have actually started (bumped here in go()); requestStop() records the
// queued-count snapshot at the moment of the stop, and go() only clears
// stopFlag if its own dequeue index is past that snapshot — i.e. only if no
// stop was requested for this specific go or an earlier still-pending one.
static std::atomic<int> goEnqueuedCount{0};
static std::atomic<int> goDequeuedCount{0};
static std::atomic<int> stopAppliesThroughGo{0};
static std::atomic<bool> searchingFlag{false};

static uint64_t nodeCount = 0;
static clock_t_::time_point startTime;
static int64_t softMs = -1, hardMs = -1;
static bool limited = false;
static int moveOverheadMs = 60;

static Move killers[MAX_PLY][2];
static int historyTable[12][64];
static Move counterMoveTable[12][64];
// Continuation history: indexed by [parent piece][parent to-square][this
// piece][this to-square]. Distinct from counterMoveTable (which stores only
// a single best-response move per parent context) — this tracks a full
// score per (parent-context, candidate-move) pair, the same way plain
// history tracks a score per move alone, giving a much richer ordering
// signal for "what tends to work after this specific reply" beyond just
// the single best-known response.
static int contHistory[12][64][12][64];
// Capture history, indexed by [attacker piece][to-square]. MVV-LVA/SEE
// already order captures well by material outcome; this adds a learned
// tiebreaker among captures of similar material value (kept deliberately
// small relative to the MVV-LVA score spread — see scoreCapture — so it
// only refines ordering, never overrides the material-based hierarchy).
static int captureHistory[12][64];
static Move searchStackMove[MAX_PLY];
// Tracks the best root move found so far during the CURRENT depth's root
// search, updated as soon as a root move's full comparison completes (not
// just when the whole iteration finishes) — this lets go() recover a
// meaningful move if a depth-1 iteration itself gets interrupted mid-loop,
// instead of falling back to an arbitrary first-generated move.
static Move rootBestMoveSoFar = NO_MOVE;

static inline void pushHistoryKey(uint64_t h) { if (historyTop < MAX_HIST) historyStack[historyTop++] = h; }
static inline void popHistoryKey() { if (historyTop > 0) historyTop--; }

static bool isRepetition(const Board& b) {
    if (b.halfmoveClock >= 100) return true;
    int limit = historyTop - 1 - b.halfmoveClock;
    if (limit < 0) limit = 0;
    for (int i = historyTop - 3; i >= limit; i -= 2)
        if (historyStack[i] == b.hash) return true;
    return false;
}

static void computeTimeBudget(const SearchLimits& limits, Color us) {
    bool bothClocksUnset = limits.wtime == SearchLimits::NOT_SET && limits.btime == SearchLimits::NOT_SET;
    if (limits.infinite || (limits.depth > 0 && limits.movetime == SearchLimits::NOT_SET && bothClocksUnset)) {
        limited = false; softMs = hardMs = -1; return;
    }
    if (limits.movetime != SearchLimits::NOT_SET) {
        // A GUI can legitimately send a tiny or non-positive movetime; clamp
        // rather than treat it as "no limit".
        int64_t mt = std::max<int64_t>(1, limits.movetime);
        limited = true;
        hardMs = std::max<int64_t>(1, mt - 15 - moveOverheadMs);
        softMs = std::max<int64_t>(1, mt - 30 - moveOverheadMs);
        return;
    }
    int64_t myTimeRaw = (us == WHITE) ? limits.wtime : limits.btime;
    if (myTimeRaw == SearchLimits::NOT_SET) { limited = false; softMs = hardMs = -1; return; }

    int64_t myInc = (us == WHITE) ? limits.winc : limits.binc;
    if (myInc < 0) myInc = 0;
    // A clock at or past zero is still a real, bounded situation — clamp to a
    // minimal positive value instead of (incorrectly) treating it as unlimited.
    int64_t myTime = std::max<int64_t>(1, myTimeRaw);

    limited = true;
    int mtg = limits.movestogo > 0 ? limits.movestogo : 30;
    int64_t alloc = myTime / mtg + (myInc * 4) / 5;
    int64_t safety = 60 + moveOverheadMs;
    int64_t maxUse = myTime - safety;
    if (maxUse < 5) maxUse = 5;
    if (alloc > maxUse) alloc = maxUse;
    int64_t capFraction = myTime * 2 / 5;
    if (alloc > capFraction) alloc = capFraction;
    if (alloc < 1) alloc = 1;
    softMs = alloc;
    hardMs = std::min<int64_t>(maxUse, alloc * 3 + 50);
    if (hardMs < softMs) hardMs = softMs;
    if (myTime < 200) {
        softMs = std::min<int64_t>(softMs, myTime / 3 + 1);
        hardMs = std::min<int64_t>(hardMs, myTime > 20 ? myTime - 15 : 5);
        if (hardMs < 1) hardMs = 1;
        if (softMs > hardMs) softMs = hardMs;
    }
}

static void checkTime() {
    if (stopFlag.load(std::memory_order_relaxed)) return;
    if (!limited) return;
    auto now = clock_t_::now();
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    if (elapsed >= hardMs) stopFlag.store(true, std::memory_order_relaxed);
}

static int scoreCapture(const Board& b, Move m) {
    PieceType victim = isEnPassant(m) ? PAWN : typeOf(b.pieceOn[moveTo(m)]);
    PieceType attacker = typeOf(b.pieceOn[moveFrom(m)]);
    int score = 1000000 + PieceVal[victim] * 10 - PieceVal[attacker];
    if (isPromo(m)) score += PieceVal[promoType(m)];
    Piece ap = b.pieceOn[moveFrom(m)];
    score += captureHistory[ap][moveTo(m)] / 16;
    return score;
}

// Static Exchange Evaluation: material result of the capture sequence on the
// target square, assuming both sides recapture with their least valuable
// attacker each time. Standard "swap algorithm" (chessprogramming.org).
static int see(const Board& b, Move m) {
    int from = moveFrom(m), to = moveTo(m);
    Color us = colorOf(b.pieceOn[from]);
    Bitboard occ = b.occAll;
    int capturedVal;
    if (isEnPassant(m)) {
        capturedVal = PieceVal[PAWN];
        int capSq = to + (us == WHITE ? SOUTH : NORTH);
        occ &= ~squareBB(capSq);
    } else if (isCapture(m)) {
        capturedVal = PieceVal[typeOf(b.pieceOn[to])];
    } else {
        capturedVal = 0;
    }

    PieceType curType = typeOf(b.pieceOn[from]);
    occ &= ~squareBB(from);
    Color side = ~us;

    int gain[32];
    gain[0] = capturedVal;
    int d = 0;

    Bitboard attackers = b.attackersTo(to, occ);

    while (true) {
        Bitboard sideAtt = attackers & b.occ[side] & occ;
        if (!sideAtt) break;
        PieceType nextType = NO_PIECE_TYPE;
        Bitboard nextBB = 0;
        for (int pt = PAWN; pt <= KING; pt++) {
            Bitboard cand = sideAtt & b.pieceBB[makePiece(side, (PieceType)pt)];
            if (cand) { nextType = (PieceType)pt; nextBB = cand & (0ULL - cand); break; }
        }
        if (d + 1 >= 32) break;
        d++;
        gain[d] = PieceVal[curType] - gain[d - 1];
        if (std::max(-gain[d - 1], gain[d]) < 0) break;
        occ &= ~nextBB;
        attackers = b.attackersTo(to, occ);
        curType = nextType;
        side = ~side;
    }
    while (d > 0) { gain[d - 1] = -std::max(-gain[d - 1], gain[d]); d--; }
    return gain[0];
}

static int scoreMove(const Board& b, Move m, Move ttMove, int ply) {
    if (m == ttMove) return 2000000;
    if (isCapture(m)) return scoreCapture(b, m);
    if (isPromo(m)) return 900000 + PieceVal[promoType(m)];
    if (m == killers[ply][0]) return 800000;
    if (m == killers[ply][1]) return 799000;
    int contScore = 0;
    if (ply > 0) {
        Move parentMove = searchStackMove[ply - 1];
        if (parentMove != NO_MOVE) {
            Piece parentPiece = b.pieceOn[moveTo(parentMove)];
            if (counterMoveTable[parentPiece][moveTo(parentMove)] == m) return 750000;
            Piece p = b.pieceOn[moveFrom(m)];
            contScore = contHistory[parentPiece][moveTo(parentMove)][p][moveTo(m)];
        }
    }
    Piece p = b.pieceOn[moveFrom(m)];
    return historyTable[p][moveTo(m)] + contScore;
}

static Move pickNextMove(MoveList& list, int* scores, int from) {
    int best = from;
    for (int j = from + 1; j < list.count; j++)
        if (scores[j] > scores[best]) best = j;
    if (best != from) {
        std::swap(list.moves[from], list.moves[best]);
        std::swap(scores[from], scores[best]);
    }
    return list.moves[from];
}

static bool hasNonPawnMaterial(const Board& b, Color c) {
    return (b.pieceBB[makePiece(c, KNIGHT)] | b.pieceBB[makePiece(c, BISHOP)] |
            b.pieceBB[makePiece(c, ROOK)] | b.pieceBB[makePiece(c, QUEEN)]) != 0;
}

static int quiescence(Board& b, int alpha, int beta, int ply) {
    nodeCount++;
    if ((nodeCount & 255) == 0) checkTime();
    if (stopFlag.load(std::memory_order_relaxed)) return 0;
    if (ply >= MAX_PLY - 1) return evaluate(b);

    bool inCheck = b.inCheck(b.sideToMove);
    int standPat = 0;
    if (!inCheck) {
        standPat = evaluate(b);
        if (standPat >= beta) return standPat;
        if (standPat > alpha) alpha = standPat;
    }

    MoveList list;
    if (inCheck) generatePseudoLegal(b, list);
    else generateCaptures(b, list);

    int scores[256];
    for (int i = 0; i < list.count; i++)
        scores[i] = isCapture(list.moves[i]) ? scoreCapture(b, list.moves[i])
                  : (isPromo(list.moves[i]) ? 900000 + PieceVal[promoType(list.moves[i])] : 0);

    int bestScore = inCheck ? -INF_SCORE : standPat;
    int legalCount = 0;

    for (int i = 0; i < list.count; i++) {
        Move m = pickNextMove(list, scores, i);

        if (!inCheck && isCapture(m)) {
            PieceType victim = isEnPassant(m) ? PAWN : typeOf(b.pieceOn[moveTo(m)]);
            if (standPat + PieceVal[victim] + 200 < alpha && !isPromo(m)) continue;
            if (!isPromo(m) && see(b, m) < 0) continue;
        }

        Undo u;
        b.makeMove(m, u);
        if (b.inCheck(~b.sideToMove)) { b.unmakeMove(m, u); continue; }
        legalCount++;
        int score = -quiescence(b, -beta, -alpha, ply + 1);
        b.unmakeMove(m, u);

        if (stopFlag.load(std::memory_order_relaxed)) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) break;
            }
        }
    }

    if (inCheck && legalCount == 0) return -MATE_SCORE + ply;
    return bestScore;
}

static int negamax(Board& b, int depth, int alpha, int beta, int ply, bool doNull) {
    nodeCount++;
    if ((nodeCount & 255) == 0) checkTime();
    if (stopFlag.load(std::memory_order_relaxed)) return 0;

    bool pvNode = (beta - alpha) > 1;

    if (ply > 0) {
        if (isRepetition(b)) return -CONTEMPT;
        if (ply >= MAX_PLY - 1) return evaluate(b);
        alpha = std::max(alpha, -MATE_SCORE + ply);
        beta = std::min(beta, MATE_SCORE - ply);
        if (alpha >= beta) return alpha;
    }

    bool inCheck = b.inCheck(b.sideToMove);
    if (depth <= 0) {
        if (!inCheck) return quiescence(b, alpha, beta, ply);
        depth = 1;
    }

    Move ttMove = NO_MOVE;
    TTEntry tte;
    bool ttHit = g_tt.probe(b.hash, tte);
    if (ttHit) {
        ttMove = tte.move;
        if (ply > 0 && !pvNode && tte.depth >= depth) {
            int ttScore = TT::scoreFromTT(tte.score, ply);
            if (tte.flag == TT_EXACT) return ttScore;
            if (tte.flag == TT_ALPHA && ttScore <= alpha) return alpha;
            if (tte.flag == TT_BETA && ttScore >= beta) return beta;
        }
    }

    // Internal iterative reduction: a non-PV node with no TT move at all
    // will have noticeably worse move ordering, so shave a ply off rather
    // than pay full price for it (cheaper than IID's full re-search, used
    // below only for PV nodes).
    if (!ttHit && !pvNode && depth >= 4 && !inCheck) depth--;

    int staticEval = inCheck ? -INF_SCORE : evaluate(b);

    // Reverse futility pruning: if static eval is already comfortably above beta
    // at shallow depth, assume the position is too good and cut off early.
    if (!inCheck && !pvNode && ply > 0 && depth <= 7 &&
        staticEval - 80 * depth >= beta && staticEval < MATE_IN_MAX) {
        return staticEval;
    }

    // Razoring: hopelessly far below alpha at very shallow depth — drop
    // straight to quiescence rather than doing a full-width search that is
    // very unlikely to recover.
    if (!inCheck && !pvNode && ply > 0 && depth <= 3 &&
        staticEval + 200 + 150 * depth <= alpha) {
        int razorScore = quiescence(b, alpha, beta, ply);
        if (stopFlag.load(std::memory_order_relaxed)) return 0;
        if (razorScore <= alpha) return razorScore;
    }

    if (!inCheck && !pvNode && doNull && depth >= 3 && ply > 0 &&
        staticEval >= beta && hasNonPawnMaterial(b, b.sideToMove)) {
        Undo u;
        b.makeNullMove(u);
        pushHistoryKey(b.hash);
        searchStackMove[ply] = NO_MOVE;
        int R = 3 + depth / 6;
        int score = -negamax(b, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
        popHistoryKey();
        b.unmakeNullMove(u);
        if (stopFlag.load(std::memory_order_relaxed)) return 0;
        if (score >= beta) return (score >= MATE_IN_MAX) ? beta : score;
    }

    // Internal iterative deepening: PV nodes with no TT move to order by are
    // the most expensive to search badly-ordered, so spend a shallower
    // search first just to get a decent move to try first.
    if (pvNode && ttMove == NO_MOVE && depth >= 6) {
        negamax(b, depth - 2, alpha, beta, ply, true);
        if (stopFlag.load(std::memory_order_relaxed)) return 0;
        TTEntry iidTte;
        if (g_tt.probe(b.hash, iidTte)) ttMove = iidTte.move;
    }

    MoveList list;
    generatePseudoLegal(b, list);
    int scores[256];
    for (int i = 0; i < list.count; i++) scores[i] = scoreMove(b, list.moves[i], ttMove, ply);

    int bestScore = -INF_SCORE;
    Move bestMove = NO_MOVE;
    int origAlpha = alpha;
    int legalCount = 0;
    Move quietsTried[64];
    int quietsTriedCount = 0;
    Move capturesTried[64];
    int capturesTriedCount = 0;

    for (int i = 0; i < list.count; i++) {
        Move m = pickNextMove(list, scores, i);
        bool isQuiet = !isCapture(m) && !isPromo(m);
        bool isPlainCapture = isCapture(m);

        Undo u;
        b.makeMove(m, u);
        if (b.inCheck(~b.sideToMove)) { b.unmakeMove(m, u); continue; }
        legalCount++;
        pushHistoryKey(b.hash);

        bool givesCheck = b.inCheck(b.sideToMove);
        int extension = givesCheck ? 1 : 0;
        int newDepth = depth - 1 + extension;
        int score;

        // SEE pruning: at shallow depth, skip captures that lose material outright
        // (bad trades are very unlikely to be worth searching once we already have
        // a reasonable candidate move).
        if (!inCheck && !pvNode && !isQuiet && !isPromo(m) && !givesCheck && legalCount > 1 &&
            depth <= 8 && alpha > -MATE_IN_MAX && see(b, m) < -20 * depth) {
            popHistoryKey();
            b.unmakeMove(m, u);
            continue;
        }

        // Futility pruning: at shallow depth, a quiet non-checking move that can't
        // plausibly close the gap to alpha given the static eval is skipped outright.
        static const int FutilityMargin[9] = { 0, 120, 180, 260, 340, 420, 500, 580, 660 };
        if (!inCheck && !pvNode && isQuiet && !givesCheck && legalCount > 1 &&
            depth >= 1 && depth <= 8 && alpha > -MATE_IN_MAX &&
            staticEval + FutilityMargin[depth] <= alpha) {
            popHistoryKey();
            b.unmakeMove(m, u);
            continue;
        }

        // Late move pruning: at shallow depth, once enough quiet moves have
        // already been tried without improving alpha, further quiets are
        // very unlikely to matter — skip them outright regardless of eval.
        static const int LMPThreshold[9] = { 0, 6, 9, 13, 18, 24, 30, 37, 45 };
        if (!inCheck && !pvNode && isQuiet && !givesCheck && legalCount > 1 &&
            depth >= 1 && depth <= 8 && alpha > -MATE_IN_MAX &&
            legalCount > LMPThreshold[depth]) {
            popHistoryKey();
            b.unmakeMove(m, u);
            continue;
        }

        searchStackMove[ply] = m;

        if (legalCount == 1) {
            score = -negamax(b, newDepth, -beta, -alpha, ply + 1, true);
        } else {
            int reduction = 0;
            if (isQuiet && extension == 0 && depth >= 3 && legalCount > 3 && !inCheck) {
                reduction = 1 + (legalCount > 8 ? 1 : 0) + (legalCount > 16 ? 1 : 0) + (depth > 8 ? 1 : 0);
                if (reduction > newDepth - 1) reduction = newDepth > 0 ? newDepth - 1 : 0;
                if (reduction < 0) reduction = 0;
            }
            score = -negamax(b, newDepth - reduction, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && reduction > 0)
                score = -negamax(b, newDepth, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && score < beta)
                score = -negamax(b, newDepth, -beta, -alpha, ply + 1, true);
        }

        popHistoryKey();
        b.unmakeMove(m, u);

        if (stopFlag.load(std::memory_order_relaxed)) return 0;

        if (isQuiet && quietsTriedCount < 64) quietsTried[quietsTriedCount++] = m;
        if (isPlainCapture && capturesTriedCount < 64) capturesTried[capturesTriedCount++] = m;

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
            if (ply == 0) rootBestMoveSoFar = m;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    if (isQuiet) {
                        if (killers[ply][0] != m) { killers[ply][1] = killers[ply][0]; killers[ply][0] = m; }
                        Piece p = b.pieceOn[moveFrom(m)];
                        int& h = historyTable[p][moveTo(m)];
                        h += depth * depth;
                        if (h > (1 << 20)) {
                            for (int pp = 0; pp < 12; pp++)
                                for (int sq = 0; sq < 64; sq++)
                                    historyTable[pp][sq] >>= 1;
                        }
                        Piece parentPiece = NO_PIECE;
                        int parentTo = -1;
                        if (ply > 0) {
                            Move parentMove = searchStackMove[ply - 1];
                            if (parentMove != NO_MOVE) {
                                parentPiece = b.pieceOn[moveTo(parentMove)];
                                parentTo = moveTo(parentMove);
                                counterMoveTable[parentPiece][parentTo] = m;
                                int& ch = contHistory[parentPiece][parentTo][p][moveTo(m)];
                                ch += depth * depth;
                                if (ch > (1 << 20)) ch = (1 << 20);
                            }
                        }
                        // History malus: quiet moves tried before the one that
                        // caused this cutoff turned out to be worse than it, so
                        // penalize them too — this sharpens the ordering signal
                        // beyond only ever rewarding the winner. Mirrored into
                        // continuation history when a parent context exists.
                        for (int qi = 0; qi < quietsTriedCount - 1; qi++) {
                            Move qm = quietsTried[qi];
                            Piece qp = b.pieceOn[moveFrom(qm)];
                            int& hq = historyTable[qp][moveTo(qm)];
                            hq -= depth * depth;
                            if (hq < -(1 << 20)) hq = -(1 << 20);
                            if (parentPiece != NO_PIECE) {
                                int& chq = contHistory[parentPiece][parentTo][qp][moveTo(qm)];
                                chq -= depth * depth;
                                if (chq < -(1 << 20)) chq = -(1 << 20);
                            }
                        }
                    } else if (isPlainCapture) {
                        // Capture history: a much smaller-magnitude bonus/malus
                        // than plain history (see scoreCapture's /16 scaling) —
                        // MVV-LVA/SEE already order captures well by material,
                        // this only refines ordering among similarly-valued
                        // captures, never overriding the material hierarchy.
                        static const int CAP_CLAMP = 1 << 14;
                        Piece ap = b.pieceOn[moveFrom(m)];
                        int& ch = captureHistory[ap][moveTo(m)];
                        ch += depth * depth;
                        if (ch > CAP_CLAMP) ch = CAP_CLAMP;
                        for (int ci = 0; ci < capturesTriedCount - 1; ci++) {
                            Move cm = capturesTried[ci];
                            Piece cp = b.pieceOn[moveFrom(cm)];
                            int& chc = captureHistory[cp][moveTo(cm)];
                            chc -= depth * depth;
                            if (chc < -CAP_CLAMP) chc = -CAP_CLAMP;
                        }
                    }
                    break;
                }
            }
        }
    }

    if (legalCount == 0) return inCheck ? (-MATE_SCORE + ply) : 0;

    int flag = (bestScore <= origAlpha) ? TT_ALPHA : (bestScore >= beta) ? TT_BETA : TT_EXACT;
    g_tt.store(b.hash, depth, bestScore, staticEval, flag, bestMove, ply);

    return bestScore;
}

static std::string extractPV(Board b, int maxLen) {
    std::string s;
    uint64_t seen[MAX_PLY];
    int seenCount = 0;
    for (int i = 0; i < maxLen && i < MAX_PLY; i++) {
        TTEntry tte;
        if (!g_tt.probe(b.hash, tte) || tte.move == NO_MOVE) break;
        MoveList list;
        generateLegal(b, list);
        bool found = false;
        for (int j = 0; j < list.count; j++) if (list.moves[j] == tte.move) { found = true; break; }
        if (!found) break;
        s += (s.empty() ? "" : " ") + moveToUCI(tte.move);
        Undo u;
        b.makeMove(tte.move, u);
        bool rep = false;
        for (int k = 0; k < seenCount; k++) if (seen[k] == b.hash) { rep = true; break; }
        if (seenCount < MAX_PLY) seen[seenCount++] = b.hash;
        if (rep) break;
    }
    return s;
}

static void printInfo(int depth, int score, Board& board) {
    auto now = clock_t_::now();
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    if (ms < 0) ms = 0;
    uint64_t nps = ms > 0 ? (nodeCount * 1000ULL / (uint64_t)ms) : nodeCount * 1000ULL;

    std::ostringstream oss;
    oss << "info depth " << depth << " score ";
    if (score >= MATE_IN_MAX) {
        int mateMoves = (MATE_SCORE - score + 1) / 2;
        oss << "mate " << mateMoves;
    } else if (score <= -MATE_IN_MAX) {
        int mateMoves = (MATE_SCORE + score + 1) / 2;
        oss << "mate " << -mateMoves;
    } else {
        oss << "cp " << score;
    }
    oss << " nodes " << nodeCount << " nps " << nps << " time " << ms
        << " hashfull " << g_tt.hashfull()
        << " pv " << extractPV(board, depth);
    {
        std::lock_guard<std::mutex> lk(g_coutMutex);
        std::cout << oss.str() << "\n" << std::flush;
    }
}

void Search::newGame() {
    g_tt.clear();
    memset(historyTable, 0, sizeof(historyTable));
    memset(contHistory, 0, sizeof(contHistory));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(killers, 0, sizeof(killers));
    memset(counterMoveTable, 0, sizeof(counterMoveTable));
    gameHistoryLen = 0;
}

void Search::setHashMB(size_t mb) { g_tt.resize(mb); }
void Search::setMoveOverhead(int ms) { moveOverheadMs = ms; }
void Search::resetGameHistory() { gameHistoryLen = 0; }
void Search::pushGameHistory(uint64_t h) { if (gameHistoryLen < MAX_HIST) gameHistoryStack[gameHistoryLen++] = h; }
void Search::requestStop() {
    stopFlag.store(true, std::memory_order_relaxed);
    stopAppliesThroughGo.store(goEnqueuedCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
}
bool Search::isSearching() const { return searchingFlag.load(std::memory_order_relaxed); }
void Search::noteGoQueued() { goEnqueuedCount.fetch_add(1, std::memory_order_relaxed); }

void Search::go(Board board, const SearchLimits& limits) {
    searchingFlag.store(true, std::memory_order_relaxed);
    int myGoIndex = goDequeuedCount.fetch_add(1, std::memory_order_relaxed) + 1;
    bool stopAlreadyPendingForThis = myGoIndex <= stopAppliesThroughGo.load(std::memory_order_relaxed);
    if (!stopAlreadyPendingForThis) {
        stopFlag.store(false, std::memory_order_relaxed);
    }
    // else: a stop/quit raced ahead of this go while it was still queued —
    // leave stopFlag set so the search below notices within a couple
    // thousand nodes and returns almost immediately instead of running
    // unbounded (critical for "go infinite", where nothing else would ever
    // stop it).
    nodeCount = 0;
    startTime = clock_t_::now();
    computeTimeBudget(limits, board.sideToMove);
    g_tt.newSearch();
    memset(killers, 0, sizeof(killers));

    historyTop = gameHistoryLen;
    for (int i = 0; i < gameHistoryLen; i++) historyStack[i] = gameHistoryStack[i];
    pushHistoryKey(board.hash);

    MoveList rootMoves;
    generateLegal(board, rootMoves);

    if (rootMoves.count == 0) {
        {
            std::lock_guard<std::mutex> lk(g_coutMutex);
            std::cout << "bestmove 0000\n" << std::flush;
        }
        searchingFlag.store(false, std::memory_order_relaxed);
        return;
    }

    Move bestMove = rootMoves.moves[0];
    int bestScore = 0;
    // A forced move (only one legal reply, e.g. escaping check with a single
    // flight square) can't change no matter how deep we search it — cap the
    // depth in that case so we don't burn the position's time budget on a
    // decision that was never in question, banking it for a position that
    // actually has one to make. Only applies to time-limited searches; an
    // explicit "go depth N" or "go infinite" is respected as asked.
    bool forcedMove = (rootMoves.count == 1 && limited);
    int maxDepth = (limits.depth > 0) ? limits.depth : (forcedMove ? 6 : (MAX_PLY - 1));
    int stableDepths = 0;

    for (int depth = 1; depth <= maxDepth; depth++) {
        rootBestMoveSoFar = NO_MOVE;
        int score;
        if (depth >= 4) {
            int window = 25;
            int alpha = bestScore - window, beta = bestScore + window;
            while (true) {
                score = negamax(board, depth, alpha, beta, 0, true);
                if (stopFlag.load(std::memory_order_relaxed)) break;
                if (score <= alpha) { alpha = std::max(alpha - window, -INF_SCORE); window *= 2; }
                else if (score >= beta) { beta = std::min(beta + window, INF_SCORE); window *= 2; }
                else break;
                if (window > 2000) { alpha = -INF_SCORE; beta = INF_SCORE; }
            }
        } else {
            score = negamax(board, depth, -INF_SCORE, INF_SCORE, 0, true);
        }

        if (stopFlag.load(std::memory_order_relaxed) && depth > 1) break;

        bestScore = score;
        TTEntry tte;
        Move newBest = bestMove;
        if (g_tt.probe(board.hash, tte) && tte.move != NO_MOVE) {
            newBest = tte.move;
        } else if (depth == 1 && rootBestMoveSoFar != NO_MOVE) {
            // depth-1 itself got interrupted before it could store to TT —
            // fall back to whatever partial root progress was made rather
            // than an arbitrary first-generated move.
            newBest = rootBestMoveSoFar;
        }
        stableDepths = (newBest == bestMove) ? stableDepths + 1 : 0;
        bestMove = newBest;
        printInfo(depth, bestScore, board);

        if (stopFlag.load(std::memory_order_relaxed)) break;
        if (bestScore >= MATE_IN_MAX || bestScore <= -MATE_IN_MAX) {
            if (depth >= 2) break;
        }
        if (limited) {
            auto now = clock_t_::now();
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            // If the best move keeps changing, allow searching a bit longer
            // (up to the hard limit) before committing to it.
            int64_t effectiveSoft = (stableDepths < 2) ? std::min(hardMs, softMs + softMs / 2) : softMs;
            if (elapsed >= effectiveSoft) break;
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_coutMutex);
        std::cout << "bestmove " << moveToUCI(bestMove) << "\n" << std::flush;
    }
    searchingFlag.store(false, std::memory_order_relaxed);
}
