#include "videomixer.h"
#include "audioplayer.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLShader>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QSurface>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QVideoSink>
#include <QRandomGenerator>
#include <cmath>
#include <algorithm>

namespace {

// Vertices of a fullscreen quad, interleaved [x, y, u, v].
// Screen top samples v = 0, which matches a QImage uploaded row 0 first.
// Canonical GL_TRIANGLE_STRIP order: both triangles end up wound the same
// way (BL->BR->TL, TL->BR->TR). The alternative BR/TR interleaving leaves a
// wedge of the screen undrawn on some drivers.
const GLfloat kQuad[16] = {
    // x      y      u    v
    -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left
     1.0f, -1.0f, 1.0f, 1.0f, // bottom-right
    -1.0f,  1.0f, 0.0f, 0.0f, // top-left
     1.0f,  1.0f, 1.0f, 0.0f, // top-right
};

const char *kDefaultFrag = ":/shaders/crossfade.frag";
const char *kDefaultVert = ":/shaders/fullscreen.vert";

// Minimum duration of a visual transition when the outgoing deck stopped
// instead of fading (natural EndOfMedia advances are a hard audio cut, so
// there is no volume ramp to follow).
const float kTransitionMs = 1500.0f;
// fade() steps deck volumes every 500 ms; interpolate across one step.
const float kFadeStepMs = 500.0f;

} // namespace

VideoMixer::VideoMixer(QWidget *parent)
    : QOpenGLWidget(parent)
{
    m_clock.start();
    m_transitionStartMs = m_clock.elapsed();
    m_seed = QRandomGenerator::global()->generateDouble();

    m_timer = new QTimer(this);
    m_timer->setInterval(33); // ~30 fps
    connect(m_timer, &QTimer::timeout, this, QOverload<>::of(&QOpenGLWidget::update));
}

VideoMixer::~VideoMixer() = default;

void VideoMixer::setDeck(int index, AudioPlayer *player)
{
    if (index == 1 || index == 2)
        m_decks[index - 1] = player;
}

void VideoMixer::setIncomingDeck(int index)
{
    if (index != 1 && index != 2)
        return;
    m_incoming = index;

    // An explicit deck change is a track change: always start a fresh
    // transition. At EndOfMedia the outgoing deck has already stopped, so the
    // volume ratio jumps instead of fading — without this the effect would
    // skip straight to the end. Resetting progress to 0 keeps the outgoing
    // picture on screen (its texture is held until the transition ends).
    m_transitionStartMs = m_clock.elapsed();
    m_seed = QRandomGenerator::global()->generateDouble();
    m_inTransition = true;
    m_smoothProgress = 0.0f;
    m_targetProgress = 0.0f;
    // Re-anchor the volume-ratio lerper: the raw ratio inverts with the deck
    // roles, and lerping the stale pre-swap value (≈1) down through the new
    // transition made min(eased, volTarget) dip and progress run backwards.
    m_volLerpAtMs = -1;
}

void VideoMixer::refreshIncomingDeck()
{
    // When there is no active crossfade, decide the "to" deck from whatever
    // video is actually running, so re-opening the window shows the right feed.
    if (m_decks[0] && m_decks[0]->isVideoActive() && !(m_decks[1] && m_decks[1]->isVideoActive()))
        m_incoming = 1;
    else if (m_decks[1] && m_decks[1]->isVideoActive() && !(m_decks[0] && m_decks[0]->isVideoActive()))
        m_incoming = 2;
    else if (!m_decks[0]->isVideoActive() && !m_decks[1]->isVideoActive())
        m_incoming = 1;
}

void VideoMixer::setEffects(const QList<Effect> &effects)
{
    m_effects = effects;
    m_failedEffects.clear(); // files may have changed on disk; allow retries
    if (!m_currentEffect.isEmpty() && effectExists(m_currentEffect))
        return;
    m_currentEffect = m_effects.value(0).id;
    m_loadedId.clear(); // force rebuild
}

void VideoMixer::setCurrentEffect(const QString &id)
{
    if (effectExists(id)) {
        m_currentEffect = id;
        m_loadedId.clear();
    }
}

bool VideoMixer::effectExists(const QString &id) const
{
    for (const Effect &e : m_effects)
        if (e.id == id)
            return true;
    return false;
}

