#include "configdialog.h"
#include "ui_configdialog.h"
#include <QMediaDevices>
#include <QAudioDevice>

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

    populateOutputDevices( settings.value("audio/outputDevice").toByteArray() );

    // keep the device list in sync while the dialog is open (hot-plug)
    QMediaDevices *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            [this]() { populateOutputDevices(ui->output_device->currentData().toByteArray()); });

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

void ConfigDialog::populateOutputDevices(const QByteArray &selectId)
{
    ui->output_device->clear();
    ui->output_device->addItem(tr("Padrão do sistema"), QByteArray());

    int selectIndex = selectId.isEmpty() ? 0 : -1;

    const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : devices) {
        ui->output_device->addItem(device.description(), device.id());
        if (device.id() == selectId)
            selectIndex = ui->output_device->count() - 1;
    }

    // saved device is gone: keep the system default selected
    if (selectIndex >= 0)
        ui->output_device->setCurrentIndex(selectIndex);
}
