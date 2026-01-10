#!/usr/bin/env bash
set -e

# ===========================================================
# This is the hackiest script you will ever see to help me
# get good screen recordings of buggy gameplay so I can go
# cry about bugs on Discord lol :)
#
# This just records just the gbc window under X11
# ===========================================================

FPS=60
CRF=18
DISPLAY="${DISPLAY:-:0}"
OUTPUT="gbc_$(basename | sed 's/\.[^.]*$//')_$(date +%Y%m%d_%H%M%S).mp4"

run_gbc() {
  if [[ -x "./release/gbc" ]]; then
    exec ./release/gbc
  elif [[ -x "./build/gbc" ]]; then
    exec ./build/gbc
  else
    echo "Error: gbc not found in ./build or ./release"
    exit 1
  fi
}


cleanup() {
  if [[ -n "$FFMPEG_PID" ]]; then
    echo "Stopping recording..."
    kill -INT "$FFMPEG_PID" 2>/dev/null || true
    wait "$FFMPEG_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "Launching gbc..."
run_gbc &
FFMPEG_PID=""
GBC_PID=$!

echo "Waiting for gbc window..."
while true; do
  WIN_ID=$(xdotool search --pid "$GBC_PID" --onlyvisible 2>/dev/null | head -n1 || true)
  if [[ -n "$WIN_ID" ]]; then
    # Get geometry robustly
    if eval "$(xdotool getwindowgeometry --shell "$WIN_ID" 2>/dev/null)"; then
      if [[ -n "$WIDTH" && -n "$HEIGHT" ]]; then
        X="$X"
        Y="$Y"
        W="$WIDTH"
        H="$HEIGHT"
        break
      fi
    fi
  fi
  sleep 0.05
done

echo "Window geometry: ${W}x${H}+${X}+${Y}"
echo "Starting recording..."
ffmpeg \
  -y \
  -f x11grab \
  -framerate "$FPS" \
  -video_size "${W}x${H}" \
  -i "${DISPLAY}+${X},${Y}" \
  -c:v libx264 \
  -preset veryfast \
  -crf "$CRF" \
  "$OUTPUT" &
FFMPEG_PID=$!

# Wait for gbc to exit
wait "$GBC_PID" || true
echo "gbc exited, recording saved to $OUTPUT"
