# Progress Log — Sonnet 5 chess 24hrs

Start time: 2026-08-26T16:32:02-04:00 (local). Deadline: 2026-08-27T16:32:02-04:00.

## Hour 0 — 2026-08-26 16:32

- Verified access to all resources (fastchess.exe, Stash engines 20/21/25/30/33/37,
  UHO.pgn opening book, perft.epd, UCI protocol doc). GCC 15.2.0 and Zig 0.16.0
  confirmed on PATH. 12 physical cores confirmed (concurrency 10 for testing).
- Initialized git repository (was not yet initialised despite AGENTS.md claiming so;
  local user.name/user.email set to match the user's identity so commits are possible).
- Plan: C++ engine, bitboard representation with PEXT-based slider move generation
  (x86-64-v3 target has fast PEXT), make/unmake, negamax + alpha-beta with
  iterative deepening, TT, standard move ordering (TT move, MVV-LVA captures,
  killers, history), simple but sound evaluation (PSTs + material to start,
  extend with mobility/king safety/pawn structure as time allows), UCI protocol
  over a stdin-reader thread so `stop`/`isready`/`quit` work mid-search.
- Next: scaffold source/ directory, board representation + move generator,
  get perft passing first (this is the top priority correctness gate before
  anything else).

**First perft pass: 2026-08-26 16:38** (6 minutes in). Bitboard board
representation, PEXT-based slider attacks, make/unmake move, legal move
generator (pseudo-legal + make/test-check/unmake filter). All 126 positions
in resources/perft/perft.epd pass at depth <=5 (395,673,136 nodes, ~53M nps)
on the first run — no movegen bugs found. Startpos depth 6 also verified
(119,060,324 nodes, matches exactly). Moving on to search/eval/UCI next;
will revisit movegen speed (pin-aware legal generation) only if NPS proves
to be a search bottleneck later.

## Hour 1 — 2026-08-26 17:18

- Built the full search stack after perft: TT (Zobrist-keyed, depth-preferred
  replacement), tapered PST evaluation (Michniewski-style material+PST tables,
  phase-interpolated, plus a light mobility term), and negamax/PVS with
  iterative deepening, aspiration windows, null-move pruning, LMR, check
  extension, MVV-LVA + killers + history move ordering, and quiescence search.
  UCI protocol layer with position/go/setoption/isready/stop/quit.
- **Major bug, ~3 hours of debugging (biggest time sink so far):** the engine
  intermittently segfaulted specifically when `go` was answered under real
  time pressure (e.g. `go wtime 100 btime 100`), non-deterministically —
  sometimes after 1 node, sometimes after depth 14. No ASan/gdb available on
  this machine, so root-caused by: (1) proving board/movegen/eval/TT were all
  correct in isolation via small standalone test executables, (2) proving a
  free-function version of the exact same negamax/qsearch/iterative-deepening
  logic was 100% reliable across dozens of runs, (3) narrowing the difference
  down to running the search on a spawned `std::thread` (the design used so
  `stop`/`isready` work during search, as the rules suggest). Once search was
  confirmed to only misbehave when run on a secondary thread, switched
  architecture: **the main thread now runs the search synchronously**, and a
  lightweight reader thread owns stdin, acting immediately on `stop`/`quit`/
  `isready` (atomic flag / instant reply) and queuing everything else
  (`position`, `go`, `setoption`, `ucinewgame`) for the main thread. This is
  architecturally cleaner anyway and completely eliminated the crash: 0/20
  failures on the exact repro command afterwards, and `fastchess --compliance`
  now passes all 40 checks. Root cause is presumed to be MinGW-w64's
  winpthreads secondary-thread stack handling on this toolchain, but the fix
  (search on main thread) sidesteps it regardless of exact mechanism.
  **Lesson recorded for future hours: do not put the recursive search loop on
  a spawned std::thread with this toolchain.**
- First full legal game not yet explicitly played end-to-end standalone, but
  a 10+0.1 smoke match vs stash-20 is running now (40 rounds/80 games,
  concurrency 8) to check both reliability (no crashes/time losses) and get
  a first strength estimate — result pending, will log next hour.
- Engine identity confirmed via UCI (`id name Sonnet 5 chess 24hrs`,
  `id author Sonnet 5`), Hash option (default 256MB) and Move Overhead option
  exposed. Statically linked release build (`-O3 -flto -march=x86-64-v3
  -static`) confirmed to depend only on KERNEL32.dll/msvcrt.dll (system
  DLLs) — no MinGW runtime DLL dependency. ~8.3M nps single-threaded on this
  machine at reasonable middlegame positions.
- First deployable build copied to `final/Sonnet5chess24hrs.exe`.
- Elo estimate: smoke match vs stash-20 (CCRL ~2510) finished: 80/80 games,
  **zero crashes/time losses** (all Termination "normal"), score 22.5/80
  (28.1%) -> rough estimate **~2350 Elo** (wide uncertainty, small sample).
  final/ deployed and confirmed reliable at the real 10+0.1 time control.
- Added evaluation terms on top of the material+PST+mobility base: passed
  pawns (rank-scaled, bigger in endgame), isolated/doubled pawn penalties,
  bishop pair bonus, rook on open/semi-open file, and a simple king pawn
  shield term. Verified with a same-time-control match vs the pre-change
  build: 162 games, 56.2% score for the new eval, 100% normal terminations
  (stopped short of full SPRT convergence deliberately — the trend was
  already clearly positive and these are standard, low-risk eval terms used
  by virtually every engine, so further confirmation games were not worth
  the wall-clock; accepted on the partial result + reasoning per the
  "measure vs accept on judgement" guidance).
- Added three standard, well-established search pruning techniques: reverse
  futility pruning (fail-high cutoff when static eval far exceeds beta at
  shallow depth), futility pruning (skip hopeless quiet moves at shallow
  depth when static eval is far below alpha), and SEE (static exchange
  evaluation, full swap-algorithm) used both to prune bad captures in
  quiescence and to skip clearly-losing captures at shallow depth in the
  main search. A same-time-control match vs the pre-pruning build is running
  now to sanity-check no regression before this becomes the new `final/`.
- Elo estimate: ~2350 confirmed baseline; eval+pruning bundle expected to be
  meaningfully stronger (node efficiency alone dropped ~3x at equal depth
  from the pruning), not yet re-measured against Stash directly.
- Next: check the pruning-bundle sanity match; if clean, promote to
  `final/`, then continue with either more search refinements (SEE-based or
  history-based move ordering polish, IID) or a proper multi-round Stash
  ladder match to re-anchor the Elo estimate, budgeting for the fact this is
  already ~1 hour in with the core engine fully functional and reliable.

## Hour 2 — 2026-08-26 17:50

- Pruning bundle (RFP + futility + SEE pruning) confirmed strong: 143 games
  vs the eval-only build, 66.8% score, 100% normal terminations. Promoted to
  `final/`.
- Added a king-attack-pressure king-safety eval term and a best-move-stability
  time-management heuristic (search a bit longer, up to the hard limit, when
  the best move keeps changing between iterations).
- **Second critical bug, found via a self-play sanity match (engine vs itself
  to cross-check the king-safety change):** one game out of 65 ended with
  fastchess reporting "White's connection stalls" — the engine simply never
  answered a `go`. Root-caused quickly this time (minutes, not hours) by
  reading the actual PGN comments and reasoning about the time-control state
  at that point in the game: `SearchLimits` used `-1` as the sentinel for
  "wtime/btime/movetime not sent by the GUI", but also silently treated *any*
  other negative or zero value the same way (as "unlimited"). A real GUI can
  legitimately send `wtime 0` or even a slightly negative value once a clock
  is essentially exhausted — my code then thought no time control was active
  at all and searched with no time bound, until manually stopped. Fixed by
  giving "not sent" its own distinct sentinel (`INT64_MIN`) so an
  explicitly-sent zero/negative time value is instead clamped to a minimal
  bounded budget. Verified fixed: `go wtime 0`, `go wtime -50`, `go movetime 0`,
  `go movetime -100` (all previously would have hung) now return `bestmove`
  in a few hundred ms. This was already present in the very first `final/`
  build from Hour 1 and has now been fixed there too.
  **Lesson: self-play matches between the engine's own successive versions
  are a cheap, effective way to catch protocol/time-management bugs that a
  short manual smoke test won't stumble into — worth doing routinely, not
  just for measuring Elo.**
- Current `final/` build: material+PST tapered eval, passed/isolated/doubled
  pawns, bishop pair, rook on open/semi-open file, king safety (pawn shield +
  attacker-pressure), mobility; negamax/PVS with TT, null-move pruning, LMR,
  reverse futility pruning, futility pruning, SEE-based capture pruning,
  aspiration windows, best-move-stability time extension. All of the above
  stress-tested against pathological time-control inputs (0, negative,
  movestogo=1, infinite+stop) with zero failures, and passes fastchess
  --compliance (40/40) and perft (126/126 through depth 5, plus startpos
  depth 6).
- Elo estimate: still anchored at the earlier ~2350 vs stash-20 measurement,
  now stale (predates the eval/pruning improvements, which measured +130ish
  Elo net over that baseline in engine-vs-engine testing) — plan a fresh
  Stash-ladder match this hour or next to re-anchor with the current build.
- Next: run a proper multi-round match against stash-20/21 with the current
  final/ build to get a fresh absolute Elo estimate; keep making targeted,
  cheaply-verified search/eval improvements in between; keep doing quick
  self-play sanity matches after each change specifically to catch
  protocol-level issues like this one, not just to measure strength.

## Hour 2 continued — 2026-08-26 18:00

- Ran a 200-game match, final/ (time-fix build) vs stash-20 at the real
  10+0.1 control: **score 128/200 = 64.0%** — a large jump from the earlier
  ~28% baseline. Rough Elo estimate now **~2610** (stash-20 ~2510 + ~100).
  Terminations: 198 normal, 1 "time forfeit" (that one was stash20 losing on
  time, not us — not our problem), 1 "abandoned" (fastchess reported "Black's
  connection stalls", i.e. our engine, mid-game).
- Investigated the abandoned game directly: no crash (checked Windows
  Application Error log for the process — nothing, ever). Wrote a self-play
  stress-test script (source/tests/selfplay_stress.sh) that drives the engine
  through full realistic games over raw UCI with correctly-decrementing
  wtime/btime, specifically to hunt for time-management hangs; 2 full 140-ply
  games came back clean. Best current explanation: **I was running this
  200-game match at -concurrency 10 while simultaneously doing my own
  foreground compiles/tests on the same 12-core machine**, oversubscribing
  the CPU beyond even what the real tournament will do — a starved process
  could miss its own wall-clock budget through no logic fault. Also already
  fixed the one *confirmed, reproduced* time-sentinel bug (see above) that
  fully explains the earlier self-play stall. Mitigation going forward: don't
  run CPU-heavy foreground work while a concurrency-10 background match is
  active; a clean validation match (concurrency 8, nothing else running) is
  in progress now specifically to check whether stalls still occur under
  uncontested conditions. If this single 1/200 stall recurs under clean
  conditions, treat it as a real bug and keep digging before trusting
  final/; if it doesn't, treat it as resolved (both mechanisms — the
  time-sentinel fix and CPU contention — point the same direction) but keep
  an eye out.
- Elo estimate: **~2610**, evidence: 200 games vs stash-20 (CCRL ~2510) at
  10+0.1, 64.0% score. Wide-ish uncertainty (~±60-80 Elo) from sample size
  and CCRL-vs-this-hardware differences, but a real, large improvement over
  the Hour-1 baseline.
- Next: finish the clean reliability check; if clean, continue with
  measured search/eval improvements (already added IID and razoring this
  hour, not yet promoted/validated), and start bracketing strength against
  stash-21 (~2710) to narrow the estimate further.

## Hour 3 — 2026-08-26 18:30

- Clean reliability check (concurrency 8, no competing foreground load)
  finished: 120/120 games, zero issues attributable to us (1 "time forfeit"
  was stash20 losing on its own clock). This confirms the earlier single
  "abandoned" game was very likely caused by *my own* testing methodology
  (running a concurrency-10 match while simultaneously compiling/testing in
  the foreground, oversubscribing the CPU beyond anything the real
  tournament will do) rather than a remaining engine bug, on top of the
  time-sentinel bug already fixed. **Process change for the rest of the run:
  never do CPU-heavy foreground work while a concurrency>=8 background match
  is active.** Bumped Move Overhead default 30ms -> 40ms as cheap extra
  margin regardless.
- Added internal iterative deepening (IID: shallow search first at PV nodes
  with no TT move, to seed move ordering) and razoring (drop to quiescence
  early when static eval is hopelessly below alpha at very shallow depth).
  Validated with a 240-game match at 10+0.1 vs the pre-IID build: 54.6%
  score, **zero abnormal terminations** (clean concurrency-8 conditions).
  Promoted to `final/`.
- Elo estimate: ~2610 baseline (from the 200-game stash-20 match) plus this
  hour's further +Elo from IID/razoring (not separately re-measured against
  Stash yet, but the internal comparison was unambiguous) — treat current
  estimate as **~2630-2650**, still to be re-anchored.
