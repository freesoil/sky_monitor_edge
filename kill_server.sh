#!/usr/bin/env bash
set -euo pipefail

# Kill uvicorn / FastAPI dev servers cleanly, with an optional port filter.
# Usage:
#   ./kill_server.sh           # kill matching processes
#   ./kill_server.sh --force   # skip grace period, SIGKILL immediately
#   ./kill_server.sh --port 8000   # only kill procs bound to port 8000
#   ./kill_server.sh --dry-run  # show what would be killed

GRACE_SECONDS=4
FORCE=0
DRYRUN=0
PORT_FILTER=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) FORCE=1; shift ;;
    --dry-run) DRYRUN=1; shift ;;
    --port) PORT_FILTER="$2"; shift 2 ;;
    *) echo "Unknown arg: $1"; exit 2 ;;
  esac
done

# Build candidate PID list
# Match common dev-server patterns: uvicorn, "python ... server", "main:app"
mapfile -t PIDS < <(pgrep -f -a -l -d $'\n' 'uvicorn|python .*server|main:app' \
  | awk '{print $1}')

# Optionally filter by port (requires lsof)
if [[ -n "$PORT_FILTER" ]]; then
  if ! command -v lsof >/dev/null 2>&1; then
    echo "lsof not found, cannot filter by port" >&2
    exit 1
  fi
  mapfile -t PORT_PIDS < <(lsof -ti :"$PORT_FILTER" || true)
  if [[ ${#PORT_PIDS[@]} -eq 0 ]]; then
    echo "No processes listening on port $PORT_FILTER."
    exit 0
  fi
  # intersect PIDS with PORT_PIDS
  TMP=()
  for p in "${PIDS[@]}"; do
    for q in "${PORT_PIDS[@]}"; do
      if [[ "$p" == "$q" ]]; then TMP+=("$p"); fi
    done
  done
  PIDS=("${TMP[@]}")
fi

# Deduplicate and remove own PID
SELF=$$
UNIQ=()
for p in "${PIDS[@]}"; do
  [[ -n "$p" && "$p" != "$SELF" ]] && UNIQ+=("$p")
done
PIDS=($(printf "%s\n" "${UNIQ[@]}" | sort -u))

if [[ ${#PIDS[@]} -eq 0 ]]; then
  echo "No matching processes found."
  exit 0
fi

echo "Found PIDs: ${PIDS[*]}"

if [[ "$DRYRUN" -eq 1 ]]; then
  echo "[dry-run] Would send ${FORCE:+SIGKILL}${FORCE==0:+SIGTERM} to: ${PIDS[*]}"
  exit 0
fi

if [[ "$FORCE" -eq 1 ]]; then
  echo "Sending SIGKILL to: ${PIDS[*]}"
  kill -9 "${PIDS[@]}" || true
  exit 0
fi

# Graceful first
echo "Sending SIGTERM to: ${PIDS[*]}"
kill "${PIDS[@]}" || true

# Wait up to GRACE_SECONDS for exit
deadline=$((SECONDS + GRACE_SECONDS))
still_up=()
while (( SECONDS < deadline )); do
  still_up=()
  for p in "${PIDS[@]}"; do
    if kill -0 "$p" 2>/dev/null; then
      still_up+=("$p")
    fi
  done
  if [[ ${#still_up[@]} -eq 0 ]]; then
    echo "All processes exited gracefully."
    exit 0
  fi
  sleep 0.5
done

echo "Forcing SIGKILL to remaining PIDs: ${still_up[*]}"
kill -9 "${still_up[@]}" || true
echo "Done."

