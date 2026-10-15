#include "app/host/texture_pack_path.h"

#include "engine/core/config/config.h"

namespace ps2r::app {

std::string TexturePackPath::Resolve(const Config& config) {
    const std::string& pack = config.video.texture_pack;
    if (pack.empty() || pack == "none") return {};
    for (const char c : pack)   // a pack NAME: no path separators or traversal
        if (c == '/' || c == '\\' || c == ':') return {};
    if (pack == "." || pack == "..") return {};
    const std::string& packs = config.paths.texture_packs;
    if (packs.empty()) return {};
    return packs + "/" + pack;
}

}  // namespace ps2r::app
