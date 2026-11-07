# Adrift

Adrift is a fork of [Strawberry Music Player](https://github.com/strawberrymusicplayer/strawberry) by LostandAdrift.
It is a native Qt 6 desktop player focused on a calm, readable listening experience and on being honest about what happens to your audio.

Adrift is free software under the GNU GPL v3 or later, the same license as Strawberry.
All credit for the player, the audio engine and the collection management goes to Jonas Kvinge and the Strawberry and Clementine contributors; see `COPYING` and the upstream repository.
Internal names (settings, binary, D-Bus, file formats) are unchanged so the fork stays compatible with upstream and can keep merging it.

## What is different

### Color themes (Settings → Appearance → Colors → Color theme)

- **System**: the default, unchanged.
- **Adrift Dark**: a deep blue-black theme with warm off-white text and one luminous accent. Text roles are above 7:1 contrast against their backgrounds.
- **Omarchy**: follows the current Omarchy desktop theme. Adrift reads `~/.config/omarchy/current/theme/colors.toml` (or `$XDG_CONFIG_HOME/omarchy/...`) as plain data using a small built-in parser. It never runs a shell or any command from the theme. It understands `background`, `foreground`, `accent` and `color0`–`color15` as `"#rrggbb"`.
  Text roles are corrected to at least 4.5:1 contrast (WCAG AA) against their backgrounds, so a low-contrast theme still stays readable.
  If no theme file is found, Adrift silently falls back to the system or custom colors.

The theme is resolved at startup, so the Omarchy option follows your desktop theme the next time Adrift starts.
A named theme overrides the custom color set; choose **System** to go back to it.
Some Qt styles (for example Breeze) ignore palettes; use the Fusion style for full effect.

### Audio signal path badge

While a track plays, a badge next to the playlist summary shows, for example, `FLAC 24/96 · Hi-Res · no processing`.
Hover or focus it for the full path: source, volume, each software processing stage, and the output.

What it is based on:

- **Source quality** comes from the track's own metadata (codec, bit depth, sample rate). Hi-Res is only claimed for lossless sources that report more than 16 bit or more than 48 kHz. A lossy file with a "24 bit" tag is still lossy.
- **Processing** comes from the engine settings: volume below 100% (or mute), ReplayGain, EBU R 128 normalization, equalizer, stereo balance, crossfeed (bs2b), forced channel count, and fade/crossfade.
- **Output** is classified as direct ALSA (`hw:`), ALSA with a plugin layer, a sound server (PulseAudio/PipeWire), automatic, or other.

What it deliberately does **not** do: claim "bit-perfect".
Adrift cannot see what a sound server, driver, or DAC does after the samples leave the application, so the strongest statement is "no software processing configured (not verified end to end)", together with the caveats that apply to the chosen output.

### Playlist import core (Tidal / Qobuz)

`src/streaming/playlistimport.{h,cpp}` contains the tested, network-free core of a read-only playlist importer:
parsers for Tidal and Qobuz playlist listings and playlist tracks, a paginator that always terminates (empty page, reported total, or a hard page limit), and a merge step that appends only tracks not already present, so refreshing never creates duplicates.
Remote playlists are only ever read. There is no write-back and no bidirectional sync, and none is planned because the services' public APIs do not support it reliably.

Limits: the network layer that calls the services and the "Import playlists" user interface are not wired up yet. See the status list below.

## Getting the best output on Linux (direct ALSA guide)

This is a guide, not an automatic preset, because the right device depends on your hardware and Adrift never changes your volume.

1. List your cards: `aplay -l`. Note the card name, for example `CARD=DAC`.
2. In Settings → Playback (Backend), choose the **ALSA** output and select the device whose name starts with `hw:` (not `default`, `plughw:` or `dmix`). Use the exact device Adrift lists for your DAC.
3. Turn off processing you do not want: ReplayGain, loudness normalization, equalizer, stereo balance, crossfeed, forced channels, and fade/crossfade.
4. Set the software volume to 100% and control loudness at the DAC or amplifier. A direct `hw:` device is usually exclusive, so other applications cannot play at the same time.
5. Start a track and read the badge. Direct ALSA with no processing reads "No software processing, direct ALSA output (not verified end to end)".

If a `hw:` device refuses a file, its sample rate or bit depth is probably not supported by the hardware. Use a `plughw:` device or a sound server for that file, and note that the badge will then tell you a conversion may happen.

## Building

Adrift builds exactly like Strawberry (C++17, Qt 6.4+, GStreamer). See `README.md` and `CLAUDE.md` for dependencies.

```
cmake -S . -B build
cmake --build build --parallel $(nproc)
```

Optional components can be disabled when their dependencies are missing, for example `-DENABLE_LAYERSHELLQT=OFF -DENABLE_TRANSLATIONS=OFF -DENABLE_STREAMTAGREADER=OFF`.

## Tests

```
cmake --build build --target run_strawberry_tests
```

New tests: `themepalette_test`, `signalpath_test`, `playlistimport_test`.

## Status

- Done: Adrift Dark and Omarchy themes with contrast enforcement, signal path badge, tested playlist import core, direct ALSA guide.
- Not done yet: live Tidal/Qobuz playlist fetching and its UI, an in-app direct ALSA preset button, live theme reload without restart, a rebranded name, icon and desktop file.
