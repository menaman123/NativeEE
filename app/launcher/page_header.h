// app/launcher/page_header.h -- a page's title (display) and its one-line description (body, muted), at the design's
// offsets. Every page starts with one.
#pragma once
#include <QWidget>
class QLabel;
namespace ps2r::launcher {

class PageHeader : public QWidget {
    Q_OBJECT
public:
    PageHeader(const QString& title, const QString& description, QWidget* parent = nullptr);
    void setDescription(const QString& description);

private:
    void restyle();
    QLabel* title_;
    QLabel* description_;
};

}  // namespace ps2r::launcher
