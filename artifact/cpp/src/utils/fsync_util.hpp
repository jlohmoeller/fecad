#ifndef FECAD_FSYNC_UTIL_HPP
#define FECAD_FSYNC_UTIL_HPP

#include <string>

namespace fecad {

// fsync file and its dentry
// BeeGFS drops buffered writes and new directory entries on a non-synced close,
// so a fresh secret_key.bin can read back as ENOENT or zero bytes
void fsyncPath(const std::string &path);

}  // namespace fecad

#endif
