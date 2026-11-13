// Regression test for <firmir/gui/detail/drmuapi.hpp>.
//
// Pre-4.0 DRM uapi headers (kernels before ~4.0, still shipped by older
// targets) declare struct drm_buf_map with a member literally named "virtual":
//
//     struct drm_buf_map { int count; void *virtual; struct drm_buf_pub *list; };
//
// Including such a header from C++ fails with
//
//     error: expected unqualified-id before 'virtual'
//
// which is what drmuapi.hpp has to hide. The fixture tree in
// tests/fixtures/old_drm_header/ supplies exactly that header and is put first
// on the include path for this target only, so the wrapper is compiled against
// the broken header here instead of the system one. The check is: it compiles,
// the struct keeps its C layout, and the offending member is still reachable
// under the internal name used while the keyword is masked.

#include <firmir/gui/detail/drmuapi.hpp>

#include <cstddef>
#include <cstdio>

int main() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };

    // The member exists under the masked name and the layout matches the C ABI
    // of the header (int count; void* virt; drm_buf_pub* list;). The pointer
    // members are pointer-aligned, so the offsets are derived the same way the
    // compiler lays the C struct out.
    constexpr std::size_t virtOffset =
        (sizeof(int) + alignof(void*) - 1) / alignof(void*) * alignof(void*);
    drm_buf_map map{};
    map.count = 1;
    map.firmir_drm_uapi_virtual_member = nullptr;
    map.list = nullptr;
    check(map.count == 1, "drm_buf_map::count is usable");
    check(offsetof(drm_buf_map, count) == 0, "drm_buf_map::count at offset 0");
    check(offsetof(drm_buf_map, firmir_drm_uapi_virtual_member) == virtOffset,
          "drm_buf_map::virt follows count");
    check(offsetof(drm_buf_map, list) == virtOffset + sizeof(void*),
          "drm_buf_map::list follows virt");

    // drm_mode structures come through the same wrapper.
    drm_mode_modeinfo mode{};
    mode.hdisplay = 1920;
    mode.vdisplay = 1080;
    check(mode.hdisplay == 1920 && mode.vdisplay == 1080,
          "drm_mode_modeinfo is usable");

    if (failures == 0)
        std::printf("drm uapi wrapper: ok\n");
    return failures == 0 ? 0 : 1;
}
