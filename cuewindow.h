#ifndef CUEWINDOW_H
#define CUEWINDOW_H

#include <QDialog>
#include <QCloseEvent>
#include "audioplayer.h"

namespace Ui {
class CueWindow;
}

class CueWindow : public QDialog
{
    Q_OBJECT

public:
    explicit CueWindow(QWidget *parent = nullptr);
    ~CueWindow();

    void loadAndPlay(const QString &path, const QString &displayName);
    void setOutputDevice(const QAudioDevice &device);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_btn_play_clicked();
    void on_btn_stop_clicked();
    void on_seeker_sliderPressed();
    void on_seeker_sliderReleased();
    void on_cue_volume_valueChanged(int value);
    void updatePosition(qint64 position);
    void onPlaybackFinished();

private:
    void applyVolume(int percent);
    void refreshTime();
    void updateTimeLabel(qint64 position, qint64 total);
    void updateTransportIcon();

    Ui::CueWindow *ui;
    AudioPlayer m_player;

    // true while the user is dragging the seeker: the slider is then the
    // source of truth and the position feed must not write over it
    bool m_seeking = false;
};

#endif // CUEWINDOW_H
