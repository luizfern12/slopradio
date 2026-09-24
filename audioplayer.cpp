#include <QtWidgets>
#include <QMediaPlayer>
#include "audioplayer.h"
#include <QDebug>
#include <QTimer>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QMediaDevices>

AudioPlayer::AudioPlayer()
{
    current_length = 0;
    player = new QMediaPlayer;
    audioOutput = new QAudioOutput;
    audioOutput->setDevice(configuredAudioDevice());

    player->setAudioOutput(audioOutput);
    audioOutput->setVolume(1.0);

    connect(player, &QMediaPlayer::positionChanged, this, &AudioPlayer::positionChanged);
    connect(player, &QMediaPlayer::errorOccurred, this, &AudioPlayer::onPlayerError);
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [=](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            emit playbackFinished();
    });

    QTimer *fadeTimer = new QTimer(this);
    connect(fadeTimer, &QTimer::timeout, this, &AudioPlayer::fade);
    fadeTimer->start(500);
}

AudioPlayer::~AudioPlayer()
{
    delete player;
    delete audioOutput;
}

void AudioPlayer::Reset()
{
    player->stop();
    player->setSource(QUrl());
    _fadeOut = false;
    _fadeIn = false;
    isFading = false;
    fadeFactor = 0.1f;
    maxVolume = 1.0f;
    current_length = 0;
    m_hasError = false;
}

void AudioPlayer::Play()
{
    if (!cleanFilePath.isEmpty())
        player->play();
}

void AudioPlayer::Stop()
{
    player->stop();
}

void AudioPlayer::addMedia(QString file)
{
    cleanFilePath = transcodeIfNeeded(file);
    player->setSource(QUrl::fromLocalFile(cleanFilePath));
}

QString AudioPlayer::transcodeIfNeeded(const QString &file)
{
    if (!file.endsWith(".mp3", Qt::CaseInsensitive))
        return file;

    QString tmpWav = QDir::tempPath() + "/laradio_"
        + QString::number(qAbs(QFileInfo(file).lastModified().toMSecsSinceEpoch()
            ^ QFileInfo(file).size()))
        + ".wav";
    if (!QFileInfo::exists(tmpWav)) {
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {
            "-y", "-v", "error",
            "-i", file,
            "-vn",
            "-acodec", "pcm_s16le",
            "-f", "wav",
            tmpWav
        });
        ffmpeg.waitForFinished(30000);
        if (ffmpeg.exitCode() == 0)
            return tmpWav;
    } else {
        return tmpWav;
    }
    return file;
}

void AudioPlayer::positionChanged(qint64 position)
{
    emit update_position(position);
}

void AudioPlayer::onPlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    m_hasError = true;
    qWarning() << "AudioPlayer error:" << error << errorString;
    emit mediaError(player->source().toLocalFile(), errorString);
}

void AudioPlayer::Seek(int mseconds)
{
    player->setPosition(mseconds);
}

qint64 AudioPlayer::getPosition() { return player->position(); }
qint64 AudioPlayer::getDuration() { return player->duration(); }
qint64 AudioPlayer::remainingTime() { return player->duration() - player->position(); }
bool AudioPlayer::isPlaying() { return player->playbackState() == QMediaPlayer::PlayingState; }
bool AudioPlayer::isPaused() { return player->playbackState() == QMediaPlayer::PausedState; }
bool AudioPlayer::isStopped() { return player->playbackState() == QMediaPlayer::StoppedState; }

qreal AudioPlayer::getVolume() { return audioOutput->volume(); }
void AudioPlayer::setVolume(float volume) { audioOutput->setVolume(volume); }

void AudioPlayer::setAudioDevice(const QAudioDevice &device)
{
    audioOutput->setDevice(device);
}

void AudioPlayer::fadeOut() { if(isPlaying()) _fadeOut = true; }
void AudioPlayer::fadeIn() { if(isStopped()) _fadeIn = true; }

void AudioPlayer::fade()
{
    if(_fadeOut || (isPlaying() && getVolume()>maxVolume && _fadeIn==false )) { _fadeIn=false; isFading = true; setVolume( getVolume() - fadeFactor ); }
    if(_fadeIn || (isPlaying() && getVolume()<maxVolume && _fadeOut==false )) { _fadeOut=false; isFading = true; setVolume( getVolume() + fadeFactor ); }

    if(_fadeOut && getVolume()<=0 && isPlaying()) { Stop(); _fadeOut = false; isFading = false; }
    if(getVolume()>=maxVolume && isPlaying()) { _fadeIn = false; isFading = false; }

    if(getVolume()>0 && isStopped()) { _fadeIn = false; _fadeIn = true; isFading = false; maxVolume = 0; setVolume(0); }
}

void AudioPlayer::setBuffer(QAudioBufferOutput *output)
{
    audioBufferOutput = output;
    player->setAudioBufferOutput(output);
}

bool AudioPlayer::isValidMediaFile(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable())
        return false;

    QProcess ffprobe;
    ffprobe.start("ffprobe", {
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        path
    });
    ffprobe.waitForFinished(5000);

    if (ffprobe.exitCode() != 0)
        return false;

    QString out = ffprobe.readAllStandardOutput().trimmed();
    if (out.isEmpty())
        return false;

    bool ok = false;
    out.toDouble(&ok);
    return ok;
}

QAudioDevice AudioPlayer::configuredAudioDevice()
{
    QSettings settings("LaraRadio", "LaraRadio");
    const QByteArray wantedId = settings.value("audio/outputDevice").toByteArray();

    // saved device still plugged in?
    if (!wantedId.isEmpty()) {
        const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
        for (const QAudioDevice &device : devices) {
            if (device.id() == wantedId)
                return device;
        }
    }

    // nothing saved (or the saved device is gone): system default
    return QMediaDevices::defaultAudioOutput();
}
