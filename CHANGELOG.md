# Changelog — LaraRadio

All changes between the original `gutierre69/lararadio` release
and this fork (`brdelphus/lararadio`).

Original source: https://github.com/gutierre69/lararadio
Fork: https://github.com/brdelphus/lararadio

---

## [Unreleased]

### Added

#### `mainwindow.h` / `mainwindow.cpp`
- **File drag & drop into the playlist**: audio files can now be dragged
  onto the playlist (`audio_list`) from the OS file manager — or from
  the built-in file/jingle browsers (drag is now enabled on both
  `QTreeView`s). Files are added as `music` items, folders as
  `folder-music` items, matching the existing "add from file browser"
  behavior.
- **`addFileToPlaylist()`**: the TagLib metadata + duration logic that
  was duplicated in the double-click handlers was extracted into a
  single helper, now shared by drag & drop and both file browsers.
- **`dragEnterEvent()` / `dropEvent()`**: drops carrying file URLs are
  accepted anywhere on the window and added to the playlist. The drop
  target deliberately doesn't rely on `widgetAt()`/position mapping
  (which is unreliable during real drags and after window resizes), so
  drag & drop keeps working at any window size — including after the
  window is resized.
- **Delete key on the playlist**: pressing `Del` while the playlist
  (`audio_list`) has focus removes the selected item — same as the
  remove button. Ignored while the file browsers have focus.
- **Multi-selection on the playlist**: `ExtendedSelection` mode —
  `Ctrl`+click for individual rows, `Shift`+click for ranges. The
  remove button and the `Del` shortcut now remove **all** selected
  rows at once (removing falls back to the current row when nothing
  is selected, matching the old single-item behavior).
- **Playback pointers survive removal**: `current_play`/`next_play` are
  decremented for each removed row above them, so removing rows above
  the currently-playing track keeps pointing at that same track (was
  index-clamped only, which could skip tracks after a deletion).

#### `configdialog.ui` / `configdialog.cpp`
- **Sound output settings (Saídas tab)**: the placeholder tab now
  lets you pick the audio output device — "System default" or any
  device reported by `QMediaDevices::audioOutputs()`. The list
  refreshes live while the dialog is open (device hot-plug/unplug)
  and the choice is saved to `audio/outputDevice` (by device id).

#### `audioplayer.h` / `audioplayer.cpp` / `buttonhole.h` / `buttonhole.cpp`
- **`AudioPlayer::configuredAudioDevice()`**: resolves the saved
  device id against the currently connected devices, falling back to
  the system default when nothing is saved or the saved device is
  gone. Every player applies it on construction — both crossfade
  players and the botoeira (`ButtonHole`) buttons.

#### `mainwindow.h` / `mainwindow.cpp`
- **`applyAudioOutputDevice()`**: pushes the configured device to all
  live outputs (crossfade players, hour-announcement player, botoeira
  buttons) at startup and again after the settings dialog closes —
  only when the device actually changed, so opening the dialog
  mid-song doesn't interrupt playback.

#### `cuewindow.ui` / `cuewindow.h` / `cuewindow.cpp`
- **Pre-cue mini player (Pré Escuta)**: the previously disabled
  "Pré Escuta" playlist context-menu action now opens a small
  always-on-top preview window with its own transport — seek bar with
  `mm:ss` times, play/pause, stop and a headphone volume slider. It
  plays the right-clicked row (`music`/`jingle` files, a random file
  from folder items, the current-hour time audio) through a dedicated
  `AudioPlayer` that is never connected to the VU meter or silence
  watchdog, and never stops on its own — only the window's own
  controls or closing it end the preview. Picking another row
  retargets the same window.

#### `mainwindow.h` / `mainwindow.cpp`
- **Pre-cue from the file browsers**: right-clicking a row in either
  left-hand file browser (`files` — Músicas, `jingle_files` — Jingles)
  now offers **Pré Escuta**, opening the same cue window as the
  playlist's action. Media files play as they are; folders preview a
  random track inside, matching what the playlist's folder rows do.
  Non-media files (`.xml`, `.png`, `.desktop`, …) show no menu, since
  the player could not open them anyway. Both browsers share a single
  context-menu handler.
