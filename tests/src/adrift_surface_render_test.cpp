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

// Full-surface renders of the Listening Room: the production ListeningRoom widget with its real transport, side rail, queue (a view of a real Playlist),
// playlists and streaming pages, filled with synthetic data. Covers are drawn by code. The images are for review only; nothing reads the user's
// configuration, library, database or accounts. Run on Qt's offscreen platform, so no window appears on screen.
//
// The Library page hosts the classic window, which only exists inside MainWindow (it needs the full application and a database). Here that page
// shows a plainly labelled stand-in instead.

#include "gtest_include.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QMap>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

#include "core/themepalette.h"
#include "engine/signalpath.h"
#include "listeningroom/coverartwidget.h"
#include "listeningroom/listeningroom.h"
#include "listeningroom/transportbar.h"
#include "listeningroom_fixture.h"
#include "widgets/signalpathwidget.h"

#ifndef ADRIFT_SCREENSHOT_DIR
#  define ADRIFT_SCREENSHOT_DIR ""
#endif

namespace {

const char kOmarchySample[] =
    "mode = \"dark\"\naccent = \"#faa968\"\nselection = \"#134e5a\"\nbackground = \"#05182e\"\ndark_background = \"#031222\"\n"
    "darker_background = \"#020c17\"\nlighter_background = \"#0a2540\"\nforeground = \"#f6dcac\"\n";

QPalette PaletteNamed(const QString &name) {

  if (name == QLatin1String("adrift-dark")) return ThemePalette::BuildPalette(QApplication::style()->standardPalette(), ThemePalette::AdriftDarkColors());
  if (name == QLatin1String("omarchy")) return ThemePalette::BuildPalette(QApplication::style()->standardPalette(), ThemePalette::PaletteFromOmarchyColors(ThemePalette::ParseColorsToml(QByteArray(kOmarchySample))));
  return QApplication::style()->standardPalette();

}

void Save(const QImage &image, const QString &name) {

  ASSERT_FALSE(image.isNull());
  const QString dir = QString::fromUtf8(ADRIFT_SCREENSHOT_DIR);
  if (dir.isEmpty()) return;
  QDir().mkpath(dir);
  ASSERT_TRUE(image.save(QDir(dir).filePath(name + QStringLiteral(".png")))) << name.toStdString();

}

SignalPath::Report HiResDirect() {
  SignalPath::Source source;
  source.format = QStringLiteral("FLAC");
  source.lossiness = SignalPath::Lossiness::Lossless;
  source.bitdepth = 24;
  source.samplerate = 96000;
  SignalPath::Config config;
  config.output = QStringLiteral("alsasink");
  config.device = QStringLiteral("hw:CARD=Example,DEV=0");
  config.volume_percent = 100;
  return SignalPath::Analyze(source, config);
}

SignalPath::Report CdThroughMixer() {
  SignalPath::Source source;
  source.format = QStringLiteral("FLAC");
  source.lossiness = SignalPath::Lossiness::Lossless;
  source.bitdepth = 16;
  source.samplerate = 44100;
  SignalPath::Config config;
  config.output = QStringLiteral("pipewiresink");
  config.volume_percent = 100;
  return SignalPath::Analyze(source, config);
}

RoomTrack TrackAt(const int row) {
  const RoomFixture::Track fixture = RoomFixture::Tracks().at(row % RoomFixture::Tracks().size());
  RoomTrack track;
  track.title = fixture.title;
  track.artist = fixture.artist;
  track.album = fixture.album;
  track.detail = QStringLiteral("2021 · Track %1").arg(row % 4 + 1);
  return track;
}

QList<RoomPlaylistCard> SamplePlaylists() {

  RoomPlaylistCard evening;
  evening.id = 1;
  evening.name = QStringLiteral("Evening Drift");
  evening.track_count = 14;
  evening.is_current = true;
  evening.is_playing = true;
  RoomPlaylistCard focus;
  focus.id = 2;
  focus.name = QStringLiteral("Focus: deep work");
  focus.track_count = 118;
  RoomPlaylistCard snapshot;
  snapshot.id = 3;
  snapshot.name = QStringLiteral("Tidal: Night Ferry");
  snapshot.track_count = 42;
  snapshot.kind = QStringLiteral("Tidal snapshot");
  RoomPlaylistCard rain;
  rain.id = 4;
  rain.name = QStringLiteral("Rainy Sunday");
  rain.track_count = 27;
  return { evening, focus, snapshot, rain };

}

QList<RoomServiceCard> SampleServices() {

  RoomServiceCard tidal;
  tidal.key = QStringLiteral("tidal");
  tidal.name = QStringLiteral("Tidal");
  tidal.status = QStringLiteral("Signed in");
  tidal.signed_in = true;
  RoomServiceCard qobuz;
  qobuz.key = QStringLiteral("qobuz");
  qobuz.name = QStringLiteral("Qobuz");
  qobuz.status = QStringLiteral("Not signed in");
  qobuz.signed_in = false;
  return { tidal, qobuz };

}

// The room as MainWindow drives it. Every call below is one the application makes.
struct Surface {
  explicit Surface(const QString &theme, const int width, const int height) {
    room.setPalette(PaletteNamed(theme));
    QLabel *stand_in = new QLabel(QStringLiteral("The classic Strawberry window appears here.\n(Stand-in for this render: it is hosted by MainWindow.)"));
    stand_in->setAlignment(Qt::AlignCenter);
    room.library_container()->layout()->addWidget(stand_in);
    room.SetPlaylists(SamplePlaylists());
    room.SetStreamingServices(SampleServices());
    room.resize(width, height);
    requested_ = QSize(width, height);
  }

