#pragma once
// McHostStore: the memory card as a host directory. The card root is
// `<mc_dir>/mc0` (port 0; port 1 = `mc1`), its contents 1:1 with the card
// (one directory per save, files inside). A 16-entry descriptor table backs
// Open/Read/Write/Close (descriptors start at 2: 0 and 1 are never handed
// out), Mkdir/Delete map to filesystem operations, Chdir keeps a validated
// card-relative prefix, and GetDir enumerates a directory with the SDK's
// trailing-'*' pattern.
//
// Result codes are libmc's (sceMcRes*): 0 ok, -4 NoEntry (a missing path or
// parent), -5 DeniedPermit (a bad descriptor), -8 out of descriptors. The
// mcman rules the store reproduces (verified against PCSX2 by the original
// runtime):
//   * a wildcard listing of an existing SUBdirectory carries "." and ".."
//     (attribute 0x8427) ahead of the files; root listings and exact-name
//     lookups carry no dot entries;
//   * a listing whose parent does not resolve is -4, not an empty listing;
//   * chdir to a missing directory is -4 and the prefix is unchanged;
//   * a path never escapes the card: ".." segments are dropped.
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class McHostStore {
public:
    static constexpr int kMaxDescriptors = 16;
    static constexpr int kFirstDescriptor = 2;
    static constexpr int32_t kResOk = 0;
    static constexpr int32_t kResNoEntry = -4;
    static constexpr int32_t kResDeniedPermit = -5;
    static constexpr int32_t kResNoDescriptor = -8;
    static constexpr uint16_t kAttrDirectory = 0x8427;
    static constexpr uint16_t kAttrFile = 0x8497;
    // libmc open mode bits.
    static constexpr uint32_t kModeRead = 0x1, kModeWrite = 0x2, kModeCreate = 0x200;

    struct DirEntry {
        std::string name;
        uint32_t size = 0;
        bool is_dir = false;
    };

    // `mc_dir` is Config paths.mc_dir; the port directories are created on demand.
    explicit McHostStore(std::string mc_dir);

    // Open returns the descriptor (>= 2) or a negative result code.
    int32_t Open(const std::string& guest_path, uint32_t mode);
    int32_t Close(int32_t fd);
    // Read/Write return the byte count (Read: 0 at end of file) or a negative code.
    int32_t Read(int32_t fd, std::vector<uint8_t>& out, uint32_t size);
    int32_t Write(int32_t fd, const uint8_t* data, uint32_t size);
    int32_t Mkdir(const std::string& guest_path);
    int32_t Chdir(const std::string& guest_path);
    int32_t Delete(const std::string& guest_path);
    // Pattern = "/dir/prefix*" or an exact path; `max` bounds the entries.
    // Returns the entry count or -4.
    int32_t GetDir(const std::string& pattern, uint32_t max, std::vector<DirEntry>& out);

    const std::string& Cwd() const { return cwd_; }
    std::string HostPath(const std::string& guest_path) const;
    int OpenDescriptors() const;

private:
    struct Descriptor {
        std::fstream stream;
        std::string path;
        bool used = false;
    };
    std::string root_;   // <mc_dir>/mc0
    std::string cwd_;    // card-relative, "" = root
    Descriptor fds_[kMaxDescriptors];
};