- **`cuePreview()` split into `cuePath()` / `cuePlay()`**: the window
  creation, `loadAndPlay()` and raise/activate sequence moved into
  `cuePlay(path, displayName)`, shared by the playlist and the file
  browsers. The folder random pick moved to the `randomMediaInFolder()`
  helper, and the new `mediaDisplayName()` gives a file-browser item
  the same `title - artist` label the playlist uses.

#### `configdialog.ui` / `configdialog.cpp`
- **Cue output device (Saídas tab)**: a second combo
  (`Dispositivo de fones (cue)`, saved to `audio/cueDevice`) routes
  previews to a dedicated headphone device; its first entry
  (*Usar saída principal*) follows the main output. Both combos
  refresh together on device hot-plug.

#### `audioplayer.h` / `audioplayer.cpp`
- **`configuredCueDevice()` / `Pause()`**: resolves the saved cue
  device with fallback to the main output when unset or gone, and adds
  a pause state for the pre-cue window. The cue player keeps
  `maxVolume` in sync with its volume slider so the shared fade timer
  never touches the preview level.

#### `spectrumanalyzer.h` / `spectrumanalyzer.cpp` (new)
- **Spectrum analysis for the video output**: an iterative radix-2 FFT
  over a Hann-windowed mono downmix, fed from `calculateRMS()` — the
  same buffers that already drive the VU meters, so there is no extra
  tap on the audio path. No new dependency.
- **64 bars, log-spaced where the transform allows it**: band edges are
  spaced logarithmically from 30 Hz, then converted to FFT bins
  (`bin = Hz * kFftSize / rate`) and each bar is given at least one bin.
  Without that last step the low bands would all collapse onto bin 1
  (43 Hz wide at 44.1 kHz), read identically, and put the peak in
  whichever band came last.
- **Instant attack, gradual release**: bars snap up on a transient and
  ease back down, and a peak marker lingers a couple of seconds above
  them. Levels are scaled across -72..-12 dB.
- **One filter, one owner**: `analyse()` now only computes the meter
  (instant attack, timed release) while `takeFrame()` only applies the
  display filter and writes the row. Both used to smooth *and* both
  wrote `m_row`, on separate clocks — a filter running ~98 times a
  second instead of once per repaint, plus two threads racing over the
  same pixels. The filter is a 20 ms time constant applied once per
  painted frame and only on the way down, so attack is not filtered at
  all. Time from a transient to 90% of full scale dropped from 64 ms
  (5 painted frames) to 32 ms (3 painted frames).
- **Paced to the display's refresh rate**: the FFT is throttled to one
  update per painted frame rather than one per audio callback. The
  interval comes from `QScreen::refreshRate()`, so the graph advances in
  step with the display instead of at a fixed 30 fps.
- The only thing handed to the renderer is a 64x1 RGBA image (256
  bytes: bar height in R, peak in G), copied under a mutex — the
  `audioBufferReceived` callback runs on the multimedia thread while
  `paintGL` runs on the GUI thread.

#### `videomixer.h` / `videomixer.cpp` / `shaders/eqbars.frag` (new)
- **EQ visualizer pass**: when the incoming deck has no active video
  (`!isVideoActive()`) and no transition is running, the output shows
  the bar graph instead of a transition effect running over black.
  Real video is untouched, and `fromHeld` — which keeps the outgoing
  deck's last frame on screen — is never blanked mid-crossfade.
- **`renderEq()`**: the fragment shader draws the bars, the gaps
  between them and the green→yellow→red ramp from height. The CPU only
  uploads the 256-byte level row per frame, so the visual is entirely
  GPU-side.
- A dedicated pass rather than an effect `.frag`, so custom user shaders
  in the shader folder keep their existing single-sampler interface.
- Failures are sticky, per shader: a missing or broken fragment is
  reported once and then skipped, never retried per frame.

#### `shaders/eqcircle.frag` (new) / `videomixer.h` / `videomixer.cpp`
- **Circular (radial) EQ mode**: the same 64-band row fanned around the
  centre of the output instead of standing on its bottom edge, with a
  clear hub, spokes growing outward and the peak marker riding at its
  own radius. Selected with `video/eqvisualizer = circle`.
- The spoke fan is offset by half a slot, so a spoke lands on every
  screen axis. Without it the seams fall exactly on 0/90/180/270° and
  those four directions come out blank.
