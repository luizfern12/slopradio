#include "configdialog.h"
#include "ui_configdialog.h"
#include "videomixer.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QProcess>
#include <QMessageBox>

ConfigDialog::ConfigDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConfigDialog)
{
    ui->setupUi(this);
    setFixedSize(size());

    QCoreApplication::setOrganizationName("LaraRadio");
    QCoreApplication::setApplicationName("LaraRadio");
    QSettings settings;

    ui->fadeInOut->setValue( settings.value("volume/TransitionAudioTime").toInt() );
    ui->speedFade->setValue( settings.value("volume/speedFade").toInt() );

    ui->music_path->setText( settings.value("files/defaultDir").toString() );
    ui->jingle_path->setText( settings.value("files/jingleDir").toString() );
    ui->time_path->setText( settings.value("files/audioTimeDir").toString() );

    ui->sayClock->setChecked( settings.value("volume/sayClock").toBool() );
    ui->sayClockFade->setChecked( settings.value("volume/sayClockFade").toBool() );

    ui->stopFade->setChecked( settings.value("volume/stopFade").toBool() );
    ui->talkFade->setChecked( settings.value("volume/talkFade").toBool() );

    // video tab: transition effect list (built-ins + custom shader folder)
    const QString shaderDir = settings.value("video/shaderDir").toString();
    const QList<VideoMixer::Effect> effects = VideoMixer::availableEffects(shaderDir);
    for (const VideoMixer::Effect &e : effects)
        ui->video_transition->addItem(e.displayName, e.id);
    ui->video_shaderDir->setText(shaderDir);

    const QString wantedEffect = settings.value("video/transition", "crossfade").toString();
    const int idx = ui->video_transition->findData(wantedEffect);
    ui->video_transition->setCurrentIndex(idx >= 0 ? idx : 0);

    // video tab: hardware decode backend (Qt FFmpeg). "auto" leaves the env
    // var unset (Qt picks the first working backend); an empty value would
    // disable HW decode, so the empty string is never used.
    ui->video_hwdecode->addItem(tr("Automático (recomendado)"), QStringLiteral("auto"));
    ui->video_hwdecode->addItem(tr("VA-API — AMD e Intel"), QStringLiteral("vaapi"));
    ui->video_hwdecode->addItem(tr("CUDA — NVIDIA"), QStringLiteral("cuda"));
    ui->video_hwdecode->addItem(tr("Desligada (CPU)"), QStringLiteral("off"));
    const QString hwMode = settings.value("video/hwdecode", QStringLiteral("auto")).toString();
    const int hidx = ui->video_hwdecode->findData(hwMode);
    ui->video_hwdecode->setCurrentIndex(hidx >= 0 ? hidx : 0);

    populateOutputDevices( settings.value("audio/outputDevice").toByteArray(),
                           settings.value("audio/cueDevice").toByteArray() );

    // keep the device lists in sync while the dialog is open (hot-plug)
    QMediaDevices *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            [this]() {
                populateOutputDevices(ui->output_device->currentData().toByteArray(),
                                      ui->cue_device->currentData().toByteArray());
            });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

ConfigDialog::~ConfigDialog()
{
    delete ui;
}

void ConfigDialog::accept()
{
    settings.setValue("volume/TransitionAudioTime", ui->fadeInOut->value());
    settings.setValue("volume/speedFade", ui->speedFade->value());
    settings.setValue("files/defaultDir", ui->music_path->text());
    settings.setValue("files/jingleDir", ui->jingle_path->text());
    settings.setValue("files/audioTimeDir", ui->time_path->text());
    settings.setValue("volume/sayClock", ui->sayClock->isChecked());
    settings.setValue("volume/sayClockFade", ui->sayClockFade->isChecked());
    settings.setValue("volume/stopFade", ui->stopFade->isChecked());
    settings.setValue("volume/talkFade", ui->talkFade->isChecked());
    settings.setValue("audio/outputDevice", ui->output_device->currentData().toByteArray());
    settings.setValue("audio/cueDevice", ui->cue_device->currentData().toByteArray());
    settings.setValue("video/transition", ui->video_transition->currentData().toString());
    settings.setValue("video/shaderDir", ui->video_shaderDir->text());
    settings.setValue("video/hwdecode", ui->video_hwdecode->currentData().toString());

    this->close();
}

void ConfigDialog::on_btn_searchMusicPath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Selecionar pasta de músicas"), QDir::homePath());
    if (!dir.isEmpty()) {
        ui->music_path->setText(dir);
    }
}

void ConfigDialog::on_btn_searchJinglePath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Selecionar pasta de vinhetas"), QDir::homePath());
    if (!dir.isEmpty()) {
        ui->jingle_path->setText(dir);
    }
}

void ConfigDialog::on_btn_searchTimePath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Selecionar pasta de Locução de Hora"), settings.value("files/audioTimeDir", QDir::homePath()).toString());
    if (!dir.isEmpty()) {
        ui->time_path->setText(dir);
    }
}

void ConfigDialog::on_btn_searchShaderDir_clicked()
{
    QString start = ui->video_shaderDir->text().isEmpty()
        ? QDir::homePath() : ui->video_shaderDir->text();
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Selecionar pasta de shaders do vídeo"), start);
    if (!dir.isEmpty()) {
        ui->video_shaderDir->setText(dir);
    }
}

void ConfigDialog::on_btn_checkVAAPI_clicked()
{
    QProcess proc;
    proc.start(QStringLiteral("vainfo"), QStringList());
    if (!proc.waitForStarted(3000)) {
        QMessageBox::information(this, tr("VA-API"),
            tr("O comando 'vainfo' não foi encontrado.\n"
               "Instale o pacote libva-utils (ou equivalente).\n\n"
               "Usuários NVIDIA podem obter suporte VA-API via nvidia-vaapi-driver\n"
               "(ou simplesmente escolher CUDA — nesse caso é normal não haver VA-API)."));
        return;
    }
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        QMessageBox::warning(this, tr("VA-API"),
                             tr("'vainfo' não respondeu (timeout de 5 segundos)."));
        return;
    }
    const QString out =
        QString::fromLocal8Bit(proc.readAllStandardOutput() + "\n"
                               + proc.readAllStandardError())
            .trimmed();
    QMessageBox box(this);
    box.setWindowTitle(tr("VA-API"));
    box.setText(tr("'vainfo' terminou com código %1.").arg(proc.exitCode())
                + (out.isEmpty() ? tr("\n(sem saída)") : QString()));
    box.setDetailedText(out);
    box.exec();
}

void ConfigDialog::populateOutputDevices(const QByteArray &selectId, const QByteArray &selectCueId)
{
    fillDeviceCombo(ui->output_device, selectId, tr("Padrão do sistema"));
    fillDeviceCombo(ui->cue_device, selectCueId, tr("Usar saída principal"));
}

void ConfigDialog::fillDeviceCombo(QComboBox *combo, const QByteArray &selectId, const QString &defaultLabel)
{
    combo->clear();
    combo->addItem(defaultLabel, QByteArray());

    int selectIndex = selectId.isEmpty() ? 0 : -1;

    const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : devices) {
        combo->addItem(device.description(), device.id());
        if (device.id() == selectId)
            selectIndex = combo->count() - 1;
    }

    // saved device is gone: keep the first entry selected
    if (selectIndex >= 0)
        combo->setCurrentIndex(selectIndex);
}
