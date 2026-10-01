#pragma once

#include <stdexcept>
#include <string>
#include <cstdlib>
#include <QCryptographicHash>

#ifdef _WIN32
#include <QDir>
#else
#include <cerrno>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

inline std::string getIpcSocketPath() {
    const char *instance = std::getenv("REDUCE_WHITE_INSTANCE");
    const std::string instanceSuffix = instance && *instance
        ? "-" + QCryptographicHash::hash(QByteArray(instance), QCryptographicHash::Sha256).toHex().left(16).toStdString()
        : std::string{};
#ifdef _WIN32
    // Hash the profile path to keep pipe names short and distinct per account.
    const auto user = QCryptographicHash::hash(QDir::homePath().toUtf8(),
                                              QCryptographicHash::Sha256).toHex();
    return "reduce-white-" + user.toStdString() + instanceSuffix;
#else
    const auto isPrivateDirectory = [](const std::string &path) {
        struct stat info {};
        return !path.empty() && path.front() == '/' &&
               lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
               info.st_uid == geteuid() && (info.st_mode & 0077) == 0;
    };
    std::string directory;
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && isPrivateDirectory(runtime)) directory = runtime;
    const std::string suffix = "/reduce-white-ipc" + instanceSuffix + ".sock";
    if (directory.empty() || directory.size() + suffix.size() >= sizeof(sockaddr_un::sun_path)) {
        directory = "/tmp/reduce-white-" + std::to_string(geteuid());
        if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
            throw std::runtime_error("Cannot create the private IPC directory.");
        }
        if (!isPrivateDirectory(directory)) {
            throw std::runtime_error("IPC directory must be owned by this user with mode 0700.");
        }
    }
    return directory + suffix;
#endif
}
