#include "videomixer.h"
#include "audioplayer.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLShader>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPixelTransferOptions>
#include <QSurface>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QVideoSink>
#include <QVideoFrameFormat>
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

// YUV → RGB converter (NV12 luma+interleaved CbCr, or YUV420P planar)
// rendered into a per-deck RGBA FBO. The YUV planes are uploaded with the
// first row (video top) at v = 0, matching the QImage upload convention, so
// sampling uses flipped texcoords to keep the FBO texture in the same
// orientation as the m_tex[] RGBA uploads.
const char *kYuvVert = R"(
attribute vec2 vertex;
attribute vec2 texCoord;
varying vec2 tex;
void main()
{
    tex = texCoord;
    gl_Position = vec4(vertex, 0.0, 1.0);
}
)";

const char *kYuvFrag = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 tex;
uniform sampler2D uY;   // luma (R8)
uniform sampler2D uCb;  // NV12: RG8 CbCr (Cb = .r, Cr = .g) or planar Cb
uniform sampler2D uCr;  // planar Cr only (unused for NV12)
uniform int uMode;      // 1 = NV12, 2 = yuv420p planar
uniform int uMatrix;    // 0 = BT.601, 1 = BT.709
uniform int uFull;      // 1 = full range, 0 = limited

void main()
{
    vec2 tc = vec2(tex.x, 1.0 - tex.y);
    float y = texture2D(uY, tc).r;
    float cb, cr;
    if (uMode == 1) {
        vec2 c = texture2D(uCb, tc).rg;
        cb = c.r;
        cr = c.g;
    } else {
        cb = texture2D(uCb, tc).r;
        cr = texture2D(uCr, tc).r;
    }
    float yf, cbf, crf;
    if (uFull == 1) {
        yf = y;
        cbf = cb - 0.5;
        crf = cr - 0.5;
    } else {
        yf = clamp((y * 255.0 - 16.0) / 219.0, 0.0, 1.0);
        cbf = (cb * 255.0 - 128.0) / 224.0;
        crf = (cr * 255.0 - 128.0) / 224.0;
    }
    float r, g, b;
    if (uMatrix == 1) { // BT.709
        r = yf + 1.5748 * crf;
        g = yf - 0.1873 * cbf - 0.4681 * crf;
        b = yf + 1.8556 * cbf;
    } else { // BT.601
        r = yf + 1.402 * crf;
        g = yf - 0.344136 * cbf - 0.714136 * crf;
        b = yf + 1.772 * cbf;
    }
    gl_FragColor = vec4(r, g, b, 1.0);
}
)";

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
    if (!convertYuvFrames()) {
        // Converter shader failed to build: fall the decks back to the CPU
        // toImage() path and force a re-upload next frame.
        for (int i = 0; i < 2; ++i) {
            m_yuvMode[i] = 0;
            m_yuvDirty[i] = false;
            m_lastFrameStart[i] = -1;
        }
    }
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

        // Fast path for hardware/planar decode: upload the YUV planes as-is
        // and convert to RGB on the GPU. Only NV12 and YUV420P are handled
        // (the 8-bit formats every hw decoder on this class of GPU delivers);
        // anything else falls through to the toImage() path below.
        if (frame.pixelFormat() == QVideoFrameFormat::Format_NV12
            || frame.pixelFormat() == QVideoFrameFormat::Format_YUV420P) {
            if (uploadYuvPlanes(i, frame)) {
                m_yuvDirty[i] = true;
                continue;
            }
            // map() failed → the frame is not CPU-readable; fall back.
            m_yuvMode[i] = 0;
        }

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

