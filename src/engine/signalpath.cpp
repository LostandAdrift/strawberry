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

#include "config.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/song.h"
#include "signalpath.h"

using namespace Qt::Literals::StringLiterals;

namespace SignalPath {

namespace {

QString Tr(const char *text) {
  return QCoreApplication::translate("SignalPath", text);
}

QString FormatSamplerate(const int samplerate) {

  if (samplerate <= 0) return QString();
  if (samplerate % 1000 == 0) return QString::number(samplerate / 1000);
  return QString::number(static_cast<double>(samplerate) / 1000.0, 'f', 1);

}

}  // namespace

QString TierText(const Tier tier) {

  switch (tier) {
    case Tier::Lossy:    return Tr("Lossy");
    case Tier::Lossless: return Tr("Lossless");
    case Tier::HiRes:    return Tr("Hi-Res");
    case Tier::DSD:      return Tr("DSD");
    case Tier::Unknown:  break;
  }
  return Tr("Quality unknown");

}

QString FormatDepthRate(const int bitdepth, const int samplerate) {

  const QString rate = FormatSamplerate(samplerate);
  if (bitdepth > 0 && !rate.isEmpty()) return QStringLiteral("%1/%2").arg(bitdepth).arg(rate);
  if (bitdepth > 0) return Tr("%1 bit").arg(bitdepth);
  if (!rate.isEmpty()) return Tr("%1 kHz").arg(rate);
  return QString();

}

OutputKind ClassifyOutput(const QString &output, const QString &device) {

  const QString sink = output.trimmed().toLower();
  const QString dev = device.trimmed().toLower();

  if (sink.contains("pulse"_L1) || sink.contains("pipewire"_L1) || sink.contains("jack"_L1)) return OutputKind::SoundServer;

  if (sink.contains("alsa"_L1)) {
    if (dev.startsWith("hw:"_L1)) return OutputKind::AlsaDirect;
    if (dev.isEmpty() || dev == "default"_L1 || dev.startsWith("plughw:"_L1) || dev.startsWith("sysdefault"_L1) || dev.startsWith("dmix"_L1) || dev == "pulse"_L1 || dev == "pipewire"_L1) return OutputKind::AlsaPlugin;
    return OutputKind::AlsaPlugin;
  }

  if (sink.isEmpty() || sink.startsWith("auto"_L1)) return OutputKind::Automatic;
  return OutputKind::Other;

}

Source SourceFromSong(const Song &song) {

  Source source;
  source.bitdepth = song.bitdepth() > 0 ? song.bitdepth() : 0;
  source.samplerate = song.samplerate() > 0 ? song.samplerate() : 0;
  source.bitrate = song.bitrate() > 0 ? song.bitrate() : 0;

  switch (song.filetype()) {
    case Song::FileType::FLAC:      source.format = u"FLAC"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::OggFlac:   source.format = u"Ogg FLAC"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::WavPack:   source.format = u"WavPack"_s; source.lossiness = Lossiness::Unknown; break;  // can be hybrid lossy
    case Song::FileType::WAV:       source.format = u"WAV"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::AIFF:      source.format = u"AIFF"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::ALAC:      source.format = u"ALAC"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::APE:       source.format = u"APE"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::TrueAudio: source.format = u"TTA"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::PCM:       source.format = u"PCM"_s; source.lossiness = Lossiness::Lossless; break;
    case Song::FileType::CDDA:      source.format = u"CD"_s; source.lossiness = Lossiness::Lossless; if (source.bitdepth == 0) source.bitdepth = 16; if (source.samplerate == 0) source.samplerate = 44100; break;
    case Song::FileType::DSF:       source.format = u"DSF"_s; source.lossiness = Lossiness::Lossless; source.dsd = true; break;
    case Song::FileType::DSDIFF:    source.format = u"DSDIFF"_s; source.lossiness = Lossiness::Lossless; source.dsd = true; break;
    case Song::FileType::OggVorbis: source.format = u"Vorbis"_s; source.lossiness = Lossiness::Lossy; break;
    case Song::FileType::OggOpus:   source.format = u"Opus"_s; source.lossiness = Lossiness::Lossy; break;
    case Song::FileType::OggSpeex:  source.format = u"Speex"_s; source.lossiness = Lossiness::Lossy; break;
    case Song::FileType::MPEG:      source.format = u"MP3"_s; source.lossiness = Lossiness::Lossy; break;
    case Song::FileType::MP4:       source.format = u"AAC"_s; source.lossiness = Lossiness::Lossy; break;
    case Song::FileType::ASF:       source.format = u"WMA"_s; source.lossiness = Lossiness::Unknown; break;
    case Song::FileType::MPC:       source.format = u"Musepack"_s; source.lossiness = Lossiness::Lossy; break;
    case Song::FileType::Stream:    source.format = u"Stream"_s; break;
    default: break;
  }

  // Tracker/chip formats are synthesized, not recorded: report them without a quality claim.
  return source;

}

Tier TierForSource(const Source &source) {

  if (source.dsd) return Tier::DSD;
  if (source.lossiness == Lossiness::Lossy) return Tier::Lossy;
  if (source.lossiness == Lossiness::Unknown) return Tier::Unknown;

  // Lossless: anything beyond 16 bit or 48 kHz is Hi-Res. Missing numbers do not allow a Hi-Res claim.
  if (source.bitdepth > 16 || source.samplerate > 48000) return Tier::HiRes;
  return Tier::Lossless;

}

Report Analyze(const Source &source, const Config &config) {

  Report report;
  report.tier = TierForSource(source);
  report.output_kind = ClassifyOutput(config.output, config.device);

  // Source and decoder
  const QString depth_rate = FormatDepthRate(source.bitdepth, source.samplerate);
  QStringList source_details;
  if (!depth_rate.isEmpty()) source_details << depth_rate;
  if (source.bitrate > 0 && source.lossiness != Lossiness::Lossless) source_details << Tr("%1 kbps").arg(source.bitrate);
  report.stages << Stage{ source.format.isEmpty() ? Tr("Source") : Tr("Source: %1").arg(source.format), source_details.join(u" · "_s), false };

  // Software processing
  auto add_alteration = [&report](const QString &name, const QString &detail) {
    report.stages << Stage{ name, detail, true };
    report.alterations << name;
  };

  if (config.volume_control) {
    if (config.muted) {
      add_alteration(Tr("Volume"), Tr("Muted"));
    }
    else if (config.volume_percent < 100) {
      add_alteration(Tr("Volume"), Tr("Attenuated to %1%").arg(config.volume_percent));
    }
    else {
      report.stages << Stage{ Tr("Volume"), Tr("100%, unity gain"), false };
    }
  }
  if (config.replaygain) add_alteration(Tr("ReplayGain"), Tr("Gain applied"));
  if (config.ebur128) add_alteration(Tr("Loudness normalization"), Tr("EBU R 128 gain applied"));
  if (config.equalizer) add_alteration(Tr("Equalizer"), Tr("Enabled"));
  if (config.stereo_balancer) add_alteration(Tr("Stereo balance"), Tr("Enabled"));
  if (config.bs2b) add_alteration(Tr("Crossfeed"), Tr("Headphone crossfeed enabled"));
  if (config.channels_forced) add_alteration(Tr("Channel remix"), Tr("Output channel count is forced"));
  if (config.fade_or_crossfade) add_alteration(Tr("Fade / crossfade"), Tr("Applied at track changes"));

  // Output
  QString output_name;
  switch (report.output_kind) {
    case OutputKind::AlsaDirect:  output_name = Tr("ALSA direct (hw)"); break;
    case OutputKind::AlsaPlugin:  output_name = Tr("ALSA (plugin layer)"); break;
    case OutputKind::SoundServer: output_name = Tr("Sound server"); break;
    case OutputKind::Automatic:   output_name = Tr("Automatic output"); break;
    case OutputKind::Other:       output_name = Tr("Output: %1").arg(config.output); break;
  }
  report.stages << Stage{ output_name, config.device, false };

  // What we cannot know
  switch (report.output_kind) {
    case OutputKind::AlsaDirect:
      report.caveats << Tr("The driver and DAC are not visible to Adrift. A hw: device can still reject the format or be shared with other software.");
      break;
    case OutputKind::AlsaPlugin:
      report.caveats << Tr("ALSA plugins such as plug, dmix or the default device may resample, convert or mix.");
      break;
    case OutputKind::SoundServer:
      report.caveats << Tr("The sound server may mix and resample to its own format.");
      break;
    case OutputKind::Automatic:
      report.caveats << Tr("The output is chosen automatically, so Adrift cannot tell whether it is resampled.");
      break;
    case OutputKind::Other:
      report.caveats << Tr("Adrift cannot inspect this output.");
      break;
  }
  if (report.tier == Tier::Unknown) report.caveats << Tr("The source did not report enough information to classify its quality.");
  if (report.tier == Tier::Lossless && source.bitdepth == 0) report.caveats << Tr("Bit depth was not reported by the source.");
  if (source.dsd) report.caveats << Tr("DSD is decoded to PCM unless the output supports DSD natively.");

  // Badge
  QStringList badge;
  if (!source.format.isEmpty() && source.format != "Stream"_L1) badge << source.format;
  if (!depth_rate.isEmpty()) badge << depth_rate;
  QString badge_text = badge.join(u' ');
  const QString tier_text = TierText(report.tier);
  badge_text = badge_text.isEmpty() ? tier_text : badge_text + u" · "_s + tier_text;
  report.badge = badge_text;

  // Verdict: never "bit-perfect".
  if (!report.alterations.isEmpty()) {
    report.verdict = Tr("Altered by: %1").arg(report.alterations.join(u", "_s));
  }
  else if (report.output_kind == OutputKind::AlsaDirect) {
    report.verdict = Tr("No software processing, direct ALSA output (not verified end to end)");
  }
  else {
    report.verdict = Tr("No software processing configured (not verified end to end)");
  }

  // Tooltip / accessible description
  QStringList lines;
  lines << report.badge << report.verdict << QString();
  for (const Stage &stage : std::as_const(report.stages)) {
    QString line = QStringLiteral("• ") + stage.name;
    if (!stage.detail.isEmpty()) line += QStringLiteral(": ") + stage.detail;
    if (stage.alters_signal) line += QStringLiteral(" ") + Tr("[alters signal]");
    lines << line;
  }
  if (!report.caveats.isEmpty()) {
    lines << QString();
    for (const QString &caveat : std::as_const(report.caveats)) {
      lines << caveat;
    }
  }
  report.description = lines.join(u'\n');

  return report;

}

}  // namespace SignalPath
