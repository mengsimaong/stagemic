#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Loads StageMic into a real OBS Studio (Linux) on a virtual screen, plays a
# looping test tone through the filter, and checks the OBS log.
#
# Usage: scripts/obs-smoke-test.sh <path/to/stagemic.so> [output-dir]
# Needs: obs-studio, xvfb (Xvfb), ffmpeg, imagemagick (for the screenshot).
set -euo pipefail

PLUGIN_SO="$(realpath "$1")"
OUT="$(realpath -m "${2:-./obs-smoke-output}")"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

rm -rf "$OUT"
mkdir -p "$OUT/home"
export HOME="$OUT/home"
# CI machines often set XDG_CONFIG_HOME, which OBS prefers over $HOME/.config.
export XDG_CONFIG_HOME="$HOME/.config"
CFG="$XDG_CONFIG_HOME/obs-studio"

# --- Install the plugin into a throwaway OBS profile -------------------------
mkdir -p "$CFG/plugins/stagemic/bin/64bit" "$CFG/plugins/stagemic/data" "$CFG/basic/scenes" "$CFG/basic/profiles/Test"
cp "$PLUGIN_SO" "$CFG/plugins/stagemic/bin/64bit/stagemic.so"
cp -r "$REPO_DIR/data/." "$CFG/plugins/stagemic/data/"

# Test tone: 440 Hz sine (ffmpeg default amplitude 1/8, then x0.5, upmixed to stereo)
ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:duration=10" -af "volume=0.5" -ar 48000 -ac 2 "$OUT/tone.wav"

cat > "$CFG/global.ini" <<EOF
[General]
FirstRun=true
[Basic]
Profile=Test
ProfileDir=Test
SceneCollection=StageMicTest
SceneCollectionFile=StageMicTest
EOF
cp "$CFG/global.ini" "$CFG/user.ini"

cat > "$CFG/basic/profiles/Test/basic.ini" <<EOF
[General]
Name=Test
[Audio]
SampleRate=48000
ChannelSetup=Stereo
[Video]
BaseCX=640
BaseCY=360
OutputCX=640
OutputCY=360
EOF

python3 - "$OUT" "$CFG/basic/scenes/StageMicTest.json" <<'PY'
import json, sys
out, path = sys.argv[1], sys.argv[2]
track = {
    "id": "ffmpeg_source", "versioned_id": "ffmpeg_source", "name": "Karaoke Track",
    "uuid": "11111111-1111-4111-8111-111111111111",
    "settings": {"local_file": out + "/tone.wav", "looping": True, "is_local_file": True},
    "mixers": 255, "volume": 1.0, "enabled": True, "monitoring_type": 0,
    "filters": [{"id": "stagemic_filter", "versioned_id": "stagemic_filter", "name": "StageMic",
                 "uuid": "22222222-2222-4222-8222-222222222222", "settings": {}, "enabled": True}],
}
scene = {"id": "scene", "versioned_id": "scene", "name": "Scene", "uuid": "33333333-3333-4333-8333-333333333333",
         "settings": {"items": [{"name": "Karaoke Track", "source_uuid": track["uuid"], "visible": True, "id": 1}],
                      "id_counter": 1}}
json.dump({"name": "StageMicTest", "current_scene": "Scene", "current_program_scene": "Scene",
           "scene_order": [{"name": "Scene"}], "sources": [scene, track], "groups": [], "transitions": [],
           "current_transition": "Fade", "transition_duration": 300}, open(path, "w"), indent=1)
PY

# --- Run OBS on a virtual screen ---------------------------------------------
DISPLAY_NUM=":$((90 + RANDOM % 9))"
Xvfb "$DISPLAY_NUM" -screen 0 1600x1000x24 >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill $XVFB_PID 2>/dev/null || true' EXIT
sleep 1

export DISPLAY="$DISPLAY_NUM" LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb STAGEMIC_OPEN_EDITOR_ON_LOAD=1
obs --collection StageMicTest --profile Test --disable-shutdown-check --disable-updater >"$OUT/obs-stdout.txt" 2>&1 &
OBS_PID=$!

sleep 15
command -v import >/dev/null && import -window root "$OUT/screenshot.png" || true

kill -INT "$OBS_PID"
for _ in $(seq 1 30); do kill -0 "$OBS_PID" 2>/dev/null || break; sleep 1; done
if kill -0 "$OBS_PID" 2>/dev/null; then
  echo "FAIL: OBS did not exit within 30 s"; kill -9 "$OBS_PID"; exit 1
fi
wait "$OBS_PID" || { echo "FAIL: OBS exited with an error. Last lines of OBS output:"; tail -n 40 "$OUT/obs-stdout.txt"; exit 1; }

# --- Check the log -----------------------------------------------------------
LOG="$(ls -t "$CFG"/logs/*.txt 2>/dev/null | head -1 || true)"
if [ -z "$LOG" ]; then
  echo "FAIL: OBS wrote no log file. Last lines of OBS output:"
  tail -n 40 "$OUT/obs-stdout.txt" || true
  exit 1
fi
cp "$LOG" "$OUT/obs-log.txt"
echo "---- StageMic lines from the OBS log ----"
grep -E "\[StageMic\]|Number of memory leaks" "$LOG" || true
echo "-----------------------------------------"

status=0
for expected in \
  "\[StageMic\] Loaded" \
  "\[StageMic\] Filter created" \
  "\[StageMic\] Audio is flowing" \
  "\[StageMic\] Filter destroyed" \
  "\[StageMic\] Unloaded" \
  "Number of memory leaks: 0"; do
  if grep -qE "$expected" "$LOG"; then echo "PASS: $expected"; else echo "FAIL: $expected"; status=1; fi
done

if grep -qiE "JUCE.*leak|Leaked objects detected" "$OUT/obs-stdout.txt"; then
  echo "FAIL: JUCE reported leaked objects"; status=1
fi

exit $status
