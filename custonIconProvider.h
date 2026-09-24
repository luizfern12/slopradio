#ifndef CUSTONICONPROVIDER_H
#define CUSTONICONPROVIDER_H

#include <QApplication>
#include <QTreeView>
#include <QFileSystemModel>
#include <QFileIconProvider>
#include <QIcon>

class CustomIconProvider : public QFileIconProvider {
public:
    QIcon icon(const QFileInfo &info) const override {
        if (info.isDir()) {
            return QIcon(":/images/icons/folder.png"); // Ícone de pasta do sistema
        }

        QString suffix = info.suffix().toLower();
        if (suffix == "mp3" || suffix == "wav" || suffix == "flac" || suffix == "ogg" || suffix == "aac") {
            return QIcon(":/images/icons/audio-x-generic.png"); // Ícone de áudio
        }

        if (suffix == "mp4" || suffix == "mkv" || suffix == "webm" || suffix == "mov"
            || suffix == "avi" || suffix == "m4v") {
            // Ícone de vídeo (tema do sistema, com fallback genérico)
            return QIcon::fromTheme("video-x-generic",
                                    QIcon(":/images/icons/system-component-codecs.png"));
        }

        return QFileIconProvider::icon(info); // Ícone padrão
    }
};

#endif // CUSTONICONPROVIDER_H
