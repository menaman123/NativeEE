// app/launcher/progress_panel.h -- the 4 px progress bar with its line above (what is happening) and its caption
// below (elapsed · remaining · the step); indeterminate = the fill sweeps.
#pragma once
#include <QWidget>
class QLabel;
class QTimer;
namespace ps2r::launcher {

class ProgressPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgressPanel(QWidget* parent = nullptr);
    void setText(const QString& text);
    void setCaption(const QString& caption);
    void setProgress(int done, int total);   // total 0 = indeterminate
    void setIndeterminate(bool on);
    void clear();
    QString text() const;
    QString caption() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void restyle();
    QLabel* text_;
    QLabel* caption_;
    QWidget* bar_;
    QTimer* sweep_;
    int done_ = 0, total_ = 0, sweepPos_ = 0;
    bool indeterminate_ = false;
};

}  // namespace ps2r::launcher
