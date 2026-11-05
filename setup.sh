#!/usr/bin/env bash
# Configures, builds and tests Firmir. By default only the plain configuration
# is built and ctest is executed. Sanitizers and coverage are opt-in.

set -u
cd "$(dirname "$0")"

ORIG_ARGS=("$@")
JOBS="$(nproc 2>/dev/null || echo 4)"
KEEP_GOING=0
ALL_TESTS=0
MODE="plain"
MODE_FLAGS=0
BUILD_EXAMPLES=1
DOC_BUILD=1
DOC_GENERATE=1
FIRML_TOOL=1
LIB_SELECTION=0
PLUGIN_SELECTION=0
RENDER_PLUGIN_SELECTION=0
LIB_CORE=0
LIB_JSON=0
LIB_XML=0
LIB_DBUS=0
LIB_NETWORKING=0
LIB_GUI=0
LIB_GUI_CONTROLS=0
LIB_RENDER=0
LIB_WAYLAND=0
LIB_WAYLAND_COMPOSITOR=0
PLUGIN_DRM=0
PLUGIN_EVDEV=0
PLUGIN_HEADLESS=0
PLUGIN_RENDER_NULL=0
PLUGIN_RENDER_SOFT=0
PLUGIN_RENDER_OPENGL=0
PLUGIN_RENDER_VULKAN=0

usage() {
    cat <<'USAGE'
Usage: ./setup.sh [options]

Configurations:
  --asan                 build and test with AddressSanitizer+UBSan (build-asan)
  --tsan                 build and test with ThreadSanitizer (build-tsan)
  --coverage             build and run gcov coverage (build-cov; needs lcov/genhtml)
  --all-tests            run sequentially: plain, --asan, --tsan, --coverage

Component selection:
  --all-libs             build all libraries (default when no --lib-* is given)
  --lib-core             build firmir-core
  --lib-dbus             build firmir-dbus (also enables core+xml)
  --lib-gui              build firmir-gui and firmir-gui-controls (also enables core)
  --lib-wayland          build firmir-wayland-client/server
  --lib-wayland-compositor build the compositor module (also enables gui+wayland)
  --lib-json             build firmir-json (also enables core)
  --lib-networking       build firmir-networking (also enables core)
  --lib-render           build firmir-render (also enables core)
  --lib-xml              build firmir-xml (also enables core)
  --plugins-all          build all GUI plugins (default when no --plugin-* is given)
  --plugins-gui-drm      build the DRM GUI plugin
  --plugin-gui-evdev     build the evdev GUI plugin
  --plugin-gui-headless  build the headless GUI plugin
  --plugins-render-all   build all render plugins (default when firmir-render is on)
  --plugin-render-null   build the null render plugin
  --plugin-render-soft   build the software render plugin (used by firmir-gui)
  --plugin-render-opengl build the OpenGL ES 2 render plugin (EGL/GLES2)
  --plugin-render-vulkan build the Vulkan render plugin (offscreen/readback)
  --no-examples          do not build examples

Documentation:
  --no-doc-generate      do not generate API documentation
  --no-doc-build         do not build firmir-doc (also disables doc generation)
  --no-firmlc            do not build the firmlc transpiler tool

Other:
  -j, --jobs N           parallel build jobs (default: nproc)
  --keep-going           do not stop at the first failing step
  -h, --help             show this help
USAGE
}