QList<VideoMixer::Effect> VideoMixer::availableEffects(const QString &customShaderDir)
{
    QList<Effect> out;

    // Built-ins shipped in the resources, in a friendly order.
    const char *builtins[] = {
        "crossfade", "wipe",     "slide",   "circleopen",
        "flip3d",    "cube3d",   "rotate3d"
    };
    for (const char *id : builtins) {
        QString title = QString::fromLatin1(id);
        title[0] = title[0].toUpper();
        out.append({ QString::fromLatin1(id), title,
                     QString(":/shaders/") + id + ".frag", kDefaultVert });
    }

    // User shaders: every *.frag in the custom folder; a *.vert with the same
    // base name replaces the default vertex stage (e.g. real 3D geometry).
    if (!customShaderDir.isEmpty()) {
        QDir dir(customShaderDir);
        dir.setNameFilters({ "*.frag", "*.frag.txt" });
        dir.setFilter(QDir::Files | QDir::Readable);
        const QStringList frags = dir.entryList();
        for (const QString &frag : frags) {
            QString base = frag;
            base.replace(".txt", "");
            base.chop(5); // ".frag"
            QString vert = dir.filePath(base + ".vert");
            out.append({
                base,
                base,
                dir.filePath(frag),
                QFile::exists(vert) ? vert : QString(kDefaultVert),
            });
        }
    }

    return out;
}

QString VideoMixer::readSource(const QString &path) const
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = f.readAll();
        if (!data.isEmpty())
            return QString::fromUtf8(data);
    }
    return QString();
}

// ---------------------------------------------------------------------------
// GL lifecycle
// ---------------------------------------------------------------------------

void VideoMixer::initializeGL()
{
    m_glReady = true;

    for (int i = 0; i < 2; ++i) {
        m_tex[i] = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        m_tex[i]->setFormat(QOpenGLTexture::RGBA8_UNorm);
        m_tex[i]->setMinificationFilter(QOpenGLTexture::Linear);
        m_tex[i]->setMagnificationFilter(QOpenGLTexture::Linear);
    }

    m_blackTex = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    m_blackTex->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_blackTex->setSize(1, 1);
    m_blackTex->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    {
        const unsigned char black[] = {0, 0, 0, 255};
        m_blackTex->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, black);
    }
    m_blackTex->setMinificationFilter(QOpenGLTexture::Nearest);
    m_blackTex->setMagnificationFilter(QOpenGLTexture::Nearest);

    // Quad geometry. Attributes are always bound to locations 0 (vertex) and
    // 1 (texCoord) via bindAttributeLocation in rebuildProgram().
    m_vao = std::make_unique<QOpenGLVertexArrayObject>();
    const bool vaoOk = m_vao->create();
    m_vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_vbo->create();
    m_vbo->bind();
    m_vbo->allocate(kQuad, sizeof(kQuad));
    m_vbo->release();

    if (vaoOk) {
        m_vao->bind();
        m_vbo->bind();
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                 reinterpret_cast<const void *>(0));
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                 reinterpret_cast<const void *>(2 * sizeof(GLfloat)));
        m_vao->release();
        m_vbo->release();
    }

    if (m_effects.isEmpty()) {
        m_effects = availableEffects(QString());
        m_currentEffect = m_effects.value(0).id;
    }
    ensureProgram();
}

void VideoMixer::resizeGL(int w, int h)
{
    m_ratio = h > 0 ? float(w) / float(h) : 1.0f;
}

void VideoMixer::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    // pick up the currently active deck on first show
    refreshIncomingDeck();
    ensureTimer();
}

void VideoMixer::hideEvent(QHideEvent *event)
{
    QOpenGLWidget::hideEvent(event);
    if (m_timer)
        m_timer->stop();
}

void VideoMixer::ensureTimer()
{
    if (m_timer && !m_timer->isActive())
        m_timer->start();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void VideoMixer::paintGL()
{
    if (!m_glReady)
        return;
    if (!ensureProgram())
        return;

    uploadFrames();
    renderScene();
}

void VideoMixer::uploadFrames()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_decks[i] || !m_decks[i]->videoSink())
            continue;
        QVideoFrame frame = m_decks[i]->videoSink()->videoFrame();
        if (!frame.isValid())
            continue;
        if (frame.startTime() == m_lastFrameStart[i])
            continue;

        m_lastFrameStart[i] = frame.startTime();
        m_latest[i] = frame;

        // QVideoFrame::toImage() may make the video's own GL context current
        // and leave it that way; every texture operation below must run in
        // the widget's context or the uploads silently go to the wrong one
        // (which renders a black window).
        QOpenGLContext *prevCtx = QOpenGLContext::currentContext();
        QSurface *prevSurf = prevCtx ? prevCtx->surface() : nullptr;
        QImage img = frame.toImage();
        if (prevCtx && prevCtx != QOpenGLContext::currentContext())
            prevCtx->makeCurrent(prevSurf);
        if (img.isNull())
            continue;
        if (img.format() != QImage::Format_RGBA8888)
            img = img.convertToFormat(QImage::Format_RGBA8888);

        QOpenGLTexture *tex = m_tex[i].get();
        if (tex->width() != img.width() || tex->height() != img.height()
            || !tex->isStorageAllocated()) {
            // QOpenGLTexture::setSize() refuses to change dimensions once
            // storage is allocated (it warns and returns), which left the
            // texture at the previous track's resolution: setData() then
            // over-read the smaller QImage (crash) or drew stale strips
            // (corruption) whenever a video's resolution differed from the
            // last one. Destroy and recreate instead. destroy() also resets
            // format, wrap and filters, so every one is re-applied below.
            tex->destroy();
            tex->setFormat(QOpenGLTexture::RGBA8_UNorm);
            tex->setSize(img.width(), img.height());
            tex->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
            tex->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
            tex->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
            tex->setMinificationFilter(QOpenGLTexture::Linear);
            tex->setMagnificationFilter(QOpenGLTexture::Linear);
        }
        tex->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, img.constBits());
    }
}

