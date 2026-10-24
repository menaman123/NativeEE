// app/launcher/generated_artwork.h -- a card's neutral artwork generated from the title id: a two-tone field with a
// diagonal band and the title's initials. Nothing from the disc; painted at the device pixel ratio.
#pragma once
#include <QWidget>
namespace ps2r::launcher {

class GeneratedArtwork : public QWidget {
    Q_OBJECT
public:
    GeneratedArtwork(const QString& titleName, const QString& seed, int side, QWidget* parent = nullptr);
    static QString initialsOf(const QString& name);
    static QPixmap render(const QString& titleName, const QString& seed, int side, qreal deviceRatio, bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString name_, seed_;
    int side_;
};

}  // namespace ps2r::launcher