bool() {
    if [ "$1" -eq 1 ]; then printf 'ON'; else printf 'OFF'; fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        -j|--jobs)
            [ $# -ge 2 ] || { echo "error: $1 needs a value" >&2; exit 2; }
            JOBS="$2"; shift 2 ;;
        --keep-going) KEEP_GOING=1; shift ;;
        --coverage) MODE="coverage"; MODE_FLAGS=$((MODE_FLAGS + 1)); shift ;;
        --asan) MODE="asan"; MODE_FLAGS=$((MODE_FLAGS + 1)); shift ;;
        --tsan) MODE="tsan"; MODE_FLAGS=$((MODE_FLAGS + 1)); shift ;;
        --all-tests) ALL_TESTS=1; shift ;;
        --all-libs)
            LIB_SELECTION=1
            LIB_CORE=1; LIB_JSON=1; LIB_XML=1; LIB_DBUS=1; LIB_NETWORKING=1; LIB_GUI=1; LIB_RENDER=1
            LIB_GUI_CONTROLS=1; LIB_WAYLAND=1; LIB_WAYLAND_COMPOSITOR=1
            shift ;;
        --lib-core) LIB_SELECTION=1; LIB_CORE=1; shift ;;
        --lib-dbus) LIB_SELECTION=1; LIB_DBUS=1; shift ;;
        --lib-gui) LIB_SELECTION=1; LIB_GUI=1; LIB_GUI_CONTROLS=1; shift ;;
        --lib-wayland) LIB_SELECTION=1; LIB_WAYLAND=1; shift ;;
        --lib-wayland-compositor)
            LIB_SELECTION=1; LIB_WAYLAND=1; LIB_WAYLAND_COMPOSITOR=1; LIB_GUI=1; LIB_GUI_CONTROLS=1; shift ;;
        --lib-json) LIB_SELECTION=1; LIB_JSON=1; shift ;;
        --lib-networking) LIB_SELECTION=1; LIB_NETWORKING=1; shift ;;
        --lib-render) LIB_SELECTION=1; LIB_RENDER=1; shift ;;
        --lib-xml) LIB_SELECTION=1; LIB_XML=1; shift ;;
        --plugins-all)
            PLUGIN_SELECTION=1
            PLUGIN_DRM=1; PLUGIN_EVDEV=1; PLUGIN_HEADLESS=1
            shift ;;
        --plugins-gui-drm|--plugin-gui-drm) PLUGIN_SELECTION=1; PLUGIN_DRM=1; shift ;;
        --plugins-gui-evdev|--plugin-gui-evdev) PLUGIN_SELECTION=1; PLUGIN_EVDEV=1; shift ;;
        --plugins-gui-headless|--plugin-gui-headless) PLUGIN_SELECTION=1; PLUGIN_HEADLESS=1; shift ;;
        --plugins-render-all)
            RENDER_PLUGIN_SELECTION=1
            PLUGIN_RENDER_NULL=1; PLUGIN_RENDER_SOFT=1; PLUGIN_RENDER_OPENGL=1; PLUGIN_RENDER_VULKAN=1
            shift ;;
        --plugins-render-null|--plugin-render-null) RENDER_PLUGIN_SELECTION=1; PLUGIN_RENDER_NULL=1; shift ;;
        --plugins-render-soft|--plugin-render-soft) RENDER_PLUGIN_SELECTION=1; PLUGIN_RENDER_SOFT=1; shift ;;
        --plugins-render-opengl|--plugin-render-opengl) RENDER_PLUGIN_SELECTION=1; PLUGIN_RENDER_OPENGL=1; shift ;;
        --plugins-render-vulkan|--plugin-render-vulkan) RENDER_PLUGIN_SELECTION=1; PLUGIN_RENDER_VULKAN=1; shift ;;
        --no-examples) BUILD_EXAMPLES=0; shift ;;
        --no-doc-generate) DOC_GENERATE=0; shift ;;
        --no-doc-build|--no-doc) DOC_BUILD=0; DOC_GENERATE=0; shift ;;
        --no-firmlc) FIRML_TOOL=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$ALL_TESTS" -eq 1 ] && [ "$MODE_FLAGS" -gt 0 ]; then
    echo "error: --all-tests cannot be combined with --asan/--tsan/--coverage" >&2
    exit 2
fi
if [ "$MODE_FLAGS" -gt 1 ]; then
    echo "error: choose only one of --asan, --tsan or --coverage" >&2
    exit 2
fi

if [ "$LIB_SELECTION" -eq 0 ]; then
    LIB_CORE=1; LIB_JSON=1; LIB_XML=1; LIB_DBUS=1; LIB_NETWORKING=1; LIB_GUI=1; LIB_RENDER=1
    LIB_GUI_CONTROLS=1; LIB_WAYLAND=1; LIB_WAYLAND_COMPOSITOR=1
fi
if [ "$LIB_JSON" -eq 1 ] || [ "$LIB_XML" -eq 1 ] || [ "$LIB_DBUS" -eq 1 ] || \
   [ "$LIB_NETWORKING" -eq 1 ] || [ "$LIB_GUI" -eq 1 ] || [ "$LIB_RENDER" -eq 1 ]; then
    LIB_CORE=1
fi
if [ "$LIB_DBUS" -eq 1 ]; then
    LIB_XML=1
fi
if [ "$LIB_GUI" -eq 1 ]; then
    # firmir-gui renders through firmir-render.
    LIB_RENDER=1
fi
if [ "$LIB_WAYLAND_COMPOSITOR" -eq 1 ]; then
    LIB_RENDER=1
fi
if [ "$PLUGIN_SELECTION" -eq 0 ]; then
    if [ "$LIB_GUI" -eq 1 ]; then
        PLUGIN_DRM=1; PLUGIN_EVDEV=1; PLUGIN_HEADLESS=1
    else
        PLUGIN_DRM=0; PLUGIN_EVDEV=0; PLUGIN_HEADLESS=0
    fi
