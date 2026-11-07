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

  switch (song.source()) {
    case Song::Source::LocalFile:
    case Song::Source::Collection:
    case Song::Source::CDDA:
    case Song::Source::Device:
    case Song::Source::Unknown:
      break;
    default:
      source.origin = song.TextForSource();
      break;
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

  const QString depth_rate = FormatDepthRate(source.bitdepth, source.samplerate);
  const bool source_known = !source.format.isEmpty() && source.format != "Stream"_L1;

  // --- Source
  QStringList source_parts;
  if (source_known) source_parts << source.format;
  if (!depth_rate.isEmpty()) source_parts << depth_rate;
  // Metadata can lag behind stream negotiation: stay "unknown" instead of guessing.
  report.source_text = source_parts.isEmpty() ? Tr("Source unknown") : source_parts.join(u' ');

  report.source_rows << qMakePair(Tr("Format"), source_known ? source.format : Tr("Unknown"));
  report.source_rows << qMakePair(Tr("Quality"), TierText(report.tier));
  report.source_rows << qMakePair(Tr("Bit depth"), source.bitdepth > 0 ? Tr("%1 bit").arg(source.bitdepth) : Tr("Not reported"));
  report.source_rows << qMakePair(Tr("Sample rate"), source.samplerate > 0 ? Tr("%1 Hz").arg(source.samplerate) : Tr("Not reported"));
  if (source.bitrate > 0 && source.lossiness != Lossiness::Lossless) report.source_rows << qMakePair(Tr("Bitrate"), Tr("%1 kbps").arg(source.bitrate));
  if (!source.origin.isEmpty()) report.source_rows << qMakePair(Tr("Origin"), source.origin);

  QStringList summary;
  if (source.bitdepth > 0) summary << Tr("%1 bit").arg(source.bitdepth);
  if (source.samplerate > 0) summary << Tr("%1 Hz").arg(source.samplerate);
  if (source.bitrate > 0 && source.lossiness != Lossiness::Lossless) summary << Tr("%1 kbps").arg(source.bitrate);
  if (!source.origin.isEmpty()) summary << source.origin;
  report.source_summary = summary.join(QStringLiteral(" · "));

  QStringList source_details;
  if (!depth_rate.isEmpty()) source_details << depth_rate;
  if (source.bitrate > 0 && source.lossiness != Lossiness::Lossless) source_details << Tr("%1 kbps").arg(source.bitrate);
  report.stages << Stage{ source_known ? Tr("Source: %1").arg(source.format) : Tr("Source"), source_details.join(u" · "_s), false };

  // --- Software processing (only what really changes samples)
  auto add_processing = [&report](const QString &name, const bool alters, const QString &state, const QString &detail = QString()) {
    report.processing_rows << qMakePair(name, state);
    if (!alters) report.processing_off << name;
    if (alters) {
      report.processing_active << qMakePair(name, state);
      report.stages << Stage{ name, detail.isEmpty() ? state : detail, true };
      report.alterations << name;
    }
  };

  // Mute and volume only act through the engine's volume element, which exists only when volume control is enabled.
  // With volume control off, the volume slider and mute do nothing, so neither can alter the signal.
  if (!config.volume_control) {
    add_processing(Tr("Volume"), false, Tr("Off (volume control disabled, mute has no effect)"));
  }
  else if (config.muted) {
    add_processing(Tr("Volume"), true, Tr("Muted"));
  }
  else if (config.volume_percent < 100) {
    add_processing(Tr("Volume"), true, Tr("Attenuated to %1%").arg(config.volume_percent));
  }
  else {
    add_processing(Tr("Volume"), false, Tr("100%, unity gain"));
    report.stages << Stage{ Tr("Volume"), Tr("100%, unity gain"), false };
  }

  add_processing(Tr("ReplayGain"), config.replaygain, config.replaygain ? Tr("On") : Tr("Off"));
  add_processing(Tr("Loudness normalization"), config.ebur128, config.ebur128 ? Tr("On (EBU R 128)") : Tr("Off"));
  add_processing(Tr("Equalizer"), config.equalizer, config.equalizer ? Tr("On") : Tr("Off"));
  add_processing(Tr("Stereo balance"), config.stereo_balancer, config.stereo_balancer ? Tr("On") : Tr("Off"));
  add_processing(Tr("Crossfeed"), config.bs2b, config.bs2b ? Tr("On (headphone crossfeed)") : Tr("Off"));
  add_processing(Tr("Channel remix"), config.channels_forced, config.channels_forced ? Tr("On (output channel count forced)") : Tr("Off"));
  add_processing(Tr("Fade / crossfade"), config.fade_or_crossfade, config.fade_or_crossfade ? Tr("On (applied at track changes)") : Tr("Off"));

  report.app_dsp_active = !report.alterations.isEmpty();
  report.dsp_text = report.app_dsp_active ? Tr("App DSP on") : Tr("App DSP off");

  // --- Output
  switch (report.output_kind) {
    case OutputKind::AlsaDirect:  report.output_text = Tr("Direct ALSA"); break;
    case OutputKind::AlsaPlugin:  report.output_text = Tr("ALSA plugin"); break;
    case OutputKind::SoundServer: report.output_text = Tr("System mixer"); break;
    case OutputKind::Automatic:   report.output_text = Tr("Auto output"); break;
    case OutputKind::Other:       report.output_text = config.output.isEmpty() ? Tr("Output") : config.output; break;
  }
  report.stages << Stage{ report.output_text, config.device, false };

  report.output_detail = (config.output.isEmpty() ? Tr("automatic output") : config.output) + QStringLiteral(" · ") + (config.device.isEmpty() ? Tr("default device") : config.device);
  report.output_rows << qMakePair(Tr("Output"), config.output.isEmpty() ? Tr("Automatic") : config.output);
  report.output_rows << qMakePair(Tr("Device"), config.device.isEmpty() ? Tr("Default") : config.device);
  report.output_rows << qMakePair(Tr("Path"), report.output_text);
  if (config.exclusive_mode) report.output_rows << qMakePair(Tr("Exclusive mode"), Tr("On"));

  switch (report.output_kind) {
    case OutputKind::AlsaDirect:
      report.caveats << Tr("The driver and DAC are not visible to Adrift. A hw: device can still reject the format or be shared with other software.");
      break;
    case OutputKind::AlsaPlugin:
      report.caveats << Tr("ALSA plugins such as plug, dmix or the default device may resample, convert or mix.");
      break;
    case OutputKind::SoundServer:
      report.caveats << Tr("The sound server (PulseAudio or PipeWire) mixes with other applications and may resample to its own format.");
      break;
    case OutputKind::Automatic:
      report.caveats << Tr("The output is chosen automatically, so Adrift cannot tell whether it is resampled.");
      break;
    case OutputKind::Other:
      report.caveats << Tr("Adrift cannot inspect this output.");
      break;
  }
  if (report.tier == Tier::Unknown) report.caveats << Tr("The source did not report enough information to classify its quality yet.");
  if (report.tier == Tier::Lossless && source.bitdepth == 0) report.caveats << Tr("Bit depth was not reported by the source.");
  if (source.dsd) report.caveats << Tr("DSD is decoded to PCM unless the output supports DSD natively.");

  // --- Texts. App DSP and output certainty are two different statements and are never merged into a single "bit-perfect" style claim.
  report.badge = report.source_text + u" · "_s + report.output_text;

  if (report.app_dsp_active) {
    report.dsp_verdict = Tr("Altered by: %1").arg(report.alterations.join(u", "_s));
  }
  else {
    report.dsp_verdict = Tr("App DSP off: no software processing is configured in Adrift.");
  }

  switch (report.output_kind) {
    case OutputKind::AlsaDirect:  report.output_verdict = Tr("Direct ALSA output. Not verified end to end."); break;
    case OutputKind::SoundServer: report.output_verdict = Tr("Goes through the system mixer, which may mix and resample."); break;
    case OutputKind::AlsaPlugin:  report.output_verdict = Tr("Goes through an ALSA plugin layer, which may convert or mix."); break;
    case OutputKind::Automatic:   report.output_verdict = Tr("Output chosen automatically. Not verified."); break;
    case OutputKind::Other:       report.output_verdict = Tr("Output cannot be inspected."); break;
  }
  report.verdict = report.dsp_verdict + u' ' + report.output_verdict;

  // --- Tooltip
  QStringList lines;
  lines << report.badge << report.dsp_text << QString();
  lines << report.dsp_verdict << report.output_verdict;
  if (!report.caveats.isEmpty()) {
    lines << QString();
    for (const QString &caveat : std::as_const(report.caveats)) lines << caveat;
  }
  lines << QString() << Tr("Click for details.");
  report.description = lines.join(u'\n');

  // --- Copyable technical text
  auto section = [](const QString &title, const Report::Rows &rows) {
    QStringList out;
    out << title;
    for (const auto &row : rows) out << QStringLiteral("  %1: %2").arg(row.first, row.second);
    return out.join(u'\n');
  };
  QStringList technical;
  technical << QStringLiteral("Adrift signal path");
  technical << section(Tr("Source"), report.source_rows);
  technical << section(Tr("Processing"), report.processing_rows);
  technical << section(Tr("Output"), report.output_rows);
  technical << report.dsp_verdict << report.output_verdict;
  for (const QString &caveat : std::as_const(report.caveats)) technical << QStringLiteral("Note: ") + caveat;
  technical << Tr("This describes Adrift's configuration and the track's metadata. It does not prove bit-perfect playback.");
  report.technical_text = technical.join(u'\n');

  return report;

}

}  // namespace SignalPath
