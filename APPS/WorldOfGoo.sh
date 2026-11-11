#!/bin/sh
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT="$BASE/WorldOfGoo_RGDSPlus";RT="$ROOT/runtime";LOGS="$ROOT/logs"
mkdir -p "$LOGS" "$ROOT/savedata/home" "$ROOT/savedata/config" "$ROOT/savedata/data" || exit 1
LOCK="$ROOT/savedata/.running"
if ! mkdir "$LOCK" 2>/dev/null; then
 old=$(cat "$LOCK/pid" 2>/dev/null);case "$old" in ''|*[!0-9]*) old='';; esac
 [ -z "$old" ] || ! kill -0 "$old" 2>/dev/null || exit 1
 rm -f "$LOCK/pid";rmdir "$LOCK" 2>/dev/null && mkdir "$LOCK" || exit 1
fi
echo "$$" > "$LOCK/pid"
[ ! -f "$LOGS/Launcher.txt" ] || cp "$LOGS/Launcher.txt" "$LOGS/Previous_Launcher.txt"
exec > "$LOGS/Launcher.txt" 2>&1
LOWER=/sys/class/backlight/backlight/brightness;VOLUME=/sys/class/anbernic_misc/openbor_volume
lower_saved='' volume_saved='' top_node='' top_power='' top_brightness='' game_pid='' input_pid='' mirror_pid='' temp=''
cleanup(){
 trap - EXIT INT TERM HUP
 if [ -n "$game_pid" ]; then
  kill -TERM "$game_pid" 2>/dev/null || :;n=0
  while kill -0 "$game_pid" 2>/dev/null && [ "$n" -lt 3 ]; do sleep 1;n=$((n+1));done
  kill -KILL "$game_pid" 2>/dev/null || :;wait "$game_pid" 2>/dev/null || :
 fi
 [ -z "$mirror_pid" ] || { kill "$mirror_pid" 2>/dev/null || :;wait "$mirror_pid" 2>/dev/null || :; }
 [ -z "$input_pid" ] || { kill "$input_pid" 2>/dev/null || :;wait "$input_pid" 2>/dev/null || :; }
 [ -z "$top_power" ] || printf '%s\n' "$top_power" > "$top_node/bl_power" 2>/dev/null || :
 [ -z "$top_brightness" ] || printf '%s\n' "$top_brightness" > "$top_node/brightness" 2>/dev/null || :
 [ -z "$lower_saved" ] || printf '%s\n' "$lower_saved" > "$LOWER" 2>/dev/null || :
 [ -z "$volume_saved" ] || printf '%s\n' "$volume_saved" > "$VOLUME" 2>/dev/null || :
 cat /proc/meminfo > "$LOGS/Memory_End.txt" 2>/dev/null || :
 echo "Restored state: volume=$(cat "$VOLUME" 2>/dev/null), lower=$(cat "$LOWER" 2>/dev/null)"
 case "$temp" in /tmp/worldofgoo-rgdsplus.*) rm -rf "$temp";; esac
 rm -f "$LOCK/pid";rmdir "$LOCK" 2>/dev/null || :
 
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
printf 'World of Goo RGDS+ v1.1.0 (Box64 translated)\n';date;uname -srvm
case "$(uname -m)" in aarch64|arm64) :;; *) echo 'ARM64 Linux required';exit 2;; esac
command -v python3 >/dev/null 2>&1 || { echo 'Firmware Python 3 unavailable';exit 3; }
python3 -B "$RT/prepare.py" "$ROOT" || exit 3
python3 -B "$RT/prepare_game.py" "$ROOT" || exit 3
for d in /var/run /run "/run/user/$(id -u)"; do
 for s in "$d"/wayland-*; do
  [ -S "$s" ] || continue;export XDG_RUNTIME_DIR="$d" WAYLAND_DISPLAY="${s##*/}";break 2
 done
done
[ -n "${WAYLAND_DISPLAY:-}" ] || { echo 'No firmware Wayland display found';exit 4; }
unset LD_PRELOAD SDL_DYNAMIC_API BOX64_EMULATED_LIBS BOX64_PREFER_EMULATED
export LANG=C LC_ALL=C
export LD_LIBRARY_PATH="$RT/audio:/lib:/usr/lib:/usr/lib64"
export BOX64_DYNAREC_STRONGMEM=1 BOX64_DYNAREC_ALIGNED_ATOMICS=1
unset BOX64_BOX32
export BOX64_DYNAREC=1 BOX64_NOPERSONA32BITS=1 BOX64_DYNACACHE=0 BOX64_LOG=1 BOX64_SHOWSEGV=1
export BOX64_LD_LIBRARY_PATH="$RT/libs.x64:$ROOT/game/lib64"
export BOX64_LD_PRELOAD="$RT/libs.x64/libgoo_rgds.so"
export BOX64_LIBGL=/lib/libGLESv2.so.2
export GOO_NATIVE_GLES=1
export SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=alsa SDL_OPENGL_ES_DRIVER=1 SDL_VIDEO_GL_DRIVER=/lib/libGLESv2.so.2
# Use firmware native SDL2 and the guest compatibility preload.
unset SDL_DYNAMIC_API
export SDL_VIDEO_FULLSCREEN_DISPLAY=1 SDL2_SWAP_LCD=0
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
unset DISPLAY
export XDG_CONFIG_HOME="$ROOT/savedata/config" XDG_DATA_HOME="$ROOT/savedata/data"
# Keep SDL preferences private to this port.
export GOO_SAVED_HOME="$ROOT/savedata/home"
export GOO_VOLUME_NODE="$VOLUME"
export GOO_PREF_PATH="$ROOT/savedata/preferences"
temp=$(mktemp -d /tmp/worldofgoo-rgdsplus.XXXXXX) || exit 5
export GOO_INPUT_STATE="$temp/input.bin" GOO_EXIT_REQUEST="$temp/exit-request" GOO_TOP_READY="$temp/top-ready" GOO_MIRROR_FRAME="$temp/mirror-frame"
if mkfifo -m 600 "$temp/frame-notify"; then
 export GOO_MIRROR_NOTIFY="$temp/frame-notify"
