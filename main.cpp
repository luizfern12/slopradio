#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QTranslator>
#include <QSplashScreen>
#include <QTimer>
#include <QObject>
#include <QSharedMemory>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>
#include <csignal>
#include <cstdlib>

// Signal handler for fatal crashes — shows a dialog so the app doesn't silently die
static void crashHandler(int sig)
{
    static bool crashed = false;
    if (crashed) _exit(128 + sig);  // prevent loops
    crashed = true;

    // Can't use Qt GUI from signal handler, but fprintf + abort is better than silent SIGSEGV
    fprintf(stderr, "\n*** LaraRadio crashed (signal %d) ***\n", sig);
    fprintf(stderr, "Unexpected fatal error — possibly a bug in LaraRadio, Qt or the media stack.\n");
    fprintf(stderr, "If it happens again, note what was playing and report the log output above.\n");
    fflush(stderr);

    _exit(128 + sig);
}

int main(int argc, char *argv[])
{
    // Use GStreamer backend (FFmpeg backend has mp3float decoder crash bug)
    // Hybrid approach: QMediaPlayer (FFmpeg/muted) for UI + ffplay child process for audio
    // qputenv("QT_MEDIA_BACKEND", QByteArray("gstreamer"));
    // qputenv("GST_AUDIOSINK", QByteArray("alsasink"));

    // Install crash handlers so a fatal crash prints a message instead of dying silently
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);

    // qputenv("QT_QPA_PLATFORMTHEME", QByteArray("gtk3"));
    // qputenv("GTK_THEME", QByteArray("Adwaita:dark"));
    // qputenv("QT_QUICK_CONTROLS_STYLE", QByteArray("org.kde.desktop"));
    // qputenv("KDE_COLOR_SCHEME", QByteArray("Dark"));

    QApplication a(argc, argv);

    QSharedMemory sharedMemory;
    sharedMemory.setKey("com.radiotools.lararadio.singleinstance");

    if (!sharedMemory.create(1)) {
        return 0;
    }

    QCoreApplication::setOrganizationName("LaraRadio");
    QCoreApplication::setApplicationName("LaraRadio");

    // Video hardware decode backend (Qt FFmpeg) — applied now, before any
    // QMediaPlayer exists. "auto" leaves the env var UNSET (Qt picks the first
    // working backend); note that setting it to an empty string would actually
    // DISABLE hw decode, so we only ever set it for the explicit modes.
    QSettings hwSettings;
    const QString hwMode = hwSettings.value("video/hwdecode", QStringLiteral("auto")).toString();
    if (hwMode == QLatin1String("vaapi"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray("vaapi"));
    else if (hwMode == QLatin1String("cuda"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray("cuda"));
    else if (hwMode == QLatin1String("off"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray(","));

    // QTranslator translator;
    // translator.load(":/languages/en_US.qm");
    // a.installTranslator(&translator);

    MainWindow w;

    QPixmap pixmap(":/images/splash-02.png");
    QPixmap pixmapForSplash = pixmap.scaled(500, 300);
    QSplashScreen splash(pixmapForSplash);


    QStringList styles = QStyleFactory::keys();

    if (styles.contains("gtk3", Qt::CaseInsensitive)) {
        a.setStyle(QStyleFactory::create("gtk3"));
    } else {
        a.setStyle(QStyleFactory::create("Fusion"));

        QPalette darkPalette;

        // Cores baseadas no tema Adwaita Dark
        QColor backgroundColor(48, 48, 48);
        QColor baseColor(36, 36, 36);
        QColor textColor(220, 220, 220);
        QColor highlightColor(85, 170, 255);
        QColor disabledTextColor(127, 127, 127);
        QColor buttonColor(64, 64, 64);

        darkPalette.setColor(QPalette::Window, backgroundColor);
        darkPalette.setColor(QPalette::WindowText, textColor);
        darkPalette.setColor(QPalette::Base, baseColor);
        darkPalette.setColor(QPalette::AlternateBase, backgroundColor);
        darkPalette.setColor(QPalette::ToolTipBase, textColor);
        darkPalette.setColor(QPalette::ToolTipText, textColor);
        darkPalette.setColor(QPalette::Text, textColor);
        darkPalette.setColor(QPalette::Button, buttonColor);
        darkPalette.setColor(QPalette::ButtonText, textColor);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, highlightColor);
        darkPalette.setColor(QPalette::Highlight, highlightColor);
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);

        // Desabilitados
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabledTextColor);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledTextColor);

        a.setPalette(darkPalette);
        a.setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
    }

    splash.show();
    a.processEvents();

    // Simulate some work
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        splash.finish(&w);
        splash.close();
        w.show();
    });
    timer.start(2000);

    return a.exec();
}
