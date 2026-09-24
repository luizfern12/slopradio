#ifndef VIDEOWINDOW_H
#define VIDEOWINDOW_H

#include <QWidget>
#include <QCloseEvent>

class AudioPlayer;
class VideoMixer;

namespace Ui {
class VideoWindow;
}

// Separate window that shows the video mix. Independent from the main window:
// it can be moved to a second monitor (e.g. captured by OBS) and toggled with
// the "Vídeo" button in the main window.
class VideoWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWindow(QWidget *parent = nullptr);
    ~VideoWindow() override;

    void setDeck(int index, AudioPlayer *player);
    void setIncomingDeck(int index);
    void attachToPlayers(AudioPlayer *player1, AudioPlayer *player2);
    VideoMixer *mixer() const;

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void restoreGeometryFromSettings();

    Ui::VideoWindow *ui;
    VideoMixer *m_mixer = nullptr;
};

#endif // VIDEOWINDOW_H