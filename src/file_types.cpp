#include "dfm/file_types.hpp"

#include <sys/stat.h>

#include <cerrno>

namespace dfm {

namespace fs = std::filesystem;

std::string to_string(EntryKind kind) {
    switch (kind) {
        case EntryKind::NotFound: return "not_found";
        case EntryKind::Regular: return "regular";
        case EntryKind::Directory: return "directory";
        case EntryKind::Symlink: return "symlink";
        case EntryKind::Fifo: return "fifo";
        case EntryKind::Socket: return "socket";
        case EntryKind::BlockDevice: return "block_device";
        case EntryKind::CharDevice: return "char_device";
        case EntryKind::Unknown: return "unknown";
        case EntryKind::Inaccessible: return "inaccessible";
    }
    return "unknown";
}

bool is_supported_kind(EntryKind kind) {
    return kind == EntryKind::Regular || kind == EntryKind::Directory || kind == EntryKind::Symlink;
}

EntryInfo inspect(const fs::path& path) {
    EntryInfo info;
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0) {
        info.kind = (errno == ENOENT || errno == ENOTDIR) ? EntryKind::NotFound : EntryKind::Inaccessible;
        return info;
    }

    if (S_ISLNK(st.st_mode)) {
        info.kind = EntryKind::Symlink;
        info.is_symlink = true;
    } else if (S_ISREG(st.st_mode)) {
        info.kind = EntryKind::Regular;
        info.size = static_cast<std::uintmax_t>(st.st_size);
    } else if (S_ISDIR(st.st_mode)) {
        info.kind = EntryKind::Directory;
    } else if (S_ISFIFO(st.st_mode)) {
        info.kind = EntryKind::Fifo;
    } else if (S_ISSOCK(st.st_mode)) {
        info.kind = EntryKind::Socket;
    } else if (S_ISBLK(st.st_mode)) {
        info.kind = EntryKind::BlockDevice;
    } else if (S_ISCHR(st.st_mode)) {
        info.kind = EntryKind::CharDevice;
    } else {
        info.kind = EntryKind::Unknown;
    }

    info.permissions = static_cast<fs::perms>(st.st_mode & 0xFFF);
    return info;
}

bool exists_no_follow(const fs::path& path) { return inspect(path).kind != EntryKind::NotFound; }

bool is_regular_no_follow(const fs::path& path) { return inspect(path).kind == EntryKind::Regular; }

bool is_directory_no_follow(const fs::path& path) { return inspect(path).kind == EntryKind::Directory; }

bool is_symlink_no_follow(const fs::path& path) { return inspect(path).kind == EntryKind::Symlink; }

}  // namespace dfm
