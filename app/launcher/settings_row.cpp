// app/launcher/settings_row.cpp -- the settings row.
#include "settings_row.h"
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include "flat_button.h"
#include "launcher_theme.h"
#include "segmented_control.h"
#include "switch_control.h"
namespace ps2r::launcher {

namespace {
constexpr int kMaxSegments = 6;
}

SettingsRow::SettingsRow(const SettingsRowSpec& spec, QWidget* parent) : QWidget(parent), spec_(spec) {
    const LauncherTheme& t = LauncherTheme::instance();
    setObjectName(QStringLiteral("setting_%1").arg(spec.key));
    label_ = new QLabel(spec.label, this);
    label_->setFont(t.font(LauncherTheme::Label));
    label_->setMinimumWidth(176);
    label_->setWordWrap(true);
    help_ = new QLabel(spec.help, this);
    help_->setFont(t.font(LauncherTheme::Caption));
    help_->setWordWrap(true);
    help_->setFixedWidth(420);
    help_->setVisible(!spec.help.isEmpty());
    help_->setMinimumHeight(qMax(0, help_->heightForWidth(420)));   // a wrapped label's height, so the row below never overlaps
    QWidget* control = nullptr;
    if (spec.kind == "switch") {
        switch_ = new SwitchControl(this);
        switch_->setAccessibleName(spec.label);
        connect(switch_, &QAbstractButton::toggled, this, [this](bool on) { emit valueChanged(spec_.key, on ? QStringLiteral("on") : QStringLiteral("off")); });
        control = switch_;
    } else if (spec.choices.size() <= kMaxSegments) {
        segmented_ = new SegmentedControl(this);
        QStringList labels, values;
        for (const SettingsChoice& c : spec.choices) { labels << c.label; values << c.value; }
        segmented_->setItems(labels, values);
        segmented_->setAccessibleName(spec.label);
        connect(segmented_, &SegmentedControl::valueChanged, this, [this](const QString& v) { emit valueChanged(spec_.key, v); });
        control = segmented_;
    } else {
        popup_ = new FlatButton(spec.choices.isEmpty() ? QString() : spec.choices.first().label, FlatButton::Secondary, this);
        popup_->setAccessibleName(spec.label);
        popupValue_ = spec.choices.isEmpty() ? QString() : spec.choices.first().value;
        connect(popup_, &QAbstractButton::clicked, this, &SettingsRow::showPopup);
        control = popup_;
    }
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, LauncherTheme::S2, 0, LauncherTheme::S2);
    grid->setHorizontalSpacing(LauncherTheme::S3);
    grid->setVerticalSpacing(LauncherTheme::S1);
    grid->addWidget(label_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(control, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(help_, 1, 1, Qt::AlignLeft);
    grid->setColumnStretch(2, 1);
    connect(&t, &LauncherTheme::changed, this, &SettingsRow::restyle);
    restyle();
    setValue(spec.defaultValue);
}

QString SettingsRow::value() const {
    if (switch_) return switch_->isChecked() ? QStringLiteral("on") : QStringLiteral("off");
    if (segmented_) return segmented_->currentValue();
    return popupValue_;
}

void SettingsRow::setValue(const QString& value) {
    if (switch_) {
        const bool on = value == "on" || value == "true" || value == "1" || value == "yes";
        switch_->blockSignals(true);
        switch_->setChecked(on);
        switch_->blockSignals(false);
    } else if (segmented_) {
        segmented_->setCurrentValue(value);
    } else if (popup_) {
        for (const SettingsChoice& c : spec_.choices) if (c.value == value) { popupValue_ = value; popup_->setText(c.label); }
    }
}

void SettingsRow::setHelpOverride(const QString& help) {
    help_->setText(help.isEmpty() ? spec_.help : help);
    help_->setVisible(!help_->text().isEmpty());
    help_->setMinimumHeight(help_->heightForWidth(420));
}

void SettingsRow::setLocked(bool locked) {
    if (switch_) switch_->setEnabled(!locked);
    if (segmented_) segmented_->setEnabled(!locked);
    if (popup_) popup_->setEnabled(!locked);
}

void SettingsRow::showPopup() {
    QMenu menu(this);
    for (const SettingsChoice& c : spec_.choices) {
        QAction* a = menu.addAction(c.label);
        a->setData(c.value);
    }
    QAction* chosen = menu.exec(popup_->mapToGlobal(QPoint(0, popup_->height())));
    if (!chosen) return;
    const QString v = chosen->data().toString();
    if (v == popupValue_) return;
    setValue(v);
    emit valueChanged(spec_.key, v);
}

void SettingsRow::restyle() {
    const LauncherTheme& t = LauncherTheme::instance();
    label_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::Text)));
    help_->setStyleSheet(QStringLiteral("color: %1;").arg(t.css(LauncherTheme::TextMuted)));
}

void SettingsRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setPen(QPen(LauncherTheme::instance().color(LauncherTheme::Line), 1));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

}  // namespace ps2r::launcher