  void Play(const int row, const TransportBar::State state, const bool with_cover = true, const SignalPath::Report &report = HiResDirect()) {
    room.SetQueuePlaylist(queue.playlist(), QStringLiteral("Evening Drift"));
    room.SetVolume(62);
    room.SetPlaybackState(state);
    queue.SetCurrent(row);
    room.SetNowPlaying(TrackAt(row));
    room.SetCover(with_cover ? RoomFixture::Cover(row / 5) : QImage());
    room.SetCanSeek(true);
    room.SetPosition(94000, RoomFixture::Tracks().at(row).seconds * 1000);
    room.signal_path_widget()->SetReport(report);
  }

  QImage Capture() {
    room.show();
    QCoreApplication::processEvents();
    room.layout()->activate();
    // The file names say 1440x900 or 1000x700: the render must really be that size, not whatever the minimums allowed.
    EXPECT_EQ(requested_, room.size());
    // The first widget of a freshly shown window takes the focus; its keyboard focus ring does not belong in a still image.
    if (QWidget *focused = QApplication::focusWidget()) focused->clearFocus();
    QCoreApplication::processEvents();
    return room.grab().toImage();
  }

  RoomFixture::QueueFixture queue{ 14 };
  ListeningRoom room;
  QSize requested_;
};

class SurfaceRenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    saved_style_ = QApplication::style()->objectName();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  }
  void TearDown() override { QApplication::setStyle(saved_style_); }

  QString saved_style_;
};

}  // namespace

TEST_F(SurfaceRenderTest, PlayingAtNormalAndCompactSizes) {

  struct Size {
    int width;
    int height;
    const char *name;
  };
  for (const Size size : { Size{ 1440, 900, "1440x900" }, Size{ 1000, 700, "1000x700" } }) {
    Surface surface(QStringLiteral("adrift-dark"), size.width, size.height);
    surface.Play(3, TransportBar::State::Playing);
    const QImage image = surface.Capture();
    EXPECT_EQ(size.width, image.width() / static_cast<int>(image.devicePixelRatio()));
    Save(image, QStringLiteral("room_playing_%1").arg(QLatin1String(size.name)));
  }

}

