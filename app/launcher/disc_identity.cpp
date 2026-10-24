// app/launcher/disc_identity.cpp -- the status keys persisted in launcher.json.
#include "disc_identity.h"
namespace ps2r::launcher {

QString DiscIdentity::statusKey(Status s) {
    switch (s) {
    case Supported: return QStringLiteral("supported");
    case OtherRegion: return QStringLiteral("other_region");
    case Modified: return QStringLiteral("modified");
    case NotTheGame: return QStringLiteral("not_the_game");
    case NotAnIso: return QStringLiteral("not_a_disc");
    case Unreadable: break;
    }
    return QStringLiteral("unreadable");
}

DiscIdentity::Status DiscIdentity::statusFromKey(const QString& key) {
    for (Status s : {Supported, OtherRegion, Modified, NotTheGame, NotAnIso}) if (statusKey(s) == key) return s;
    return Unreadable;
}

}  // namespace ps2r::launcher