bool VideoMixer::uploadYuvPlanes(int deck, QVideoFrame &frame)
{
    const int w = frame.width();
    const int h = frame.height();
    if (w <= 0 || h <= 0)
        return false;
    if (!frame.map(QVideoFrame::ReadOnly))
        return false;

    const bool nv12 = frame.pixelFormat() == QVideoFrameFormat::Format_NV12;
    const int n = frame.planeCount();
    if (n < (nv12 ? 2 : 3)) {
        frame.unmap();
        return false;
    }

    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    const int cw = (w + 1) / 2;
    const int ch2 = (h + 1) / 2;

    const uchar *yBits = frame.bits(0);
    if (!yBits) {
        frame.unmap();
        return false;
    }
    const int yBpl = frame.bytesPerLine(0);

    // Luma (R8). Recreated whenever the resolution changes, mirroring the
    // RGBA texture logic (QOpenGLTexture::setSize() refuses to resize once
    // storage is allocated).
    QOpenGLTexture *yTex = m_planeY[deck].get();
    if (!yTex) {
        yTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
        m_planeY[deck].reset(yTex);
    }
    if (yTex->width() != w || yTex->height() != h || !yTex->isStorageAllocated()) {
        yTex->destroy();
        yTex->setFormat(QOpenGLTexture::R8_UNorm);
        yTex->setSize(w, h);
        yTex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
        yTex->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
        yTex->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
        yTex->setMinificationFilter(QOpenGLTexture::Linear);
        yTex->setMagnificationFilter(QOpenGLTexture::Linear);
    }
    {
        QOpenGLPixelTransferOptions opts;
        opts.setAlignment(1); // 1-byte rows; avoids GL's 4-byte padding
        if (yBpl != w)
            opts.setRowLength(yBpl); // R8 → 1 byte/pixel
        yTex->setData(0, QOpenGLTexture::Red, QOpenGLTexture::UInt8, yBits, &opts);
    }

    if (nv12) {
        // Interleaved CbCr (RG8), half resolution on both axes.
        const uchar *uvBits = frame.bits(1);
        if (!uvBits) {
            frame.unmap();
            return false;
        }
        const int uvBpl = frame.bytesPerLine(1);
        QOpenGLTexture *uvTex = m_planeUV[deck].get();
        if (!uvTex) {
            uvTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_planeUV[deck].reset(uvTex);
        }
        if (uvTex->width() != cw || uvTex->height() != ch2 || !uvTex->isStorageAllocated()) {
            uvTex->destroy();
            uvTex->setFormat(QOpenGLTexture::RG8_UNorm);
            uvTex->setSize(cw, ch2);
            uvTex->allocateStorage(QOpenGLTexture::RG, QOpenGLTexture::UInt8);
            uvTex->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
            uvTex->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
            uvTex->setMinificationFilter(QOpenGLTexture::Linear);
            uvTex->setMagnificationFilter(QOpenGLTexture::Linear);
        }
        QOpenGLPixelTransferOptions opts;
        opts.setAlignment(1);
        if (uvBpl != cw * 2)
            opts.setRowLength(uvBpl / 2); // RG8 → 2 bytes/pixel
        uvTex->setData(0, QOpenGLTexture::RG, QOpenGLTexture::UInt8, uvBits, &opts);
    } else {
        // Planar YUV420P: separate Cb and Cr planes (R8).
        const uchar *uBits = frame.bits(1);
        const uchar *vBits = frame.bits(2);
        if (!uBits || !vBits) {
            frame.unmap();
            return false;
        }
        const int uBpl = frame.bytesPerLine(1);
        const int vBpl = frame.bytesPerLine(2);
        QOpenGLTexture *uTex = m_planeU[deck].get();
        if (!uTex) {
            uTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_planeU[deck].reset(uTex);
        }
        if (uTex->width() != cw || uTex->height() != ch2 || !uTex->isStorageAllocated()) {
            uTex->destroy();
            uTex->setFormat(QOpenGLTexture::R8_UNorm);
            uTex->setSize(cw, ch2);
            uTex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
            uTex->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
            uTex->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
            uTex->setMinificationFilter(QOpenGLTexture::Linear);
            uTex->setMagnificationFilter(QOpenGLTexture::Linear);
        }
        QOpenGLTexture *vTex = m_planeV[deck].get();
        if (!vTex) {
            vTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_planeV[deck].reset(vTex);
        }
        if (vTex->width() != cw || vTex->height() != ch2 || !vTex->isStorageAllocated()) {
            vTex->destroy();
            vTex->setFormat(QOpenGLTexture::R8_UNorm);
            vTex->setSize(cw, ch2);
            vTex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
            vTex->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
            vTex->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
            vTex->setMinificationFilter(QOpenGLTexture::Linear);
            vTex->setMagnificationFilter(QOpenGLTexture::Linear);
        }
        {
            QOpenGLPixelTransferOptions opts;
            opts.setAlignment(1);
            if (uBpl != cw)
                opts.setRowLength(uBpl);
            uTex->setData(0, QOpenGLTexture::Red, QOpenGLTexture::UInt8, uBits, &opts);
        }
        {
            QOpenGLPixelTransferOptions opts;
            opts.setAlignment(1);
            if (vBpl != cw)
                opts.setRowLength(vBpl);
            vTex->setData(0, QOpenGLTexture::Red, QOpenGLTexture::UInt8, vBits, &opts);
        }
    }

    frame.unmap();

    // Undo the unpack state the uploads may have changed.
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    const QVideoFrameFormat fmt = frame.surfaceFormat();
    m_yuvMatrix[deck] =
        (fmt.colorSpace() == QVideoFrameFormat::ColorSpace_BT709) ? 1 : 0;
    if (fmt.colorSpace() == QVideoFrameFormat::ColorSpace_Undefined)
        m_yuvMatrix[deck] = (h > 576) ? 1 : 0; // heuristic: ≥720p ⇒ HDTV
    m_yuvFull[deck] =
        (fmt.colorRange() == QVideoFrameFormat::ColorRange_Full) ? 1 : 0;
    m_yuvMode[deck] = nv12 ? 1 : 2;
    m_yuvSize[deck] = QSize(w, h);
    return true;
}

