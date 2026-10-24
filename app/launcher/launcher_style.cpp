// app/launcher/launcher_style.cpp -- the flat application style over Fusion.
#include "launcher_style.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>
#include "launcher_theme.h"
namespace ps2r::launcher {

LauncherStyle::LauncherStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

void LauncherStyle::apply() {
    qApp->setStyle(new LauncherStyle());
    qApp->setPalette(palette());
    qApp->setStyleSheet(stylesheet());
    qApp->setFont(LauncherTheme::instance().font(LauncherTheme::Body));
}

QPalette LauncherStyle::palette() {
    const LauncherTheme& t = LauncherTheme::instance();
    QPalette p;
    p.setColor(QPalette::Window, t.color(LauncherTheme::Window));
    p.setColor(QPalette::WindowText, t.color(LauncherTheme::Text));
    p.setColor(QPalette::Base, t.color(LauncherTheme::Surface));
    p.setColor(QPalette::AlternateBase, t.color(LauncherTheme::SurfaceRaised));
    p.setColor(QPalette::Text, t.color(LauncherTheme::Text));
    p.setColor(QPalette::PlaceholderText, t.color(LauncherTheme::TextFaint));
    p.setColor(QPalette::Button, t.color(LauncherTheme::Surface));
    p.setColor(QPalette::ButtonText, t.color(LauncherTheme::Text));
    p.setColor(QPalette::Highlight, t.color(LauncherTheme::Accent));
    p.setColor(QPalette::HighlightedText, t.color(LauncherTheme::AccentText));
    p.setColor(QPalette::ToolTipBase, t.color(LauncherTheme::SurfaceRaised));
    p.setColor(QPalette::ToolTipText, t.color(LauncherTheme::Text));
    p.setColor(QPalette::Mid, t.color(LauncherTheme::Line));
    p.setColor(QPalette::Dark, t.color(LauncherTheme::Line));
    p.setColor(QPalette::Light, t.color(LauncherTheme::SurfaceRaised));
    p.setColor(QPalette::Disabled, QPalette::Text, t.color(LauncherTheme::TextFaint));
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.color(LauncherTheme::TextFaint));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.color(LauncherTheme::TextFaint));
    return p;
}

QString LauncherStyle::stylesheet() {
    const LauncherTheme& t = LauncherTheme::instance();
    const QString line = t.css(LauncherTheme::Line), surface = t.css(LauncherTheme::Surface),
                  raised = t.css(LauncherTheme::SurfaceRaised), text = t.css(LauncherTheme::Text),
                  accent = t.css(LauncherTheme::Accent), accentSoft = t.css(LauncherTheme::AccentSoft),
                  muted = t.css(LauncherTheme::TextMuted), window = t.css(LauncherTheme::Window);
    return QString(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 12px; margin: 4px 2px 4px 2px; }"
        "QScrollBar::handle:vertical { background: %1; min-height: 32px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: %6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px 4px 2px 4px; }"
        "QScrollBar::handle:horizontal { background: %1; min-width: 32px; border-radius: 4px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QMenu { background: %3; color: %4; border: 1px solid %1; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 6px 16px; border-radius: 4px; }"
        "QMenu::item:selected { background: %7; color: %5; }"
        "QToolTip { background: %3; color: %4; border: 1px solid %1; padding: 4px 8px; }"
        "QPlainTextEdit { background: %2; color: %4; border: 1px solid %1; border-radius: 6px; padding: 8px; selection-background-color: %5; }"
        "QLineEdit { background: %2; color: %4; border: 1px solid %1; border-radius: 6px; padding: 0 8px; min-height: 30px; selection-background-color: %5; }"
        "QLineEdit:focus { border: 2px solid %5; }"
        "QMainWindow, QDialog { background: %8; }"
        "QLabel { background: transparent; }")
        .arg(line, surface, raised, text, accent, muted, accentSoft, window);
}

void LauncherStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                  const QWidget* widget) const {
    const LauncherTheme& t = LauncherTheme::instance();
    switch (element) {
    case PE_Frame:
    case PE_FrameWindow:
    case PE_PanelScrollAreaCorner:
    case PE_FrameStatusBarItem:
        return;   // flat: no frame
    case PE_FrameFocusRect: {
        // the 2 px accent ring, drawn outside the control (focus)
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(t.color(LauncherTheme::Accent), 2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(option->rect).adjusted(1, 1, -1, -1), LauncherTheme::radiusControl(),
                                 LauncherTheme::radiusControl());
        painter->restore();
        return;
    }
    case PE_PanelMenu: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(t.color(LauncherTheme::Line), 1));
        painter->setBrush(t.color(LauncherTheme::SurfaceRaised));
        painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        painter->restore();
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void LauncherStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                const QWidget* widget) const {
    QProxyStyle::drawControl(element, option, painter, widget);
}

int LauncherStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const {
    switch (metric) {
    case PM_ScrollBarExtent: return 12;
    case PM_DefaultFrameWidth: return 0;
    case PM_MenuPanelWidth: return 1;
    case PM_MenuHMargin: return 4;
    case PM_MenuVMargin: return 4;
    default: return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int LauncherStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                             QStyleHintReturn* returnData) const {
    switch (hint) {
    case SH_ScrollBar_Transient: return 0;
    case SH_Menu_FlashTriggeredItem: return 0;
    case SH_Widget_Animate: return 0;
    case SH_DialogButtonBox_ButtonsHaveIcons: return 0;
    default: return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

}  // namespace ps2r::launcher
