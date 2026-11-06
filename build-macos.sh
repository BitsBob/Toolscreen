#!/bin/bash
# Builds the macOS release of Toolscreen (universal arm64 + x86_64) and stages a distributable folder.
#
#   ./build-macos.sh            release build + out/macos-dist/Toolscreen-<version>-macos(.zip)
#   ./build-macos.sh --debug    debug build of the host architecture only (out/build-macos)
#   ./build-macos.sh --test     release build, then the unit tests
#
# Requirements: Xcode or the Command Line Tools, CMake 3.21+, Ninja, and a JDK (for liblogger's JNI headers).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

mode="release"
run_tests=0
for arg in "$@"; do
    case "$arg" in
    --debug) mode="debug" ;;
    --test) run_tests=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

if [[ -z "${JAVA_HOME:-}" || ! -f "${JAVA_HOME}/include/jni.h" ]]; then
    JAVA_HOME="$(/usr/libexec/java_home 2>/dev/null || true)"
    for candidate in /opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home /usr/local/opt/openjdk/libexec/openjdk.jdk/Contents/Home; do
        [[ -f "${JAVA_HOME:-/nonexistent}/include/jni.h" ]] && break
        [[ -f "$candidate/include/jni.h" ]] && JAVA_HOME="$candidate"
    done
fi
export JAVA_HOME

if [[ "$mode" == "debug" ]]; then
    BUILD_DIR="out/build-macos"
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
    BUILD_DIR="out/build-macos-release"
    cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
fi
cmake --build "$BUILD_DIR"

if ((run_tests)); then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

[[ "$mode" == "release" ]] || exit 0

VERSION="$(sed -nE 's/^set\(TOOLSCREEN_VERSION_MAJOR ([0-9]+)\)$/\1/p' ToolscreenVersion.cmake).$(sed -nE 's/^set\(TOOLSCREEN_VERSION_MINOR ([0-9]+)\)$/\1/p' ToolscreenVersion.cmake).$(sed -nE 's/^set\(TOOLSCREEN_VERSION_PATCH ([0-9]+)\)$/\1/p' ToolscreenVersion.cmake)"
DIST_NAME="Toolscreen-${VERSION}-macos"
DIST="out/macos-dist/$DIST_NAME"
rm -rf "$DIST" "out/macos-dist/$DIST_NAME.zip"
mkdir -p "$DIST"
cp "$BUILD_DIR/bin/libToolscreen.dylib" "$DIST/"
[[ -f "$BUILD_DIR/bin/liblogger.dylib" ]] && cp "$BUILD_DIR/bin/liblogger.dylib" "$DIST/"
cp scripts/macos/install-macos.sh "$DIST/"
cp docs/macos.md "$DIST/README-macos.md"
cp LICENSE "$DIST/"
(cd out/macos-dist && ditto -c -k --keepParent "$DIST_NAME" "$DIST_NAME.zip")
echo "Staged $DIST and out/macos-dist/$DIST_NAME.zip"
