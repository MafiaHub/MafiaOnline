#!/usr/bin/env bash
set -euo pipefail

# Local Wine/Hyprland test harness. All writable paths stay in this repo.
# Set MAFIA1ONLINE_TEST_WINEDEBUG=-all,+seh to trace Wine exceptions in the
# per-client console logs without enabling Wine's other diagnostic channels.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)
runtime="$root/builds/runtime"
bin="$root/builds/build-32/bin"
game_root="$runtime/game/Mafia"
monitor=DP-1
workspace=${MAFIA1ONLINE_TEST_WORKSPACE:-8}
mode=${1:-two}
test_port=${MAFIA1ONLINE_TEST_PORT:-}
window_width=1024
window_height=768
window_gap=96
window_margin=80

[[ $# -le 1 && ( "$mode" == two || "$mode" == --late-join ) ]] || {
    echo "Usage: $0 [--late-join]" >&2
    exit 2
}

for command in wine wineserver hyprctl jq setsid flock pactl python3; do
    command -v "$command" >/dev/null || { echo "Missing $command" >&2; exit 1; }
done

[[ -f "$bin/Mafia1OnlineLauncher.exe" ]] || { echo "Build Mafia1OnlineLauncher 32 first" >&2; exit 1; }
[[ -f "$bin/Mafia1OnlineClient.dll" ]] || { echo "Build Mafia1OnlineClient 32 first" >&2; exit 1; }
[[ -f "$bin/config/client.json" ]] || { echo "Create $bin/config/client.json for quick join" >&2; exit 1; }
[[ -f "$game_root/Mafia/Game.exe" ]] || { echo "Expected isolated game copy at $game_root/Mafia/Game.exe" >&2; exit 1; }
for prefix in "$runtime/prefix" "$runtime/prefix-second"; do
    [[ -d "$prefix/drive_c" ]] || { echo "Expected repo-local Wine prefix at $prefix" >&2; exit 1; }
done

monitor_details=$(
    hyprctl monitors -j | jq -r --arg name "$monitor" '.[] | select(.name == $name and .scale == 1) | [.id, .width, .height, .x, .y] | @tsv'
)
if [[ -z "$monitor_details" ]]; then
    echo "A scale-1 $monitor monitor is required" >&2
    exit 1
fi
read -r monitor_id monitor_width monitor_height monitor_x monitor_y <<< "$monitor_details"
layout_width=$((window_width * 2 + window_gap))
if ((monitor_width < layout_width + window_margin * 2 || monitor_height < window_height + window_margin * 2)); then
    echo "A scale-1 $monitor monitor of at least $((layout_width + window_margin * 2))x$((window_height + window_margin * 2)) is required" >&2
    exit 1
fi
first_x=$((monitor_x + (monitor_width - layout_width) / 2))
second_x=$((first_x + window_width + window_gap))
window_y=$((monitor_y + (monitor_height - window_height) / 2))

mafia_windows() {
    hyprctl clients -j | jq -r '.[] | select(.class == "mafia1onlinelauncher.exe") | [.address, .pid] | @tsv' |
        while IFS=$'\t' read -r address pid; do
            state=$(ps -o stat= -p "$pid" 2>/dev/null) || continue
            if [[ $state != Z* ]]; then printf '%s\n' "$address"; fi
        done
}

existing=$(mafia_windows)
if [[ "$mode" == two && -n "$existing" ]]; then
    echo "Close existing Mafia1Online windows before starting the two-client harness" >&2
    exit 1
fi
if [[ "$mode" == --late-join && $(printf '%s\n' "$existing" | sed '/^$/d' | wc -l) -ne 1 ]]; then
    echo "Late join requires exactly one running Mafia1Online window" >&2
    exit 1
fi

# Closing a game window can leave Wine services alive in its test prefix. A
# later launch may then exit before native graphics initialization.
if [[ "$mode" == two ]]; then
    WINEPREFIX="$runtime/prefix" wineserver -k || true
fi
WINEPREFIX="$runtime/prefix-second" wineserver -k || true

mkdir -p "$runtime/cache" "$runtime/config"
if [[ -n "$test_port" ]]; then
    [[ "$test_port" =~ ^[0-9]+$ && $test_port -ge 1 && $test_port -le 65535 ]] || {
        echo "MAFIA1ONLINE_TEST_PORT must be a UDP port" >&2
        exit 2
    }
    jq --argjson port "$test_port" '.quickJoin.port = $port' \
        "$bin/config/client.json" > "$runtime/config/client-first.json"
else
    cp "$bin/config/client.json" "$runtime/config/client-first.json"
fi
jq --arg nickname Smoke2 '.quickJoin.nickname = $nickname' \
    "$runtime/config/client-first.json" > "$runtime/config/client-second.json"
# Override Omarchy's default-opacity tag for this app at runtime. The rule is
# named so another harness run updates it instead of accumulating duplicates.
hyprctl eval 'hl.window_rule({name="mafia1online-test-opaque",match={class="^mafia1onlinelauncher[.]exe$"},tag="-default-opacity",opacity="1 1"})' >/dev/null
hyprctl eval "hl.dispatch(hl.dsp.focus({monitor=\"$monitor\"})); hl.dispatch(hl.dsp.focus({workspace=\"$workspace\"}))" >/dev/null

wait_for_new_window() {
    local old_windows=$1 new_window
    for ((attempt = 0; attempt < 45; ++attempt)); do
        new_window=$(comm -13 <(printf '%s\n' "$old_windows" | sed '/^$/d' | sort) <(mafia_windows | sort) | head -n 1)
        if [[ -n "$new_window" ]]; then
            printf '%s\n' "$new_window"
            return 0
        fi
        sleep 1
    done
    echo "Timed out waiting for a Mafia window" >&2
    return 1
}

launch_client() {
    local index=$1 prefix=$2 config_path="$runtime/config/client-first.json" profile_base=0
    if [[ $index == 2 ]]; then
        config_path="$runtime/config/client-second.json"
        profile_base=8
    fi
    (
        cd -- "$bin"
        setsid -f env \
            WINEPREFIX="$prefix" \
            WINEDEBUG="${MAFIA1ONLINE_TEST_WINEDEBUG:--all}" \
            XDG_CACHE_HOME="$runtime/cache" \
            XDG_CONFIG_HOME="$runtime/config" \
            MAFIA1ONLINE_GAME_ROOT="$game_root" \
            MAFIA1ONLINE_CLIENT_CONFIG="$config_path" \
            FW_CEF_PROFILE_BASE="$profile_base" \
            wine "$bin/Mafia1OnlineLauncher.exe" \
            </dev/null >"$runtime/client-$index-console.log" 2>&1
    )
}

place_window() {
    local address=$1 x=$2
    hyprctl eval "hl.dispatch(hl.dsp.focus({window=\"address:$address\"})); hl.dispatch(hl.dsp.window.move({workspace=\"$workspace\",follow=false})); hl.dispatch(hl.dsp.focus({window=\"address:$address\"})); hl.dispatch(hl.dsp.window.move({window=\"address:$address\",x=$x,y=$window_y,relative=false}))" >/dev/null
}

if [[ "$mode" == two ]]; then
    before=$(mafia_windows)
    launch_client 1 "$runtime/prefix"
    first=$(wait_for_new_window "$before")
else
    first=$existing
fi
place_window "$first" "$first_x"

before=$(mafia_windows)
launch_client 2 "$runtime/prefix-second"
second=$(wait_for_new_window "$before")
place_window "$second" "$second_x"

hyprctl eval "hl.dispatch(hl.dsp.focus({monitor=\"$monitor\"})); hl.dispatch(hl.dsp.focus({workspace=\"$workspace\"}))" >/dev/null

hyprctl clients -j | jq -e --arg first "$first" --arg second "$second" --argjson workspace "$workspace" --argjson monitor "$monitor_id" \
    --argjson width "$window_width" --argjson height "$window_height" \
    --argjson firstX "$first_x" --argjson secondX "$second_x" --argjson windowY "$window_y" '
    [ .[] | select(.address == $first or .address == $second)
      | select(.monitor == $monitor and .workspace.id == $workspace and .size == [$width,$height] and .floating)
      | select((.address == $first and .at == [$firstX,$windowY]) or (.address == $second and .at == [$secondX,$windowY])) ] | length == 2
' >/dev/null || { echo "One or both windows did not reach the requested ${window_width}x${window_height} workspace" >&2; exit 1; }

for address in "$first" "$second"; do
    [[ $(hyprctl getprop "address:$address" opacity) =~ ^1(\.0+)?$ &&
       $(hyprctl getprop "address:$address" opacity_inactive) =~ ^1(\.0+)?$ ]] || {
        echo "Window $address is still transparent" >&2
        exit 1
    }
done

echo "Two ${window_width}x${window_height} clients on $monitor workspace $workspace: $first at ($first_x,$window_y), $second at ($second_x,$window_y)"

# A single monitor follows both Wine prefixes. It survives --late-join and
# exits once neither client has a window. flock prevents duplicate monitors.
setsid -f flock -n "$runtime/focus-audio.lock" python3 "$root/code/projects/mafia1online/tools/focus_client_audio.py" \
    "$runtime/prefix" "$runtime/prefix-second" </dev/null >>"$runtime/focus-audio.log" 2>&1