bool VideoMixer::ensureYuvProgram()
{
    if (m_yuvProgram)
        return true;
    auto program = std::make_unique<QOpenGLShaderProgram>();
    program->bindAttributeLocation("vertex", 0);
    program->bindAttributeLocation("texCoord", 1);
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, QLatin1String(kYuvVert)))
        return false;
    if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, QLatin1String(kYuvFrag)))
        return false;
    if (!program->link())
        return false;
    m_yuvProgram = std::move(program);
    return true;
}

bool VideoMixer::convertYuvFrames()
{
    bool any = false;
    for (int i = 0; i < 2; ++i)
        if (m_yuvMode[i] != 0 && m_yuvDirty[i])
            any = true;
    if (!any)
        return true;
    if (!ensureYuvProgram())
        return false;

    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    m_yuvProgram->bind();
    m_yuvProgram->setUniformValue(m_yuvProgram->uniformLocation("uY"), 2);
    m_yuvProgram->setUniformValue(m_yuvProgram->uniformLocation("uCb"), 3);
    m_yuvProgram->setUniformValue(m_yuvProgram->uniformLocation("uCr"), 4);

    for (int i = 0; i < 2; ++i) {
        if (m_yuvMode[i] == 0 || !m_yuvDirty[i])
            continue;
        const QSize vs = m_yuvSize[i];
        if (vs.isEmpty())
            continue;
        if (!m_yuvProgram)
            break;

        if (!m_fbo[i] || m_fbo[i]->size() != vs)
            m_fbo[i] = std::make_unique<QOpenGLFramebufferObject>(vs);
        if (!m_fbo[i]->isValid())
            continue;

        m_fbo[i]->bind();
        f->glViewport(0, 0, vs.width(), vs.height());

        f->glActiveTexture(GL_TEXTURE2);
        m_planeY[i]->bind();
        const int mode = m_yuvMode[i];
        if (mode == 1) {
            f->glActiveTexture(GL_TEXTURE3);
            m_planeUV[i]->bind();
        } else {
            f->glActiveTexture(GL_TEXTURE3);
            m_planeU[i]->bind();
            f->glActiveTexture(GL_TEXTURE4);
            m_planeV[i]->bind();
        }
        m_yuvProgram->setUniformValue("uMode", mode);
        m_yuvProgram->setUniformValue("uMatrix", m_yuvMatrix[i]);
        m_yuvProgram->setUniformValue("uFull", m_yuvFull[i]);

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

        m_fbo[i]->release();
        m_yuvDirty[i] = false;
    }

    m_yuvProgram->release();
    // QOpenGLFramebufferObject::bind() leaves the FBO bound; restore the
    // widget's draw target so renderScene() renders to the screen.
    f->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    return true;
}

GLuint VideoMixer::sceneTextureId(int deck) const
{
    if (m_yuvMode[deck] != 0 && m_fbo[deck] && m_fbo[deck]->isValid())
        return m_fbo[deck]->texture();
    if (m_tex[deck])
        return m_tex[deck]->textureId();
    return 0;
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
    GLuint fromId = (fromActive || fromHeld) ? sceneTextureId(from) : 0;
    GLuint toId = toActive ? sceneTextureId(to) : 0;
    if (!fromId)
        fromId = m_blackTex->textureId();
    if (!toId)
        toId = m_blackTex->textureId();
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, fromId);
    f->glActiveTexture(GL_TEXTURE1);
    f->glBindTexture(GL_TEXTURE_2D, toId);

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