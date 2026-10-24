// app/launcher/first_run_sheet.h -- the first-run checklist shown as a sheet over Games until every required step is
// done: the rows, the progress panel, the totals line, Details; it closes itself when the installer finishes.
#pragma once
#include <QVector>
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class ChecklistRow;
class DetailsSheet;
class FlatButton;
class Installer;
class ProgressPanel;
class SheetOverlay;

class FirstRunSheet : public QWidget {
    Q_OBJECT
public:
    FirstRunSheet(Installer* installer, DetailsSheet* details, QWidget* host);
    void open();
    bool isOpen() const;
    void close();
    ChecklistRow* row(int index) const { return rows_.value(index); }
    FlatButton* detailsButton() const { return details_; }

private:
    void refreshTotals();
    Installer* installer_;
    DetailsSheet* detailsSheet_;
    SheetOverlay* overlay_;
    QVector<ChecklistRow*> rows_;
    ProgressPanel* progress_;
    QLabel* totals_;
    FlatButton* details_;
    FlatButton* close_;
};

}  // namespace ps2r::launcher
