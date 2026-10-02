#ifndef BUTTONHOLE_H
#define BUTTONHOLE_H

#include "audioplayer.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QWidget>
#include <QPushButton>
#include <QSettings>
#include <QKeyEvent>

class ButtonHole : public QWidget
{
    Q_OBJECT

    public:
        explicit ButtonHole(QWidget *parent = nullptr);

        void setPositon(int newX, int newY);
        void setBtnText(QString newText);
        void setAudioDevice(const QAudioDevice &device);

    protected:
        void keyPressEvent(QKeyEvent *event) override;

    public slots:
        void bntContextMenu(QPoint pos);
        void buttonHoleClick();
        void buttonHoleStop();
        void buttonHoleKeyPress();
        void flash();

    private:
        // Cached "buttonhole/btn_<n>" lookup. flash() polls every 300 ms on
        // every button; the only writers of this key are this widget's own
        // context-menu actions, which invalidate the cache.
        QString assignedPath() const;
        void invalidateAssignedPath();

        QPushButton *button;

        QString filename = "";
        QString text = "";
        mutable QString m_assignedPath;
        mutable bool m_assignedPathValid = false;
        int x = 0;
        int y = 0;
        int width = 60;
        int height = 40;

        QSettings *settings;

        QMediaPlayer *player;
        QAudioOutput *audioOutput;
};

#endif // BUTTONHOLE_H
