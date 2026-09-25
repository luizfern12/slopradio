# SlopRadio
SlopRadio is an enhanced fork of [gutierre69/lararadio](https://github.com/gutierre69/lararadio) that leans heavily on LLM-generated code — hence the "slop" in the name.
> See [What's new vs the original](#whats-new-vs-the-original) for what this fork adds on top.

---

## Install

### Requirements

- x86_64 Linux — the AppImage is built on Ubuntu 22.04 and runs on Ubuntu 22.04+, Debian 12+ and similar distros.
- `ffmpeg` in `PATH` is **recommended**: it lets SlopRadio transcode MP3 files with embedded album art on the fly (avoids a decoder crash). Playback works without it — such files simply fall back to playing the original.

### Download & run

The AppImage is rebuilt automatically on every push to `main` and published as a *continuous* pre-release:

**[⬇ Download the latest AppImage](https://github.com/luizfern12/slopradio/releases/tag/continuous)**

```bash
chmod +x LaraRadio-1.1.0-x86_64.AppImage
./LaraRadio-1.1.0-x86_64.AppImage
```

---

## What's new vs the original

The original LaraRadio kept the station on air with a solid core: playlist automation, jingles, time announcements, VU meters and clock. This fork keeps all of that and adds:

### 🎬 Video output with crossfade transitions
Tracks with video play in a dedicated video window, and the same crossfade used for audio blends the video between tracks (OpenGL mixer, shader transitions supported). Video decoding can be hardware-accelerated — Auto / VA-API / CUDA / Off, chosen in **Settings → Video** — which keeps weak CPUs from dropping frames.

### 🚀 Faster startup, modern look
- Splash screen removed — the main window appears as soon as initialization finishes (≈ 2 s faster).
- The main window is freely resizable and keeps its proportions at any size.

### 🛡️ Stability the original didn't have
- **Crash handler** — fatal errors print a diagnostic message instead of dying silently.
- **Error auto-skip** — a failed track is skipped automatically instead of stalling the rotation.
- **Silence watchdog** — if a track "plays" but no audio reaches the meters for 10 s, it is skipped.
- **MP3 album-art transcoding** — avoids the `mp3float` crash after several songs with cover art.
- Fixed: segfaults when editing the playlist mid-playback, double-advance races on short jingles, and a Stop button that didn't stop with an empty playlist.

### 🎧 Studio workflow
- **Pre-cue** — right-click any playlist row to open a cue window with its own transport (seek, play/pause/stop) and **headphone volume**, routed to a separate output device.
- **Output device selection** — pick the main output *and* a dedicated cue/headphones device (**Settings → Saídas**), hot-plug aware.
- **Playlist improvements** — drag & drop from the file manager (files or whole folders), multi-select with `Ctrl`/`Shift` + `Del` to remove, and playback pointers that survive row removals.

### 🛠️ Settings
The settings dialog was reorganized into tabs — Fade, Caminhos (paths), Saídas (outputs), Comportamento (behavior) — plus the new Video tab.

---

## Build from source

Requirements: Qt 6.8+, CMake 3.16+, a C++ compiler and TagLib.

```bash
./build.sh          # compiles translations, configures and builds
./build/appLaraRadio
```

---

## Resources

- Full change log: [`CHANGELOG.md`](CHANGELOG.md)

---