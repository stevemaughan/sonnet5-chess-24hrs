#!/usr/bin/env bash
# Quick SPRT helper: sprt.sh <new_exe> <old_exe> <out_pgn> [rounds] [elo1]
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NEW="$1"
OLD="$2"
OUT="$3"
ROUNDS="${4:-400}"
ELO1="${5:-15}"

"$ROOT/resources/fastchess/fastchess.exe" \
  -engine cmd="$NEW" name=new \
  -engine cmd="$OLD" name=old \
  -each tc=10+0.1 option.Hash=256 \
  -openings file="$ROOT/resources/fastchess/UHO.pgn" format=pgn order=random plies=16 \
  -sprt elo0=0 elo1="$ELO1" alpha=0.05 beta=0.05 \
  -rounds "$ROUNDS" -repeat -concurrency 10 -recover \
  -ratinginterval 20 \
  -pgnout file="$OUT"
