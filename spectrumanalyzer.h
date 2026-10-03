#ifndef SPECTRUMANALYZER_H
#define SPECTRUMANALYZER_H

#include <QAudioBuffer>
#include <QObject>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <atomic>
#include <cmath>
#include <vector>

class QScreen;

// Real-time spectrum analysis for the video output's EQ visualizer.
//
// Fed from MainWindow::calculateRMS(), which already receives every audio
// buffer for the VU meters, so no extra tap on the audio path is needed.
// An iterative radix-2 FFT runs over a Hann-windowed mono downmix and the
// magnitudes are folded into kBands bands, spaced logarithmically wherever the
// transform's resolution allows it (see rebuildBands()).
//
// What leaves this class is deliberately tiny: a kBands x 1 RGBA image with
// the bar height in R and the peak-hold height in G. VideoMixer uploads that
// one row per frame (~256 bytes) and lets the fragment shader draw the bars,
// so nothing about the rendering lives on the CPU.
//
// Two stages, each with exactly one owner:
//
//   analyse()   -> m_level / m_peak        the meter's own dynamics
//   takeFrame() -> m_levelSmoothed / ...   what actually gets painted
//
// Keeping the display filter out of analyse() matters: they run on different
// threads at different rates (one FFT per audio buffer, one row per repaint),
// and a filter in both would run twice as often as its time constant says.
class SpectrumAnalyzer : public QObject
{
    Q_OBJECT

public:
    static constexpr int kFftSize = 1024;   // must be a power of two
    static constexpr int kBands = 64;       // bar slots across the output

    // Lowest frequency plotted. Nothing musical lives below this, and bins below
    // it would only widen the unused gap on the left of the graph.
    static constexpr float kLowestHz = 30.0f;

    // Highest frequency plotted, and the real reason the graph stops where it
    // does. Going all the way to Nyquist would spend the last three of the 64
    // slots on 16.1 -> 22 kHz, which is empty air for every 128 kbps file in
    // existence — codecs lowpass near 16 kHz — so those bars stayed dark and
    // the graph never reached the right edge of the window. 16 kHz is also the
    // top band of a classic graphic equalizer, so this trades a range that is
    // almost always silent for a graph that actually fills.
    //
    // Capped by Nyquist as well, so a low sample rate still wins.
    static constexpr float kHighestHz = 16000.0f;

    // Frame interval the analyzer aims for, in milliseconds, so the graph
    // advances once per painted frame rather than at a fixed 30 fps. The audio
    // buffer callback fires far more often than either, and analysing every
    // callback would burn CPU for updates nobody can see.
    //
    // Starts at 60 Hz and is set from the display's real refresh rate by
    // VideoMixer, which is the only place that knows which screen the video
    // output is on.
    static int frameIntervalMs(const QScreen *screen);
    void setFrameIntervalMs(int ms);

    // dB range the band levels are stretched across.
    static constexpr float kFloorDb = -72.0f;
    static constexpr float kCeilDb = -12.0f;

    // Bar fall rates, in normalised (0..1) height per second. The level
    // releases quickly so the graph keeps moving; the peak marker lingers so
    // transients stay readable.
    static constexpr float kLevelFallPerSecond = 1.8f;
    static constexpr float kPeakFallPerSecond = 0.5f;

    // Display filter time constant. Applied once per painted frame in
    // takeFrame(), and only on the way down: the bar must jump the instant the
    // music does, so attack is not filtered at all. This is long enough to
    // spread the ~26 ms steps of an MP3 decode buffer across a repaint, and
    // short enough that a falling bar is not visibly trailing its own meter.
    static constexpr float kSmoothingSeconds = 0.020f;

    struct Frame
    {
        QImage row;                        // kBands x 1 RGBA8888
        quint64 seq = 0;                   // bumped on every update
        bool valid = false;
    };

    explicit SpectrumAnalyzer(QObject *parent = nullptr);

    // Decode the buffer's sample format and downmix to mono.
    void feed(const QAudioBuffer &buffer);

    // True once the first FFT has produced levels, i.e. there is something
    // worth drawing.
    bool hasData() const;

    // How many transforms have actually run. The pacing gate in feed() decides
    // this, and it is otherwise invisible: too many means burning CPU on frames
    // nobody paints, too few means a graph that crawls. Exposed so the gate can
    // be checked directly instead of by timing a loop.
    quint64 analysisCount() const;

    // Latest band levels plus their sequence number. Cheap: one 256-byte
    // copy under a lock.
    Frame takeFrame();

    // Drop the bars and peak markers, e.g. when the transport stops.
    void reset();

private:
    // Map each bar to its FFT bin range. Depends only on the sample rate.
    void rebuildBands();
    void analyse(float dtSeconds);

    // Iterative in-place radix-2 Cooley-Tukey. re/im are kFftSize long.
    void fft(float *re, float *im) const;

    mutable QMutex m_mutex;

    // Rolling mono window, filled by feed() and consumed by analyse().
    std::vector<float> m_window;

    std::vector<float> m_re;
    std::vector<float> m_im;
    std::vector<float> m_windowFn;

    // FFT bin range [first, last] backing each bar, rebuilt whenever the sample
    // rate changes. Every bar claims at least one bin; see rebuildBands() for
    // why the low end ends up wider than the log spacing asks for.
    std::vector<int> m_bandFirst;
    std::vector<int> m_bandLast;
    int m_bandSampleRate = 0;

    // Metered bar height and peak marker, both 0..1, with instant attack and a
    // timed release. Written only by analyse(). Held on the CPU deliberately:
    // the fragment shader has no memory between frames, and reading back from
    // the GPU to decay them would cost more than the FFT.
    std::vector<float> m_level;
    std::vector<float> m_peak;

    // m_level / m_peak after the display filter — what the shader is handed.
    // Written only by takeFrame(), at repaint cadence, so one filter
    // application means one painted frame.
    std::vector<float> m_levelSmoothed;
    std::vector<float> m_peakSmoothed;

    qint64 m_lastTakeMs = -1;     // elapsed time of last takeFrame() call (-1: none yet)

    QImage m_row;
    QElapsedTimer m_updateTimer;
    quint64 m_seq = 0;         // monotonic, so a reset is visible to the renderer
    quint64 m_analyses = 0;    // transforms actually run; see analysisCount()
    bool m_hasData = false;
    int m_sampleRate = 44100;   // from the buffer; needed to map Hz -> bins

    // Written by the GUI thread (setFrameIntervalMs, reset) and read by the
    // multimedia thread in feed(), so these must be atomic.
    std::atomic<int> m_frameIntervalMs{16};
    std::atomic<bool> m_forceNext{true};

    // Elapsed-time origin for the pacing gate; only touched by feed(), so it
    // stays on the multimedia thread and needs no lock.
    qint64 m_sinceAnalysisMs = 0;
};

#endif // SPECTRUMANALYZER_H