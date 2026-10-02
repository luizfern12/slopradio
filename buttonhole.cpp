#include "buttonhole.h"

#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QFileDialog>

ButtonHole::ButtonHole(QWidget *parent) : QWidget(parent)
{
    player = new QMediaPlayer;
    audioOutput = new QAudioOutput;

    player->setAudioOutput(audioOutput);
    audioOutput->setVolume(1.0f);
    audioOutput->setDevice(AudioPlayer::configuredAudioDevice());

    settings = new QSettings("LaraRadio", "LaraRadio", this);

    button = new QPushButton("", this);
    button->resize( width, height );

    button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(button, &QPushButton::customContextMenuRequested, this, [=](const QPoint& pos) { bntContextMenu(pos); });
    connect(button, &QPushButton::clicked, this, [=]() { buttonHoleClick(); });

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ButtonHole::flash);
    timer->start(300);
}

void ButtonHole::setPositon(int newX, int newY)
{
    //button->setGeometry(newX, newY, 60, 60);
}

void ButtonHole::setBtnText(QString newText)
{
    text = newText;
    // the settings key is derived from the label
    invalidateAssignedPath();
    button->setText( text );
}

QString ButtonHole::assignedPath() const
{
    if (!m_assignedPathValid) {
        m_assignedPath = settings->value("buttonhole/btn_" + text).toString();
        m_assignedPathValid = true;
    }
    return m_assignedPath;
}

void ButtonHole::invalidateAssignedPath()
{
    m_assignedPathValid = false;
    m_assignedPath.clear();
}

void ButtonHole::setAudioDevice(const QAudioDevice &device)
{
    audioOutput->setDevice(device);
}

void ButtonHole::bntContextMenu(QPoint pos)
{
    QMenu *menu = new QMenu(this);

    QAction* loadAudio = new QAction(QIcon(":/images/icons/go-bottom.svg"), "Carregar Áudio", this);
    QAction* playAudio = new QAction(QIcon(":/images/icons/media-playback-start.svg"), "Tocar Áudio", this);
    QAction* stopAudio = new QAction(QIcon(":/images/icons/media-playback-stop.svg"), "Parar Áudio", this);
    QAction* deleteAudio = new QAction(QIcon(":/images/icons/edit-delete.svg"), "Apagar Áudio", this);
    menu->addAction( loadAudio );
    menu->addAction( playAudio );
    menu->addAction( stopAudio );
    menu->addSeparator();
    menu->addAction( deleteAudio );
    menu->popup(button->mapToGlobal(pos));

    connect(loadAudio, &QAction::triggered, this, [=]() {
        QString filename = QFileDialog::getOpenFileName(this, tr("Carregar Audio"), QDir::homePath(), tr("Arquivos de Audio")+" (*.mp3 *.wav *.ogg *.flac *.mp4);;");

        if (!filename.isEmpty()) {
            settings->setValue("buttonhole/btn_"+text, filename);
            invalidateAssignedPath();
            button->setStyleSheet("background-color: #fc0; color: #000;");
        }
    });

    connect(deleteAudio, &QAction::triggered, this, [=]() {
        settings->setValue("buttonhole/btn_"+text, "");
        invalidateAssignedPath();
        button->setStyleSheet("");
    });

    connect(playAudio, &QAction::triggered, this, [=]() {
        buttonHoleClick();
    });

    connect(stopAudio, &QAction::triggered, this, [=]() {
        buttonHoleStop();
    });
}

void ButtonHole::buttonHoleClick()
{
    // /home/gutierre69/Documentos/Studio/EFEITOS/Brasil_sil_sil.mp3
    filename = assignedPath();
    if(filename=="")
        return;

    player->stop();
    player->setSource( QUrl::fromLocalFile( filename ) );
    player->play();
}

void ButtonHole::buttonHoleStop()
{
    player->stop();
}

void ButtonHole::buttonHoleKeyPress()
{
    if(player->isPlaying()) buttonHoleStop(); else buttonHoleClick();
}

void ButtonHole::flash()
{
    if(player->isPlaying()) {
        if(button->styleSheet()!="") {
            button->setStyleSheet("");
        } else {
            button->setStyleSheet("background-color: #fc0; color: #000;");
        }
        return;
    }

    // Only touch the stylesheet when the result actually differs. Calling
    // setStyleSheet() re-parses the rule set and repolishes the widget, so
    // doing it unconditionally made all 10 buttons repaint every 300 ms
    // just to write the same string back.
    const QString style =
        !assignedPath().isEmpty()
            ? QStringLiteral("background-color: #fc0; color: #000;")
            : QString();
    if (button->styleSheet() != style)
        button->setStyleSheet(style);
}

void ButtonHole::keyPressEvent(QKeyEvent *event){
    if(event->key() == Qt::Key_1) qDebug() << "btn 1";
}
