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

### Changed

#### `mainwindow.ui` / `mainwindow.h` / `mainwindow.cpp`
- **Resizable window**: the window was fixed-size (`setFixedSize` +
  `sizePolicy Fixed`). It is now freely resizable (minimum 800×480).
  Since the UI is built with absolute positioning, `resizeEvent()` now
  scales every widget proportionally to the design size (1048×622) —
  the layout keeps its exact proportions at any window size, including
  the programmatic VU meters and button-hole buttons.

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
