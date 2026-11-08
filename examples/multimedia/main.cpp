// firmir-multimedia example: plays a media file (argv[1]) through the first
// available VideoSource backend into the VideoOutput declared in player.fml
// (firmlc compiles the UI to C++ at build time).
//
//   firmir-example-multimedia-video-player <file.(mp4|mkv|webm|ogg|mp3|...)>
//
// Without a decode backend (no gstreamer/ffmpeg dev libraries) the window
// still opens and reports the missing backend in the status line.

#include "player.hpp"

#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/multimedia/videosource.hpp>
#include <firmir/string.hpp>
#include <firmir/thread.hpp>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <media file or URI> [gstreamer|ffmpeg]\n",
                     argv[0]);
        return 1;
    }
    auto* platform = Firmir::Gui::Platform::instance();
    if (platform->screens().empty()) {
        std::fprintf(stderr, "no screens — check FIRMIR_GUI_DISPLAY / "
                             "FIRMIR_GUI_PLUGIN_PATH\n");
        return 1;
    }

    // The Firml-generated tree: window + VideoOutput + status Text.
    PlayerObjects objects = createPlayer(platform->screens().front());
    objects.root->connect<bool>(
        objects.root, Firmir::Gui::Window::signalVisibilityChanged,
        [](bool visible) {
            if (!visible)
                Firmir::Thread::mainThread()->quit();
        });

    auto* source = Firmir::Multimedia::createVideoSource(
        Firmir::String(argc > 2 ? argv[2] : "auto"), objects.root);
    objects.video->setSource(source);

    if (!source) {
        objects.status->setText("no decode backend — install gstreamer/ffmpeg "
                                "dev libraries and rebuild firmir-multimedia");
    } else if (!source->open(Firmir::String(argv[1]))) {
        objects.status->setText(Firmir::String(
            (std::string("cannot open: ") + argv[1]).c_str()));
    } else {
        objects.status->setText(Firmir::String(
            (std::string("backend: ") + source->backendName()).c_str()));
        source->play();
    }
    if (source)
        source->connect<Firmir::String>(
            objects.status, Firmir::Multimedia::VideoSource::signalError,
            [&objects](const Firmir::String& message) {
                objects.status->setText(message);
            },
            Firmir::ConnectionMode::Queued);

    const int rc = Firmir::Thread::mainThread()->exec();
    if (source)
        source->close();
    delete objects.root; // owns the whole tree (including the source)
    return rc;
}
