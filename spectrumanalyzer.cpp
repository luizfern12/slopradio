#include "spectrumanalyzer.h"

#include <QGuiApplication>
#include <QMutexLocker>
#include <QScreen>
#include <algorithm>

SpectrumAnalyzer::SpectrumAnalyzer(QObject *parent)
    : QObject(parent)
{
    m_window.resize(kFftSize);
    m_re.resize(kFftSize);
    m_im.resize(kFftSize);
    m_windowFn.resize(kFftSize);
    m_level.assign(kBands, 0.0f);
    m_peak.assign(kBands, 0.0f);
    m_levelSmoothed.assign(kBands, 0.0f);
    m_peakSmoothed.assign(kBands, 0.0f);
    m_lastTakeMs = -1;
    m_bandFirst.assign(kBands, -1);
    m_bandLast.assign(kBands, -1);

    // Periodic Hann window: cheap, and its -31 dB sidelobes are low enough
    // that bin leakage won't show up as false bands on the display.
    for (int i = 0; i < kFftSize; ++i)
        m_windowFn[i] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * i / (kFftSize - 1)));

    // RGBA8888 is stored R,G,B,A in memory on little-endian, which is the
    // order GL_RGBA expects. Format_RGB32 would be BGRA and swap red and blue
    // on upload.
    m_row = QImage(kBands, 1, QImage::Format_RGBA8888);
    m_row.fill(qRgba(0, 0, 0, 255));

    // Time origin for feed()'s pacing gate and takeFrame()'s per-frame dt.
    // Without start(), elapsed() reports a huge negative number and the gate
    // can never measure a gap.
    m_updateTimer.start();

    m_frameIntervalMs.store(frameIntervalMs(nullptr));  // follow display refresh rate
}

int SpectrumAnalyzer::frameIntervalMs(const QScreen *screen)
{
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    // A rate of 0 (or anything absurd) is what backends report when they
    // don't know, and 60 Hz is the safest assumption there.
    const qreal hz = screen ? screen->refreshRate() : 0.0;
    if (!(hz > 1.0) || hz > 1000.0)
        return 17; // ~60 Hz

    // Floor at 1 ms. Even a 1000 Hz display cannot ask for more audio analysis
    // than the audio callback rate allows, and the buffer pacing above would
    // degenerate into a busy loop.
    return std::clamp(int(1000.0 / hz + 0.5), 1, 100);
}

void SpectrumAnalyzer::setFrameIntervalMs(int ms)
{
    m_frameIntervalMs.store(std::clamp(ms, 1, 100));
}

void SpectrumAnalyzer::reset()
{
    QMutexLocker lock(&m_mutex);
    std::fill(m_level.begin(), m_level.end(), 0.0f);
    std::fill(m_peak.begin(), m_peak.end(), 0.0f);
    std::fill(m_levelSmoothed.begin(), m_levelSmoothed.end(), 0.0f);
    std::fill(m_peakSmoothed.begin(), m_peakSmoothed.end(), 0.0f);
    m_row.fill(qRgba(0, 0, 0, 255));
    ++m_seq;          // let the renderer know the bars were cleared
    m_hasData = false;
    // Next buffer must be analysed regardless of the pacing gate, otherwise
    // the graph would stay blank until a full frame interval had passed.
    m_forceNext.store(true, std::memory_order_relaxed);
}

bool SpectrumAnalyzer::hasData() const
{
    QMutexLocker lock(&m_mutex);
    return m_hasData;
}

quint64 SpectrumAnalyzer::analysisCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_analyses;
}