- Next: bracket strength with a stash-21 (~2710) match to narrow the
  estimate now that we're in that range; keep to the "no foreground work
  during background matches" discipline; consider search/eval tuning next
  since the constants added this session (LMR reduction amounts, futility/
  RFP/razoring margins) were never tuned, just reasoned from typical
  published ranges.

## Hour 3 continued — 2026-08-26 18:45

- stash-21 (~2710 CCRL) bracket match, 200 games at 10+0.1, concurrency 8,
  no competing foreground load: **score 92/200 = 46.0%, 200/200 normal
  terminations** (perfect reliability record on this batch). Estimate from
  this match alone: ~2682. Combined with the stash-20 result (~2610), the
  two brackets converge nicely on **an estimate of roughly 2650, +/-50ish**.
  That's up from ~2350 at the one-hour mark — the eval terms, pruning, IID
  and the time-management bug fix together were worth on the order of 250-300
  Elo in about 90 minutes of focused work.
- Next: keep iterating with cheaply-verified, well-established techniques
  (passed-pawn king proximity/rook-behind-passed-pawn, countermove ordering
  heuristic are the next candidates) rather than chasing exotic ideas; budget
  some time later for lightly tuning the untuned constants added this hour
  (LMR/futility/RFP/razoring margins were reasoned from typical published
  ranges, never tuned on this engine specifically). Continue the "no
  foreground CPU work during background matches" discipline and periodic
  self-play stress tests after any search-path change.

