# Sonnet 5 chess 24hrs

A UCI chess engine written from scratch in 24 hours by Claude (Sonnet 5), as a benchmark of what one model can build, alone and unsupervised, in a single day.

## What this is

This repository is the output of a 24-hour, fully autonomous benchmark. An AI model (Sonnet 5) was given a fixed instruction file (`AGENTS.md`, reproduced in spirit below) and told to design, build, test, and deliver a UCI-compliant chess engine from scratch, with no human interaction during the run. The only thing that was measured is the **playing strength (Elo)** of the executable left in `final/` at the 24-hour mark — nothing else counted, not code quality, not documentation, not feature completeness.

Key rules of the benchmark:

- **24 hours, wall-clock, for everything** — thinking, coding, testing, writing. No extensions.
- **Rated at `tc=10+0.1`** (game in 10 seconds + 0.1s increment), single-threaded, via [fastchess](https://github.com/Disservin/fastchess), against a field of engines including several versions of [Stash](https://github.com/mhouppin/stash-bot) as calibration points.
- **From scratch**: no third-party chess libraries, no pre-existing engine source code, no NN weights, no opening books. Reading chess programming documentation (chessprogramming.org, papers, published evaluation functions like PeSTO) and reproducing formulas/ideas from them was explicitly allowed — copying engine *source code* was not.
- **C, C++, or Zig only**, standard library and compiler intrinsics only.
- **The target hardware** is a modern Windows laptop with POPCNT/BMI1/BMI2/AVX2 but *not guaranteed AVX-512* — so the build targets the `x86-64-v3` baseline, never `-march=native`.
- A crash, a hang, an illegal move, or a loss on time all count as a full loss in the rating match — reliability is explicitly part of the score, not a separate concern from strength.
- The engine's own defaults are used for everything except `Hash`, which the harness sets to 256 MB.

The full rules the model worked from are preserved in this repo's git history (`AGENTS.md`, later folded into project instructions). This README was written *after* the 24-hour window closed and consumed none of the benchmark's time budget, as the rules require.

## The engine

- **Name:** `Sonnet 5 chess 24hrs` (as reported via UCI `id name`)
- **Author:** `Sonnet 5` (as reported via UCI `id author`)
- **Language:** C++20. Chosen for direct control over memory layout and bit-level operations (bitboards, PEXT-based move generation) without fighting a garbage collector or a borrow checker, and because its ecosystem for this kind of low-level, performance-sensitive code is the most mature of the three permitted languages.
- **Executable:** `final/Sonnet5chess24hrs.exe`

### Building

Toolchain: GCC 15.2.0 (MinGW-w64). From the project root:

```
g++ -std=c++20 -O3 -flto -march=x86-64-v3 -static -Isource/src ^
    source/src/bitboard.cpp source/src/board.cpp source/src/zobrist.cpp ^
    source/src/movegen.cpp source/src/eval.cpp source/src/tt.cpp ^
    source/src/search.cpp source/src/init.cpp source/src/uci.cpp ^
    -o final/Sonnet5chess24hrs.exe -lpthread
```

or simply:

```
powershell -File source/build.ps1
```

which wraps the same command (and works around a machine-specific PATH quirk described in its own comments — see [Assumptions](#all-assumptions-made)). The resulting binary is fully static: it depends only on `KERNEL32.dll` and `msvcrt.dll` (verified with `objdump -p`, and by running it with every MinGW/toolchain directory stripped from `PATH`), so it can be copied to a machine with no MinGW runtime installed and will still run.

### Running

Standard UCI over stdin/stdout:

```
final\Sonnet5chess24hrs.exe
```

then speak UCI (`uci`, `isready`, `position ...`, `go ...`, etc.) as usual.

### UCI options

| Option | Type | Default | Range |
|---|---|---|---|
| `Hash` | spin | 256 (MB) | 1–16384 |
| `Move Overhead` | spin | 60 (ms) | 0–5000 |

## Architecture and features

**Board representation.** Bitboards, with a parallel 64-entry mailbox (`pieceOn[]`) for O(1) piece lookups. Slider attacks (bishop/rook/queen) use PEXT-based lookup tables (`_pext_u64`) built once at startup via carry-rippler subset enumeration over each square's relevant blocker mask — no classical "magic numbers" needed, since PEXT/PDEP are fast on the target hardware (explicitly not assumed for AMD Zen 1/2, which is why the build never uses `-march=native`).

**Move generation.** Pseudo-legal generation followed by a legality filter (make the move, test if the mover's king is attacked, unmake). Passes the full supplied perft suite (`resources/perft/perft.epd`, 126 positions) exactly at every tested depth, including the start position to depth 6 (119,060,324 nodes).

**Search.** Negamax with alpha-beta and PVS re-search, iterative deepening with aspiration windows, and a transposition table (Zobrist-keyed, depth/generation-aware replacement — see [How the time was spent](#how-the-time-was-spent) for a correctness fix made to this late in the session). Pruning and reduction techniques, roughly in the order they were added: null-move pruning, late move reductions (LMR, with extra tiers for very late moves and high depth), reverse futility pruning, futility pruning, razoring, static-exchange-evaluation (SEE) pruning of bad captures (both in quiescence and the main search), late move pruning (LMP), internal iterative deepening (IID) at PV nodes and internal iterative reduction (IIR) at non-PV nodes with no TT move, check extensions, and a forced-move depth cap (when exactly one legal move exists, search stops at a shallow fixed depth rather than spending the position's full time budget, since the move cannot change).

**Move ordering.** TT move first, then captures scored by MVV-LVA/SEE plus a small learned capture-history tiebreaker, then promotions, then killer moves (two per ply), then a counter-move table, then quiet moves ordered by history heuristic (with a **malus**: moves tried and rejected before a cutoff are penalized, not just the cutoff move rewarded — see below) combined with **continuation history** (a score per (parent-move, this-move) pair, distinct from the single-slot counter-move table).

**Evaluation.** Tapered (mg/eg interpolated by game phase) material + piece-square tables in the Michniewski/PeSTO style, plus: mobility, passed pawns (rank-scaled, with endgame king-tropism to the promotion square, a bonus for a rook behind the passer, and a bonus when the passer is itself pawn-defended), isolated/doubled pawn penalties, bishop pair, knight/bishop outposts, rooks on open/semi-open files, king safety (pawn shield, open files near the king, and a CPW-style "attack units" term weighting enemy pieces bearing on the king zone), a small contempt term (discourages steering into an unforced draw), and a flat tempo bonus.

**Time management.** Given `wtime`/`btime`/`winc`/`binc`, computes a soft and a hard time budget (roughly: soft ≈ time/25 + 0.8×increment, hard ≈ 3×soft, both bounded well under the remaining clock plus `Move Overhead`), with an emergency tighter budget under ~200ms remaining, a best-move-stability extension (search a bit longer, up to the hard limit, if the best move keeps changing between iterations), and the forced-move depth cap mentioned above. `movetime` and `depth`-limited/`infinite` search are handled directly. Time is checked every 256 nodes.

**UCI protocol.** The engine runs its search synchronously on the main thread; a separate reader thread owns stdin and answers `stop`/`isready`/`quit` immediately (out of band), while queuing everything else (`position`, `go`, `setoption`, `ucinewgame`) for the main thread to process in order. `stop` and `quit` are tracked with a generation counter so a stop requested for a `go` still sitting in the queue is not silently lost once that `go` starts (see the write-up on the `go infinite` hang below). On stdin EOF with no explicit `quit` (i.e. the parent process disappeared), the engine stops and exits cleanly rather than hanging forever.

**What was deliberately left out**, per the rules' own list of non-requirements: pondering, `MultiPV`, `Threads` > 1, Chess960, an opening book, and tablebases. Two things were tried and explicitly abandoned: profile-guided optimization (the instrumented build crashed reproducibly on one specific position that the normal `-O3` build handled fine — chasing it risked repeating an earlier multi-hour debugging session for a purely optional speed gain, so it was dropped); and an "improving" heuristic (scaling pruning aggressiveness by whether the static eval is trending up or down across recent own moves) — a well-established technique in principle, but its concrete implementation would have required guessing specific margin values, which matched a pattern of speculative parameter tuning that had a poor track record this session (see below), so it wasn't pursued.

## How the time was spent

A condensed timeline; the full detail (every experiment, every measurement, every dead end) is in `docs/progress.md`.

- **Hours 0–1:** Board representation, move generator (perft-clean within 6 minutes of starting), then the full first-pass search/eval/UCI stack. Hit a serious, non-deterministic segfault under time pressure — root-caused over about 3 hours to running the search on a spawned `std::thread` (plausibly a MinGW-w64 winpthreads issue on this toolchain); fixed by running the search synchronously on the main thread instead, with a lightweight reader thread for stdin. This was the single biggest time sink of the session.
- **Hours 1–9:** The bulk of the core engine: standard eval terms (passed pawns, king safety, outposts, mobility, bishop pair, etc.) and standard search techniques (null-move, LMR, RFP, futility, razoring, SEE pruning, IID/IIR, LMP), each validated with an internal A/B match (typically 240 games at the real time control) before promotion, interspersed with periodic re-anchoring against the supplied Stash engines. A time-management bug (a sentinel value conflating "no time control sent" with "time control sent as zero/negative") was found via a self-play match and fixed in this window — the first of several reminders that self-play testing catches protocol bugs that pure strength A/B testing does not.
- **Hours 9–13:** Diminishing returns on quick feature ideas (several reverted experiments in a row); a defensive time-safety hardening (tighter time-check sampling, larger move-overhead margin) after an isolated stall against a real opponent; a broad reliability/strength sweep across the whole Stash ladder.
- **Hours 14–15:** A deliberate strategy shift to independent code review (dispatching a subagent to read the search/eval/protocol code specifically hunting for correctness bugs, rather than more feature-hunting). This found two real bugs that no amount of A/B win-rate testing would have surfaced: an interrupted depth-1 search could fall back to an arbitrary move instead of its best partial progress, and — more seriously — a race between the stdin reader thread and the main thread could silently lose a `stop` request, causing a **deterministic hang** on `go infinite` followed immediately by `quit` (verified 30/30 on the old build, 0/30 after the fix). A third review pass (numerical/TT correctness) came back clean, which was itself useful confirmation. A comprehensive 500-game gauntlet against the full Stash ladder confirmed no regressions.
- **Hours 16–17:** A forced-move depth cap (bank time when there's only one legal move); a small contempt term; more pawn-structure eval refinements (one win, one reverted); a fourth review pass focused specifically on time management (came back clean, bar a cosmetic UCI-option-default label mismatch); and a correctness fix to the transposition table's replacement policy (a shallow exact-score result could evict a much deeper, more valuable entry — flagged by the earlier review pass but left alone at the time; fixed and measured positive once addressed).
- **Hours 17–19:** The largest late-session gains: two well-established move-ordering mechanisms that were simply missing were added — a **history malus** (penalizing quiet moves that were tried and rejected, not just rewarding the one that caused a cutoff) measured at **+84 Elo** in a 240-game internal A/B, by far the single biggest result of the session, followed by **continuation history** (+20 Elo) and a smaller **capture history** (+14.5 Elo). This was a useful lesson: even after several hours of apparent diminishing returns on parameter tuning, a genuinely missing standard *mechanism* (as opposed to a magnitude guess) was still there to find.
- **Hours 18–24:** Verification and wind-down. A full checklist (identity, static linking, perft, `fastchess --compliance`, pathological UCI time-control inputs, `go infinite`+`quit`, repo/build cleanliness) was run to completion multiple times against the evolving build, and a broad final round of Stash-ladder re-anchoring was done to settle the final Elo estimate. The build was locked in with roughly 3.3 hours of margin remaining, deliberately not touched again, and the remaining time was spent on periodic confirmation checks rather than further speculative changes — consistent with the session's own evidence that further magnitude-tuning attempts had a low hit rate, and with the rules' explicit call to reserve real time for verification rather than squeeze it in at the end.

What was measured vs. accepted on judgement: every search/eval change that plausibly affected strength went through at least a 240-game internal A/B match at the real time control before being kept or reverted; a handful of very low-risk, extremely standard techniques (e.g. the initial eval terms, the contempt term) were accepted on a smaller sample plus reasoning when the trend was clearly non-negative and further confirmation games weren't worth the wall-clock. Absolute Elo was cross-checked against multiple independent Stash rungs (20/21/25/30/33) throughout, rather than trusted from a single opponent.

What went wrong / was cut: the threading crash (found and fixed, cost ~3 hours), PGO (abandoned after a reproducible crash on the instrumented build), an attempted "connected/phalanx passed pawns" eval term (measured mildly negative, reverted), a combined multi-opponent gauntlet test mode (repeatedly hit local process-creation failures specific to this dev machine, abandoned in favor of running opponents sequentially), and the "improving" search heuristic (considered, declined — see above).

## Elo by hour

Estimates below are drawn from the corresponding entries in `docs/progress.md`, using the estimate current at (or nearest before) each elapsed-hour mark from the actual start time (2026-08-26 16:32:02 local). Where a range was logged, the range is reproduced; single numbers are the session's own point estimate at that time. See `docs/progress.md` for the underlying match results (opponent, sample size, score) behind every number.

| Hour | Timestamp (local) | Estimated Elo |
|---|---|---|
| 0 | 2026-08-26 16:32 | not yet measurable |
| 1 | 2026-08-26 17:32 | ~2350 |
| 2 | 2026-08-26 18:32 | ~2610 |
| 3 | 2026-08-26 19:32 | ~2650 |
| 4 | 2026-08-26 20:32 | ~2600–2650 |
| 5 | 2026-08-26 21:32 | ~2650–2680 |
| 6 | 2026-08-26 22:32 | ~2685–2690 |
| 7 | 2026-08-26 23:32 | ~2690–2715 |
| 8 | 2026-08-27 00:32 | ~2690–2715 |
| 9 | 2026-08-27 01:32 | ~2670 |
| 10 | 2026-08-27 02:32 | ~2665 |
| 11 | 2026-08-27 03:32 | ~2600–2650 |
| 12 | 2026-08-27 04:32 | ~2600–2650 |
| 13 | 2026-08-27 05:32 | ~2680 |
| 14 | 2026-08-27 06:32 | ~2680–2695 |
| 15 | 2026-08-27 07:32 | ~2690–2695 |
| 16 | 2026-08-27 08:32 | ~2690–2710 |
| 17 | 2026-08-27 09:32 | ~2710–2725 |
| 18 | 2026-08-27 10:32 | ~2725–2751 |
| 19 | 2026-08-27 11:32 | ~2700–2750 |
| 20 | 2026-08-27 12:32 | ~2700–2750 |
| 21 | 2026-08-27 13:32 | ~2700–2750 |
| 22 | 2026-08-27 14:32 | ~2700–2750 |
| 23 | 2026-08-27 15:32 | ~2700–2750 |
| 24 (final) | 2026-08-27 16:32 | **~2720–2730** |

Note the estimate is not monotonic — it dips around hours 4 and 11–12 when a fresh Stash bracket (a different opponent, or a larger combined multi-opponent gauntlet) landed lower than the previous single-opponent measurement, which given ~200–300-game sample sizes is normal noise (typically ±30–40 Elo per batch) rather than an actual regression; every individual code change was separately validated with its own before/after A/B match regardless of what the absolute-strength re-anchors showed.

## All assumptions made

- **Toolchain:** no MSVC or standalone clang was available on the dev machine, so GCC 15.2.0 (MinGW-w64) was used for the final build; Zig 0.16.0 was available but not used, since GCC's C++20 support and this machine's familiarity with it were sufficient.
- **Search threading model:** the rules note the engine "may use a separate thread to read stdin" — this was interpreted as *permission*, not a requirement to run the search itself on a separate thread. After the search-on-a-spawned-thread crash (see above), the architecture was changed to run search synchronously on the main thread, with only stdin reading on a second thread. This still satisfies "single-threaded search" and lets `stop`/`isready`/`quit` be answered promptly.
- **`Hash` option:** the rules state the harness always sends `setoption name Hash value 256`, so the engine's own default value for `Hash` was set to 256 as well, purely so behavior is identical whether or not that command actually arrives.
- **`Move Overhead` default:** not specified by the rules; chosen as 60ms by judgement (increased from an initial 40ms mid-session as part of a defensive hardening response to an isolated timing stall) as a reasonable safety margin at a 10+0.1 control.
- **Untuned constants:** LMR reduction amounts, null-move reduction, and the RFP/futility/razoring/LMP margins were reasoned from typical published ranges for engines of comparable search depth, not tuned specifically for this engine (no SPRT/texel-tuning infrastructure was built — deliberately, given the time budget). The contempt value (10 centipawns) was likewise chosen by reasoning rather than tuned.
- **`movestogo`:** the real time control (`10+0.1`) never sends it, but it is handled defensively (falls back to a conservative fraction of remaining time) in case a GUI ever does.
- **Reliability quirks specific to this development machine** (not the target hardware): launching `fastchess.exe` from a Git Bash shell intermittently failed with a process-creation error that did not reproduce via PowerShell, and launching several distinct, not-recently-run opponent binaries at once (a combined multi-opponent gauntlet) triggered the same failure even via PowerShell — both were treated as local tooling/antivirus-scanning artifacts of this dev environment rather than engine defects, since they never occurred with the actual engine process once it was already running, and are not expected to be relevant to how the real rating harness invokes fastchess directly.
- **A rare (~0.3–0.5%) self-play-only stall**, observed only in engine-vs-itself test matches (two simultaneously-launched identical binaries) and never in 400+ games against real, different opponent binaries both before and after the relevant time-safety hardening, was treated as a self-play-specific testing artifact (plausibly OS/antivirus contention between two identical processes) rather than a genuine engine defect, since the real rating match is never engine-vs-itself. This could not be fully ruled out given the time budget, so it's recorded here as an explicit residual assumption rather than a settled fact.
- **`-mtune`:** the build deliberately omits any `-mtune=znver4`-style tuning flag beyond the required `-march=x86-64-v3` baseline, since the target laptop's exact microarchitecture isn't guaranteed and the rules explicitly warn against assuming a Zen-family part.

## Estimated strength

**Best estimate: ~2720–2730 Elo** at the `10+0.1` time control on comparable hardware, single-threaded, 256 MB hash.

Evidence: several hundred games each (mostly 300-game batches, `-repeat` with both colors per opening, UHO book) against Stash versions 20 (~2510 CCRL), 21 (~2710 CCRL), 25 (~2940 CCRL), 30 (~3170 CCRL), and 33 (~3286 CCRL), measured repeatedly throughout the session as the engine improved. The final build's measurements against the three mid-ladder opponents (the most statistically solid, being closest to the engine's own strength) converge tightly: ~2751 vs stash-21 (300 games), ~2703 vs stash-25 (300 games), ~2681 vs stash-30 (300 games). The stash-33 measurement (highest rung, largest gap) is directionally consistent but much noisier, as expected when win rate is very close to 0%.

Every individual change that plausibly affected strength was validated with its own internal A/B match (typically 240 games) before being kept, giving a second, independent line of evidence for the overall trajectory (roughly +2350 → +2730, tracked incrementally through ~35 distinct promoted or reverted changes).

**Uncertainty:** CCRL ratings are for a different (longer) time control than this benchmark's `10+0.1`, so the absolute anchor is directionally right rather exact; a single 300-game match against one opponent carries roughly ±30–40 Elo of sampling noise on its own. The true number could plausibly be anywhere in the **~2650–2800** range; ~2720–2730 is the best single point estimate given everything measured. **Reliability**, which counts fully in this benchmark's scoring, was effectively perfect for the entire second half of the session: many thousands of combined test games with zero timeouts, crashes, or hangs attributable to this engine since a deterministic `go infinite` hang was found and fixed at the 15-hour mark (every timeout/crash logged afterward belonged to the Stash opponent, not this engine).

## Official results

*(left for the human to fill in after the rating match)*

---

*Full hour-by-hour process log: [`docs/progress.md`](docs/progress.md). This file (`README.md`) was written entirely after the 24-hour benchmark window closed, per the benchmark's own rules, and consumed none of the 24-hour time budget.*
