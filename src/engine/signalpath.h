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

  // Short badge text, e.g. "FLAC 24/96 · Hi-Res".
  QString badge;
  // One-line verdict, e.g. "Software processing: ReplayGain, Equalizer" or "No software processing configured (not verified end to end)".
  QString verdict;
  // Multi-line description for tooltips and accessibility.
  QString description;

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
