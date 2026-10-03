#ifndef VIDEOMIXER_H
#define VIDEOMIXER_H

#include <QOpenGLWidget>
#include <QVideoFrame>
#include <QList>
#include <QSet>
#include <QString>
#include <QElapsedTimer>
#include <memory>

class AudioPlayer;
class SpectrumAnalyzer;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLVertexArrayObject;
class QOpenGLBuffer;
class QOpenGLFramebufferObject;
class QTimer;
class QElapsedTimer;

// VideoMixer blends the two "deck" players with a GLSL transition effect.
//
// The scene is two textures: the outgoing deck ("from") and the incoming
// deck ("to"). Each effect is a (vertex, fragment) shader pair sharing a
// gl-transitions-like interface:
//     uniform sampler2D from;  // unit 0
//     uniform sampler2D to;    // unit 1
//     uniform float    progress; // 0..1, follows the audio crossfade
//     uniform float    ratio;    // widget width / height
//     uniform float    seed;     // random, one value per transition
//     uniform float    time;     // seconds since the transition started
//     uniform vec2     center;   // vec2(0.5) — convenience, set if present
//
// progress is derived from the decks' live audio volumes, so the visual mix
// follows the audio crossfade produced by AudioPlayer::fade(). When there is
// no fade to follow (the outgoing deck hard-stopped at EndOfMedia), an eased
// 1.5 s ramp takes over so every track change still animates.
class VideoMixer : public QOpenGLWidget
{
    Q_OBJECT

public:
    struct Effect
    {
        QString id;          // unique id (file base name)
        QString displayName;
        QString fragmentPath; // ":/shaders/..." or absolute file path
        QString vertexPath;   // optional; falls back to the default quad
    };

    explicit VideoMixer(QWidget *parent = nullptr);
    ~VideoMixer() override;

    // deck index is 1 or 2, matching audioplayer1 / audioplayer2
    void setDeck(int index, AudioPlayer *player);
    // which deck is currently fading in (becomes the "to" texture)
    void setIncomingDeck(int index);
    void refreshIncomingDeck();

    void setEffects(const QList<Effect> &effects);
    void setCurrentEffect(const QString &id);
    QString currentEffectId() const { return m_currentEffect; }
    QList<Effect> effects() const { return m_effects; }

    static QList<Effect> availableEffects(const QString &customShaderDir);

    // EQ visualizer: what to show on the video output when the incoming
    // deck is playing audio with no video.
    //
    // This table is the single source of truth. The persisted setting
    // (video/eqvisualizer), the combo in the settings dialog, the fragment
    // shader linked for the current mode and the "draw the EQ rather than
    // an effect" gate all read from it, so adding a visualizer costs one
    // row here, one shaders/eq*.frag and one resources.qrc line.
    //
    // `id` is what lands in the settings and must never change once
    // shipped. It doubles as the opt-out: an id this build does not know
    // resolves to null (i.e. off), which is what an unconfigured or
    // upgraded installation stores anyway.
    //
    // `label` is a raw literal rather than a QString because the table is
    // built before Qt loads any translations, and because lupdate can only
    // extract a string it can actually see: QT_TRANSLATE_NOOP leaves the
    // text untouched while still registering it under the VideoMixer
    // context. ConfigDialog translates it with
    // QCoreApplication::translate("VideoMixer", ...).
    struct EqVisualizer
    {
        QString     id;
        const char *label;
        QString     fragPath; // ":/shaders/eq....frag"
    };
    static const QList<EqVisualizer> &eqVisualizers();
    static const EqVisualizer *eqVisualizerFor(const QString &id);

    // The analyzer is not owned here — it lives in MainWindow and is fed from
    // the same audio buffers that drive the VU meters.
    void setSpectrumAnalyzer(SpectrumAnalyzer *analyzer);
    void setEqVisualizer(const QString &id);
    QString eqVisualizerId() const { return m_eqId; }
    // The visualizer to draw, or null when it is off/unknown — also the
    // gate renderScene() uses to pick this pass over an effect.
    const EqVisualizer *eqVisualizer() const { return eqVisualizerFor(m_eqId); }

    // current visual mix progress, 0..1 (exposed for tests/diagnostics)
    float progress() const { return m_smoothProgress; }

