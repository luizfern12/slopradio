# Plan — Video hardware decode (portable) + frame-drop fixes

Status: **implemented and verified — B1 + B2 + runbook committed.** Written 2026-09-24.
B1 = `ef212a6`, B2 = `6c59588`; runbook = `docs/aceleracao-de-video.md`.

## Problem

On the target box (E-300 / Radeon HD 6310, "PALM") audio plays fine but **video drops
lots of frames**. The E-300 is a 1.3 GHz Bobcat CPU — software H.264 ≥720p decode alone
can't keep 30 fps, and even with hardware decode our render path burns CPU.

### Video pipeline (current)

1. **Decode** — `QMediaPlayer` (Qt 6.11 FFmpeg backend). HW decode may or may not engage.
2. **Readback** — `VideoMixer::uploadFrames()` calls `QVideoFrame::toImage()` → full-frame
   YUV→RGBA conversion **on the CPU**, then `QOpenGLTexture::setData()` (4 B/px upload),
   at ~30 fps per deck (paint timer `m_timer` interval = 33 ms).
3. **Crossfade** — at `seconds == startTransitionAudioTime` (`mainwindow.cpp:846`) **both
   decks decode + upload simultaneously** (2× cost exactly when CPU is maxed).

## Known facts / recon

### Target box (SSH: luizfern12@192.168.1.160, user gave creds)

- Arch Linux, kernel 6.18.53-1-lts, Qt `qt6-base 6.11.2-3`, mesa 26.2.3, libva 2.24.1.
- App runs as **AppImage**: `~/Desktop/LaraRadio-1.1.0-x86_64.AppImage` (72 MB, bundles Qt).
- X session on `:0`; tools present: `python3`, `gcc`, `ffprobe`, `libXtst` (XTest via
  ctypes → can synthesize clicks, no installs needed); no `xdotool`.
- `vainfo` works: Mesa Gallium driver for AMD PALM, H.264 Main/High (VLD) + MPEG-2/VC-1
  profiles. **UVD 3.0: H.264/MPEG-2/VC-1 only** — no HEVC/VP9/AV1 ever.
- Config `~/.config/LaraRadio/LaraRadio.conf` currently has **`video/enabled=false`**
  and no persisted playlist (playlist is per-session). `defaultDir=/home/luizfern12`.
- Dev box is also Arch + Qt 6.11.2 → binary runs on the box if it uses the AppImage's
  bundled Qt libs (extract AppImage, set `LD_LIBRARY_PATH`/`QT_PLUGIN_PATH`).

### Qt 6.11 (official docs, advanced-ffmpeg-configuration)

- `QT_FFMPEG_DECODING_HW_DEVICE_TYPES` — priority list of backends
  (`vaapi`, `cuda`, `d3d11va`, …). **Unset = auto-pick. `=,` = disable all HW decode.**
- `QT_FFMPEG_DEBUG=1` + `QT_LOGGING_RULES="*.multimedia.*=true"` → logs
  "Creating VAAPI HW accelerator" etc. Both set → full codec dump on first decode.
- `QT_FFMPEG_HW_ALLOW_PROFILE_MISMATCH=1` → lets HW decode profiles that don't match
  reported caps (troubleshooting knob for old UVD; may produce wrong output).
- VAAPI hw texture conversion is off by default; `QT_XCB_GL_INTEGRATION=xcb_egl` enables
  it — **irrelevant to us**: our `toImage()` bypasses Qt's texture path entirely.

### Vendor portability (requirement: works for AMD, Intel, NVIDIA)

| Vendor | Path |
|---|---|
| AMD (any gen incl. PALM) | mesa VA-API (`r600`/`radeonsi`) — same code path |
| Intel | VA-API (native) — `intel-media-driver` / `libva-intel-driver` |
| NVIDIA | Qt `cuda` device type (NVDEC, proprietary driver), or `nvidia-vaapi-driver` |

⇒ the app must **not** hardcode `vaapi`; expose Auto/VA-API/CUDA/Off.
⇒ the frame-path fix (B2) is decode-agnostic: it works for HW *and* software decode.

## Phases

