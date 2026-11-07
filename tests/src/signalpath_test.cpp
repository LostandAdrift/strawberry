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

#include "gtest_include.h"

#include <QString>

#include "core/song.h"
#include "engine/signalpath.h"

using namespace SignalPath;

namespace {

Source Flac(const int bitdepth, const int samplerate) {
  Source source;
  source.format = QStringLiteral("FLAC");
  source.lossiness = Lossiness::Lossless;
  source.bitdepth = bitdepth;
  source.samplerate = samplerate;
  return source;
}

Config DirectAlsa() {
  Config config;
  config.output = QStringLiteral("alsasink");
  config.device = QStringLiteral("hw:CARD=Example,DEV=0");
  return config;
}

}  // namespace

TEST(SignalPathTest, TierClassification) {

  EXPECT_EQ(Tier::Lossless, TierForSource(Flac(16, 44100)));
  EXPECT_EQ(Tier::Lossless, TierForSource(Flac(16, 48000)));
  EXPECT_EQ(Tier::HiRes, TierForSource(Flac(24, 44100)));
  EXPECT_EQ(Tier::HiRes, TierForSource(Flac(16, 96000)));
  EXPECT_EQ(Tier::HiRes, TierForSource(Flac(24, 192000)));

  // Missing numbers never produce a Hi-Res claim.
  EXPECT_EQ(Tier::Lossless, TierForSource(Flac(0, 0)));

  Source mp3;
  mp3.format = QStringLiteral("MP3");
  mp3.lossiness = Lossiness::Lossy;
  mp3.bitdepth = 24;  // bogus tag must not upgrade a lossy source
  mp3.samplerate = 96000;
  EXPECT_EQ(Tier::Lossy, TierForSource(mp3));

  EXPECT_EQ(Tier::Unknown, TierForSource(Source()));

  Source dsd = Flac(1, 2822400);
  dsd.dsd = true;
  EXPECT_EQ(Tier::DSD, TierForSource(dsd));

}

TEST(SignalPathTest, ClassifyOutput) {

  EXPECT_EQ(OutputKind::AlsaDirect, ClassifyOutput(QStringLiteral("alsasink"), QStringLiteral("hw:0,0")));
  EXPECT_EQ(OutputKind::AlsaDirect, ClassifyOutput(QStringLiteral("alsasink"), QStringLiteral("HW:CARD=X,DEV=0")));
  EXPECT_EQ(OutputKind::AlsaPlugin, ClassifyOutput(QStringLiteral("alsasink"), QStringLiteral("plughw:0,0")));
  EXPECT_EQ(OutputKind::AlsaPlugin, ClassifyOutput(QStringLiteral("alsasink"), QStringLiteral("default")));
  EXPECT_EQ(OutputKind::AlsaPlugin, ClassifyOutput(QStringLiteral("alsasink"), QString()));
  EXPECT_EQ(OutputKind::SoundServer, ClassifyOutput(QStringLiteral("pulsesink"), QString()));
  EXPECT_EQ(OutputKind::SoundServer, ClassifyOutput(QStringLiteral("pipewiresink"), QString()));
  EXPECT_EQ(OutputKind::Automatic, ClassifyOutput(QStringLiteral("autoaudiosink"), QString()));
  EXPECT_EQ(OutputKind::Automatic, ClassifyOutput(QString(), QString()));
  EXPECT_EQ(OutputKind::Other, ClassifyOutput(QStringLiteral("osssink"), QString()));

  // A hw: device string alone does not make a pulse sink direct.
  EXPECT_EQ(OutputKind::SoundServer, ClassifyOutput(QStringLiteral("pulsesink"), QStringLiteral("hw:0,0")));

}

TEST(SignalPathTest, FormatDepthRate) {

  EXPECT_EQ(QStringLiteral("24/96"), FormatDepthRate(24, 96000));
  EXPECT_EQ(QStringLiteral("16/44.1"), FormatDepthRate(16, 44100));
  EXPECT_EQ(QStringLiteral("24/176.4"), FormatDepthRate(24, 176400));
  EXPECT_EQ(QString(), FormatDepthRate(0, 0));
  EXPECT_FALSE(FormatDepthRate(24, 0).isEmpty());
  EXPECT_FALSE(FormatDepthRate(0, 96000).isEmpty());

}

TEST(SignalPathTest, NeverClaimsBitPerfect) {

  const Report report = Analyze(Flac(24, 96000), DirectAlsa());
  EXPECT_TRUE(report.unaltered_configuration());
  EXPECT_FALSE(report.verdict.contains(QStringLiteral("bit-perfect"), Qt::CaseInsensitive));
  EXPECT_FALSE(report.verdict.contains(QStringLiteral("bit perfect"), Qt::CaseInsensitive));
  EXPECT_FALSE(report.description.contains(QStringLiteral("bit-perfect"), Qt::CaseInsensitive));
  EXPECT_TRUE(report.verdict.contains(QStringLiteral("not verified")));
  EXPECT_FALSE(report.caveats.isEmpty());
  EXPECT_EQ(QStringLiteral("FLAC 24/96 · Hi-Res"), report.badge);

}

