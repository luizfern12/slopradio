#include "cuewindow.h"
#include "ui_cuewindow.h"

CueWindow::CueWindow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CueWindow)
{
    ui->setupUi(this);

    // stay visible above the main window while monitoring, without the
    // context-help button QDialog puts on X11 windows
    setWindowFlags((windowFlags() | Qt::WindowStaysOnTopHint) & ~Qt::WindowContextHelpButtonHint);

    ui->btn_play->setIcon(QIcon(":/images/icons/media-playback-start.svg"));
    ui->btn_stop->setIcon(QIcon(":/images/icons/media-playback-stop.svg"));

    connect(&m_player, &AudioPlayer::update_position, this, &CueWindow::updatePosition);
    connect(&m_player, &AudioPlayer::playbackFinished, this, &CueWindow::onPlaybackFinished);
}

CueWindow::~CueWindow()
{
    delete ui;
}

void CueWindow::loadAndPlay(const QString &path, const QString &displayName)
{
    m_player.Stop();
    m_player.addMedia(path);

    // bypass the fade logic: the cue player plays at the slider's level
    applyVolume(ui->cue_volume->value());

    ui->trackName->setText(displayName);
    setWindowTitle(tr("Pré Escuta") + " — " + displayName);
    ui->seeker->setValue(0);
    refreshTime();

    m_player.Play();
    updateTransportIcon();
}

void CueWindow::setOutputDevice(const QAudioDevice &device)
{
    m_player.setAudioDevice(device);
}

void CueWindow::closeEvent(QCloseEvent *event)
{
    // closing the window is an explicit stop of the preview
    m_player.Stop();
    QDialog::closeEvent(event);
}

void CueWindow::on_btn_play_clicked()
{
    if (m_player.isPlaying()) {
        m_player.Pause();
    } else if (m_player.isPaused()) {
        m_player.Play();
    } else {
        // stopped: restart from the beginning
        m_player.Seek(0);
        applyVolume(ui->cue_volume->value());
        m_player.Play();
    }
    updateTransportIcon();
}

void CueWindow::on_btn_stop_clicked()
{
    m_player.Stop();
    m_player.Seek(0);
    refreshTime();
    updateTransportIcon();
}

void CueWindow::on_seeker_sliderReleased()
{
    m_player.Seek(ui->seeker->value());
}

void CueWindow::on_cue_volume_valueChanged(int value)
{
    applyVolume(value);
}

void CueWindow::updatePosition(qint64 position)
{
    const qint64 total = m_player.getDuration();

    ui->seeker->setMaximum(total > 0 ? (int)total : 0);
    ui->seeker->setValue((int)position);

    auto format = [](qint64 ms) {
        const qint64 seconds = (ms / 1000) % 60;
        const qint64 minutes = (ms / 1000) / 60;
        return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    };
    ui->timeLabel->setText(format(position) + " / " + format(total));
}

void CueWindow::onPlaybackFinished()
{
    m_player.Seek(0);
    refreshTime();
    updateTransportIcon();
}

void CueWindow::applyVolume(int percent)
{
    const float volume = percent / 100.0f;

    // keep maxVolume in sync so the shared fade() timer leaves the
    // level alone (it drains any player whose volume exceeds maxVolume)
    m_player.maxVolume = volume;
    m_player.setVolume(volume);
}

void CueWindow::refreshTime()
{
    updatePosition(m_player.getPosition());
}

void CueWindow::updateTransportIcon()
{
    if (m_player.isPlaying())
        ui->btn_play->setIcon(QIcon(":/images/icons/media-playback-pause.svg"));
    else
        ui->btn_play->setIcon(QIcon(":/images/icons/media-playback-start.svg"));
}
