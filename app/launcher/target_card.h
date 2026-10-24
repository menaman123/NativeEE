// app/launcher/target_card.h -- one target's card: the name, what the last build produced (when, hash, size, up to
// date), the artefact's path, how to install it on that machine, the reason when this host cannot build it; Build /
// Rebuild, Show in Finder; the progress bar while it builds.
#pragma once
#include <QWidget>
#include "title_manifest.h"
class QLabel;
namespace ps2r::launcher {
class FlatButton;
class LauncherApp;
class ProgressPanel;

class TargetCard : public QWidget {
    Q_OBJECT
public:
    TargetCard(LauncherApp* app, const TargetSpec& spec, QWidget* parent = nullptr);
    const TargetSpec& spec() const { return spec_; }
    void refresh();
    FlatButton* buildButton() const { return build_; }
    FlatButton* showButton() const { return show_; }
    FlatButton* detailsButton() const { return details_; }
    ProgressPanel* progress() const { return progress_; }
    QString statusText() const;

signals:
    void buildRequested();
    void detailsRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    LauncherApp* app_;
    TargetSpec spec_;
    QLabel* title_;
    QLabel* status_;
    QLabel* path_;
    QLabel* howTo_;
    FlatButton* build_;
    FlatButton* show_;
    FlatButton* details_;
    FlatButton* cancel_;
    ProgressPanel* progress_;
};

}  // namespace ps2r::launcher
