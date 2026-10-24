// app/launcher/segmented_control.h -- a choice among up to six values drawn as one flat segmented bar (the selected
// segment accent-soft with accent text); beyond six the row uses a popup. Left / Right and Home / End move the choice.
#pragma once
#include <QStringList>
#include <QWidget>
namespace ps2r::launcher {

class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(QWidget* parent = nullptr);
    void setItems(const QStringList& labels, const QStringList& values);
    QStringList values() const { return values_; }
    QString currentValue() const;
    int currentIndex() const { return current_; }
    void setCurrentValue(const QString& value);
    void setCurrentIndex(int index, bool emitSignal = false);
    QSize sizeHint() const override;

signals:
    void valueChanged(const QString& value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    int indexAt(const QPoint& pos) const;
    int segmentWidth() const;
    QStringList labels_, values_;
    int current_ = -1, hover_ = -1;
};

}  // namespace ps2r::launcher
