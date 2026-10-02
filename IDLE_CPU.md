# Idle CPU usage — investigation and backlog

Tracking doc for CPU-usage work on `appLaraRadio`. Started after the
pre-cue seeker fix; the profiler surfaced several unrelated idle-cost
items that are easy to lose track of.

Status: **A, B, E done** (59% idle reduction, measured). C, D pending.
VideoMixer deferred by choice.

## How to reproduce the measurement

Launch against a display, with `QSettings` redirected so the real config
is untouched:

```sh
rm -rf /tmp/idleprof; mkdir -p /tmp/idleprof/config
XDG_CONFIG_HOME=/tmp/idleprof/config setsid nohup ./build/appLaraRadio \
  > /tmp/idleprof/app.log 2>&1 < /dev/null &
PID=$(pgrep -x appLaraRadio)

# let startup settle, then sample utime+stime over 30s
sleep 25
T1=$(sed 's/.*) //' /proc/$PID/stat | awk '{print $12+$13}')
sleep 30
T2=$(sed 's/.*) //' /proc/$PID/stat | awk '{print $12+$13}')
awk -v a=$T1 -v b=$T2 -v h=$(getconf CLK_TCK) \
  'BEGIN{d=(b-a)/h; printf "CPU%%=%.2f\n", d/30*100}'
pkill -9 -x appLaraRadio
```

`perf record -F 999 -g -p $PID -- sleep 12` + `perf report --stdio --no-children`
for the per-symbol breakdown.

Note: the baseline config had **no** buttonhole files assigned and
`video/enabled=false`. Buttonhole cost therefore only shows up once an
operator loads sounds onto the buttons.

## Results

| Build | Idle CPU | Note |
|---|---|---|
| Baseline | **5.80%** | 1.74 CPU-s over 30 s wall, 14 threads |
| After A+B+E | **2.40%** | 0.72 CPU-s over 30 s wall |

All of it was on the GUI thread (5.0–5.8%); every other thread
(PipeWire, Wayland, cuda, inotify) sat at ≤0.4%. `perf` showed no
app-binary symbol with meaningful self time — the cost was entirely in
Qt paint primitives: `QPainter::setBrush(QColor)` 0.97%,
`paintSiblingsRecursive` 0.85%, `drawWidget` 0.74%,
`QTextEngine::itemize()` 1.18%, `QTextLine::layout_helper` 0.88%,
`QUnicodeTools::initCharAttributes` 0.67%, plus ~20% in an unresolved
flat-copy region of libc.

So the cost was **repaint frequency, not logic**.

---

## Done

### A. Display timer capped at 30 fps — `mainwindow.cpp`

`m_displayTimer->start(10)` → `start(33)`.

`updateDisplay()` ran at 100 Hz and called `VuMeter::setLevel()` twice
per tick, each of which calls `update()` unconditionally — **200 forced
VU-meter repaints per second while idle**. Nothing needed 100 Hz: the
meters are 33 discrete LEDs.

### B. Silence watchdog made time-based — `mainwindow.cpp` / `mainwindow.h`

Required by A, not optional. The watchdog accumulated
`m_silenceMs += 10; // displayTimer interval` against a 10000 ms
timeout, hard-wired to the tick rate. Capping the timer at 30 fps
without this would have fired the timeout after ~3.3 s and skipped
tracks constantly. `int m_silenceMs` is now a `QElapsedTimer`
(`m_silenceTimer`), started when the silence condition begins and
inverted when it breaks.

### E. ButtonHole polling — `buttonhole.cpp` / `buttonhole.h`

Ten buttons each poll on a 300 ms timer (`buttonhole.cpp` ctor), so
~33 wake-ups/sec. Two fixes:

- `flash()` compared the desired stylesheet against the current one and
  only called `setStyleSheet()` on a change. It previously re-applied
  `background-color: #fc0; color: #000;` unconditionally whenever a
  file was assigned — 10 repaints every 300 ms writing back an identical
  string. `setStyleSheet()` re-parses the rules and repolishes the widget.
- The `buttonhole/btn_<n>` lookup is cached (`assignedPath()` /
  `invalidateAssignedPath()`), removing a `QSettings` read plus a
  `QString` concat per button per tick. The cache is invalidated in the
  context-menu actions that write the key and in `setBtnText()` (the key
  is derived from the label). `buttonHoleClick()` now uses the cache too.

Latent win — not part of the 5.80% baseline, because the test config had
no buttonhole files assigned. Matters on a real station where all ten
are loaded.

---

## Pending

### C. Early-out in `VuMeter::setLevel()` — `vumeter.cpp`

The single biggest remaining win. `setLevel()` calls `update()`
unconditionally; the painted output is a pure function of `level`, so
skipping the repaint when the value is unchanged is safe. Idle repaints
would go to zero rather than merely 30/sec. This is what actually gets
idle near zero — A alone only cuts frequency 3.3×.

### D. Cheapen `VuMeter::paintEvent()` — `vumeter.cpp`

- Drop `QPainter::Antialiasing`: all radii are `0,0`, so it costs real
  time and buys nothing visually.
- Hoist the six per-iteration `QColor` constructions + `setBrush`
  conversions out of the 33-LED loop (~198 `QColor` constructions per
  paint; `setBrush(QColor)` showed up at 0.97% in the profile).

### VideoMixer 30 fps shader pass — `videomixer.cpp` (deferred)

`videomixer.cpp` starts a 33 ms timer driving `QOpenGLWidget::update()`
on `showEvent`, stopping it on `hideEvent`. Whenever the video window is
**visible** it renders 30×/sec indefinitely — even with nothing playing,
where both deck textures fall back to `m_blackTex` and the output is
solid black. Plus ~7 `uniformLocation(const char*)` string lookups per
frame.

Not in the baseline (`video/enabled=false` meant the timer never ran), so
deferring it costs nothing measurable so far. Fixing it means stopping
the timer when the picture settles and restarting on demand.

Traps to respect when picking this up:
- A naive "skip if progress unchanged" **spins forever**:
  `m_smoothProgress` is an exponential smoother that never exactly
  reaches its target. Needs an epsilon settle test.
- The re-arm path has to cover new frames (`uploadFrames()` sets
  `m_yuvDirty`) and transition starts (`setIncomingDeck()`).
- `fromHeld` holds the outgoing deck's last frame through a transition,
  so "nothing active" must not blank the screen mid-transition.

---

## Checked and deliberately left alone

- `updateAudioList()` rebuilds the whole tree widget but is called only
  on events, never from the display timer.
- 3× `AudioPlayer::fade` (500 ms), `MainWindow::flash` (500 ms),
  `TimerClock` (1 Hz) — cheap enough.
- Ten `QMediaPlayer` + `QAudioOutput` pairs from `ButtonHole`. Lazy, not
  idle cost; only worth revisiting if profiling ever points here.