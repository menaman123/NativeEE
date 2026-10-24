// app/launcher/checklist_row.h -- one step of the first-run checklist: the state glyph (ring / pulsing dot / tick /
// cross / dash), the name, the plain-English state, the size; a failed row adds its reason and Retry / Details
// (and Install when the system can offer the component).
#pragma once
#include <QWidget>
#include "installer.h"
class QLabel;
class QTimer;
namespace ps2r::launcher {
class FlatButton;

class ChecklistRow : public QWidget {
    Q_OBJECT
public:
    explicit ChecklistRow(const InstallStep& step, QWidget* parent = nullptr);
    void setStep(const InstallStep& step);
    FlatButton* retryButton() const { return retry_; }
    FlatButton* detailsButton() const { return details_; }
    FlatButton* installButton() const { return install_; }

signals:
    void retryRequested();
    void detailsRequested();
    void installRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void restyle();
    void elideDetail();
    InstallStep step_;
    QLabel* name_;
    QLabel* detail_;
    QLabel* size_;
    QLabel* reason_;
    FlatButton* retry_;
    FlatButton* details_;
    FlatButton* install_;
    QWidget* reasonRow_;
    QTimer* pulse_;
    int phase_ = 0;
};

}  // namespace ps2r::launcher
