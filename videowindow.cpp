#include "videowindow.h"
#include "ui_videowindow.h"
#include "videomixer.h"
#include "audioplayer.h"

#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QVBoxLayout>

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::VideoWindow)
{
    ui->setupUi(this);

    setWindowTitle(tr("Saída de Vídeo"));
    setWindowFlags(Qt::Window);

    m_mixer = new VideoMixer(this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_mixer);

    restoreGeometryFromSettings();
}

VideoWindow::~VideoWindow()
{
    delete ui;
}

void VideoWindow::setDeck(int index, AudioPlayer *player)
{
    if (m_mixer)
        m_mixer->setDeck(index, player);
}

void VideoWindow::setIncomingDeck(int index)
{
    if (m_mixer)
        m_mixer->setIncomingDeck(index);
}

void VideoWindow::attachToPlayers(AudioPlayer *player1, AudioPlayer *player2)
{
    setDeck(1, player1);
    setDeck(2, player2);
}

VideoMixer *VideoWindow::mixer() const
{
    return m_mixer;
}

void VideoWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings("LaraRadio", "LaraRadio");
    settings.setValue("video/windowGeometry", saveGeometry());
    emit closed();
    QWidget::closeEvent(event);
}

void VideoWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_mixer)
        m_mixer->refreshIncomingDeck();
}

void VideoWindow::restoreGeometryFromSettings()
{
    QSettings settings("LaraRadio", "LaraRadio");
    const QByteArray geometry = settings.value("video/windowGeometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(960, 540);
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen)
            move(screen->availableGeometry().center() - rect().center());
    }
}