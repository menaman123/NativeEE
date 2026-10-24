// app/launcher/launcher_theme.h -- the design's palette, type scale and spacing grid
// as one object. Light or dark follows the system; every widget asks the theme
// for a role colour, a named font or a spacing token and never uses a literal.
#pragma once
#include <QColor>
#include <QFont>
#include <QObject>
namespace ps2r::launcher {

class LauncherTheme : public QObject {
    Q_OBJECT
public:
    enum Role { Window, Sidebar, Surface, SurfaceRaised, Line, Text, TextMuted, TextFaint, Accent, AccentText, AccentSoft, Ok, Warn, Error };
    enum TextStyle { Display, Title, Body, Label, Caption, Mono };
    enum Space { S1 = 4, S2 = 8, S3 = 16, S4 = 24, S5 = 32, S6 = 48 };

    explicit LauncherTheme(QObject* parent = nullptr);
    static LauncherTheme& instance();

    bool dark() const { return dark_; }
    void setDark(bool dark);
    void followSystem();
    QColor color(Role role) const;
    QFont font(TextStyle style) const;
    static int radiusControl() { return 6; }
    static int radiusCard() { return 10; }
    static int controlHeight() { return 32; }
    static int largeControlHeight() { return 40; }
    static int rowHeight() { return 48; }
    static int sidebarWidth() { return 220; }
    QString css(Role role) const;

signals:
    void changed();

private:
    bool dark_ = false;
    bool forced_ = false;
};

}  // namespace ps2r::launcher
