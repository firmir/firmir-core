#pragma once

// Internal ABI between libfirmir-multimedia and its decode backend plugins
// (plugins/multimedia/<name>/ builds firmir-mm-<name>.so). Not a public
// API: the struct layout must match the library build exactly.

#include <firmir/multimedia/videosourcebackend.hpp>

namespace Firmir {
namespace Multimedia {

#define FIRMIR_MM_PLUGIN_ABI 2
#define FIRMIR_MM_PLUGIN_ENTRY "firmir_mm_plugin"

struct MmSourcePlugin {
    int abiVersion = FIRMIR_MM_PLUGIN_ABI;
    /// Backend name reported by availableVideoSourceBackends() and accepted
    /// by createVideoSource() ("gstreamer", "ffmpeg", ...).
    const char* name = nullptr;
    /// Cheap availability probe (dev runtime present, e.g. gst_init works);
    /// called before create().
    bool (*available)() = nullptr;
    /// Creates the decode backend for @p host (the VideoSource takes
    /// ownership; the backend reports through the host's emit*() functions).
    VideoSourceBackend* (*create)(VideoSource* host) = nullptr;
};

/// Every plugin exports `extern "C" const MmSourcePlugin* firmir_mm_plugin()`.
using MmPluginEntry = const MmSourcePlugin* (*)();

} // namespace Multimedia
} // namespace Firmir