fi
for spec in "$VOLUME" "$LOWER"; do
 [ -r "$spec" ] && [ -w "$spec" ] || { echo "Hardware setting unavailable: $spec";continue; }
 value=$(cat "$spec");case "$value" in ''|*[!0-9]*) continue;; esac
 case "$spec" in "$VOLUME") [ "$value" -gt 10 ] || volume_saved="$value";; "$LOWER") lower_saved="$value";; esac
done
python3 -u "$RT/device.py" "$GOO_INPUT_STATE" "$GOO_EXIT_REQUEST" > "$LOGS/Controls.txt" 2>&1 & input_pid=$!
n=0;while [ ! -s "$GOO_INPUT_STATE" ] && [ "$n" -lt 30 ]; do sleep .1;n=$((n+1));done
kill -0 "$input_pid" 2>/dev/null || { echo "Input reader stopped; see Controls.txt";exit 6; }
[ -s "$GOO_INPUT_STATE" ] || { echo 'Input mailbox initialization failed';exit 6; }
cat /proc/meminfo > "$LOGS/Memory_Start.txt"
chmod +x "$RT/upper-presenter" "$RT/box64-box32" "$ROOT/game/WorldOfGoo.rgds.x86_64" 2>/dev/null || :
ulimit -c 0
cd "$ROOT/game" || exit 7
LD_PRELOAD="$RT/librgds_video.so" "$RT/box64-box32" ./WorldOfGoo.rgds.x86_64 > "$LOGS/Game.txt" 2>&1 & game_pid=$!
# Verified on stock RGDS+ by PolluxPT's ScummVM port and the Alpha 1.3 log.
if [ -d "${LOWER%/*}" ] && [ -d /sys/class/backlight/backlight1 ]; then
 top_node=/sys/class/backlight/backlight1
else
 echo 'Stock top backlight unavailable'
fi
if [ -n "$top_node" ]; then
 if [ -r "$top_node/bl_power" ] && [ -w "$top_node/bl_power" ]; then
  value=$(cat "$top_node/bl_power")
  case "$value" in ''|*[!0-9]*) ;; *) top_power="$value";printf '0\n' > "$top_node/bl_power" || :;; esac
 fi
 if [ -r "$top_node/brightness" ] && [ -w "$top_node/brightness" ]; then
  value=$(cat "$top_node/brightness")
  case "$value" in ''|*[!0-9]*) ;; *) top_brightness="$value"; [ "$value" -ne 0 ] || printf '%s\n' "${lower_saved:-70}" > "$top_node/brightness" || :;; esac
 fi
fi
mirror_attempted=0
lower_dark=0
: > "$LOGS/Memory_Peak.txt"
# The guest input bridge injects SDL_QUIT; retain a timeout for hung exits.
while kill -0 "$game_pid" 2>/dev/null; do
 awk '/^VmRSS:|^VmHWM:/{printf "%s %s kB ",$1,$2} /^MemAvailable:/{printf "%s %s kB",$1,$2} END{print ""}'      "/proc/$game_pid/status" /proc/meminfo >> "$LOGS/Memory_Peak.txt" 2>/dev/null

 if [ "$mirror_attempted" -eq 0 ] && [ -s "$GOO_MIRROR_FRAME" ]; then
  mirror_attempted=1
  "$RT/upper-presenter" "$GOO_MIRROR_FRAME" "$GOO_TOP_READY" > "$LOGS/Mirror.txt" 2>&1 & mirror_pid=$!
 fi
 # Preserve lower scanout for the game's render surface. Only blank its lamp
 # after the upper Wayland surface has received a frame callback on its output.
 if [ -n "$mirror_pid" ] && kill -0 "$mirror_pid" 2>/dev/null && [ -f "$GOO_TOP_READY" ]; then
  if [ "$lower_dark" -eq 0 ] && [ -n "$lower_saved" ] && printf '0\n' > "$LOWER"; then
   lower_dark=1
   echo "Upper presenter active. Bottom brightness=$(cat "$LOWER" 2>/dev/null)"
  fi
 elif [ "$lower_dark" -eq 1 ]; then
  printf '%s\n' "$lower_saved" > "$LOWER" 2>/dev/null || :
  lower_dark=0
  echo 'Upper presentation lost. Restored bottom backlight.'
 fi
 if [ -f "$GOO_EXIT_REQUEST" ]; then
  echo "Exit requested by Start + Select"
  n=0;while kill -0 "$game_pid" 2>/dev/null && [ "$n" -lt 5 ]; do sleep 1;n=$((n+1));done
  kill -TERM "$game_pid" 2>/dev/null || :
  n=0;while kill -0 "$game_pid" 2>/dev/null && [ "$n" -lt 3 ]; do sleep 1;n=$((n+1));done
  kill -KILL "$game_pid" 2>/dev/null || :
 fi
 sleep 1
done
wait "$game_pid";result=$?;game_pid='';echo "Game exit code: $result"
if [ "$result" -eq 137 ]; then
 dmesg 2>/dev/null | grep -iE 'out of memory|oom-kill|killed process' | tail -25 > "$LOGS/Kernel_OOM.txt"
fi
exit "$result"
