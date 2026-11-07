/*
 * Adrift - a fork of Strawberry Music Player
 * Copyright 2026, LostandAdrift
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef SIGNALPATH_H
#define SIGNALPATH_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class Song;

// Describes what Adrift knows about the audio path of the current track, and nothing more.
//
// The analysis is based on source metadata (as reported by tags or the streaming service) and on the engine settings.
// It cannot see what the sound server, driver or DAC does after the data leaves the application,
// so it never claims "bit-perfect". The strongest statement it makes is that no software processing is configured
// and the output is addressed directly, which is a necessary but not a sufficient condition.
namespace SignalPath {

enum class Lossiness {
  Unknown,
  Lossy,
  Lossless
};

enum class Tier {
  Unknown,
  Lossy,
  Lossless,  // CD quality: 16 bit, 44.1/48 kHz
  HiRes,     // more than 16 bit or more than 48 kHz
  DSD
};

struct Source {
  QString format;  // Human readable codec/container, e.g. "FLAC". Empty if unknown.
  Lossiness lossiness = Lossiness::Unknown;
  bool dsd = false;
  int bitdepth = 0;    // Bits per sample, 0 if not reported.
  int samplerate = 0;  // Hz, 0 if not reported.
  int bitrate = 0;     // kbps, 0 if not reported.
  QString origin;      // Streaming service name for remote tracks, empty for local files.
};

enum class OutputKind {
  Automatic,       // autoaudiosink: Adrift does not know which sink GStreamer picks
  AlsaDirect,      // alsasink on a hw: device
  AlsaPlugin,      // alsasink on plughw:/default/etc: ALSA may convert or mix
  SoundServer,     // PulseAudio / PipeWire: the server may mix and resample
  Other
};

struct Config {
  QString output;        // GStreamer sink element name, e.g. "alsasink"
  QString device;        // Device string, e.g. "hw:CARD=DAC,DEV=0". May be empty.
  bool exclusive_mode = false;

  bool volume_control = true;
  int volume_percent = 100;
  bool muted = false;

  bool replaygain = false;
  bool ebur128 = false;
  bool equalizer = false;
  bool stereo_balancer = false;
  bool bs2b = false;
  bool channels_forced = false;
  bool fade_or_crossfade = false;
};

struct Stage {
  QString name;
  QString detail;
  bool alters_signal = false;
};

struct Report {
  Tier tier = Tier::Unknown;
  OutputKind output_kind = OutputKind::Automatic;
  QList<Stage> stages;
  QStringList alterations;  // Names of stages that alter the samples.
  QStringList caveats;      // Things Adrift cannot verify.

  // Short parts for the compact badge. These are separate so the UI can style and elide them independently.
  QString source_text;  // "FLAC 24/96", or "Source unknown" until metadata arrives
  QString output_text;  // "Direct ALSA", "System mixer", "ALSA plugin", "Auto output"
  QString dsp_text;     // "App DSP off" or "App DSP on"
  bool app_dsp_active = false;

  // "FLAC 24/96 · System mixer" - source and output only. App DSP is reported separately so it is not confused with output certainty.
  QString badge;
  // Statement about the application's own processing only, e.g. "App DSP off" / "Altered by: ReplayGain".
  QString dsp_verdict;
  // Statement about what is and is not known about the output.
  QString output_verdict;
  // dsp_verdict and output_verdict in one line (for status text and accessibility).
  QString verdict;
  // Multi-line description for tooltips.
  QString description;

  // Sections for the details dialog: label / value rows.
  using Rows = QList<QPair<QString, QString>>;
  Rows source_rows;
  Rows processing_rows;
  Rows output_rows;

  // Plain text version of everything, for copying into bug reports.
  QString technical_text;

  bool unaltered_configuration() const { return alterations.isEmpty(); }
};

QString TierText(const Tier tier);
OutputKind ClassifyOutput(const QString &output, const QString &device);

// Source::format/lossiness/bitdepth/samplerate from a song. Pure mapping, no I/O.
Source SourceFromSong(const Song &song);

Tier TierForSource(const Source &source);
Report Analyze(const Source &source, const Config &config);

// "24/96" style text; "24 bit", "96 kHz" or empty when parts are missing.
QString FormatDepthRate(const int bitdepth, const int samplerate);

}  // namespace SignalPath

#endif  // SIGNALPATH_H