TEST_F(SurfaceRenderTest, StatesOfTheListeningPage) {

  {
    // Paused, CD-quality through the system mixer, second cover.
    Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
    surface.Play(7, TransportBar::State::Paused, true, CdThroughMixer());
    Save(surface.Capture(), QStringLiteral("room_paused_1440x900"));
  }
  {
    // A playing track without artwork.
    Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
    surface.Play(10, TransportBar::State::Playing, false);
    EXPECT_EQ(CoverArtWidget::Mode::NoArtwork, surface.room.cover()->mode());
    Save(surface.Capture(), QStringLiteral("room_no_artwork_1440x900"));
  }
  {
    // Stopped, but there is a queue to play from.
    Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
    surface.room.SetQueuePlaylist(surface.queue.playlist(), QStringLiteral("Evening Drift"));
    surface.room.SetVolume(62);
    Save(surface.Capture(), QStringLiteral("room_stopped_with_queue_1440x900"));
  }
  {
    // Nothing at all: no track, no playlist yet.
    Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
    surface.room.SetPlaylists({});
    surface.room.SetVolume(62);
    Save(surface.Capture(), QStringLiteral("room_empty_1440x900"));
  }
  {
    Surface surface(QStringLiteral("adrift-dark"), 1000, 700);
    surface.room.SetPlaylists({});
    surface.room.SetVolume(62);
    Save(surface.Capture(), QStringLiteral("room_empty_1000x700"));
  }
  {
    // A hostile tag, in a compact window.
    Surface surface(QStringLiteral("adrift-dark"), 1000, 700);
    surface.Play(2, TransportBar::State::Playing);
    RoomTrack long_track = TrackAt(2);
    long_track.title = QStringLiteral("A very long title that goes on and on, with a subtitle, a movement number, a remaster note and a live recording tag");
    long_track.artist = QStringLiteral("An Artist With A Name So Long It Cannot Possibly Fit, featuring Several Other Collaborators");
    surface.room.SetNowPlaying(long_track);
    Save(surface.Capture(), QStringLiteral("room_long_tags_1000x700"));
  }

}

TEST_F(SurfaceRenderTest, PlaylistsStreamingAndLibraryPages) {

  struct Page {
    ListeningRoom::View view;
    const char *name;
  };
  for (const Page page : { Page{ ListeningRoom::View::Playlists, "playlists" }, Page{ ListeningRoom::View::Streaming, "streaming" }, Page{ ListeningRoom::View::Library, "library_standin" } }) {
    Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
    surface.Play(3, TransportBar::State::Playing);
    surface.room.SetView(page.view);
    const QImage image = surface.Capture();
    Save(image, QStringLiteral("room_%1_1440x900").arg(QLatin1String(page.name)));
  }

  {
    Surface surface(QStringLiteral("adrift-dark"), 1000, 700);
    surface.Play(3, TransportBar::State::Playing);
    surface.room.SetView(ListeningRoom::View::Playlists);
    Save(surface.Capture(), QStringLiteral("room_playlists_1000x700"));
  }
  {
    Surface surface(QStringLiteral("adrift-dark"), 1000, 700);
    surface.Play(3, TransportBar::State::Playing);
    surface.room.SetView(ListeningRoom::View::Streaming);
    Save(surface.Capture(), QStringLiteral("room_streaming_1000x700"));
  }
  {
    // No streaming service available in this build.
    Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
    surface.room.SetStreamingServices({});
    surface.room.SetView(ListeningRoom::View::Streaming);
    Save(surface.Capture(), QStringLiteral("room_streaming_none_1440x900"));
  }

}

TEST_F(SurfaceRenderTest, ThemesOfTheMainSurface) {

  for (const QString &theme : { QStringLiteral("omarchy"), QStringLiteral("system") }) {
    Surface surface(theme, 1440, 900);
    surface.Play(5, TransportBar::State::Playing);
    Save(surface.Capture(), QStringLiteral("room_playing_%1_1440x900").arg(theme));
  }

}

TEST_F(SurfaceRenderTest, TheSurfaceIsNotMostlyEmpty) {

  // A guard against a blank or collapsed surface: the playing room has a lot going on, with colours from the cover and the accent.
  Surface surface(QStringLiteral("adrift-dark"), 1440, 900);
  surface.Play(3, TransportBar::State::Playing);
  const QImage image = surface.Capture().convertToFormat(QImage::Format_RGB32);
  QMap<QRgb, int> buckets;
  for (int y = 0; y < image.height(); y += 6) {
    for (int x = 0; x < image.width(); x += 6) {
      const QRgb pixel = image.pixel(x, y);
      buckets[qRgb(qRed(pixel) / 24, qGreen(pixel) / 24, qBlue(pixel) / 24)]++;
    }
  }
  EXPECT_GT(buckets.size(), 25);

  // The cover is large: at 1440x900 its sleeve covers a substantial part of the page height.
  EXPECT_GE(surface.room.cover()->SleeveRect().height(), 380);

}