fi
if [ "$RENDER_PLUGIN_SELECTION" -eq 0 ]; then
    if [ "$LIB_RENDER" -eq 1 ]; then
        PLUGIN_RENDER_NULL=1; PLUGIN_RENDER_SOFT=1; PLUGIN_RENDER_OPENGL=1; PLUGIN_RENDER_VULKAN=1
    else
        PLUGIN_RENDER_NULL=0; PLUGIN_RENDER_SOFT=0; PLUGIN_RENDER_OPENGL=0; PLUGIN_RENDER_VULKAN=0
    fi
fi
if [ "$PLUGIN_DRM" -eq 1 ] || [ "$PLUGIN_EVDEV" -eq 1 ] || [ "$PLUGIN_HEADLESS" -eq 1 ]; then
    LIB_GUI=1
    LIB_CORE=1
fi
if [ "$PLUGIN_RENDER_NULL" -eq 1 ] || [ "$PLUGIN_RENDER_SOFT" -eq 1 ] || [ "$PLUGIN_RENDER_OPENGL" -eq 1 ]; then
    LIB_RENDER=1
    LIB_CORE=1
fi
if [ "$DOC_BUILD" -eq 0 ]; then
    DOC_GENERATE=0
fi

if [ "$ALL_TESTS" -eq 1 ] || [ "$MODE" = "coverage" ]; then
    for tool in lcov genhtml; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "error: $tool not found (install lcov, e.g. sudo apt install lcov)" >&2
            exit 2
        fi
    done
fi

if [ "$ALL_TESTS" -eq 1 ]; then
    FORWARD_ARGS=()
    for arg in "${ORIG_ARGS[@]}"; do
        [ "$arg" = "--all-tests" ] && continue
        FORWARD_ARGS+=("$arg")
    done
    OVERALL=0
    for cfg in plain asan tsan coverage; do
        EXTRA_ARGS=()
        case "$cfg" in
            asan) EXTRA_ARGS=(--asan) ;;
            tsan) EXTRA_ARGS=(--tsan) ;;
            coverage) EXTRA_ARGS=(--coverage) ;;
        esac
        echo "==> setup ($cfg)"
        if "$0" "${FORWARD_ARGS[@]}" "${EXTRA_ARGS[@]}"; then
            echo "--- setup ($cfg): PASS"
        else
            echo "--- setup ($cfg): FAIL"
            OVERALL=1
            [ "$KEEP_GOING" -eq 0 ] && exit 1
        fi
    done
    exit "$OVERALL"
fi

declare -a STEP_NAMES=()
declare -a STEP_RESULTS=()
OVERALL=0

step() { # step <name> <command...>
    local name="$1"; shift
    echo "==> $name"
    if "$@"; then
        STEP_NAMES+=("$name"); STEP_RESULTS+=("PASS")
        echo "--- $name: PASS"
    else
        STEP_NAMES+=("$name"); STEP_RESULTS+=("FAIL")
        OVERALL=1
        echo "--- $name: FAIL"
        [ "$KEEP_GOING" -eq 0 ] && { summary; exit 1; }
    fi
}

generate_docs() { # generate_docs <build-dir>
    local dir="$1"
    local tool="./$dir/tools/doc/firmir-doc"
    local libs=()
    local lib
    [ -x "$tool" ] || { echo "error: firmir-doc was not built" >&2; return 1; }
    [ "$LIB_CORE" -eq 1 ] && libs+=(core)
    [ "$LIB_JSON" -eq 1 ] && libs+=(json)
    [ "$LIB_XML" -eq 1 ] && libs+=(xml)
    [ "$LIB_DBUS" -eq 1 ] && libs+=(dbus)
    [ "$LIB_NETWORKING" -eq 1 ] && libs+=(networking)
    [ "$LIB_GUI" -eq 1 ] && libs+=(gui)
    [ "$LIB_GUI_CONTROLS" -eq 1 ] && libs+=(gui-controls)
    [ "$LIB_RENDER" -eq 1 ] && libs+=(render)
    [ "$LIB_WAYLAND" -eq 1 ] && libs+=(wayland)
    for lib in "${libs[@]}"; do
        "$tool" "libs/$lib" "$dir/doc/$lib" || return 1
    done
}

coverage_report() { # coverage_report <build-dir>
    local dir="$1"
    # Drop stale .gcda from previous runs: their stamps would mismatch the
    # fresh .gcno and abort the capture.
    find "$dir" -name '*.gcda' -delete
    ctest --test-dir "$dir" --output-on-failure || return 1
    # --ignore-errors works around lcov 2.0 + GCC 13 quirks.
    lcov --capture --directory "$dir" \
         --output-file "$dir/coverage.info" \
         --ignore-errors mismatch,negative,gcov || return 1
    lcov --remove "$dir/coverage.info" '/usr/*' '*/tests/*' \
         --output-file "$dir/coverage.info" || return 1
    genhtml "$dir/coverage.info" \
            --output-directory "$dir/coverage-html" || return 1
    lcov --summary "$dir/coverage.info"
}

