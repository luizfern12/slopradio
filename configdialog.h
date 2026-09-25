#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include <QDialog>
#include <QSettings>
#include <QCoreApplication>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QByteArray>

namespace Ui {
class ConfigDialog;
}

class QComboBox;

class ConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigDialog(QWidget *parent = nullptr);
    ~ConfigDialog();

public slots:
    void accept();
    //void openFileDialog(QMouseEvent event);

private slots:
    void on_btn_searchMusicPath_clicked();
    void on_btn_searchJinglePath_clicked();
    void on_btn_searchTimePath_clicked();
    void on_btn_searchShaderDir_clicked();
    void on_btn_checkVAAPI_clicked();

private:
    void populateOutputDevices(const QByteArray &selectId, const QByteArray &selectCueId);
    void fillDeviceCombo(QComboBox *combo, const QByteArray &selectId, const QString &defaultLabel);

    Ui::ConfigDialog *ui;

    QSettings settings;
};

#endif // CONFIGDIALOG_H
