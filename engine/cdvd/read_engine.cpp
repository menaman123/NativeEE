// ReadEngine implementation; the model is in the header.
#include "engine/cdvd/read_engine.h"

ReadEngine::OpenFile* ReadEngine::Slot(int32_t fd) {
    if (fd < 1 || fd > kMaxFiles) return nullptr;
    OpenFile& f = files_[fd - 1];
    return f.used ? &f : nullptr;
}

int32_t ReadEngine::Open(const char* path, uint32_t /*flags*/) {
    if (!path || !fs_.IsOpen()) return -1;
    IsoFs::Entry e;
    if (!fs_.Find(path, e) || e.directory) return -1;
    for (int i = 0; i < kMaxFiles; i++) {
        if (files_[i].used) continue;
        files_[i].used = true;
        files_[i].entry = e;
        files_[i].pos = 0;
        return i + 1;
    }
    return -1;
}

int32_t ReadEngine::Close(int32_t fd) {
    OpenFile* f = Slot(fd);
    if (!f) return -1;
    f->used = false;
    return 0;
}

int32_t ReadEngine::Read(int32_t fd, uint8_t* dst, uint32_t size) {
    OpenFile* f = Slot(fd);
    if (!f || !dst) return -1;
    if (f->pos >= f->entry.size) return 0;
    const uint32_t remain = f->entry.size - f->pos;
    const uint32_t want = remain < size ? remain : size;
    const uint32_t got = static_cast<uint32_t>(fs_.ReadBytes(static_cast<uint64_t>(f->entry.lba) * IsoFs::kSectorSize + f->pos, want, dst));
    f->pos += got;
    file_bytes_read_ += got;
    return static_cast<int32_t>(got);
}

int32_t ReadEngine::Seek(int32_t fd, int32_t offset, uint32_t whence) {
    OpenFile* f = Slot(fd);
    if (!f) return -1;
    int64_t base = 0;
    switch (whence) {
        case kSeekSet: base = 0; break;
        case kSeekCur: base = f->pos; break;
        case kSeekEnd: base = f->entry.size; break;
        default: return -1;
    }
    const int64_t np = base + offset;
    if (np < 0 || np > static_cast<int64_t>(f->entry.size)) return -1;
    f->pos = static_cast<uint32_t>(np);
    return static_cast<int32_t>(f->pos);
}

uint32_t ReadEngine::ReadSectors(uint32_t lba, uint32_t count, uint8_t* dst) {
    if (!dst) return 0;
    const uint32_t n = fs_.ReadSectors(lba, count, dst);
    sectors_read_ += n;
    return n;
}

bool ReadEngine::FindFile(const char* path, uint32_t* lba, uint32_t* size) const {
    IsoFs::Entry e;
    if (!path || !fs_.Find(path, e) || e.directory) return false;
    if (lba) *lba = e.lba;
    if (size) *size = e.size;
    return true;
}

uint32_t ReadEngine::OpenFiles() const {
    uint32_t n = 0;
    for (const auto& f : files_) n += f.used ? 1 : 0;
    return n;
}
