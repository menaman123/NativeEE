// app/launcher/folder_row.h -- a folder setting: the label, the path (elided in the middle), Change… and Open in
// Finder / Explorer. 48 high.
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {
class FlatButton;

class FolderRow : public QWidget {
    Q_OBJECT
public:
    FolderRow(const QString& label, QWidget* parent = nullptr);
    void setPath(const QString& path);
    QString path() const { return path_; }
    FlatButton* changeButton() const { return change_; }
    FlatButton* openButton() const { return open_; }
    static QString openInFileManagerLabel();
    static void revealInFileManager(const QString& path);

signals:
    void changeRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void relayout();
    QLabel* label_;
    QLabel* pathLabel_;
    FlatButton* change_;
    FlatButton* open_;
    QString path_;
};

}  // namespace ps2r::launcher