- The seam at 360° wraps with `mod(floor(slot), uBands)` rather than
  clamping: `atan` returns exactly 2π along the negative-x half of the
  centre line, and clamping would map that to a one-texel-thin strip at
  the end of the row instead of back to band 0.
- Positions are corrected for aspect ratio before the radius is taken,
  so a 16:9 output gets a circle and not an ellipse.
- `ensureEqProgram()` relinks when the mode changes (the fragment path
  it was linked from is remembered), and failures are tracked per path
  so one broken visualizer doesn't take the other one down.
- **Fixed a null dereference**: `refreshIncomingDeck()` called
  `m_decks[n]->isVideoActive()` unguarded in its fallback branch, so
  showing a `VideoMixer` before `setDeck()` crashed.

#### `videomixer.h` / `videomixer.cpp` / `spectrumanalyzer.h` / `spectrumanalyzer.cpp`
- **Repaint at the display rate**: the mixer's repaint timer was pinned
  at 33 ms (~30 fps), which capped the visualizer at 30 fps even on a
  faster display. It now runs at `QScreen::refreshRate()` and is a
  `Qt::PreciseTimer`, so the repaint lands on the vsync boundary instead
  of drifting against it. Rate is re-read when the window moves to
  another monitor (`ScreenChangeInternal`).
- `syncFrameRate()` is the single place that decides the target rate and
  pushes it to the analyzer, so the two cannot drift apart.
- **Fixed a pacing bug**: `feed()` claimed to throttle to one FFT per
  painted frame but never actually skipped anything — it computed the
  elapsed time and fed it to the decay while still running the FFT on
  *every* audio callback. With the AAC in an `.mp4` (1024-frame buffers)
  that is ~43 FFTs/s regardless of what the display could show. The gate
  is now real: buffers arriving inside the frame interval are dropped
  before the FFT.

#### `videowindow.h` / `videowindow.cpp` / `mainwindow.h` / `mainwindow.cpp`
- Wires the analyzer through to the mixer. `MainWindow` owns the
  analyzer and `VideoMixer` only borrows the pointer. `reset()` is
  called on stop so the output doesn't keep showing the last frame's
  spectrum after the transport halts.

#### `configdialog.ui` / `configdialog.cpp`
- **EQ visualizer (Vídeo tab)**: a combo saved to `video/eqvisualizer`
  with *Desligado* (default), *Barras* and *Círculo*. Opt-in, so
  existing setups are unchanged.

### Fixed

#### `spectrumanalyzer.cpp`
- **The FFT computed its twiddle's imaginary part from the real part
  twice**: `vi = re[..] * ci + re[..] * cr`, where the second `re` should
  have been `im`. Every butterfly therefore corrupted `im` starting at
  the first stage, leaving a broadband floor of garbage 20–30 dB under
  the peak in *every* band. A single 1 kHz tone lit 31 of 32 bars
  between 43 and 182/255, so the graph read as a solid wall that merely
  pulsed — adding bars to it would not have helped. The tone still
  peaked in the right band, which is exactly why the tone-placement
  check passed; it now also asserts that a tone lights at most a
  quarter of the bars. After the fix the same tone lights 8 of 64 and
  every other band is a clean 0.
- **`m_updateTimer` was never started**, so `elapsed()` returned a huge
  negative number and the gate in `feed()` compared it against a
  `sinceMs < 0` branch that could never fire. This is why the pacing fix
  noted above had not actually taken effect: every audio buffer ran an
  FFT (~38/s from MP3) no matter what the display could show. The timer
  starts in the constructor, the gate drops buffers inside the frame
  interval again (200 buffers fed back to back now cost 1 transform, not
  204), and `analysisCount()` exposes the count so the gate can be
  checked directly rather than by timing a loop.

### Changed

#### `mainwindow.ui` / `resources.qrc` / `deploy/linux/` / `.github/workflows/appimage.yml`
- **New app icon**: the main window now sets `windowIcon` from
  `:/images/icon.png` (previously it had no icon at all and fell back to
  the generic Qt one). `images/icon.png` (1024×1024) is the master and
  is embedded through `resources.qrc`; the old
  `deploy/linux/lararadio.png` was replaced by
  `deploy/linux/icon.png` (512×512, regenerate with
  `convert images/icon.png -resize 512x512 -strip deploy/linux/icon.png`).
  Two sizes are needed because linuxdeploy **rejects** anything outside
  the hiconf sizes (max 512×512) and aborts the AppImage build, while Qt
  is happy with the master. The desktop entry's `Icon=` is now `icon`
  — with no extension, as appimagetool resolves it as
  `AppDir/<Icon=>.png`.