void SpectrumAnalyzer::feed(const QAudioBuffer &buffer)
{
    // Pace by wall clock, not by buffer count. How often the audio callback
    // fires depends entirely on the file: the AAC in an .mp4 arrives in
    // 1024-frame chunks, so the callback can run ~43 times a second at
    // 44.1 kHz, while a 4096-frame WAV buffer only fires ~11 times. Analysing
    // every callback therefore does far more work than any display can show.
    //
    // m_sinceAnalysisMs tracks the time of the last analysis that actually
    // ran. Skipped callbacks must not touch it, otherwise the measured gap
    // would stay zero and the gate would never open again.
    const int intervalMs = m_frameIntervalMs.load(std::memory_order_relaxed);
    const qint64 nowMs = m_updateTimer.elapsed();

    // Analyse the first buffer after construction so hasData() turns true
    // right away, and the first one after reset() so the bars come back.
    const bool forced = m_forceNext.exchange(false, std::memory_order_relaxed);
    const qint64 sinceMs = nowMs - m_sinceAnalysisMs;

    // One FFT per displayed frame, decided by the wall clock. Anything arriving
    // inside the interval is dropped before the transform, so a callback that
    // outruns the display cannot burn CPU for updates nobody can paint.
    if (!forced && sinceMs < intervalMs)
        return;
    m_sinceAnalysisMs = nowMs;

    // After a pause the real gap can be arbitrarily long; clamping keeps one
    // long silence from wiping every bar in a single step.
    const qint64 dtMs = std::clamp<qint64>(sinceMs, 0, 4 * qint64(intervalMs));

    const int channels = buffer.format().channelCount();
    if (channels < 1 || channels > 2)
        return;

    const int samples = buffer.sampleCount();
    if (samples <= 0)
        return;

    const int frames = samples / channels;
    if (frames < kFftSize)
        return;

    const void *raw = buffer.constData<void>();
    const QAudioFormat::SampleFormat fmt = buffer.format().sampleFormat();

    // Bin index is not a frequency: bin i covers i * rate / kFftSize Hz, so
    // rebuildBands() needs the real rate rather than a fixed assumption.
    const int rate = buffer.format().sampleRate();
    if (rate > 0)
        m_sampleRate = rate;

    // Downmix to mono over the most recent kFftSize frames.
    if (fmt == QAudioFormat::Int16) {
        const qint16 *base = static_cast<const qint16 *>(raw) + (frames - kFftSize) * channels;
        for (int i = 0; i < kFftSize; ++i) {
            double v = base[i * channels] / 32768.0;
            if (channels == 2)
                v = (v + base[i * channels + 1] / 32768.0) * 0.5;
            m_window[i] = float(v);
        }
    } else if (fmt == QAudioFormat::Float) {
        const float *base = static_cast<const float *>(raw) + (frames - kFftSize) * channels;
        for (int i = 0; i < kFftSize; ++i) {
            double v = base[i * channels];
            if (channels == 2)
                v = (v + base[i * channels + 1]) * 0.5;
            m_window[i] = float(v);
        }
    } else {
        return;
    }

    analyse(dtMs / 1000.0f);
}

