// app/launcher/launcher_style.h -- the application style: every Qt primitive the launcher still lets Qt draw (scroll
// bars, menus, focus, frames) drawn flat in the theme's colours over Fusion, so no default widget look shows anywhere
// (principle 4). The launcher's own controls paint themselves.
#pragma once
#include <QProxyStyle>
namespace ps2r::launcher {

class LauncherStyle : public QProxyStyle {
    Q_OBJECT
public:
    LauncherStyle();
    // Installs the style, the palette and the application stylesheet; re-applied on a theme change.
    static void apply();

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override;
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override;
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override;

private:
    static QPalette palette();
    static QString stylesheet();
};

}  // namespace ps2r::launcher
