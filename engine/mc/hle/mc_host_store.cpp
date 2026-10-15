// McHostStore implementation; the rules are described in the header.
#include "engine/mc/hle/mc_host_store.h"

#include <algorithm>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

McHostStore::McHostStore(std::string mc_dir) : root_(std::move(mc_dir) + "/mc0") {}

// Guest path -> host path under the card root: a leading '/' is card-absolute,
// anything else is relative to the chdir prefix; ".." and empty segments drop.
std::string McHostStore::HostPath(const std::string& guest) const {
    std::string rel;
    if (!guest.empty() && guest[0] == '/') rel = guest.substr(1);
    else if (!cwd_.empty()) rel = cwd_ + "/" + guest;
    else rel = guest;
    std::string out;
    size_t i = 0;
    while (i <= rel.size()) {
        const size_t j = rel.find('/', i);
        const std::string seg = rel.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!seg.empty() && seg != "..") out += "/" + seg;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return root_ + out;
}

int McHostStore::OpenDescriptors() const {
    int n = 0;
    for (const Descriptor& d : fds_) n += d.used ? 1 : 0;
    return n;
}

int32_t McHostStore::Open(const std::string& guest_path, uint32_t mode) {
    int idx = -1;
    for (int i = 0; i < kMaxDescriptors; i++)
        if (!fds_[i].used) {
            idx = i;
            break;
        }
    if (idx < 0) return kResNoDescriptor;
    const std::string host = HostPath(guest_path);
    std::ios_base::openmode om = std::ios::binary;
    const bool wr = (mode & kModeWrite) != 0;
    const bool rd = (mode & kModeRead) != 0 || !wr;
    if (rd) om |= std::ios::in;
    if (wr) om |= std::ios::out;
    if (mode & kModeCreate) {
        std::error_code ec;
        fs::create_directories(fs::path(host).parent_path(), ec);
        om |= std::ios::out | std::ios::trunc;   // create-or-truncate regardless of prior content
    }
    Descriptor& d = fds_[idx];
    d.stream.open(host, om);
    if (!d.stream.is_open()) return kResNoEntry;
    d.used = true;
    d.path = host;
    return idx + kFirstDescriptor;
}

int32_t McHostStore::Close(int32_t fd) {
    const int idx = fd - kFirstDescriptor;
    if (idx < 0 || idx >= kMaxDescriptors || !fds_[idx].used) return kResDeniedPermit;
    fds_[idx].stream.close();
    fds_[idx].used = false;
    fds_[idx].path.clear();
    return kResOk;
}

int32_t McHostStore::Read(int32_t fd, std::vector<uint8_t>& out, uint32_t size) {
    const int idx = fd - kFirstDescriptor;
    if (idx < 0 || idx >= kMaxDescriptors || !fds_[idx].used) return kResDeniedPermit;
    out.resize(size);
    fds_[idx].stream.read(reinterpret_cast<char*>(out.data()), size);
    const auto n = static_cast<uint32_t>(fds_[idx].stream.gcount());
    fds_[idx].stream.clear();   // a short read is not an error for the next operation
    out.resize(n);
    return static_cast<int32_t>(n);
}

int32_t McHostStore::Write(int32_t fd, const uint8_t* data, uint32_t size) {
    const int idx = fd - kFirstDescriptor;
    if (idx < 0 || idx >= kMaxDescriptors || !fds_[idx].used) return kResDeniedPermit;
    fds_[idx].stream.write(reinterpret_cast<const char*>(data), size);
    fds_[idx].stream.flush();
    return fds_[idx].stream.good() ? static_cast<int32_t>(size) : kResDeniedPermit;
}

// An existing directory reports ok too (the title treats nonzero as "already there").
int32_t McHostStore::Mkdir(const std::string& guest_path) {
    std::error_code ec;
    fs::create_directories(HostPath(guest_path), ec);
    return ec ? kResNoEntry : kResOk;
}

int32_t McHostStore::Chdir(const std::string& guest_path) {
    std::string cand;
    if (guest_path.empty() || guest_path == "/") cand.clear();
    else if (guest_path[0] == '/') cand = guest_path.substr(1);
    else if (cwd_.empty()) cand = guest_path;
    else cand = cwd_ + "/" + guest_path;
    if (!cand.empty()) {
        std::error_code ec;
        if (!fs::is_directory(HostPath("/" + cand), ec)) return kResNoEntry;   // prefix unchanged
    }
    cwd_ = cand;
    return kResOk;
}

int32_t McHostStore::Delete(const std::string& guest_path) {
    std::error_code ec;
    const bool removed = fs::remove(HostPath(guest_path), ec) && !ec;
    return removed ? kResOk : kResNoEntry;
}

int32_t McHostStore::GetDir(const std::string& pattern, uint32_t max, std::vector<DirEntry>& out) {
    out.clear();
    std::string dirpart = "/", prefix;
    const size_t slash = pattern.find_last_of('/');
    if (slash != std::string::npos) {
        dirpart = pattern.substr(0, slash + 1);
        prefix = pattern.substr(slash + 1);
    } else {
        prefix = pattern;
    }
    bool star = false;
    if (!prefix.empty() && prefix.back() == '*') {
        star = true;
        prefix.pop_back();
    }
    const bool parent_is_root = dirpart == "/";
    const std::string host_dir = HostPath(dirpart);
    std::error_code ec;
    if (parent_is_root) fs::create_directories(host_dir, ec);   // a formatted card's root always exists
    if (!fs::is_directory(host_dir, ec)) return kResNoEntry;
    if (star && !parent_is_root) {
        for (const char* dot : {".", ".."}) {
            if (out.size() >= max) break;
            if (!prefix.empty() && std::string(dot).rfind(prefix, 0) != 0) continue;
            out.push_back(DirEntry{dot, 0, true});
        }
    }
    std::vector<fs::directory_entry> entries;
    for (const auto& de : fs::directory_iterator(host_dir, ec)) entries.push_back(de);
    std::sort(entries.begin(), entries.end());   // a fixed order: the host's iteration order is not
    for (const auto& de : entries) {
        if (out.size() >= max) break;
        const std::string name = de.path().filename().string();
        const bool match = star ? name.rfind(prefix, 0) == 0 : name == prefix;
        if (!match) continue;
        const bool is_dir = de.is_directory(ec);
        const uint32_t size = is_dir ? 0u : static_cast<uint32_t>(de.file_size(ec));
        out.push_back(DirEntry{name, size, is_dir});
    }
    return static_cast<int32_t>(out.size());
}
