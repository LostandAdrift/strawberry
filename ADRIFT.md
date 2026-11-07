# Adrift

Adrift is a fork of [Strawberry Music Player](https://github.com/strawberrymusicplayer/strawberry) by LostandAdrift.
It is a native Qt 6 desktop player focused on a calm, readable listening experience and on being honest about what happens to your audio.

Adrift is free software under the GNU GPL v3 or later, the same license as Strawberry.
All credit for the player, the audio engine and the collection management goes to Jonas Kvinge and the Strawberry and Clementine contributors; see `COPYING` and the upstream repository.
Internal names (settings, binary, D-Bus, file formats) are unchanged so the fork stays compatible with upstream and can keep merging it.

## What is different

### Color themes (Settings → Appearance → Colors → Color theme)

- **Adrift Dark** (the default on a fresh install): a deep blue-black theme with warm off-white text and one luminous accent. Text roles are above 7:1 contrast against their backgrounds.
- **System**: the Qt/desktop colors, unchanged. Installs that already saved an explicit color choice (system or a custom color set) keep it; only installs with no saved choice get Adrift Dark.
- **Omarchy colors (read when Adrift starts)**: uses the colors of the current Omarchy theme. Adrift reads `$XDG_STATE_HOME/omarchy/current/theme/colors.toml` (default `~/.local/state/omarchy/...`) and falls back to the older `~/.config/omarchy/current/theme/colors.toml`. The file is parsed as plain data with a small built-in parser; nothing from it is ever executed. It understands the flat `key = "#rrggbb"` layout (`background`, `foreground`, `accent`, `darker_background`, `lighter_background`, `color0`–`color15`, …).
  Text roles are corrected to at least 4.5:1 contrast (WCAG AA) against their backgrounds, so a low-contrast theme stays readable.
  A missing, unreadable, oversized, invalid or incomplete file is skipped; if no usable file is found the system colors are used.

The Omarchy theme is read when Adrift starts and when you change the setting. It does not live-update when you switch Omarchy themes: restart Adrift (or re-select it in Settings) to pick up the new theme.
A named theme overrides the custom color set; choose **System** to go back to it.
Previewing a theme and pressing Cancel restores the whole palette, including highlight and link colors.
Some Qt styles (for example Breeze) ignore palettes; use the Fusion style for full effect.

### Audio signal path badge and details

While a track plays, a small badge in the status row shows the source and the output, for example `FLAC 16/44.1 · System mixer` with a separate chip `App DSP off` or `App DSP on`. Two things are kept apart on purpose:

- **App DSP** answers only "does Adrift itself change the samples?" It is on if the software volume is below 100% (or muted), or ReplayGain, loudness normalization, the equalizer, stereo balance, crossfeed, a forced channel count or fades are active. With volume control disabled the engine has no volume stage, so the volume level and mute do not count.
- **The output** is described separately: `Direct ALSA`, `ALSA plugin`, `System mixer` (PulseAudio/PipeWire), `Auto output`, or the sink name. A sound server mixes and may resample, so `App DSP off` never reads as an all-clear.

The badge never claims "bit-perfect". Adrift cannot see what a sound server, driver or DAC does after the samples leave the application. The strongest statement is `App DSP off` plus a note such as "Direct ALSA output. Not verified end to end."

The source comes from the track's own metadata (codec, bit depth, sample rate). Hi-Res is only claimed for lossless sources reporting more than 16 bit or more than 48 kHz; a lossy file with a "24 bit" tag is still lossy. Until a stream's metadata arrives the source stays `Source unknown` instead of being guessed.

The badge is a real button (keyboard focusable, Space/Enter to activate, accessible name and description). Long text is elided so a long tag cannot widen the window. Click it to open the **Audio signal path** dialog with Source, Processing and Output sections, the caveats that apply, copyable technical text ("Copy details"), and an "Output settings…" button that opens the Backend settings.

### Import streaming playlists (Tools → Import streaming playlists…)

A read-only importer for **Tidal** and **Qobuz** playlists, using the login you already configured in Settings (no keys or tokens are stored in the program).

1. Choose the service and press **Load playlists**. Every page is read, using the number of entries the server returned for paging.
2. Select a playlist and press **Import**. Adrift reads all its tracks and creates a local playlist named `Tidal: <name>`, in the remote order, keeping repeated tracks. Tracks the service marks unavailable are left out and counted.
3. Importing the same playlist again (the button then reads **Refresh snapshot**) replaces the contents of that local snapshot with the current remote list. It is a refresh, not a two-way sync: nothing is ever written to the service, and your own manually built playlists are never touched.

The remote identity (`tidal:<playlist id>`) is stored with the local playlist, so a refresh finds its snapshot after a restart. If you closed the snapshot playlist, the next import creates a new one.
The dialog shows loading (with progress and a Cancel button), empty, and error states. Controls are disabled while loading, and closing the dialog cancels the request. If anything goes wrong (network error, expired login, malformed reply, a list that changes while being read, or too many pages) nothing is imported and the existing snapshot is left as it was.

Limits: this uses the services' playlist endpoints for the signed-in user (`users/{id}/playlists` and `playlists/{id}/items` for Tidal, `playlist/getUserPlaylists` and `playlist/get` for Qobuz). Their availability for your account and the token's permissions could only be exercised with mocked replies in the tests; a live check with a real account is still needed. Tidal "favorite" playlists that you did not create are not listed.

### Direct ALSA setup (Settings → Backend, with an ALSA output)

The **Direct ALSA setup…** button opens a guided helper for sending audio straight to a hardware device with Adrift's own processing off.

- You must choose your device first. Only raw hardware (`hw:`) devices are offered, there is no default selection, and **Apply** stays disabled until you choose one.
- Applying changes the settings *page* only: output → ALSA, device → your `hw:` device, ReplayGain/loudness normalization/crossfeed/forced channels/fades → off. You still press OK or Apply in Settings to save. A summary lists exactly what will change.
- **It never changes your volume** and does not turn volume control off (which would play at full level). The dialog warns if the software volume is below 100% (which alters the samples), if the equalizer or stereo balance is on, and that a direct device can be much louder and is usually exclusive.
- The previous values are kept and **Restore previous settings** puts them back.

Direct ALSA also needs a hardware device that is not used by another program, and a file whose format the hardware supports.

## Building

Adrift builds exactly like Strawberry (C++17, Qt 6.4+, GStreamer). See `README.md` and `CLAUDE.md` for dependencies.

```
cmake -S . -B build
cmake --build build --parallel $(nproc)
```

Optional components can be disabled when their dependencies are missing, for example `-DENABLE_LAYERSHELLQT=OFF -DENABLE_TRANSLATIONS=OFF -DENABLE_STREAMTAGREADER=OFF`.

## Tests and renders

```
cmake --build build --target build_tests
cmake --build build --target test        # runs every test with CTest
```

New tests: `themepalette_test`, `signalpath_test`, `signalpathwidget_test`, `playlistimport_test` (fake network, multi-page, expiry, cancel, refresh), `directalsa_test`, and `adrift_render_test`.

`adrift_render_test` renders the real widgets, dialogs and palettes with synthetic data to PNG files without showing any window (it runs on Qt's offscreen platform). To keep the images, configure with `-DADRIFT_SCREENSHOT_DIR=<directory>`.

## Known limits

- The Omarchy theme is not live-updated (see above).
- Tidal/Qobuz import has not been run against a live account in this repository's tests.
- Sample-rate/bit-depth of streamed tracks is only shown when the service reports it; otherwise the source shows as unknown.
- Not rebranded (name, icon, desktop file) so the fork stays easy to merge with upstream.
