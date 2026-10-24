// app/launcher/save_state_card.h -- one save state: the thumbnail the game left beside it (or a neutral tile with the
// frame count when there is none), the name, when it was saved and how big it is, and Resume.
#pragma once
#include <QWidget>
#include "save_state_list.h"
class QLabel;
namespace ps2r::launcher {
class FlatButton;

class SaveStateCard : public QWidget {
    Q_OBJECT
public:
    explicit SaveStateCard(const SaveStateEntry& entry, QWidget* parent = nullptr);
    const SaveStateEntry& entry() const { return entry_; }
    FlatButton* resumeButton() const { return resume_; }
    QString factsText() const;
    static QPixmap neutralTile(const SaveStateEntry& entry, const QSize& size, qreal deviceRatio, bool dark);

signals:
    void resumeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void restyle();
    SaveStateEntry entry_;
    QLabel* thumb_;
    QLabel* name_;
    QLabel* facts_;
    FlatButton* resume_;
};

}  // namespace ps2r::launcher