#### `mainwindow.ui` / `mainwindow.h` / `mainwindow.cpp`
- **Resizable window**: the window was fixed-size (`setFixedSize` +
  `sizePolicy Fixed`). It is now freely resizable (minimum 800×480).
  Since the UI is built with absolute positioning, `resizeEvent()` now
  scales every widget proportionally to the design size (1048×622) —
  the layout keeps its exact proportions at any window size, including
  the programmatic VU meters and button-hole buttons.

#### `configdialog.ui`
- **Tabbed settings dialog**: the settings dialog now uses a
  `QTabWidget` with the tab bar on the **left side**
  (`TabPosition::West`), splitting the old single screen into four
  sections: **Fade** (fade timing spinboxes + stop/talk fade options),
  **Caminhos** (the three directories), **Saídas** (empty placeholder
  for future output settings) and **Comportamento** (clock options).
  The dialog now uses real layouts instead of absolute positioning.

#### `videomixer.h` / `videomixer.cpp` / `configdialog.cpp` / `mainwindow.cpp`
- **EQ visualizers are table-driven**: `VideoMixer::eqVisualizers()` is a
  single list of `{ id, label, fragmentPath }` rows and is now the only
  place a visualizer is declared. The persisted setting
  (`video/eqvisualizer`), the settings-dialog combo, the fragment
  `ensureEqProgram()` links and the "draw the EQ rather than an effect"
  gate in `renderScene()` all read from it, so adding one costs a table
  row, a `shaders/eq*.frag` and a `resources.qrc` entry. It used to cost
  those plus an `EqMode` enumerator, two `if` chains, a ternary, a
  condition growing another `||` and a hardcoded `addItem` in the dialog.
- **`EqMode` / `modeFromString()` / `modeToString()` are gone**: the mixer
  stores the persisted id instead, via `setEqVisualizer()`,
  `eqVisualizerId()` and `eqVisualizer()`. An id the build does not know
  — settings written by a newer version, or a hand-edited file — resolves
  to null and reads as "off", exactly like an unconfigured install, and
  `setEqVisualizer()` normalises it so nothing re-resolves it differently
  later. The stored values `bars` and `circle` are unchanged, so existing
  configurations keep working; `cfgtest` still asserts the unknown-value
  fallback, now against the table rather than a hardcoded count.
- **Combo entries stay translatable**: the labels are `QT_TRANSLATE_NOOP`
  literals inside the table, because the table is built before Qt loads
  any translation and because `lupdate` cannot extract a `QString`
  variable. `ConfigDialog` translates them with
  `QCoreApplication::translate("VideoMixer", ...)`. This is an
  improvement over the old `tr("Barras")` calls, which lived in the
  `ConfigDialog` context: a fresh `lupdate` run now finds them at all.

---

## [1.0.5] — 2026-07-27 — Changes on top of original 1.0.4

### Changed

#### `audioplayer.h` / `audioplayer.cpp`
- **Inheritance**: `AudioPlayer` changed from `QMediaPlayer` subclass to
  `QObject` subclass containing `QMediaPlayer *player` as a member
  (composition over inheritance). This decouples the audio backend from
  the public API and allows more flexible lifecycle management.
- **Volume**: default audio output volume changed from `0` to `1.0`
  (QMediaPlayer is now the actual audio source, see below)
- **MP3 transcoding**: added `transcodeIfNeeded()` — converts MP3 files
  with embedded album art to temporary WAV via `ffmpeg -vn` before
  playback. This prevents the `mp3float` decoder crash that occurred
  after multiple songs with album art.
- **Error handling**: connected `QMediaPlayer::errorOccurred` →
  `onPlayerError()` which emits `mediaError()` signal
- **End-of-track detection**: connected `QMediaPlayer::mediaStatusChanged`
  → emits `playbackFinished()` on `EndOfMedia`
