#include <firmir/wayland/server/server.hpp>
#include <wayland-server.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace Firmir {
namespace Wayland {
namespace Server {
namespace {

/// Socket directory used when the environment does not provide one. A bare
/// KMS console session has no XDG_RUNTIME_DIR, and wl_display_add_socket()
/// refuses to create a socket without it — so the compositor keeps its
/// sockets in a stable system location instead.
constexpr const char* kDefaultSocketDir = "/run/display";
constexpr const char* kSocketDirVariable = "FIRMIR_WAYLAND_SOCKET_DIR";
constexpr const char* kSocketVariable = "FIRMIR_WAYLAND_SOCKET";
/// wayland-0 .. wayland-<kMaxSocketIndex - 1> are tried in order.
constexpr int kMaxSocketIndex = 32;

const char* environment(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

bool isAbsolutePath(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

std::string directoryOf(const std::string& path) {
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) return std::string();
    if (slash == 0) return std::string("/");
    return path.substr(0, slash);
}

/// mkdir -p equivalent; true when @p path is a directory afterwards.
bool ensureDirectory(const std::string& path) {
    if (path.empty()) return true;
    std::string partial;
    for (std::string::size_type index = 0; index < path.size(); ++index) {
        partial.push_back(path[index]);
        if (path[index] != '/' && index + 1 < path.size()) continue;
        if (partial == "/") continue;
        if (::mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

/// True when a live compositor already listens on @p path. wl_display_
/// add_socket() unlinks a stale socket file before binding, so a merely
/// existing file is not a reason to skip the name — only a successful
/// connect(2) is.
bool socketInUse(const std::string& path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISSOCK(info.st_mode)) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof address.sun_path) {
        ::close(fd);
        return true;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size());
    const bool alive =
        ::connect(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof address) == 0;
    ::close(fd);
    return alive;
}

std::string joinPath(const std::string& directory, const std::string& name) {
    if (directory.empty() || isAbsolutePath(name)) return name;
    return directory + "/" + name;
}

} // namespace

class Display::Impl {
public:
    wl_display* display = nullptr;
};

Display::Display() : d(std::make_unique<Impl>()) {
    d->display = wl_display_create();
}
bool Display::valid() const { return d->display != nullptr; }
wl_display* Display::handle() const { return d->display; }
Display::~Display() { if (d->display) wl_display_destroy(d->display); }
wl_event_loop* Display::eventLoop() const {
    return d->display ? wl_display_get_event_loop(d->display) : nullptr;
}
std::string Display::addSocketAuto() {
    if (!d->display) return std::string();

    // Socket directory: /run/display by default (FIRMIR_WAYLAND_SOCKET_DIR
    // overrides it).
    std::string directory = kDefaultSocketDir;
    if (const char* custom = environment(kSocketDirVariable)) directory = custom;

    // Explicit socket name or path: FIRMIR_WAYLAND_SOCKET. An absolute value
    // is used verbatim, a plain name lives in the socket directory.
    std::string requested;
    if (const char* custom = environment(kSocketVariable)) requested = custom;

    std::vector<std::string> candidates;
    if (!requested.empty()) {
        candidates.push_back(joinPath(directory, requested));
        if (!isAbsolutePath(requested)) {
            // Unprivileged fallback: the plain name resolved against
            // XDG_RUNTIME_DIR by libwayland itself.
            candidates.push_back(requested);
        }
    } else {
        for (int index = 0; index < kMaxSocketIndex; ++index)
            candidates.push_back(joinPath(directory, "wayland-" + std::to_string(index)));
    }

    for (const std::string& path : candidates) {
        if (!isAbsolutePath(path)) {
            // Let libwayland place the socket in XDG_RUNTIME_DIR.
            if (wl_display_add_socket(d->display, path.c_str()) == 0) return path;
            continue;
        }
        // A live compositor keeps its name; a stale socket file is reused
        // (wl_display_add_socket() unlinks it before binding).
        if (socketInUse(path)) continue;
        if (!ensureDirectory(directoryOf(path))) continue;
        if (wl_display_add_socket(d->display, path.c_str()) == 0) return path;
    }

    // Last resort (e.g. /run/display cannot be created by an unprivileged
    // nested compositor): the classic XDG_RUNTIME_DIR + wayland-N name. An
    // explicit FIRMIR_WAYLAND_SOCKET request is never relocated.
    if (!requested.empty()) return std::string();
    const char* fallback = wl_display_add_socket_auto(d->display);
    return fallback ? std::string(fallback) : std::string();
}
int Display::run() { if (!d->display) return -1; wl_display_run(d->display); return 0; }
void Display::terminate() { if (d->display) wl_display_terminate(d->display); }
void Display::flushClients() { if (d->display) wl_display_flush_clients(d->display); }
int Display::dispatch(int timeoutMs) {
    return d->display ? wl_event_loop_dispatch(eventLoop(), timeoutMs) : -1;
}
void Display::destroyClients() { if (d->display) wl_display_destroy_clients(d->display); }
std::uint32_t Display::nextSerial() { return d->display ? wl_display_next_serial(d->display) : 0; }
wl_global* Display::createGlobal(const wl_interface* interface, int version,
                                 wl_global_bind_func_t bind, void* data) {
    return d->display ? wl_global_create(d->display, interface, version, data, bind) : nullptr;
}
wl_display* Client::display() const { return d->client ? wl_client_get_display(d->client) : nullptr; }
void Client::credentials(pid_t& pid, uid_t& uid, gid_t& gid) const {
    pid = uid = gid = 0;
    if (d->client) wl_client_get_credentials(d->client, &pid, &uid, &gid);
}
void Client::flush() const { if (d->client) wl_client_flush(d->client); }

} // namespace Server
} // namespace Wayland
} // namespace Firmir
