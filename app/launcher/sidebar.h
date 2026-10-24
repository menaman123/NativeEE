// app/launcher/sidebar.h -- the 220 px column: the game's name and version at the top, the four pages (Games, Save
// States, Settings, About), the game folder with Open at the bottom. Painted flat; Cmd/Ctrl+1..4 select.
#pragma once
#include <QStringList>
#include <QWidget>
namespace ps2r::launcher {
class FlatButton;

class Sidebar : public QWidget {
    Q_OBJECT
public:
    Sidebar(const QString& product, const QString& version, const QStringList& pages, QWidget* parent = nullptr);
    int current() const { return current_; }
    void setCurrent(int index);
    void setFooter(const QString& path);
    QString pageName(int index) const { return pages_.value(index); }
    FlatButton* openButton() const { return open_; }

signals:
    void selected(int index);
    void openRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect itemRect(int index) const;
    int itemAt(const QPoint& pos) const;
    QString product_, version_, footer_;
    QStringList pages_;
    FlatButton* open_;
    int current_ = 0, hover_ = -1;
};

}  // namespace ps2r::launcher
