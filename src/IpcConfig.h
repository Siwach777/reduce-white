#pragma once

#include <string>
#include <cstdlib>
#include <unistd.h>

inline std::string getIpcSocketPath() {
    const char *xdgRuntime = std::getenv("XDG_RUNTIME_DIR");
    if (xdgRuntime && xdgRuntime[0] != '\0') {
        return std::string(xdgRuntime) + "/reduce-white-ipc.sock";
    }
    // Fallback incorporating user ID to prevent multi-user permission collisions
    return "/tmp/reduce-white-" + std::to_string(getuid()) + "-ipc.sock";
}