void SpectrumAnalyzer::fft(float *re, float *im) const
{
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < kFftSize; ++i) {
        int bit = kFftSize >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    // Butterflies, doubling the span each pass.
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const float ang = -2.0f * float(M_PI) / len;
        const float wr = std::cos(ang);
        const float wi = std::sin(ang);
        for (int i = 0; i < kFftSize; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const float ur = re[i + k],           ui = im[i + k];
                const float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr;         im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

void SpectrumAnalyzer::rebuildBands()
{
    const float nyquist = 0.5f * float(m_sampleRate);
    const float binHz = float(m_sampleRate) / float(kFftSize);
    const float logLo = std::log(kLowestHz);
    // The top of the log range is kHighestHz, not Nyquist: see kHighestHz for
    // why pointing bars above 16 kHz leaves the right end of the graph dark.
    const float logHi = std::log(std::max(kLowestHz * 1.01f,
                                           std::min(kHighestHz, nyquist)));

    auto hzToBin = [&](float t) {
        const float hz = std::exp(logLo + (logHi - logLo) * t);
        // Bin 0 is DC and bin kFftSize/2 is Nyquist; neither carries audio
        // worth plotting.
        return std::clamp(int(hz / binHz), 1, kFftSize / 2 - 1);
    };

    // A pure log layout does not fit in kBands bins. Log-spaced from 30 Hz,
    // the first fifteen edges all fall inside bin 1 (43 Hz wide at 44.1 kHz),
    // so those bars would read the same value and the brightest would land on
    // whichever came last. The wide steps at the top are the ones that matter
    // for an equalizer, and there are 512 bins to spend on them.
    //
    // So the log edges are walked in order and each bar is given at least one
    // bin. The bottom of the graph therefore ends up wider than log spacing
    // asks for (43, 86, 129, 172 Hz...), which is the honest limit of a
    // 1024-point transform rather than a preference; the top keeps its log
    // steps.
    int bin = 1;
    const int binMax = kFftSize / 2 - 1;
    for (int b = 0; b < kBands; ++b) {
        const int edge = std::clamp(hzToBin(float(b + 1) / float(kBands)), bin + 1, binMax);

        m_bandFirst[b] = bin;
        m_bandLast[b] = edge - 1;

        if (edge >= binMax) {
            // Spectrum exhausted: leave the remaining slots empty so they stay
            // dark rather than repeating the top band.
            for (int rest = b + 1; rest < kBands; ++rest) {
                m_bandFirst[rest] = -1;
                m_bandLast[rest] = -1;
            }
            return;
        }
        bin = edge;
    }
}

void SpectrumAnalyzer::analyse(float dtSeconds)
{
    QMutexLocker lock(&m_mutex);

    for (int i = 0; i < kFftSize; ++i) {
        m_re[i] = m_window[i] * m_windowFn[i];
        m_im[i] = 0.0f;
    }
    fft(m_re.data(), m_im.data());

    if (m_bandSampleRate != m_sampleRate) {
        rebuildBands();
        m_bandSampleRate = m_sampleRate;
    }

    const float norm = 2.0f / float(kFftSize);
    const float levelFall = kLevelFallPerSecond * dtSeconds;
    const float peakFall = kPeakFallPerSecond * dtSeconds;

    for (int b = 0; b < kBands; ++b) {
        if (m_bandFirst[b] < 0) {
            // No bins left for this slot once the earlier bars took theirs, so
            // keep it dark rather than repeating the top band.
            m_level[b] = 0.0f;
            m_peak[b] = 0.0f;
            continue;
        }

        // Take the loudest bin in the band: a peak reads better on a bar
        // graph than an average, which drags every band toward the floor.
        float mag = 0.0f;
        for (int i = m_bandFirst[b]; i <= m_bandLast[b]; ++i)
            mag = std::max(mag, std::sqrt(m_re[i] * m_re[i] + m_im[i] * m_im[i]) * norm);

        const float db = 20.0f * std::log10(std::max(mag, 1e-7f));
        const float t = std::clamp((db - kFloorDb) / (kCeilDb - kFloorDb), 0.0f, 1.0f);

        // Instant attack, timed release: the meter itself. It deliberately
        // touches neither m_row nor m_seq — this runs once per audio buffer
        // while the row is repainted per frame, and a filter or a row write in
        // both places would mean two clocks fighting over the same state.
        m_level[b] = t > m_level[b] ? t : std::max(t, m_level[b] - levelFall);
        m_peak[b] = t > m_peak[b] ? t : std::max(t, m_peak[b] - peakFall);
    }

    m_hasData = true;
    ++m_analyses;
}

SpectrumAnalyzer::Frame SpectrumAnalyzer::takeFrame()
{
    QMutexLocker lock(&m_mutex);

    Frame f;
    if (!m_hasData)
        return f;

    const qint64 now = m_updateTimer.elapsed();
    qint64 dtMs = (m_lastTakeMs < 0) ? 16 : now - m_lastTakeMs;
    if (dtMs < 0)
        dtMs = 16;
    m_lastTakeMs = now;
    const float dtSeconds = std::clamp(float(dtMs) / 1000.0f, 0.0f, 0.1f);

    // One filter step per painted frame, so kSmoothingSeconds means what it
    // says however many audio buffers landed in between. Expressed as a time
    // constant rather than a per-call fraction: a fixed alpha would make the
    // result depend on the repaint rate.
    const float k = 1.0f - std::exp(-dtSeconds / kSmoothingSeconds);

    uchar *line = m_row.scanLine(0);
    for (int b = 0; b < kBands; ++b) {
        const float t = m_level[b];
        const float pt = m_peak[b];

        // Attack is not filtered — a bar has to jump when the music does, and
        // any lag there reads as the graph being asleep. Only the fall is
        // smoothed, which is where the ~26 ms steps of a decode buffer would
        // otherwise show as judder.
        m_levelSmoothed[b] = (t > m_levelSmoothed[b])
                                ? t
                                : m_levelSmoothed[b] + (t - m_levelSmoothed[b]) * k;
        m_peakSmoothed[b] = (pt > m_peakSmoothed[b])
                                ? pt
                                : m_peakSmoothed[b] + (pt - m_peakSmoothed[b]) * k;

        const float dlv = std::clamp(m_levelSmoothed[b], 0.0f, 1.0f);
        const float dpk = std::clamp(m_peakSmoothed[b], 0.0f, 1.0f);
        line[b * 4 + 0] = uchar(dlv * 255.0f + 0.5f);
        line[b * 4 + 1] = uchar(dpk * 255.0f + 0.5f);
        line[b * 4 + 2] = 0;
        line[b * 4 + 3] = 255;
    }

    ++m_seq;
    f.row = m_row.copy();
    f.seq = m_seq;
    f.valid = true;
    return f;
}