summary() {
    echo
    echo "=================== SUMMARY ==================="
    local i
    for i in "${!STEP_NAMES[@]}"; do
        printf '  %-28s %s\n' "${STEP_NAMES[$i]}" "${STEP_RESULTS[$i]}"
    done
    if [ "$OVERALL" -eq 0 ]; then
        echo "ALL CHECKS PASSED"
    else
        echo "SOME CHECKS FAILED"
    fi
}

BUILD_DIR="build"
BUILD_TYPE="RelWithDebInfo"
SANITIZER=""
case "$MODE" in
    asan) BUILD_DIR="build-asan"; SANITIZER="address" ;;
    tsan) BUILD_DIR="build-tsan"; SANITIZER="thread" ;;
    coverage) BUILD_DIR="build-cov"; BUILD_TYPE="Debug" ;;
esac

CMAKE_ARGS=(
    -S . -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DFIRMIR_BUILD_TESTS=ON
    -DFIRMIR_BUILD_EXAMPLES="$(bool "$BUILD_EXAMPLES")"
    -DFIRMIR_BUILD_DOC_TOOL="$(bool "$DOC_BUILD")"
    -DFIRMIR_BUILD_TOOL_FIRML="$(bool "$FIRML_TOOL")"
    -DFIRMIR_BUILD_LIB_CORE="$(bool "$LIB_CORE")"
    -DFIRMIR_BUILD_LIB_JSON="$(bool "$LIB_JSON")"
    -DFIRMIR_BUILD_LIB_XML="$(bool "$LIB_XML")"
    -DFIRMIR_BUILD_LIB_DBUS="$(bool "$LIB_DBUS")"
    -DFIRMIR_BUILD_LIB_NETWORKING="$(bool "$LIB_NETWORKING")"
    -DFIRMIR_BUILD_LIB_GUI="$(bool "$LIB_GUI")"
    -DFIRMIR_BUILD_LIB_GUI_CONTROLS="$(bool "$LIB_GUI_CONTROLS")"
    -DFIRMIR_BUILD_LIB_RENDER="$(bool "$LIB_RENDER")"
    -DFIRMIR_BUILD_LIB_WAYLAND="$(bool "$LIB_WAYLAND")"
    -DFIRMIR_BUILD_LIB_WAYLAND_COMPOSITOR="$(bool "$LIB_WAYLAND_COMPOSITOR")"
    -DFIRMIR_BUILD_PLUGIN_GUI_DRM="$(bool "$PLUGIN_DRM")"
    -DFIRMIR_BUILD_PLUGIN_GUI_EVDEV="$(bool "$PLUGIN_EVDEV")"
    -DFIRMIR_BUILD_PLUGIN_GUI_HEADLESS="$(bool "$PLUGIN_HEADLESS")"
    -DFIRMIR_BUILD_PLUGIN_RENDER_NULL="$(bool "$PLUGIN_RENDER_NULL")"
    -DFIRMIR_BUILD_PLUGIN_RENDER_SOFT="$(bool "$PLUGIN_RENDER_SOFT")"
    -DFIRMIR_BUILD_PLUGIN_RENDER_OPENGL="$(bool "$PLUGIN_RENDER_OPENGL")"
    -DFIRMIR_BUILD_PLUGIN_RENDER_VULKAN="$(bool "$PLUGIN_RENDER_VULKAN")"
)
if [ -n "$SANITIZER" ]; then
    CMAKE_ARGS+=(-DFIRMIR_SANITIZER="$SANITIZER")
fi
if [ "$MODE" = "coverage" ]; then
    CMAKE_ARGS+=(
        "-DCMAKE_CXX_FLAGS=--coverage -fprofile-update=atomic"
        "-DCMAKE_EXE_LINKER_FLAGS=--coverage"
        "-DCMAKE_SHARED_LINKER_FLAGS=--coverage"
    )
fi

step "configure ($MODE)" cmake "${CMAKE_ARGS[@]}"
step "build ($MODE)" cmake --build "$BUILD_DIR" -j "$JOBS"
if [ "$MODE" = "coverage" ]; then
    step "coverage report" coverage_report "$BUILD_DIR"
else
    step "ctest ($MODE)" ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
if [ "$MODE" = "plain" ] && [ "$DOC_GENERATE" -eq 1 ] && [ "$DOC_BUILD" -eq 1 ]; then
    step "doc generation" generate_docs "$BUILD_DIR"
fi

summary
exit "$OVERALL"
