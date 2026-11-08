// Firmir Controls Gallery: the UI is declared in gallery.fml (compiled to
// C++ by firmlc at build time); the demo logic lives in GalleryApp
// (GalleryApp.cpp). This file only creates the window, wires the demo
// objects that need C++ data, and runs the event loop.
//
//   FIRMIR_GALLERY_PAGE=<0..12>   open a page directly (menu otherwise)
//   FIRMIR_GALLERY_AUTO_QUIT=1    close after 1.5 s (smoke tests)
//   FIRMIR_GALLERY_HEADLESS=1     add a virtual screen when none exists

#include "gallery.hpp"

#include "GalleryApp.hpp"

#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#ifdef FIRMIR_DEMO_HEADLESS
#include <headless_control.hpp>
#endif

int main(int argc, char**) {
    (void)argc;
    Firmir::Gui::Platform* platform = Firmir::Gui::Platform::instance();
#ifdef FIRMIR_DEMO_HEADLESS
    if (std::getenv("FIRMIR_GALLERY_HEADLESS") && platform->screens().empty())
        firmir_gui_headless_add_screen("GALLERY", 1920, 1080);
#endif
    if (platform->screens().empty()) {
        std::fprintf(stderr, "controls-gallery: no display screens\n");
        return 1;
    }

    // Creates the window and the whole gallery tree (see gallery.fml).
    GalleryObjects objects = createGallery(platform->screens().front());
    objects.gallery->attach(objects.galleryStack, objects.galleryImage,
                            objects.galleryImageFit, objects.texturedEffect,
                            objects.shaderSource, objects.tabs);

    if (const char* page = std::getenv("FIRMIR_GALLERY_PAGE")) {
        const int index = std::clamp(std::atoi(page), 0, 12);
        objects.gallery->showPage(index);
    }
    if (std::getenv("FIRMIR_GALLERY_AUTO_QUIT"))
        Firmir::Timer::singleShot(std::chrono::milliseconds(1500), objects.root,
                                  [&] { objects.root->close(); });
    objects.root->connect<bool>(
        objects.root, Firmir::Gui::Window::signalVisibilityChanged,
        [](bool visible) {
            if (!visible)
                Firmir::Thread::mainThread()->quit();
        },
        Firmir::ConnectionMode::Direct);

    const int rc = Firmir::Thread::mainThread()->exec();
    delete objects.root;
    return rc;
}
