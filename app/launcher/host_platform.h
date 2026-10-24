// app/launcher/host_platform.h -- which of the manifest's targets is THIS machine (os + cpu architecture), and the
// plain names of the os and architecture for the Target page. Nothing title-shaped: the ids come from the manifest.
#pragma once
#include <QString>
namespace ps2r::launcher {
class TitleManifest;

class HostPlatform {
public:
    static QString os();     // macos / windows / linux
    static QString arch();   // arm64 / x86_64
    // The manifest target whose os and arch are this machine's; empty when the manifest has none for it.
    static QString targetId(const TitleManifest& manifest);
    static QString osLabel();   // "macOS" / "Windows" / "Linux"
};

}  // namespace ps2r::launcher
