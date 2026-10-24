// app/launcher/details_sheet.h -- the Details disclosure: a monospace log that follows its source live, Copy and
// Close. One per window, re-pointed at whichever log the user asked for.
#pragma once
#include <QWidget>
class QPlainTextEdit;
namespace ps2r::launcher {
class FlatButton;
class SheetOverlay;

class DetailsSheet : public QWidget {
    Q_OBJECT
public:
    explicit DetailsSheet(QWidget* host);
    // Shows the sheet with `text` (a log file's content) under `heading`; appendLine() follows a running process.
    void showLog(const QString& heading, const QString& text);
    void showLogFile(const QString& heading, const QString& path);
    void appendLine(const QString& line);
    bool isOpen() const;
    void closeSheet();
    QString text() const;

private:
    SheetOverlay* overlay_;
    QPlainTextEdit* log_;
    FlatButton* copy_;
    FlatButton* close_;
    QString heading_;
};

}  // namespace ps2r::launcher
