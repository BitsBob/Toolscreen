#!/bin/bash
# Installs the macOS build of Toolscreen into a Prism Launcher / MultiMC instance.
#
#   ./install-macos.sh [INSTANCE_DIR]              install (prompts for an instance when omitted)
#   ./install-macos.sh --uninstall [INSTANCE_DIR]  remove Toolscreen from an instance
#
# Toolscreen runs inside Minecraft's JVM as a JVMTI agent. Installing copies libToolscreen.dylib (and
# liblogger.dylib) to ~/.local/share/toolscreen-macos and adds
#     -agentpath:<that folder>/libToolscreen.dylib
# to the instance's JVM arguments (Edit Instance -> Settings -> Java -> JVM arguments).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="${TOOLSCREEN_INSTALL_DIR:-$HOME/.local/share/toolscreen-macos}"
AGENT_PATH="$INSTALL_DIR/libToolscreen.dylib"

die() {
    echo "error: $*" >&2
    exit 1
}

find_payload() {
    local name="$1"
    for dir in "$SCRIPT_DIR" "$SCRIPT_DIR/bin" "$SCRIPT_DIR/../../out/build-macos-release/bin" "$SCRIPT_DIR/../../out/build-macos/bin"; do
        if [[ -f "$dir/$name" ]]; then
            echo "$dir/$name"
            return 0
        fi
    done
    return 1
}

launcher_roots() {
    local roots=("$HOME/Library/Application Support/PrismLauncher" "$HOME/Library/Application Support/MultiMC")
    for root in "${roots[@]}"; do [[ -d "$root/instances" ]] && echo "$root"; done
    return 0
}

choose_instance() {
    local instances=()
    while IFS= read -r root; do
        for cfg in "$root"/instances/*/instance.cfg; do
            [[ -f "$cfg" ]] && instances+=("$(dirname "$cfg")")
        done
    done < <(launcher_roots)
    [[ ${#instances[@]} -gt 0 ]] || die "no Prism Launcher or MultiMC instances found; pass the instance folder explicitly"
    echo "Instances:" >&2
    local i=1
    for dir in "${instances[@]}"; do
        echo "  $i) $(basename "$dir")" >&2
        i=$((i + 1))
    done
    local choice
    read -r -p "Install into which instance? [1-${#instances[@]}] " choice
    [[ "$choice" =~ ^[0-9]+$ ]] && ((choice >= 1 && choice <= ${#instances[@]})) || die "invalid choice"
    echo "${instances[$((choice - 1))]}"
}

# Prints the value of `key` from an INI-style launcher config (first match, any section).
ini_get() {
    local file="$1" key="$2"
    [[ -f "$file" ]] || return 0
    awk -v key="$key" 'index($0, key "=") == 1 { print substr($0, length(key) + 2); exit }' "$file"
}

# Sets `key=value` in an instance.cfg, inside [General] when the file has sections.
ini_set() {
    local file="$1" key="$2" value="$3" tmp
    tmp="$(mktemp)"
    KEY="$key" VALUE="$value" awk '
        BEGIN { key = ENVIRON["KEY"]; value = ENVIRON["VALUE"]; done = 0; sawSection = 0 }
        /^\[/ {
            if (inGeneral && !done) { print key "=" value; done = 1 }
            sawSection = 1
            inGeneral = ($0 == "[General]")
            print
            next
        }
        index($0, key "=") == 1 && (!sawSection || inGeneral) {
            if (!done) { print key "=" value; done = 1 }
            next
        }
        { print }
        END { if (!done) print key "=" value }
    ' "$file" >"$tmp"
    cat "$tmp" >"$file"
    rm -f "$tmp"
}

# Removes any -agentpath pointing at a libToolscreen.dylib from a JVM argument string.
strip_agent() {
    local args="$1" out=() word
    read -r -a words <<<"$args"
    for word in "${words[@]+"${words[@]}"}"; do
        [[ "$word" == -agentpath:*libToolscreen.dylib* ]] && continue
        out+=("$word")
    done
    echo "${out[*]+"${out[*]}"}"
}

warn_if_launcher_running() {
    if pgrep -xq prismlauncher || pgrep -fq "PrismLauncher.app" || pgrep -xq MultiMC; then
        echo "note: your launcher is running. Close the instance's Edit window (or restart the launcher) so it does not" >&2
        echo "      overwrite instance.cfg; the change applies the next time the instance starts." >&2
    fi
}

uninstall=0
if [[ "${1:-}" == "--uninstall" ]]; then
    uninstall=1
    shift
fi
INSTANCE_DIR="${1:-}"
[[ -n "$INSTANCE_DIR" ]] || INSTANCE_DIR="$(choose_instance)"
INSTANCE_CFG="$INSTANCE_DIR/instance.cfg"
[[ -f "$INSTANCE_CFG" ]] || die "$INSTANCE_CFG not found (expected a Prism Launcher / MultiMC instance folder)"

current_args="$(ini_get "$INSTANCE_CFG" JvmArgs)"
override="$(ini_get "$INSTANCE_CFG" OverrideJavaArgs)"

if ((uninstall)); then
    cp "$INSTANCE_CFG" "$INSTANCE_CFG.toolscreen-backup"
    ini_set "$INSTANCE_CFG" JvmArgs "$(strip_agent "$current_args")"
    echo "Removed Toolscreen from $(basename "$INSTANCE_DIR") (backup: instance.cfg.toolscreen-backup)."
    echo "The files in $INSTALL_DIR were left in place for other instances; delete the folder to remove them."
    warn_if_launcher_running
    exit 0
fi

[[ "$INSTALL_DIR" != *" "* ]] || die "the install folder '$INSTALL_DIR' contains a space, which launchers cannot pass in an -agentpath; set TOOLSCREEN_INSTALL_DIR to a path without spaces"

TOOLSCREEN_DYLIB="$(find_payload libToolscreen.dylib)" || die "libToolscreen.dylib not found next to this script (run build-macos.sh first)"
LOGGER_DYLIB="$(find_payload liblogger.dylib || true)"

mkdir -p "$INSTALL_DIR"
cp -f "$TOOLSCREEN_DYLIB" "$INSTALL_DIR/libToolscreen.dylib"
[[ -n "$LOGGER_DYLIB" ]] && cp -f "$LOGGER_DYLIB" "$INSTALL_DIR/liblogger.dylib"
# Files downloaded through a browser are quarantined, and macOS refuses to load quarantined libraries.
xattr -dr com.apple.quarantine "$INSTALL_DIR" 2>/dev/null || true

if [[ "$override" != "true" ]]; then
    # The instance used the launcher-wide JVM arguments; keep them when switching to per-instance ones.
    while IFS= read -r root; do
        for cfg in "$root/prismlauncher.cfg" "$root/multimc.cfg"; do
            [[ -f "$cfg" ]] && current_args="$(ini_get "$cfg" JvmArgs)"
        done
    done < <(launcher_roots)
fi

new_args="$(strip_agent "$current_args")"
new_args="${new_args:+$new_args }-agentpath:$AGENT_PATH"

cp "$INSTANCE_CFG" "$INSTANCE_CFG.toolscreen-backup"
ini_set "$INSTANCE_CFG" OverrideJavaArgs true
ini_set "$INSTANCE_CFG" JvmArgs "$new_args"

echo "Installed Toolscreen into $(basename "$INSTANCE_DIR")."
echo "  libraries: $INSTALL_DIR"
echo "  JVM arguments: $new_args"
echo "Start the instance and press Ctrl+I in game to open the settings. Config and logs: ~/.config/toolscreen"
warn_if_launcher_running