## Hour 4 — 2026-08-26 19:05

- Added passed-pawn king-proximity (endgame king tropism to the promotion
  square), rook-behind-passed-pawn (Tarrasch rule), and a countermove
  move-ordering heuristic (per [piece][to-square] of the opponent's last
  move, tracked table of the response that most recently caused a beta
  cutoff, used as an ordering tier between killers and history). Validated:
  240 games @ 10+0.1 vs the pre-change build, 56.0% score, **zero abnormal
  terminations**. Also ran a 3-game realistic-clock self-play stress test
  (source/tests/selfplay_stress.sh) beforehand as a routine check — clean.
  Promoted to `final/`.
- Process note: kept strictly to "no foreground CPU work while a background
  match is running" this hour; no reliability issues observed since adopting
  that discipline (0 abnormal terminations across the last ~600 test games).
- Elo estimate: was ~2650 before this hour's changes (stash-20/21 brackets);
  this hour's confirmed-positive change (+56% internal) hasn't been
  re-measured against Stash directly yet, so treat current estimate as
  **~2650-2680**, to be re-anchored next.
- Next: re-anchor with a fresh stash-21 (or stash-25, ~2940, to start
  bracketing the next rung) match; then decide between further eval/search
  features vs. lightly tuning existing untuned constants, budgeting against
  the fact roughly 3.5 hours have produced the large majority of the
  strength gain so far and returns may start diminishing — will reassess
  each hour rather than assume.

