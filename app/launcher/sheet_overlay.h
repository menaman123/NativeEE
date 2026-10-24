// app/launcher/sheet_overlay.h -- a sheet: the page dimmed by 40 %, a surface panel (radius 10) with a heading, a
// content widget and buttons at the bottom right. Escape presses the cancelling button. One class hosts every sheet
// (first run, Details, Remove).
#pragma once
#include <QWidget>
class QAbstractButton;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;
namespace ps2r::launcher {

class SheetOverlay : public QWidget {
    Q_OBJECT
public:
    SheetOverlay(const QString& heading, QWidget* content, QWidget* parent);
    void addButton(QWidget* button);
    void setCancelButton(QAbstractButton* button) { cancel_ = button; }
    void setPanelWidth(int width);
    void open();
    void close();
    QWidget* content() const { return content_; }

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();
    QWidget* panel_;
    QLabel* heading_;
    QWidget* content_;
    QHBoxLayout* buttons_;
    QAbstractButton* cancel_ = nullptr;
    int panelWidth_ = 640;
};

}  // namespace ps2r::launcher
