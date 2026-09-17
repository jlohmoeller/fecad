#include "fsync_util.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fecad {

static std::string parentDir(const std::string &path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

void fsyncPath(const std::string &path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("fsyncPath: open " + path + ": " + std::strerror(errno));
    }
    int rc = ::fsync(fd);
    int sErr = errno;
    ::close(fd);
    if (rc != 0) {
        throw std::runtime_error("fsyncPath: fsync " + path + ": " + std::strerror(sErr));
    }
    // dentry durability
    std::string dir = parentDir(path);
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        // non-fatal: file itself is already durable
        return;
    }
    (void)::fsync(dfd);
    ::close(dfd);
}

}  // namespace fecad
