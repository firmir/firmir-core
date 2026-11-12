#pragma once

// Internal cross-platform dynamic-loading helpers shared by the plugin
// loaders (locale backends, GUI backends, render backends, multimedia
// backends). Not public API: no ABI guarantees, subject to change.

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace Firmir {
namespace detail {

/// Shared library file suffix for the running platform.
inline const char* sharedLibrarySuffix() {
#ifdef _WIN32
    return ".dll";
#else
    return ".so";
#endif
}

/// Separator of directory lists in the FIRMIR_*_PLUGIN_PATH variables
/// (a colon clashes with Windows drive letters, hence ';' there).
inline char pluginPathSeparator() {
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
}

/// Absolute directory of the running executable ("" when unknown).
inline std::string executableDir() {
#ifdef _WIN32
    char buf[32768];
    const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
        return {};
    std::string path(buf, n);
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
#else
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    std::string path(buf, static_cast<std::size_t>(n));
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
#endif
}

/// Plugin search directories: the separator-separated @p envVar first, then
/// each @p exeRelative path resolved against the executable's directory
/// (build-tree and installed layouts).
inline std::vector<std::string> pluginSearchDirs(
    const char* envVar, std::initializer_list<const char*> exeRelative) {
    std::vector<std::string> dirs;
    const char* path = std::getenv(envVar);
    if (path && *path) {
        std::string rest = path;
        const char sep = pluginPathSeparator();
        for (;;) {
            const std::size_t at = rest.find(sep);
            dirs.push_back(rest.substr(0, at));
            if (at == std::string::npos)
                break;
            rest.erase(0, at + 1);
        }
    }
    const std::string dir = executableDir();
    if (!dir.empty())
        for (const char* rel : exeRelative)
            dirs.push_back(dir + "/" + rel);
    return dirs;
}

/// Loads the shared library at @p path (nullptr on failure).
inline void* libraryOpen(const std::string& path) {
#ifdef _WIN32
    // Windows loads the same DLL again when reached through a different
    // absolute path (unlike dlopen, which deduplicates by inode). A second
    // copy would duplicate all static state (backend singletons, TLS), so
    // reuse a module already loaded under the same base name — e.g. a plugin
    // the executable also links directly (unit tests link plugin targets to
    // reach their test-control entry points). GetModuleHandleEx increments
    // the reference count, keeping libraryClose() symmetric with LoadLibrary.
    const std::size_t slash = path.find_last_of("\\/");
    const std::string base =
        slash == std::string::npos ? path : path.substr(slash + 1);
    HMODULE loaded = nullptr;
    if (::GetModuleHandleExA(0, base.c_str(), &loaded) && loaded)
        return reinterpret_cast<void*>(loaded);
    return reinterpret_cast<void*>(::LoadLibraryA(path.c_str()));
#else
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

/// Resolves @p symbol in the library @p handle (nullptr when missing).
inline void* librarySymbol(void* handle, const char* symbol) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        ::GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol));
#else
    return ::dlsym(handle, symbol);
#endif
}

/// Unloads the library.
inline void libraryClose(void* handle) {
    if (!handle)
        return;
#ifdef _WIN32
    ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

} // namespace detail
} // namespace Firmir