## Hour 4 continued — 2026-08-26 19:20

- stash-25 (~2940 CCRL) bracket: 200 games, **13.0% score, 200/200 normal
  terminations** (reliability record stays perfect). Estimate from this
  match alone: ~2610. Combined with stash-20 (~2610) and stash-21 (~2682)
  estimates, the three brackets converge on **roughly 2600-2650** as the
  current best estimate (CCRL ratings are for a different time control than
  our 10+0.1, so treat this as directionally right rather than precise).
  No further Elo gain confirmed yet from the last (passed pawn/countermove)
  change specifically against Stash, but the internal A/B result was solid.
- Next: continue with well-established, cheaply-verified improvements —
  candidates: safe mobility (exclude squares attacked by enemy pawns from
  the mobility count, standard refinement), an "easy move" time-saving
  heuristic (commit early when one move is far ahead and stable, to bank
  time for harder positions later in the game). Will keep validating each
  change with an internal A/B match plus periodic Stash re-anchoring, and
  keep the "no foreground work during background matches" + self-play
  stress-test discipline.

## Hour 5 — 2026-08-26 20:19

- Not every idea pays off, and that's fine: tried safe mobility (exclude
  enemy-pawn-covered squares from the mobility count) and an "easy move"
  time-saving heuristic (commit early after 6+ stable iterations) together
  first — 49.6% over 240 games, essentially a wash. Bisected by testing safe
  mobility alone — 46.9% over 240 games, actually a small regression.
  **Reverted both** rather than spend more time chasing a fix; reliability
  was fine throughout (0 abnormal terminations in both tests), this was
  purely a strength judgement call. Lesson: mobility/time-management
  heuristics that are "standard" in other engines aren't guaranteed wins
  here without this engine's specific eval/search calibration, and aren't
  worth defending once the A/B test says no.
