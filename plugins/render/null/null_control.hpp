#pragma once

// Test/control API of the null render backend plugin. Link against the
// firmir-render-null target and set FIRMIR_RENDER_PLUGIN_PATH to its
// directory so that Firmir::Render::Render dlopen()s the same library.

#include <cstdint>

namespace Firmir {
namespace Render {
namespace Null {

struct Stats {
    int devices = 0;
    int buffers = 0;
    int textures = 0;
    int shaders = 0;
    int pipelines = 0;
    int renderTargets = 0;
    int swapchains = 0;
    int commandBuffers = 0;
    int submissions = 0;
    int renderPasses = 0;
    int draws = 0;
    int indexedDraws = 0;
    int presents = 0;
    std::uint64_t vertices = 0;
    std::uint64_t indices = 0;
    std::uint64_t bytesWritten = 0;
};

Stats& stats();
void reset();

} // namespace Null
} // namespace Render
} // namespace Firmir