TEST(SignalPathTest, UnityVolumeDoesNotAlter) {

  Config config = DirectAlsa();
  config.volume_control = true;
  config.volume_percent = 100;
  EXPECT_TRUE(Analyze(Flac(16, 44100), config).unaltered_configuration());

  config.volume_percent = 99;
  const Report attenuated = Analyze(Flac(16, 44100), config);
  EXPECT_FALSE(attenuated.unaltered_configuration());
  EXPECT_TRUE(attenuated.verdict.contains(QStringLiteral("Volume")));

  config.volume_percent = 40;
  config.volume_control = false;  // volume stage is not in the path at all
  EXPECT_TRUE(Analyze(Flac(16, 44100), config).unaltered_configuration());

  config.volume_control = true;
  config.volume_percent = 100;
  config.muted = true;
  EXPECT_FALSE(Analyze(Flac(16, 44100), config).unaltered_configuration());

}

TEST(SignalPathTest, EachProcessingStageIsReported) {

  struct Case {
    void (*apply)(Config &);
    const char *expected;
  };
  const Case cases[] = {
    { [](Config &c) { c.replaygain = true; }, "ReplayGain" },
    { [](Config &c) { c.ebur128 = true; }, "Loudness normalization" },
    { [](Config &c) { c.equalizer = true; }, "Equalizer" },
    { [](Config &c) { c.stereo_balancer = true; }, "Stereo balance" },
    { [](Config &c) { c.bs2b = true; }, "Crossfeed" },
    { [](Config &c) { c.channels_forced = true; }, "Channel remix" },
    { [](Config &c) { c.fade_or_crossfade = true; }, "Fade / crossfade" },
  };

  for (const Case &test : cases) {
    Config config = DirectAlsa();
    test.apply(config);
    const Report report = Analyze(Flac(16, 44100), config);
    EXPECT_FALSE(report.unaltered_configuration()) << test.expected;
    EXPECT_TRUE(report.alterations.contains(QString::fromLatin1(test.expected))) << test.expected;
    EXPECT_TRUE(report.verdict.startsWith(QStringLiteral("Altered by"))) << test.expected;
  }

}

TEST(SignalPathTest, SoundServerAndAutomaticOutputsAddCaveats) {

  Config config;
  config.output = QStringLiteral("pulsesink");
  config.volume_percent = 100;
  Report report = Analyze(Flac(16, 44100), config);
  EXPECT_EQ(OutputKind::SoundServer, report.output_kind);
  EXPECT_TRUE(report.caveats.join(QString()).contains(QStringLiteral("resample")));

  config.output = QStringLiteral("autoaudiosink");
  report = Analyze(Flac(16, 44100), config);
  EXPECT_EQ(OutputKind::Automatic, report.output_kind);
  EXPECT_TRUE(report.caveats.join(QString()).contains(QStringLiteral("automatically")));

}

TEST(SignalPathTest, LossySourceAndUnknownSource) {

  Source mp3;
  mp3.format = QStringLiteral("MP3");
  mp3.lossiness = Lossiness::Lossy;
  mp3.bitrate = 320;
  const Report lossy = Analyze(mp3, DirectAlsa());
  EXPECT_EQ(Tier::Lossy, lossy.tier);
  EXPECT_EQ(QStringLiteral("MP3 · Lossy"), lossy.badge);
  EXPECT_TRUE(lossy.description.contains(QStringLiteral("320 kbps")));

  const Report unknown = Analyze(Source(), DirectAlsa());
  EXPECT_EQ(Tier::Unknown, unknown.tier);
  EXPECT_EQ(QStringLiteral("Quality unknown"), unknown.badge);
  EXPECT_TRUE(unknown.caveats.join(QString()).contains(QStringLiteral("did not report")));

}

TEST(SignalPathTest, SourceFromSong) {

  Song flac;
  flac.set_filetype(Song::FileType::FLAC);
  flac.set_bitdepth(24);
  flac.set_samplerate(96000);
  Source source = SourceFromSong(flac);
  EXPECT_EQ(QStringLiteral("FLAC"), source.format);
  EXPECT_EQ(Lossiness::Lossless, source.lossiness);
  EXPECT_EQ(Tier::HiRes, TierForSource(source));

  Song mp3;
  mp3.set_filetype(Song::FileType::MPEG);
  mp3.set_bitrate(256);
  source = SourceFromSong(mp3);
  EXPECT_EQ(Lossiness::Lossy, source.lossiness);
  EXPECT_EQ(Tier::Lossy, TierForSource(source));

  Song stream;
  stream.set_filetype(Song::FileType::Stream);
  EXPECT_EQ(Tier::Unknown, TierForSource(SourceFromSong(stream)));

  Song cd;
  cd.set_filetype(Song::FileType::CDDA);
  EXPECT_EQ(Tier::Lossless, TierForSource(SourceFromSong(cd)));

  Song dsf;
  dsf.set_filetype(Song::FileType::DSF);
  EXPECT_EQ(Tier::DSD, TierForSource(SourceFromSong(dsf)));

  // Lossless container with negative/garbage metadata does not crash or claim Hi-Res.
  Song odd;
  odd.set_filetype(Song::FileType::FLAC);
  odd.set_bitdepth(-1);
  odd.set_samplerate(-1);
  EXPECT_EQ(Tier::Lossless, TierForSource(SourceFromSong(odd)));

}
