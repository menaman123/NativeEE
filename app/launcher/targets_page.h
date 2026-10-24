// app/launcher/targets_page.h -- Targets: the segmented selector (exactly the manifest's targets: macOS, Windows,
// PS Vita), this machine's preselected, one card shown at a time; a build for a target that needs an extra install
// (VitaSDK) runs that install first.
#pragma once
#include <QVector>
#include <QWidget>
namespace ps2r::launcher {
class DetailsSheet;
class LauncherApp;
class SegmentedControl;
class TargetCard;

class TargetsPage : public QWidget {
    Q_OBJECT
public:
    TargetsPage(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    SegmentedControl* selector() const { return selector_; }
    TargetCard* card(const QString& targetId) const;
    void select(const QString& targetId);
    void buildTarget(const QString& targetId);   // the card's button, as a method for the driver

private:
    void showSelected();
    LauncherApp* app_;
    DetailsSheet* details_;
    SegmentedControl* selector_;
    QVector<TargetCard*> cards_;
    QString pendingAfterInstall_;
};

}  // namespace ps2r::launcher
