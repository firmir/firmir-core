// eglfs backend tests: integration selector normalization and the plugin ABI
// (the backend exports the display factory, the kms sub-plugin exports the
// eglfs integration factory with a matching ABI version).

#include "test_framework.hpp"

#include <firmir/detail/dynload.hpp>
#include <firmir/gui/eglfsintegration.hpp>

#include <string>

using namespace Firmir::Gui;

TEST_CASE(gui_eglfs_integration_name) {
    CHECK(eglFsIntegrationName("kms") == "kms");
    CHECK(eglFsIntegrationName("eglfs_kms") == "kms");
    CHECK(eglFsIntegrationName("EGLFS_KMS") == "kms");
    CHECK(eglFsIntegrationName(" eglfs-kms ") == "kms");
    CHECK(eglFsIntegrationName("eglfs_kms_egldevice") == "kms-egldevice");
    CHECK(eglFsIntegrationName("eglfs_emu") == "emu");
    // Without the separator the prefix is part of the name.
    CHECK(eglFsIntegrationName("eglfs") == "eglfs");
    CHECK(eglFsIntegrationName("").empty());
}

TEST_CASE(gui_eglfs_plugin_abi) {
#ifdef FIRMIR_EGLFS_TEST_PLUGIN_DIR
    const std::string dir = FIRMIR_EGLFS_TEST_PLUGIN_DIR;
    const std::string suffix = Firmir::detail::sharedLibrarySuffix();

    void* backend = Firmir::detail::libraryOpen(
        dir + "/firmir-gui-eglfs" + suffix);
    CHECK(backend != nullptr);
    CHECK(Firmir::detail::librarySymbol(backend, FIRMIR_GUI_CREATE_DISPLAY) !=
          nullptr);
    Firmir::detail::libraryClose(backend);

#ifdef FIRMIR_EGLFS_TEST_KMS_PLUGIN
    void* kms = Firmir::detail::libraryOpen(
        dir + "/eglfs/firmir-gui-eglfs-kms" + suffix);
    CHECK(kms != nullptr);
    auto abi = reinterpret_cast<unsigned (*)()>(
        Firmir::detail::librarySymbol(kms,
                                      FIRMIR_EGLFS_INTEGRATION_ABI_SYMBOL));
    CHECK(abi != nullptr);
    CHECK(abi() == FIRMIR_EGLFS_INTEGRATION_ABI);
    CHECK(Firmir::detail::librarySymbol(
              kms, FIRMIR_EGLFS_CREATE_INTEGRATION) != nullptr);
    Firmir::detail::libraryClose(kms);
#endif
#endif
}
