#!/usr/bin/env bash
# Drives one engine through a full self-play game via raw UCI, with realistic
# decrementing wtime/btime (like a real GUI), to try to reproduce time-related
# hangs. Exits nonzero and prints diagnostics if any "go" doesn't get a
# bestmove within a hard wall-clock timeout.
set -u
EXE="$1"
GAMES="${2:-3}"
TC_BASE=10000
TC_INC=100
TIMEOUT_S=5

for g in $(seq 1 "$GAMES"); do
  echo "=== game $g ==="
  coproc ENGINE { "$EXE"; }
  exec {IN}<&"${ENGINE[0]}"
  exec {OUT}>&"${ENGINE[1]}"

  echo "uci" >&$OUT
  echo "isready" >&$OUT
  # drain until readyok
  while IFS= read -r -t 5 line <&$IN; do
    [[ "$line" == "readyok" ]] && break
  done

  wtime=$TC_BASE
  btime=$TC_BASE
  moves=""
  sideToMove=w

  for ((ply=1; ply<=140; ply++)); do
    if [[ -z "$moves" ]]; then
      echo "position startpos" >&$OUT
    else
      echo "position startpos moves $moves" >&$OUT
    fi
    echo "go wtime $wtime btime $btime winc $TC_INC binc $TC_INC" >&$OUT

    start=$(date +%s%N)
    bestmove=""
    elapsed_ms=0
    while IFS= read -r -t "$TIMEOUT_S" line <&$IN; do
      if [[ "$line" == bestmove* ]]; then
        bestmove=$(echo "$line" | awk '{print $2}')
        end=$(date +%s%N)
        elapsed_ms=$(( (end-start)/1000000 ))
        break
      fi
    done

    if [[ -z "$bestmove" ]]; then
      echo "STALL DETECTED: game $g ply $ply (side=$sideToMove) wtime=$wtime btime=$btime moves=[$moves]"
      echo "quit" >&$OUT
      exec {IN}<&-
      exec {OUT}>&-
      wait "$ENGINE_PID" 2>/dev/null
      exit 1
    fi
    if [[ "$bestmove" == "0000" ]]; then
      echo "game $g ended (no legal moves) at ply $ply"
      break
    fi

    moves="$moves $bestmove"
    moves="${moves# }"

    if [[ "$sideToMove" == "w" ]]; then
      wtime=$(( wtime - elapsed_ms + TC_INC ))
      sideToMove=b
    else
      btime=$(( btime - elapsed_ms + TC_INC ))
      sideToMove=w
    fi
    if (( wtime < 1 )); then wtime=1; fi
    if (( btime < 1 )); then btime=1; fi
  done

  echo "quit" >&$OUT
  exec {IN}<&-
  exec {OUT}>&-
  wait "$ENGINE_PID" 2>/dev/null
  echo "game $g completed OK, final moves=$(echo $moves | wc -w)"
done

echo "ALL GAMES OK"
