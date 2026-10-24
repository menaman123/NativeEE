// app/launcher/launcher_theme.cpp -- the palette and type scale of the design specification.
#include "launcher_theme.h"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStyleHints>
namespace ps2r::launcher {

namespace {
struct RolePair { const char* light; const char* dark; };
// The palette, one { light, dark } pair per role, in Role order.
const RolePair kPalette[] = {
    {"#F3F4F6", "#141518"},   // Window
    {"#E9EBEF", "#1B1C21"},   // Sidebar
    {"#FFFFFF", "#22242A"},   // Surface
    {"#FFFFFF", "#2A2D34"},   // SurfaceRaised
    {"#D9DCE2", "#33363E"},   // Line
    {"#15171B", "#F0F1F4"},   // Text
    {"#5E6470", "#A1A6B2"},   // TextMuted
    {"#8A909C", "#6D7380"},   // TextFaint
    {"#D9541E", "#F0693A"},   // Accent
    {"#FFFFFF", "#141518"},   // AccentText
    {"#FBE9E0", "#3A251C"},   // AccentSoft
    {"#2E8B57", "#4CC27B"},   // Ok
    {"#B7791F", "#E0A43A"},   // Warn
    {"#C0392B", "#E8604F"},   // Error
};
bool systemIsDark() {
    if (!qGuiApp) return false;
    return qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
}  // namespace

LauncherTheme::LauncherTheme(QObject* parent) : QObject(parent) {
    dark_ = systemIsDark();
    if (qGuiApp) {
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (!forced_) { dark_ = systemIsDark(); emit changed(); }
        });
    }
}

LauncherTheme& LauncherTheme::instance() {
    static LauncherTheme theme;
    return theme;
}

void LauncherTheme::setDark(bool dark) {
    forced_ = true;
    if (dark_ == dark) return;
    dark_ = dark;
    emit changed();
}

void LauncherTheme::followSystem() {
    forced_ = false;
    const bool now = systemIsDark();
    if (now != dark_) { dark_ = now; emit changed(); }
}

QColor LauncherTheme::color(Role role) const {
    const RolePair& p = kPalette[int(role)];
    return QColor(QString::fromLatin1(dark_ ? p.dark : p.light));
}

QString LauncherTheme::css(Role role) const { return color(role).name(QColor::HexRgb); }

QFont LauncherTheme::font(TextStyle style) const {
    // Pixel sizes, not points: a point is one pixel at macOS's 72 dpi but 1.33 at Windows' 96, so point sizes made every
    // text a third larger on Windows than the layout was drawn for. Qt scales pixel sizes with the display's scale.
    QFont f = QFontDatabase::systemFont(style == Mono ? QFontDatabase::FixedFont : QFontDatabase::GeneralFont);
    switch (style) {
    case Display: f.setPixelSize(28); f.setWeight(QFont::DemiBold); break;
    case Title:   f.setPixelSize(17); f.setWeight(QFont::DemiBold); break;
    case Body:    f.setPixelSize(13); f.setWeight(QFont::Normal); break;
    case Label:   f.setPixelSize(13); f.setWeight(QFont::Medium); break;
    case Caption: f.setPixelSize(11); f.setWeight(QFont::Normal); break;
    case Mono:    f.setPixelSize(12); break;
    }
    return f;
}

}  // namespace ps2r::launcher
