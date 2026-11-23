#!/usr/bin/env bash
# Builds the CrossCrease NHL 17 runtime natively for Linux (x86-64) and, by default, packages it.
#
#   tools/linux/build_release.sh [--container auto|docker|podman|none] [--no-package]
#                                [--online-directory <Player folder>] [--server-address <host>] [--jobs <n>]
#
# --container auto (default) builds inside the Ubuntu 24.04 image from tools/linux/Containerfile with
# docker or podman, so every build uses the same compiler and libraries. --container none builds with the
# host's tools (Ubuntu 24.04 with the packages listed in the Containerfile).
#
# Output: artifacts/linux/shadps4 (the emulator) and, unless --no-package, the package from
# tools/linux/package_linux.py in artifacts/release-linux/<version>/. For a release, tools\package_release.ps1
# -LinuxRuntime artifacts/linux/shadps4 builds the same package and lists it in the release feed.
set -Eeuo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
container=auto
package=1
online=""
server=""
jobs="$(nproc)"
while (($#)); do
    case "$1" in
        --container) container="$2"; shift 2 ;;
        --no-package) package=0; shift ;;
        --online-directory) online="$2"; shift 2 ;;
        --server-address) server="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -f "$root/externals/sdl3/CMakeLists.txt" ]]; then
    echo "The submodules are missing. Run: git submodule update --init --recursive" >&2
    exit 1
fi

if [[ "$container" == auto ]]; then
    if command -v podman >/dev/null 2>&1; then container=podman
    elif command -v docker >/dev/null 2>&1; then container=docker
    else
        echo "Neither podman nor docker was found; pass --container none to build with this system's tools." >&2
        exit 1
    fi
fi

# The container sees the source as /src, so it gets a build folder of its own.
build="${CROSSCREASE_BUILD_DIR:-$root/build-linux-release}"
binary="$root/artifacts/linux/shadps4"

if [[ "$container" != none ]]; then
    image=crosscrease-nhl17-linux-build
    "$container" build -t "$image" -f "$root/tools/linux/Containerfile" "$root/tools/linux"
    user_args=(--user "$(id -u):$(id -g)")
    [[ "$container" == podman ]] && user_args=(--userns=keep-id)
    "$container" run --rm "${user_args[@]}" -v "$root:/src" -w /src \
        -e CROSSCREASE_BUILD_DIR=/src/build-linux-container \
        -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
        "$image" tools/linux/build_release.sh --container none --no-package --jobs "$jobs"
else
    cc="$(command -v clang-19 || command -v clang)"
    cxx="$(command -v clang++-19 || command -v clang++)"
    scan="$(command -v clang-scan-deps-19 || command -v clang-scan-deps || true)"
    [[ -n "$cc" && -n "$cxx" ]] || { echo "Clang is required." >&2; exit 1; }
    cmake -S "$root" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$cc" \
        -DCMAKE_CXX_COMPILER="$cxx" \
        ${scan:+-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$scan"} \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
        -DENABLE_DISCORD_RPC=OFF \
        -DENABLE_UPDATER=OFF \
        -DENABLE_TESTS=OFF \
        -DENABLE_SYSTEM_LIBRARIES=OFF
    "$cxx" --version | head -n 1
    cmake --build "$build" --parallel "$jobs"
    mkdir -p "$(dirname "$binary")"
    strip="$(command -v llvm-strip-19 || command -v llvm-strip || command -v strip)"
    "$strip" --strip-debug -o "$binary" "$build/shadps4"
    if ldd "$binary" | grep -q 'not found'; then
        ldd "$binary" >&2
        echo "The emulator needs libraries that are missing on this system." >&2
        exit 1
    fi
    echo "Built $binary"
    echo "Linked libraries:"
    ldd "$binary" | sed 's/^/  /'
fi

if ((package)); then
    args=(--runtime "$binary")
    [[ -n "$online" ]] && args+=(--online-directory "$online")
    [[ -n "$server" ]] && args+=(--server-address "$server")
    python3 "$root/tools/linux/package_linux.py" "${args[@]}"
fi
