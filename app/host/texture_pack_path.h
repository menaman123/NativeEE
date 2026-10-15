#pragma once

#include <string>

namespace ps2r { class Config; }
namespace ps2r::app {

// The texture pack directory (A4): `<paths.texture_packs>/<video.texture_pack>`.
// `paths.texture_packs` is the C4 layout's one place for packs -- ConfigLoader
// resolves an empty value to `<user data root>/textures` at Load (the root =
// `paths.user_data`, else the platform's: UserData::PlatformRoot), so no second
// root resolver lives here. Empty for `none`, a non-name (path separators,
// `.`/`..`) or when no pack directory is known (the engine tests).
class TexturePackPath {
public:
    static std::string Resolve(const Config& config);
};

}  // namespace ps2r::app