    QSize sizeHint() const override { return QSize(960, 540); }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void ensureTimer();

    // Repaint at the display's refresh rate, and pace the analyzer to match.
    void syncFrameRate();

    void uploadFrames();
    void renderScene();

    // EQ pass. uploadEqRow() pushes the latest band levels into a 1-row
    // texture; renderEq() draws them as bars.
    void uploadEqRow();
    void renderEq();
    bool ensureEqProgram();

    bool ensureProgram();
    bool rebuildProgram(const Effect &effect);
    bool effectExists(const QString &id) const;
    QString readSource(const QString &path) const;

    // Hardware/planar decode fast path. Frames delivered as NV12 or YUV420P
    // (e.g. VA-API / CUDA hw decode) are uploaded plane-by-plane and converted
    // to RGB on the GPU instead of going through QVideoFrame::toImage() on the
    // CPU.
    bool uploadYuvPlanes(int deck, QVideoFrame &frame);
    bool convertYuvFrames();
    bool ensureYuvProgram();
    GLuint sceneTextureId(int deck) const;

    AudioPlayer *m_decks[2] = {nullptr, nullptr};
    int m_incoming = 1;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLTexture> m_tex[2];
    std::unique_ptr<QOpenGLTexture> m_blackTex;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
    std::unique_ptr<QOpenGLBuffer> m_vbo;

    // YUV decode path: per-deck luma/chroma plane textures, the RGB output
    // FBO (one per deck, at video resolution) and the converter program.
    std::unique_ptr<QOpenGLTexture> m_planeY[2];   // R8  luma, full size
    std::unique_ptr<QOpenGLTexture> m_planeUV[2];  // RG8 NV12 interleaved CbCr
    std::unique_ptr<QOpenGLTexture> m_planeU[2];   // R8  planar Cb
    std::unique_ptr<QOpenGLTexture> m_planeV[2];   // R8  planar Cr
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo[2];
    std::unique_ptr<QOpenGLShaderProgram> m_yuvProgram;
    QSize m_yuvSize[2];            // video size of the last uploaded yuv frame
    int m_yuvMode[2] = {0, 0};     // 0 = RGB path, 1 = NV12, 2 = planar
    int m_yuvMatrix[2] = {0, 0};   // 0 = BT.601, 1 = BT.709
    int m_yuvFull[2] = {0, 0};     // 0 = limited range, 1 = full range
    bool m_yuvDirty[2] = {false, false};

    QList<Effect> m_effects;
    QString m_currentEffect;
    QString m_loadedId;
    QSet<QString> m_failedEffects; // ids that failed to compile (never retry)

    // EQ: analyzer owned by MainWindow, level texture + program here.
    // m_eqId is the *resolved* persisted id, so "off" is stored for a
    // value this build does not recognise and eqVisualizer() is null.
    SpectrumAnalyzer *m_analyzer = nullptr;
    QString m_eqId = QStringLiteral("off");
    std::unique_ptr<QOpenGLTexture> m_eqTex;
    std::unique_ptr<QOpenGLShaderProgram> m_eqProgram;
    quint64 m_eqLastSeq = 0;    // last frame uploaded, 0 = nothing yet
    // Fragment path m_eqProgram was linked from, so switching modes relinks,
    // and the per-path failures that were reported once and never retried.
    QString m_eqFragPath;
    QString m_eqFailedFrag;

    qint64 m_lastFrameStart[2] = {-1, -1};
    QVideoFrame m_latest[2];

    float m_targetProgress = 1.0f;
    float m_smoothProgress = 1.0f;
    float m_seed = 0.42f;
    qint64 m_transitionStartMs = 0;
    qint64 m_lastTickMs = 0;
    bool m_inTransition = false;
    float m_lastVolRaw = -1.0f;   // unsmoothed volume-ratio target
    float m_lastVolTarget = 0.0f; // volume target after step interpolation
    float m_volLerpFrom = 0.0f;
    qint64 m_volLerpAtMs = -1;
    QElapsedTimer m_clock;
    QTimer *m_timer = nullptr;
    bool m_glReady = false;
    float m_ratio = 16.0f / 9.0f;
};

#endif // VIDEOMIXER_H