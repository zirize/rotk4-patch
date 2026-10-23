#!/bin/bash
# Build the patch: the files a player drops into their own game folder (no game files).
#
#   make-patch.sh <cnc-ddraw.zip> <out.zip | out-dir>
#
# <cnc-ddraw.zip> cnc-ddraw v7.1.0.0 release zip (ddraw.dll, ddraw.ini, "cnc-ddraw config.exe")
# <out>           a .zip to write, or a directory to fill
# The shim is built first (make -C shim). Contents: ddraw.dll (shim), cnc-ddraw.dll,
# "cnc-ddraw config.exe", ddraw.ini, 읽어보기.txt and licenses/.
set -euo pipefail

[ $# -eq 2 ] || { sed -n '2,9p' "$0"; exit 1; }
cnc=$1 out=$(realpath -m "$2")
repo=$(cd "$(dirname "$0")/.." && pwd)

make -C "$repo/shim" >/dev/null

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
case "$out" in
    *.zip) dst=$stage/patch ;;
    *) dst=$out ;;
esac
mkdir -p "$dst/licenses"
7z x -bd -o"$stage/cnc" "$cnc" >/dev/null
cp "$stage/cnc/ddraw.dll" "$dst/cnc-ddraw.dll"
cp "$stage/cnc/cnc-ddraw config.exe" "$dst/"
cp "$repo/shim/ddraw.dll" "$dst/"
cp "$repo/LICENSE" "$dst/licenses/rotk4-patch.txt"
cp "$repo/tools/cnc-ddraw-LICENSE.txt" "$dst/licenses/cnc-ddraw.txt"
cp "$repo/shim/minimp3/LICENSE" "$dst/licenses/minimp3.txt"

# cnc-ddraw ini: windowed with aspect ratio kept, plus the game section; Alt+Enter goes to
# borderless fullscreen (exclusive fullscreen changes the monitor mode to the window size,
# which some screens do not take). Window size and position stay at the defaults
# (0, -32000); the shim picks a size on first start.
# The window is an integer multiple of the game (shim "화면" menu), so nearest neighbor keeps
# pixels sharp: `shader` for OpenGL (Wine picks it), `d3d9_filter` for Direct3D 9 (Windows
# picks it; its default 2 looked blurred on Windows 11). boxing keeps full screen at an integer
# multiple too. Both filters are built in, so Shaders/ is left out. Edited in [ddraw] only,
# where the config tool writes, so the user can still change them there.
# devmode=true in [SAN4W95]: the cursor lock starts off (the shim's "마우스 가두기" turns it on).
sed -e '/^\[ddraw\]\r$/,/^\[/{' \
    -e 's/^windowed=false\r$/windowed=true\r/; s/^maintas=false\r$/maintas=true\r/' \
    -e 's/^boxing=false\r$/boxing=true\r/; s/^shader=.*\r$/shader=Nearest neighbor\r/' \
    -e 's/^d3d9_filter=.*\r$/d3d9_filter=0\r/' -e '}' \
    "$stage/cnc/ddraw.ini" >"$dst/ddraw.ini"
printf '; Romance of the Three Kingdoms IV (Korean Win95)\r\n[SAN4W95]\r\nfake_mode=640x480x8\r\nnonexclusive=false\r\nadjmouse=true\r\ntoggle_borderless=true\r\nresizable=false\r\ndevmode=true\r\n' >>"$dst/ddraw.ini"
for l in windowed=true maintas=true boxing=true 'shader=Nearest neighbor' d3d9_filter=0; do
    grep -q "^$l"$'\r$' "$dst/ddraw.ini" || { echo "ddraw.ini edit failed: $l" >&2; exit 1; }
done

# Readme for Windows users: UTF-8 with BOM and CRLF so Notepad shows it right.
{ printf '\xef\xbb\xbf'; sed 's/$/\r/' "$repo/tools/dist-readme.txt"; } >"$dst/읽어보기.txt"

if [ "$dst" != "$out" ]; then
    rm -f "$out"
    (cd "$dst" && 7z a -bd -tzip -mx=9 "$out" . >/dev/null)
    ls -la "$out"
fi
