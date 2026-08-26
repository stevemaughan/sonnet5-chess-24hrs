#!/usr/bin/env bash
# Summarize a fastchess PGN: score for the engine named in $2 (default "new"), plus termination reasons.
PGN="$1"
NAME="${2:-new}"
awk -v name="$NAME" '
/^\[White/ { white=$0; gsub(/\[White "|"\]/,"",white) }
/^\[Black/ { black=$0; gsub(/\[Black "|"\]/,"",black) }
/^\[Result/ {
  result=$0; gsub(/\[Result "|"\]/,"",result)
  total++
  if (result=="1-0") { if (white==name) score+=1 }
  else if (result=="0-1") { if (black==name) score+=1 }
  else if (result=="1/2-1/2") { score+=0.5 }
}
END { printf "games=%d score=%.1f pct=%.1f%%\n", total, score, (total>0?score/total*100:0) }
' "$PGN"
echo "--- terminations ---"
grep '^\[Termination' "$PGN" | sort | uniq -c