- **Destructor**: explicit destructor to clean up `player` and `audioOutput`
- **Inline helpers**: `getPosition()`, `getDuration()`, `remainingTime()`,
  `isPlaying()`, `isPaused()`, `isStopped()`, `getVolume()`, `setVolume()`
  all simplified to single-line inline implementations
- **`isValidMediaFile()`**: static method using `ffprobe` to validate
  audio files before queueing
- **`hasError()`**: public error state flag

#### `main.cpp`
- **Crash handler**: added `crashHandler()` for `SIGSEGV`, `SIGABRT`,
  `SIGFPE` — prints diagnostic message to stderr and exits cleanly
  with code `128 + signal`. Prevents silent crashes from FFmpeg
  decoder bugs.
- **Dark theme**: activated the Fusion dark theme palette (was
  previously commented out). Dark background `#303030`, base `#242424`,
  text `#dcdcdc`, highlight `#55aaff`. Falls back to GTK3 theme when
  available.
- **Splash screen removed**: startup no longer waits on the splash's
  fixed 2-second delay (`QSplashScreen` + "simulate work" timer). The
  main window is shown as soon as initialization finishes — faster
  startup (≈2 s saved). The `splash-*.png` assets were removed from
  `resources.qrc`.

#### `mainwindow.h` / `mainwindow.cpp`
- **`skipToNext()`**: public slot — resets both players, advances
  `current_play`, and calls `next()`. Used when a track errors out.
- **`checkAdvanceTrack()`**: public slot — advances the playlist when
  `playbackFinished` fires and both players are stopped. Single point
  of playlist advancement.
- **Error recovery**: `mediaError` signal from both `AudioPlayer` instances
  connected to `skipToNext()` — auto-skips on decoder errors
- **Playlist advance**: `playbackFinished` signal from both players
  connected to `checkAdvanceTrack()`
- **Time audio**: checks `QFile::exists()` before attempting to play
  the time announcement file. If missing, logs a warning and skips —
  prevents `SayingTimer` deadlock.
- **Timeplayer error handler**: connected `QMediaPlayer::errorOccurred`
  on the time player — clears `SayingTimer` and advances on error
- **Removed `flash()` double-advance**: the 500ms `flash()` timer no
  longer advances the playlist. Advancement is exclusively handled by
  `checkAdvanceTrack()` via `playbackFinished`. This prevents race
  conditions on short tracks (jingles) where both mechanisms could
  advance independently.
- **Silence / audio failure watchdog**: monitors the VU meter via
  `updateDisplay()` (10ms timer). If a player is in `PlayingState`
  but no audio reaches the VU meter for 10 seconds (and no fade is
  active), automatically calls `skipToNext()`. Covers device failure,
  silent decoder bugs, and stuck pipes.
- **Segfault fix on playlist edit while playing**: `clearPlaylist()`
  emptied the playlist while `isPlaying` stayed true and
  `current_play` stayed stale, making `topLevelItem()` return
  nullptr → SIGSEGV. Now: `clearPlaylist()` resets both players and
  playback state; `updateAudioList()` null-checks `curItem`/`nextItem`
  before styling; `on_btn_remove_item_clicked()` clamps
  `current_play`/`next_play` after erase; `next()` clamps
  `current_play` before indexing.
- **Stop button works with empty playlist**: removing the last
  (currently-playing) track left the playlist empty while audio kept
  playing — `on_btn_stop_clicked()` early-returned on empty playlist
  and never stopped the players. Now it only early-returns when the
  playlist is empty **and** no player is playing. Also guarded
  `playlist[current_play]` access in `updateAudioList()` for the
  empty case (was out-of-bounds UB).

#### `buttonhole.h`
- Added explicit `#include <QMediaPlayer>` and `#include <QAudioOutput>`
  (were previously pulled indirectly via `audioplayer.h` when it
  inherited from `QMediaPlayer`; now it inherits from `QObject`)

### Known issues (original, not introduced by us)
- TagLib `AudioProperties::length()` is deprecated in favor of
  `lengthInSeconds()` — 3 warnings during build
- VDPAU backend warning on Radeon GPUs (harmless, video acceleration
  only)
- GTK theme parsing warning with certain `gtk-contained-dark.css`
  versions (harmless)