bool VideoMixer::ensureProgram()
{
    if (m_program && m_loadedId == m_currentEffect)
        return true;

    const Effect *effect = nullptr;
    for (const Effect &e : m_effects)
        if (e.id == m_currentEffect)
            effect = &e;

    // A shader that failed to compile stays failed until setEffects() runs
    // again; without the guard a broken custom .frag would trigger a full
    // recompile + relink attempt on every frame.
    if (effect && !m_failedEffects.contains(effect->id)) {
        if (rebuildProgram(*effect))
            return true;
        m_failedEffects.insert(effect->id);
    }

    // Already showing the crossfade fallback — don't relink it every frame.
    if (m_program && m_loadedId == QLatin1String("crossfade"))
        return true;

    // fall back to the plain crossfade
    return rebuildProgram(Effect{ "crossfade", "Crossfade", QString(kDefaultFrag),
                                  QString(kDefaultVert) });
}

bool VideoMixer::rebuildProgram(const Effect &effect)
{
    QString frag = effect.fragmentPath.isEmpty()
        ? readSource(QLatin1String(kDefaultFrag))
        : readSource(effect.fragmentPath);
    QString vert = effect.vertexPath.isEmpty()
        ? readSource(QLatin1String(kDefaultVert))
        : readSource(effect.vertexPath);
    if (frag.isEmpty() || vert.isEmpty())
        return false;

    // On OpenGL ES, fragment shaders need an explicit default float
    // precision. The built-in shaders carry their own #ifdef GL_ES guard,
    // but user-supplied gl-transitions shaders (written for desktop GL)
    // don't, so give them one here.
    if (frag.contains("precision ") == 0
        && QOpenGLContext::currentContext()
        && QOpenGLContext::currentContext()->isOpenGLES()) {
        const QString pre = QLatin1String("precision mediump float;\n");
        if (frag.startsWith("#version")) {
            const int nl = frag.indexOf(QLatin1Char('\n'));
            frag.insert(nl > 0 ? nl + 1 : 0, pre);
        } else {
            frag.prepend(pre);
        }
    }

    auto program = std::make_unique<QOpenGLShaderProgram>();
    program->bindAttributeLocation("vertex", 0);
    program->bindAttributeLocation("texCoord", 1);

    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vert))
        return false;
    if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, frag))
        return false;
    if (!program->link())
        return false;

    m_program = std::move(program);
    m_loadedId = effect.id;
    return true;
}

