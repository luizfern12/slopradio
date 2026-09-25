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

    // current visual mix progress, 0..1 (exposed for tests/diagnostics)
    float progress() const { return m_smoothProgress; }

    QSize sizeHint() const override { return QSize(960, 540); }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void ensureTimer();
    void uploadFrames();
    void renderScene();
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