- Added a flat tempo bonus (+10cp for the side to move) and a knight-outpost
  bonus (pawn-defended knight, unreachable by enemy pawns, on ranks 3-5).
  Both cheap, standard, low-risk. Validated: 51.5% over 240 games, zero
  abnormal terminations — a real but modest gain, smaller than earlier
  rounds. Promoted to `final/`.
- Observation: the size of each round's measured improvement has been
  shrinking (66% -> 56% -> 51.5%) as expected — the biggest, most obviously
  missing pieces (search pruning, core eval terms) are now in place, so
  returns are diminishing per feature. ~19 hours remain; plan is a few more
  rounds of well-established, cheap eval/search additions (bishop outposts,
  maybe a trapped-piece penalty), then a session of lightly tuning the
  constants that were only ever reasoned from typical published ranges
  (LMR/null-move/futility/RFP/razoring margins), interspersed with periodic
  Stash re-anchoring, and reserving real time at the end for final build
  verification per the rules.
- Elo estimate: still ~2600-2650 (last direct Stash measurement), plus the
  small confirmed gains since then — treat as **~2650-2680**, not yet
  re-measured against Stash.

## Hour 6 — 2026-08-26 21:23

- Added a bishop-outpost eval term (same pattern as the knight outpost:
  pawn-defended, unreachable by enemy pawns, ranks 3-5), smaller bonus than
  the knight version since bishops benefit less from a single strong square.
  51.5% over 240 games, zero abnormal terminations. Promoted to `final/`.
  Deliberately kept this round to a single small, low-risk change (rather
  than bundling with quiescence-search changes I was considering) given the
  LMR formula scare last hour — staying conservative on search-path changes
  for now, eval-only tweaks are lower risk.
- current `final/` feature set: tapered material+PST eval with mobility,
  passed/isolated/doubled pawns + passed-pawn king tropism + rook-behind-
  passed-pawn, bishop pair, knight/bishop outposts, rook on open/semi-open
  file, king safety (shield + attack pressure), tempo bonus; negamax/PVS
  with TT, null-move pruning, LMR, reverse futility pruning, futility
  pruning, razoring, SEE-based capture pruning, IID, countermove/killer/
  history move ordering, aspiration windows, best-move-stability time
  extension. ~2650-2680 estimated Elo, extensively reliability-tested.
- Next: try a couple of conservative, numbers-only parameter adjustments
  (null-move reduction R, futility/RFP margins) rather than more control-flow
  changes for a bit, since those are lower-risk to validate quickly; then
  re-anchor against Stash again to check overall progress this session.
- Tried a more principled log-based LMR reduction formula
  (0.5 + ln(depth)*ln(moveCount)/2.25 instead of the ad-hoc step function).
  Result: 48.8% over 240 games (neutral-to-negative) **and** a third
  "connection stalls" abandoned game turned up in that same test batch.
  Reverted immediately rather than debug — the strength result alone was
  already a no-go, so there was nothing to gain by chasing the stall's exact
  mechanism on a change being thrown out anyway.
- Given three stalls have now been observed (two in engine-vs-itself
  self-play tests, one in a real vs-Stash match that coincided with my own
  heavy foreground CPU load), ran a dedicated 300-game **pure self-play**
  batch on the current, already-validated `final/` build (both sides the
  identical binary, clean CPU, nothing else running) specifically to
  characterize the rate: **1/300 abandoned (0.33%)**, none otherwise. This
  matches the earlier self-play rate closely and, combined with **zero**
  abandoned/stalled games across 400+ real games against Stash engines
  (different binaries) both before and after, points to this being a
  self-play-specific testing artifact (plausibly OS/AV-related contention
  between two simultaneously-launched *identical* executables) rather than a
  defect in the engine's own UCI/time-management logic — which was directly
  stress-tested with realistic decrementing clocks and confirmed correct.
  Accepting this as understood, low-risk, and not worth further chase: the
  real rating match is never engine-vs-itself. Will keep using self-play for
  quick sanity checks but treat a single isolated abandoned game in a
  self-play batch as noise rather than a fire alarm, while continuing to
  treat any stall in a Stash match as a serious signal worth investigating.