void VideoMixer::renderScene()
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    const int dpr = devicePixelRatioF();
    f->glViewport(0, 0, width() * dpr, height() * dpr);
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    const int from = (m_incoming == 1) ? 1 : 0; // outgoing deck
    const int to = (m_incoming == 1) ? 0 : 1;   // incoming deck

    const bool fromActive = m_decks[from] && m_decks[from]->isVideoActive();
    const bool toActive = m_decks[to] && m_decks[to]->isVideoActive();

    const bool fromPlaying = m_decks[from] && (m_decks[from]->isPlaying() || m_decks[from]->isPaused());
    const bool toPlaying = m_decks[to] && (m_decks[to]->isPlaying() || m_decks[to]->isPaused());

    // Volume ratio of the incoming deck against the total = how far the audio
    // crossfade has progressed. Stopped decks contribute no volume.
    const float vf = fromPlaying ? m_decks[from]->getVolume() : 0.0f;
    const float vt = toPlaying ? m_decks[to]->getVolume() : 0.0f;
    float rawTarget;
    if ((vf + vt) > 0.0001f)
        rawTarget = vt / (vf + vt);
    else if (toActive)
        rawTarget = 1.0f;
    else if (fromActive)
        rawTarget = 0.0f;
    else
        rawTarget = 0.0f;

    const qint64 now = m_clock.elapsed();
    float dt = m_lastTickMs > 0 ? float(now - m_lastTickMs) / 1000.0f : 0.033f;
    m_lastTickMs = now;
    dt = std::clamp(dt, 0.001f, 0.5f);

    // fade() changes volumes only every 500 ms; interpolate linearly across a
    // step so progress moves continuously instead of pausing between ticks
    // (which made every effect look like it was running paused).
    if (m_volLerpAtMs < 0) {
        m_volLerpFrom = rawTarget;
        m_lastVolRaw = rawTarget;
        m_volLerpAtMs = now;
    }
    if (std::abs(rawTarget - m_lastVolRaw) > 0.004f) {
        m_volLerpFrom = m_lastVolTarget;
        m_volLerpAtMs = now;
        m_lastVolRaw = rawTarget;
    }
    const float volTarget =
        m_volLerpFrom + (m_lastVolRaw - m_volLerpFrom)
            * std::clamp(float(now - m_volLerpAtMs) / kFadeStepMs, 0.0f, 1.0f);
    m_lastVolTarget = volTarget;

    // While a transition runs, progress follows an eased 0..1 ramp so every
    // track change animates, even when the outgoing deck hard-stopped at
    // EndOfMedia (no volume fade to follow). If that deck is still playing a
    // real audio crossfade (manual skip / pre-emptive advance), follow
    // whichever is slower so the picture stays in sync with what is heard.
    //
    // The candidate is clamped against the current smoothed value so progress
    // can never move backwards: a transition is always one continuous sweep,
    // no matter how the volume ratio wobbles (state changes, the toActive
    // fallbacks, announcements ducking volume). Outside a transition the same
    // clamp holds the finished state at 1 instead of retreating — the only
    // backward move is the reset to 0 when a new transition starts.
    float target;
    if (m_inTransition) {
        const float t = std::clamp(
            float(now - m_transitionStartMs) / kTransitionMs, 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        const bool outgoing = m_decks[from] && m_decks[from]->isPlaying();
        const float candidate =
            std::max(outgoing ? std::min(eased, volTarget) : eased,
                     m_smoothProgress);
        target = candidate;
        if ((t >= 1.0f && (volTarget >= 0.995f || !outgoing))
            || (now - m_transitionStartMs) > 6 * qint64(kTransitionMs))
            m_inTransition = false;
    } else {
        target = std::max(volTarget, m_smoothProgress);
    }
    m_targetProgress = target;

    m_smoothProgress += (target - m_smoothProgress) * std::min(1.0f, 12.0f * dt);
    if (m_smoothProgress < 0.0f)
        m_smoothProgress = 0.0f;
    if (m_smoothProgress > 1.0f)
        m_smoothProgress = 1.0f;

    const float timeSec = float(now - m_transitionStartMs) / 1000.0f;

    m_program->bind();
    m_program->setUniformValue(m_program->uniformLocation("from"), 0);
    m_program->setUniformValue(m_program->uniformLocation("to"), 1);
    m_program->setUniformValue(m_program->uniformLocation("progress"), m_smoothProgress);
    m_program->setUniformValue(m_program->uniformLocation("ratio"), m_ratio);
    m_program->setUniformValue(m_program->uniformLocation("seed"), m_seed);
    m_program->setUniformValue(m_program->uniformLocation("time"), timeSec);
    const GLint centerLoc = m_program->uniformLocation("center");
    if (centerLoc >= 0)
        m_program->setUniformValue(centerLoc, QVector2D(0.5f, 0.5f));

    // Hold the outgoing deck's last frame through a transition even though it
    // has already stopped — otherwise the old video vanishes at EndOfMedia
    // and the effect appears to start from black.
    const bool fromHeld = m_inTransition && m_lastFrameStart[from] >= 0;
    QOpenGLTexture *fromTex =
        (fromActive || fromHeld) ? m_tex[from].get() : m_blackTex.get();
    QOpenGLTexture *toTex = toActive ? m_tex[to].get() : m_blackTex.get();
    fromTex->bind(0);
    toTex->bind(1);

    if (m_vao && m_vao->isCreated()) {
        m_vao->bind();
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_vao->release();
    } else {
        m_vbo->bind();
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                 reinterpret_cast<const void *>(0));
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                                 reinterpret_cast<const void *>(2 * sizeof(GLfloat)));
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    m_program->release();
}