// app/launcher/about_page.h -- About: what this is, the versions (launcher, engine, Qt), what is not supported and
// why (from the manifest), where the files are (game folder, app data, logs: each with Open), the licences, and
// Remove the launcher's own data (never the game folder or the disc images).
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class DetailsSheet;
class FlatButton;
class LauncherApp;

class AboutPage : public QWidget {
    Q_OBJECT
public:
    AboutPage(LauncherApp* app, DetailsSheet* details, QWidget* parent = nullptr);
    FlatButton* removeButton() const { return remove_; }
    void refresh();
    QString unsupportedText() const;
    // The licence or notices file shipped beside the application (Contents/Resources on macOS), or empty.
    static QString shippedFile(const QString& name);

private:
    void openRemoveSheet();
    QWidget* linkRow(const QString& label, const QString& path, QWidget* parent);
    LauncherApp* app_;
    DetailsSheet* details_;
    FlatButton* remove_;
    QLabel* versions_;
    QLabel* unsupported_;
    QWidget* gameFolderRow_;
};

}  // namespace ps2r::launcher
