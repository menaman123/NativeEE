// app/launcher/about_page.h -- About: what this is, the versions (launcher, engine, Qt, source, toolchain pins), where
// the files are (app data, settings, logs: each with Open), the licences, and Remove everything the launcher installed.
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class DetailsSheet;
class FlatButton;
class LauncherApp;
class SheetOverlay;

class AboutPage : public QWidget {
    Q_OBJECT
public:
    AboutPage(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    FlatButton* removeButton() const { return remove_; }
    void refresh();

private:
    void openRemoveSheet();
    QWidget* linkRow(const QString& label, const QString& path, QWidget* parent);
    LauncherApp* app_;
    DetailsSheet* details_;
    FlatButton* remove_;
    QLabel* versions_;
};

}  // namespace ps2r::launcher