### Phase 0 — Diagnose on the box (DONE 2026-09-24, box IP is 192.168.1.208)

`vaapitest` (headless QCoreApplication + QMediaPlayer + QVideoSink) run on the box
against **system Qt 6.11.2** (installed `qt6-multimedia`; the AppImage bundles Qt
**6.9.1** — CI-built, so a dev-built 6.11 binary can't run against its libs):

| mode | delivered format | sustained fps (720p30) |
|---|---|---|
| auto (var **unset**) | **NV12** — VAAPI engaged ✓ | ~30 |
| `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=vaapi` | NV12 ✓ | ~30 |
| `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=,` (off) | YUV420P (sw) | ~30 |
| `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=` (empty) | YUV420P (sw!) | ~30 |

- VAAPI works on PALM (UVD 3.0, H.264 Main/Baseline/High); NV12 arrives with
  BT.709 / Video-range tags when the file declares them, Undefined otherwise.
- **Decode is not the bottleneck**: two concurrent 720p HW decodes both held
  ~30 fps with load avg 0.48 → drops come from the `toImage()` readback path (B2).
- All sample clips are 8-bit H.264 (yuv420p) → UVD-compatible.
- Gotcha: empty-string value disables HW decode (only *unset* = auto). B1 must
  leave the var unset for "auto", never set it to "".

### Phase 0.5 — Texture recreate on resolution change (DONE — 4a3e91f)

Fixed before any VAAPI work, since it caused real-world SIGSEGV + corruption when
skipping between videos of different resolutions:

- Root cause: `QOpenGLTexture::setSize()` refuses to change dimensions once storage
  is allocated (Qt warns "Cannot resize a texture that already has storage
  allocated." and returns), so the resize guard in `VideoMixer::uploadFrames()` stayed
  true forever and `setData()` uploaded each new frame with the *previous* track's
  dimensions — heap over-read (crash) when old > new, stale strips when old < new.
- Fix: on dimension mismatch, `destroy()` → `setFormat(RGBA8_UNorm)` → `setSize()` →
  `allocateStorage()` → re-apply ClampToEdge/Linear (destroy resets all of those).
- Verified: repro harness (8 skips cycling 1920x960 / 1280x720 / 640x360 / 320x240)
  went from 75 warnings + SIGSEGV to rc=0 / 0 warnings; all 34 window grabs match the
  source frames; `test_durations` and `mixtransit` regressions pass. Crash-handler
  text fixed too (942e286).

### B1 — Portable hardware-decode setting (Config → Vídeo) **(DONE — ef212a6)**

- `configdialog.ui` (tabVideo, before the spacer): label
  *"Decodificação de vídeo por hardware (aplicado ao reiniciar)"*, empty combo
  `video_hwdecode`, button `btn_checkVAAPI` *"Verificar VA-API (vainfo)"*.
- `configdialog.cpp` ctor: populate combo in code (same pattern as `video_transition`):
  `auto` "Automático (recomendado)", `vaapi` "VA-API — AMD e Intel",
  `cuda` "CUDA — NVIDIA", `off` "Desligada (CPU)"; select from `video/hwdecode` (default `auto`).
  Save in `accept()`.
- `main.cpp`: after `setOrganizationName/ApplicationName`, **before `MainWindow w;`**
  (players are created in MainWindow's ctor chain):
  ```cpp
  QSettings st;
  const QString mode = st.value("video/hwdecode", "auto").toString();
  if      (mode == "vaapi") qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "vaapi");
  else if (mode == "cuda")  qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "cuda");
  else if (mode == "off")   qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
  // "auto": leave unset → Qt picks
  ```
- `on_btn_checkVAAPI_clicked()`: `QProcess` runs `vainfo` (5 s timeout); show output +
  exit code in a `QMessageBox` (`setDetailedText`); friendly message when the binary is
  missing ("instale libva-utils"). NVIDIA/cuda users may legitimately see no VAAPI —
  note that in the dialog text.
- Existing global warning *"reinicie o LaraRadio"* covers the restart requirement.

### B2 — GPU frame path in `VideoMixer` (the frame-drop fix) **(DONE — 6c59588)**

**Local verification (dev box, Qt 6.11.2, RTX 3060, NV12 delivered = fast path exercised):**
- `mixtransit`: `final=1.000 resets=4 REGRESSIONS=0`; frames show content
  (bright 0→88% through crossfades).
- `mixer_convtest` (new harness, real decode → NV12 → plane upload → GPU convert):
  red601 → (254,0,0) = ffmpeg's own decode (253,0,0) ✓; blue601 (0,0,255) exact ✓;
  gray601 (128,128,128) vs 125 ✓; green601 matches ffmpeg's quirky clip decode
  (clip itself is half-green) ✓; untagged red (heuristic BT.601+limited) ✓.
- Orientation: top-red/bottom-black clip renders TL red / BR black → NOT flipped ✓.
- `main_resize`: `resize_warnings=0 rc=0` (plane/FBO recreate path) ✓.
- `test_durations`: all pass (audio unaffected) ✓.

Replace CPU `toImage()` with: **map raw planes → upload NV12/YUV420P directly →
YUV→RGB conversion in a small GPU pre-pass** (effects keep receiving plain RGB textures,
so all built-in *and custom* `.frag` shaders keep working untouched).

**Upload (`uploadFrames()`):**
- After the `m_lastFrameStart` check, try `frame.map(QVideoFrame::ReadOnly)`:
  - `Format_NV12` → mode 1: plane 0 = Y (`R8`), plane 1 = UV interleaved (`RG8`, w/2×h/2).
  - `Format_YUV420P` → mode 2: three `R8` planes (Y, U, V).
  - anything else / map failure → **existing `toImage()` path unchanged** (safety fallback;
    also covers `Format_P010` 10-bit, RGB sources, …).
- Upload with **stride awareness**: `glPixelStorei(GL_UNPACK_ROW_LENGTH, bytesPerLine/…)`
  + `UNPACK_ALIGNMENT=1` (restore defaults after); `glTexSubImage2D` on the plane
  textures after first-frame `allocateStorage`. ~2.7× fewer bytes than RGBA (1.5 vs 4 B/px).
- Record per deck: `m_yuvMode[i]`, color matrix + range from
  `frame.videoFormat().colorSpace()/colorRange()` (undefined ⇒ resolution heuristic:
  h > 576 → BT.709 else BT.601; range default = limited), `m_yuvDirty[i] = true`.
- No context dance needed on this path (`toImage()` is the only thing that can steal
  the GL context; keep that logic for the fallback path only).

**Convert pre-pass (`convertYuvFrames()`, called from `paintGL` between upload and render):**
- Per dirty deck: render the standard quad into a per-deck `QOpenGLFramebufferObject`
  (RGBA8, video size, lazy create/recreate on size change) with a hardcoded converter
  shader (`ensureYuvProgram()`, GLSL 110-style with `GL_ES` guard, attribute locations
  0/1 bound like `rebuildProgram()`):
  - samples `uY` / `uUV` (`uV` for planar), `uMode`, `uMatrix`, `uFull` uniforms;
  - **texcoords flipped** (`v = 1.0 - v`) so the FBO texture keeps the same orientation
    convention as the QImage-uploaded textures (otherwise the picture is upside-down);
  - BT.601/BT.709 coefficients selected by uniform, limited-range expansion
    (y−16/235, chroma ±112/224) or full-range passthrough; chroma upsampling = GL
    `Linear` on the half-size UV texture.
- Viewport = video size during the pass; `renderScene()` already resets the viewport.
- Bind back `defaultFramebufferObject()` explicitly after the pass.

**Render (`renderScene()`):**
- `sceneTex(deck)` = `m_fbo[deck]->texture()` (raw `glActiveTexture`+`glBindTexture`)
  when `m_yuvMode[deck] && m_fbo[deck]`, else `m_tex[deck]` as today; black-texture
  logic unchanged (`fromHeld` keeps the last converted frame — FBOs persist ✓).

**Also:** check whether `m_latest[2]` is used anywhere else before touching it; keep the
33 ms paint timer as-is.

### Runbook doc (after B1/B2) **(DONE — this commit)**

New doc (PT-BR, `docs/aceleracao-de-video.md`): vendor setup (AMD mesa /
Intel iHD / NVIDIA cuda-or-nvidia-vaapi-driver), `vainfo` + logging recipes,
how to read the "VAAPI HW accelerator" line, Phase-0-style diagnosis steps
(ffprobe, htop, drops-always-vs-crossfade), source guidance (**H.264 8-bit, ≤720p
recommended for Bobcat-class CPUs**), env-var table (auto/vaapi/cuda/off).

### Box verification (E-300/PALM, system Qt 6.11.2, AppImage path)

**Harness `mixer_convtest`** (real decode → mixer `paintGL` → `grabFramebuffer`;
now also polls `p.videoSink()->videoFrame().surfaceFormat()` → `[F]` lines):

| hwdecode | delivered format | check |
|---|---|---|
| `auto` | **NV12** ✓ | CONV_PASS, red=(254,0,0) vs ffmpeg decode (253,0,0), orientation TL red / BR black |
| `vaapi` | **NV12** ✓ | CONV_PASS |
| `off` | **YUV420P** ✓ | CONV_PASS (planar path, same good colors) |

**App-level on the box** (new binary `appLaraRadio.new`, isolated
`XDG_CONFIG_HOME`, `[video] transition=crossfade hwdecode=auto`):
runs stably on box Qt 6.11.2; video window renders live changing frames. With a
**human pressing Play once** on the box (blind XTEST clicks proved unreliable —
first-click-focus ate the press), the red601→blue601 crossfade was **confirmed
on screen**: red phase → blend → blue phase, looping. Slight lag on the E-300 is
expected (Bobcat ceiling); visually smooth otherwise. Screenshot timeline shows
full-screen-color phases + blend moments (mask aliasing at 1 s sampling, not app
defects). `hwdecode=off` app-level playback is covered by the convtest planar
test above.

## Verification

- **Local (RTX 3060, software decode):** build via `./build.sh`; exercise the planar
  fallback/fast path with `/tmp/opencode/mixertest/videoA30.mp4` etc.; visual regression
  vs a baseline build of current HEAD (build it *before* editing); transitions in both
  directions; optionally read back `grabFramebuffer()` pixels for a solid-color clip to
  assert the converter's color math. Reuse `/tmp/opencode/verify_totals.py` patterns
  (`DISPLAY=:1`, XTEST, stale shm retry).
- **On the box:** Phase 0 baseline with the *current* AppImage → then run the new build
  (scp binary + run against extracted AppImage Qt libs) with logging; verify VAAPI line,
  pixel formats, and smoothness during crossfades; `htop` before/after.
  Never kill the user's live instance; single-instance guard exits rc=0 silently if a
  stale shm segment exists (`ipcrm -M com.radiotools.lararadio.singleinstance`, only
  when `nattch==0`).

## Deliverables / commits (separate, in order)

1. `Add hardware video decoding setting (VA-API/CUDA)` — configdialog.ui/.cpp, main.cpp,
   `lupdate` + en_US entries for new strings.
2. `Convert video frames on the GPU (NV12/YUV420P fast path)` — videomixer.h/.cpp.
3. `Add video acceleration runbook` — the doc.
4. (optional) rebuild/deploy artifact for the box — no version bump unless asked
   (version stays 1.1.0; CHANGELOG convention to be checked before touching).

## Risks / open questions

- Whether Qt delivers NV12 (hw) vs YUV420P (sw) on the box — confirmed only after Phase 0.
- Whether HW decode engages at all on PALM (baseline-profile mismatch → try
  `QT_FFMPEG_HW_ALLOW_PROFILE_MISMATCH=1` as a documented fallback, not a default).
- Color-matrix heuristic vs what `toImage()` produced before (possible subtle shift on
  1080p untagged content; declared `colorSpace` wins when present).
- `QOpenGLFramebufferObject` default internal format / release semantics — bind the
  widget FBO explicitly after each pass.
- E-300 ceiling: even optimized, 1080p dual-deck crossfades will stay marginal;
  720p is the honest target.
