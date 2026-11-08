# firmir-multimedia

Video playback for firmir applications: a `VideoOutput` item plus a
backend-neutral `VideoSource` with pluggable decode backends.

```
#include <firmir/multimedia/videooutput.hpp>

auto* source = Firmir::Multimedia::createVideoSource("auto", this); // gstreamer | ffmpeg
source->open("/path/to/clip.mp4");   // or a URL
output->setSource(source);
source->play();
```

- `VideoSource` (`include/firmir/multimedia/videosource.hpp`) — a concrete
  facade holding the decode backend: `open/play/pause/stop/seek`,
  `position/duration`, `hasVideo/hasAudio`, signals `frameReady(VideoFrame)`,
  `stateChanged(int)`, `error(String)`. Frames are RGBA8 (or GPU handles) and
  emitted on the decoder thread — consume them with a queued connection
  (`VideoOutput` does).
- `VideoSourceBackend`
  (`include/firmir/multimedia/videosourcebackend.hpp`) — the interface
  implemented by the plugins; it reports through the host's `emitFrame/
  emitState/emitError` helpers. Custom in-process backends (tests, embedders)
  plug in through the `VideoSource(std::unique_ptr<VideoSourceBackend>)`
  constructor.
- `VideoOutput` (`include/firmir/multimedia/videooutput.hpp`) — a GUI item
  painting the latest frame; `fillMode` (also a Firml property):
  `Stretch`, `PreserveAspectFit` (default, letterbox), `PreserveAspectCrop`
  (cover, clipped), `Pad` (1:1, centered).
- `VideoTextureCache` (`include/firmir/multimedia/videotexture.hpp`) — the
  frame -> texture half of the zero-copy pipeline: imports `GpuFrameHandle`
  dma-bufs through the painter's render device (cached per decoder pool
  surface) and draws them with the media-agnostic `Gui::Painter::drawTexture()`.

## Backends

Runtime plugins in `plugins/multimedia/` (built per installed dev libraries,
pkg-config), loaded via `FIRMIR_MULTIMEDIA_PLUGIN_PATH` or the exe-relative
`plugins/multimedia/` directory:

| backend   | packages                                                | video | audio |
|-----------|---------------------------------------------------------|-------|-------|
| gstreamer | libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev    | yes   | yes (autoaudiosink) |
| ffmpeg    | libavformat-dev libavcodec-dev libavutil-dev libswscale-dev | yes | not yet (no audio sink dep) |

New backends implement `VideoSourceBackend` and export the `firmir_mm_plugin` entry
(see `include/firmir/multimedia/mmplugin.hpp`); they live in
`plugins/multimedia/<name>/` and build as `firmir-mm-<name>.so`.

## Zero-copy video pipeline

Decoded frames may carry an opaque `GpuFrameHandle` (dma-buf) instead of CPU
pixels; `VideoOutput` imports them through the active `Firmir::Render` device
(Vulkan external memory / EGLImage) with GPU-side YUV->RGB (BT.601/709/2020,
limited/full) and falls back to a backend readback when importing is
unavailable — no caller changes. Pipeline details, synchronization, the
backend support matrix and observability counters: `../../doc/multimedia/zero-copy.md`.
`FIRMIR_MM_ZERO_COPY=0` forces the CPU path.

## Build

The library lives inside the firmir tree and builds with it
(`FIRMIR_BUILD_LIB_MULTIMEDIA`, default ON):

```
cmake -S . -B build && cmake --build build -j   # from the firmir root
```

Each backend compiles only when its dev packages are found via pkg-config;
the configure log lists the enabled backends. `sudo make install` ships
`libfirmir-multimedia.so`, the headers and `firmir-multimedia.pc`.

## Tests and example

- Unit tests (mock source, queued delivery, letterboxed soft-render pixels)
  run in the main `tests` binary: `multimedia_*`.
- `firmir.multimedia.mediatest` (ctest, built when the ffmpeg backend and
  `FIRMIR_BUILD_TESTS` are enabled) synthesizes a rawvideo AVI with
  libavformat and plays it through the ffmpeg backend for real.
- `examples/multimedia/` builds `firmir-example-multimedia-video-player`:
  `./build/examples/multimedia/firmir-example-multimedia-video-player file.mp4`.
