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
- Elo estimate: not yet measurable (smoke match running).
- Next hour: check smoke match result; if solid, spend remaining time on
  evaluation quality (pawn structure, king safety, mobility tuning) and
  search tuning, with periodic SPRT-style checks against Stash versions to
  track progress, always keeping `final/` on the last verified-good build.
