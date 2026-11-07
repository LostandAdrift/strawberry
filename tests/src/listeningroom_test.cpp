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

#include <cmath>

#include <QLabel>
#include <QScrollBar>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

#include "listeningroom/coverartwidget.h"
#include "listeningroom/elidedlabel.h"
#include "listeningroom/libraryview.h"
#include "listeningroom/listeningroom.h"
#include "listeningroom/roomwidgets.h"
#include "listeningroom/roomqueuemodel.h"
#include "listeningroom/roomqueueview.h"
#include "listeningroom/seekslider.h"
#include "listeningroom/transportbar.h"
#include "listeningroom_fixture.h"
#include "widgets/signalpathwidget.h"

namespace {

RoomTrack SampleTrack() {
  RoomTrack track;
  track.title = QStringLiteral("Salt Light");
  track.artist = QStringLiteral("Marin Holt");
  track.album = QStringLiteral("Slow Tide");
  track.detail = QStringLiteral("2021 · Track 1");
  return track;
}

// A real double click on a view: the first click, then the double click.
void DoubleClick(QWidget *viewport, const QPoint &position) {
  QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, position);
  QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, position);
}

void ShowAt(ListeningRoom &room, const int width, const int height) {
  room.resize(width, height);
  room.show();
  QCoreApplication::processEvents();
  room.layout()->activate();
}

}  // namespace

// --------------------------------------------------------------------------------------------------------------- navigation

TEST(ListeningRoomTest, OpensOnListeningAndNavigationButtonsSwitchPages) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  qRegisterMetaType<ListeningRoom::View>();
  QSignalSpy changed(&room, &ListeningRoom::ViewChanged);

  EXPECT_EQ(ListeningRoom::View::Listening, room.view());
  EXPECT_TRUE(room.nav_button(ListeningRoom::View::Listening)->isChecked());
  EXPECT_TRUE(room.page(ListeningRoom::View::Listening)->isVisible());

  for (const ListeningRoom::View view : { ListeningRoom::View::Playlists, ListeningRoom::View::Streaming, ListeningRoom::View::Library, ListeningRoom::View::Listening }) {
    room.nav_button(view)->click();
    EXPECT_EQ(view, room.view());
    EXPECT_TRUE(room.nav_button(view)->isChecked());
    EXPECT_TRUE(room.page(view)->isVisible());
    // Exactly one page at a time.
    int visible_pages = 0;
    for (const ListeningRoom::View other : { ListeningRoom::View::Listening, ListeningRoom::View::Library, ListeningRoom::View::Playlists, ListeningRoom::View::Streaming }) {
      if (room.page(other)->isVisible()) ++visible_pages;
    }
    EXPECT_EQ(1, visible_pages);
  }
  EXPECT_EQ(4, changed.count());

  // Selecting the view that is already shown does not announce a change.
  room.nav_button(ListeningRoom::View::Listening)->click();
  EXPECT_EQ(4, changed.count());

}

TEST(ListeningRoomTest, KeyboardShortcutsSwitchViews) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  QTest::keyClick(&room, Qt::Key_2, Qt::AltModifier);
  EXPECT_EQ(ListeningRoom::View::Library, room.view());
  QTest::keyClick(&room, Qt::Key_3, Qt::AltModifier);
  EXPECT_EQ(ListeningRoom::View::Playlists, room.view());
  QTest::keyClick(&room, Qt::Key_4, Qt::AltModifier);
  EXPECT_EQ(ListeningRoom::View::Streaming, room.view());
  QTest::keyClick(&room, Qt::Key_1, Qt::AltModifier);
  EXPECT_EQ(ListeningRoom::View::Listening, room.view());

}

TEST(ListeningRoomTest, ViewNamesRoundTripAndUnknownFallsBack) {

  for (const ListeningRoom::View view : { ListeningRoom::View::Listening, ListeningRoom::View::Library, ListeningRoom::View::Playlists, ListeningRoom::View::Streaming }) {
    EXPECT_EQ(view, ListeningRoom::ViewFromString(ListeningRoom::ViewToString(view)));
  }
  EXPECT_EQ(ListeningRoom::View::Listening, ListeningRoom::ViewFromString(QStringLiteral("nonsense")));
  EXPECT_EQ(ListeningRoom::View::Library, ListeningRoom::ViewFromString(QString(), ListeningRoom::View::Library));

}

TEST(ListeningRoomTest, LibraryIsNativeAndTheClassicWindowIsOnlyAnExplicitAdvancedPage) {

  ListeningRoom room;
  QLabel *classic = new QLabel(QStringLiteral("classic window"));
  room.classic_container()->layout()->addWidget(classic);
  ShowAt(room, 1440, 900);

  // The default surfaces never show the classic window, and the room's transport is there on every one of them.
  EXPECT_FALSE(classic->isVisible());
  EXPECT_TRUE(room.transport()->isVisible());

  room.nav_button(ListeningRoom::View::Library)->click();
  EXPECT_TRUE(room.library()->isVisible());
  EXPECT_FALSE(classic->isVisible());
  // One transport for the whole app, also in the Library: there is no second bar.
  EXPECT_TRUE(room.transport()->isVisible());
  // The navigation stays reachable from the Library, so the way back is always visible.
  EXPECT_TRUE(room.nav_button(ListeningRoom::View::Listening)->isVisible());

  // The advanced action is the only way in. The rail keeps "Library" lit, and the classic window brings its own controls.
  room.library()->advanced_button()->click();
  EXPECT_EQ(ListeningRoom::View::Classic, room.view());
  EXPECT_TRUE(classic->isVisible());
  EXPECT_FALSE(room.library()->isVisible());
  EXPECT_FALSE(room.transport()->isVisible());
  EXPECT_TRUE(room.nav_button(ListeningRoom::View::Library)->isChecked());

  for (const ListeningRoom::View view : { ListeningRoom::View::Playlists, ListeningRoom::View::Streaming, ListeningRoom::View::Listening, ListeningRoom::View::Library }) {
    room.nav_button(view)->click();
    EXPECT_FALSE(classic->isVisible());
    EXPECT_TRUE(room.transport()->isVisible());
  }

  // No other page offers the classic window.
  EXPECT_EQ(ListeningRoom::View::Listening, ListeningRoom::ViewFromString(QStringLiteral("classic"), ListeningRoom::View::Listening));

}

TEST(ListeningRoomTest, SettingsButtonRequestsSettings) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  QSignalSpy requested(&room, &ListeningRoom::SettingsRequested);
  room.settings_button()->click();
  EXPECT_EQ(1, requested.count());
  EXPECT_EQ(ListeningRoom::View::Listening, room.view());  // it is an action, not a destination

}

// --------------------------------------------------------------------------------------------------------------- now playing

TEST(ListeningRoomTest, EmptyStateOffersWaysIn) {

  ListeningRoom room;
  QSignalSpy import_requested(&room, &ListeningRoom::ImportPlaylistsRequested);
  room.SetStreamingServices({ { QStringLiteral("tidal"), QStringLiteral("Tidal"), QStringLiteral("Signed in"), true } });
  ShowAt(room, 1440, 900);

  EXPECT_FALSE(room.track().valid());
  EXPECT_EQ(CoverArtWidget::Mode::Idle, room.cover()->mode());
  EXPECT_TRUE(room.empty_library_button()->isVisible());
  EXPECT_TRUE(room.empty_import_button()->isVisible());

  room.empty_import_button()->click();
  ASSERT_EQ(1, import_requested.count());
  EXPECT_TRUE(import_requested.first().first().toString().isEmpty());  // no service preselected

  room.empty_library_button()->click();
  EXPECT_EQ(ListeningRoom::View::Library, room.view());

}

TEST(ListeningRoomTest, TrackReplacesTheEmptyStateAndShowsItsDetails) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetNowPlaying(SampleTrack());
  room.SetCover(RoomFixture::Cover());

  EXPECT_EQ(QStringLiteral("Salt Light"), room.title_label()->text());
  EXPECT_EQ(QStringLiteral("Marin Holt"), room.artist_label()->text());
  EXPECT_EQ(QStringLiteral("Slow Tide"), room.album_label()->text());
  EXPECT_TRUE(room.artist_label()->isVisible());
  EXPECT_FALSE(room.empty_library_button()->isVisible());
  EXPECT_EQ(CoverArtWidget::Mode::Artwork, room.cover()->mode());
  // The persistent transport names the track too, so it is visible on every page.
  EXPECT_EQ(QStringLiteral("Salt Light"), room.transport()->title_label()->text());
  EXPECT_EQ(QStringLiteral("Marin Holt"), room.transport()->artist_label()->text());

  // The next track replaces everything, nothing of the first stays behind.
  RoomTrack next;
  next.title = QStringLiteral("Harbour Lamps");
  next.artist = QStringLiteral("Marin Holt");
  room.SetNowPlaying(next);
  room.SetCover(QImage());
  EXPECT_EQ(QStringLiteral("Harbour Lamps"), room.title_label()->text());
  EXPECT_FALSE(room.album_label()->isVisible());
  EXPECT_EQ(CoverArtWidget::Mode::NoArtwork, room.cover()->mode());  // a deliberate no-art state, not the idle one

}

TEST(ListeningRoomTest, StoppingReturnsToTheIdleState) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetNowPlaying(SampleTrack());
  room.SetCover(RoomFixture::Cover());

  room.SetNowPlaying(RoomTrack());
  room.SetPlaybackState(TransportBar::State::Stopped);
  EXPECT_EQ(CoverArtWidget::Mode::Idle, room.cover()->mode());
  EXPECT_TRUE(room.empty_library_button()->isVisible());
  EXPECT_EQ(TransportBar::State::Stopped, room.transport()->state());
  EXPECT_FALSE(room.transport()->stop_button()->isEnabled());
  EXPECT_EQ(QStringLiteral("–:––"), room.transport()->elapsed_label()->text());

}

TEST(ListeningRoomTest, SignalPathBadgeIsTheSameWidgetTheMainWindowFeeds) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  ASSERT_NE(nullptr, room.signal_path_widget());
  EXPECT_FALSE(room.signal_path_widget()->isVisible());  // nothing to show yet

  SignalPath::Source source;
  source.format = QStringLiteral("FLAC");
  source.lossiness = SignalPath::Lossiness::Lossless;
  source.bitdepth = 16;
  source.samplerate = 44100;
  SignalPath::Config config;
  config.output = QStringLiteral("pulsesink");
  config.volume_percent = 100;
  room.signal_path_widget()->SetReport(SignalPath::Analyze(source, config));
  EXPECT_TRUE(room.signal_path_widget()->isVisible());

  QSignalSpy details(&room, &ListeningRoom::SignalPathDetailsRequested);
  room.signal_path_widget()->click();
  EXPECT_EQ(1, details.count());

}

// --------------------------------------------------------------------------------------------------------------- queue

TEST(ListeningRoomTest, QueueIsAViewOfTheRealPlaylist) {

  RoomFixture::QueueFixture fixture(10);
  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));

  RoomQueueModel *model = room.queue_model();
  ASSERT_EQ(10, model->rowCount());
  EXPECT_EQ(QStringLiteral("Salt Light"), model->data(model->index(0), RoomQueueModel::TitleRole).toString());
  EXPECT_EQ(QStringLiteral("Marin Holt"), model->data(model->index(0), RoomQueueModel::ArtistRole).toString());
  EXPECT_EQ(252, model->data(model->index(0), RoomQueueModel::LengthSecondsRole).toInt());

  // The playlist is the only store: changing it changes the queue, with no copy to go stale.
  fixture.playlist()->InsertItems(PlaylistItemPtrList() << PlaylistItem::NewFromSong(RoomFixture::MakeSong({ QStringLiteral("Added Later"), QStringLiteral("Someone"), QStringLiteral("Else"), 100 })));
  EXPECT_EQ(11, model->rowCount());
  EXPECT_EQ(QStringLiteral("Added Later"), model->data(model->index(10), RoomQueueModel::TitleRole).toString());

  fixture.playlist()->RemoveItemsWithoutUndo(QList<int>() << 0);
  EXPECT_EQ(10, model->rowCount());
  EXPECT_EQ(QStringLiteral("Harbour Lamps"), model->data(model->index(0), RoomQueueModel::TitleRole).toString());

  EXPECT_TRUE(room.queue_view()->isVisible());

}

TEST(ListeningRoomTest, CurrentTrackIsHighlightedAndPositionsStayStable) {

  RoomFixture::QueueFixture fixture(8);
  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));
  RoomQueueModel *model = room.queue_model();

  // Not playing: the playlist may remember a current row, but nothing is "now playing".
  fixture.SetCurrent(3);
  for (int row = 0; row < model->rowCount(); ++row) EXPECT_FALSE(model->data(model->index(row), RoomQueueModel::IsCurrentRole).toBool()) << row;

  room.SetPlaybackState(TransportBar::State::Playing);
  EXPECT_TRUE(model->data(model->index(3), RoomQueueModel::IsCurrentRole).toBool());
  EXPECT_FALSE(model->data(model->index(2), RoomQueueModel::IsCurrentRole).toBool());
  EXPECT_EQ(3, model->current_row());

  // The shown numbers are playlist positions. They do not change with the current track, because the playlist order says nothing about
  // what plays next (explicit queueing, repeat and shuffle can change that).
  for (int row = 0; row < model->rowCount(); ++row) EXPECT_EQ(row + 1, model->data(model->index(row), RoomQueueModel::PositionRole).toInt()) << row;
  fixture.SetCurrent(5);
  EXPECT_FALSE(model->data(model->index(3), RoomQueueModel::IsCurrentRole).toBool());
  EXPECT_TRUE(model->data(model->index(5), RoomQueueModel::IsCurrentRole).toBool());
  for (int row = 0; row < model->rowCount(); ++row) EXPECT_EQ(row + 1, model->data(model->index(row), RoomQueueModel::PositionRole).toInt()) << row;

  // Stopping removes the highlight.
  room.SetPlaybackState(TransportBar::State::Stopped);
  EXPECT_FALSE(model->data(model->index(5), RoomQueueModel::IsCurrentRole).toBool());

}

TEST(ListeningRoomTest, HeadingSaysWhereTheListComesFromNotWhatIsNext) {

  RoomFixture::QueueFixture playing(8, 1);
  RoomFixture::QueueFixture other(5, 2);
  ListeningRoom room;
  ShowAt(room, 1440, 900);

  auto heading = [&room]() {
    for (const QLabel *label : room.findChildren<QLabel*>()) {
      if (label->isVisible() && (label->text() == QStringLiteral("Playing from") || label->text() == QStringLiteral("Browsing") || label->text() == QStringLiteral("Playlist"))) return label->text();
    }
    return QString();
  };
  auto mentions_order = [&room]() {
    for (const QLabel *label : room.findChildren<QLabel*>()) {
      const QString text = label->text().toLower();
      if (label->isVisible() && (text.contains(QStringLiteral("up next")) || text == QStringLiteral("queue") || text.contains(QStringLiteral("next track")))) return true;
    }
    return false;
  };

  // Nothing plays: it is just the selected playlist.
  room.SetQueuePlaylist(playing.playlist(), QStringLiteral("Evening"));
  EXPECT_EQ(QStringLiteral("Playlist"), heading());

  // Playing: "Playing from".
  room.SetPlaybackState(TransportBar::State::Playing);
  EXPECT_EQ(QStringLiteral("Playing from"), heading());
  room.SetPlaybackState(TransportBar::State::Paused);
  EXPECT_EQ(QStringLiteral("Playing from"), heading());

  // Another playlist while one plays: "Browsing".
  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetQueuePlaylist(other.playlist(), QStringLiteral("Small Hours"));
  room.SetQueueBrowsing(QStringLiteral("Evening"));
  EXPECT_EQ(QStringLiteral("Browsing"), heading());

  // Back to the playing one.
  room.SetQueuePlaylist(playing.playlist(), QStringLiteral("Evening"));
  room.SetQueueBrowsing(QString());
  EXPECT_EQ(QStringLiteral("Playing from"), heading());

  // No wording anywhere promises an order of what plays next, whatever the playlist, shuffle or repeat do.
  EXPECT_FALSE(mentions_order());
  room.SetPlaybackState(TransportBar::State::Stopped);
  EXPECT_FALSE(mentions_order());
  EXPECT_EQ(QStringLiteral("Ready"), room.status_label()->text());

}

TEST(ListeningRoomTest, DoubleClickAndEnterPlayTheRow) {

  RoomFixture::QueueFixture fixture(8);
  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));
  QSignalSpy activated(&room, &ListeningRoom::QueueRowActivated);

  RoomQueueView *view = room.queue_view();
  const QModelIndex index = room.queue_model()->index(2);
  const QPoint centre = view->visualRect(index).center();
  DoubleClick(view->viewport(), centre);
  ASSERT_EQ(1, activated.count());
  EXPECT_EQ(2, activated.first().first().toInt());

  view->setCurrentIndex(room.queue_model()->index(5));
  view->setFocus();
  QTest::keyClick(view, Qt::Key_Return);
  ASSERT_EQ(2, activated.count());
  EXPECT_EQ(5, activated.last().first().toInt());

}

TEST(ListeningRoomTest, SwitchingPlaylistsRebindsTheQueue) {

  RoomFixture::QueueFixture first(5, 1);
  RoomFixture::QueueFixture second(9, 2);
  ListeningRoom room;
  ShowAt(room, 1440, 900);

  room.SetQueuePlaylist(first.playlist(), QStringLiteral("First"));
  EXPECT_EQ(5, room.queue_model()->rowCount());
  room.SetQueuePlaylist(second.playlist(), QStringLiteral("Second"));
  EXPECT_EQ(9, room.queue_model()->rowCount());
  EXPECT_EQ(second.playlist(), room.queue_model()->playlist());

  // Changes of the playlist that is no longer shown do not touch the queue.
  first.playlist()->InsertItems(PlaylistItemPtrList() << PlaylistItem::NewFromSong(RoomFixture::MakeSong(RoomFixture::Tracks().first())));
  EXPECT_EQ(9, room.queue_model()->rowCount());

  // No playlist (for example while playlists load) is an empty queue, not a crash.
  room.SetQueuePlaylist(nullptr, QString());
  EXPECT_EQ(0, room.queue_model()->rowCount());

}

TEST(ListeningRoomTest, QueueSurvivesItsPlaylistBeingDestroyed) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  {
    RoomFixture::QueueFixture fixture(6);
    room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Temporary"));
    ASSERT_EQ(6, room.queue_model()->rowCount());
  }
  // The playlist was closed and deleted: the queue empties instead of pointing at freed memory.
  EXPECT_EQ(0, room.queue_model()->rowCount());
  EXPECT_EQ(nullptr, room.queue_model()->playlist());

}

// --------------------------------------------------------------------------------------------------------------- transport

TEST(ListeningRoomTest, TransportButtonsEmitRequests) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  TransportBar *bar = room.transport();
  QSignalSpy previous(bar, &TransportBar::PreviousRequested);
  QSignalSpy play(bar, &TransportBar::PlayPauseRequested);
  QSignalSpy next(bar, &TransportBar::NextRequested);
  QSignalSpy stop(bar, &TransportBar::StopRequested);
  QSignalSpy mute(bar, &TransportBar::MuteToggleRequested);

  // Stop is disabled while stopped, the others are always available.
  EXPECT_FALSE(bar->stop_button()->isEnabled());
  bar->previous_button()->click();
  bar->play_button()->click();
  bar->next_button()->click();
  bar->speaker_button()->click();
  EXPECT_EQ(1, previous.count());
  EXPECT_EQ(1, play.count());
  EXPECT_EQ(1, next.count());
  EXPECT_EQ(1, mute.count());

  room.SetPlaybackState(TransportBar::State::Playing);
  EXPECT_TRUE(bar->stop_button()->isEnabled());
  bar->stop_button()->click();
  EXPECT_EQ(1, stop.count());

}

TEST(ListeningRoomTest, PlayButtonShowsAndNamesTheAction) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  TransportBar *bar = room.transport();

  EXPECT_EQ(TransportButton::Kind::Play, bar->play_button()->kind());
  EXPECT_EQ(QStringLiteral("Play"), bar->play_button()->accessibleName());
  room.SetPlaybackState(TransportBar::State::Playing);
  EXPECT_EQ(TransportButton::Kind::Pause, bar->play_button()->kind());
  EXPECT_EQ(QStringLiteral("Pause"), bar->play_button()->accessibleName());
  room.SetPlaybackState(TransportBar::State::Paused);
  EXPECT_EQ(TransportButton::Kind::Play, bar->play_button()->kind());

  // Every control has an accessible name and can be reached with the keyboard.
  for (QAbstractButton *button : { static_cast<QAbstractButton*>(bar->previous_button()), static_cast<QAbstractButton*>(bar->play_button()), static_cast<QAbstractButton*>(bar->next_button()), static_cast<QAbstractButton*>(bar->stop_button()), static_cast<QAbstractButton*>(bar->speaker_button()) }) {
    EXPECT_FALSE(button->accessibleName().isEmpty());
    EXPECT_EQ(Qt::StrongFocus, button->focusPolicy());
  }

}

TEST(ListeningRoomTest, SeekFollowsPlaybackSilentlyAndUserSeeksEmit) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  TransportBar *bar = room.transport();
  QSignalSpy seek(bar, &TransportBar::SeekRequested);

  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetCanSeek(true);
  room.SetPosition(61000, 252000);
  EXPECT_EQ(QStringLiteral("1:01"), bar->elapsed_label()->text());
  EXPECT_EQ(QStringLiteral("4:12"), bar->duration_label()->text());
  EXPECT_EQ(0, seek.count());  // following playback is not a seek request

  // A click on the bar seeks to that fraction of the track.
  SeekSlider *slider = bar->seek_slider();
  const int x = slider->width() / 2;
  QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier, QPoint(x, slider->height() / 2));
  ASSERT_EQ(1, seek.count());
  EXPECT_NEAR(126000, seek.first().first().toInt(), 6000);

  // The keyboard seeks in steps and stays within the track.
  slider->setFocus();
  slider->SetPosition(10000, 252000);
  QTest::keyClick(slider, Qt::Key_Right);
  EXPECT_EQ(15000, seek.last().first().toInt());
  QTest::keyClick(slider, Qt::Key_Home);
  EXPECT_EQ(0, seek.last().first().toInt());
  QTest::keyClick(slider, Qt::Key_End);
  EXPECT_EQ(252000, seek.last().first().toInt());

  // While the user drags, playback updates do not fight the handle.
  QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() / 4, slider->height() / 2));
  const int dragged_to = slider->value();
  room.SetPosition(200000, 252000);
  EXPECT_EQ(dragged_to, slider->value());
  QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() / 4, slider->height() / 2));

}

TEST(ListeningRoomTest, SeekIgnoredWhenTrackCannotBeSeeked) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  QSignalSpy seek(room.transport(), &TransportBar::SeekRequested);
  room.SetCanSeek(false);
  room.SetPosition(1000, 100000);
  SeekSlider *slider = room.transport()->seek_slider();
  QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier, QPoint(slider->width() / 2, slider->height() / 2));
  slider->setFocus();
  QTest::keyClick(slider, Qt::Key_Right);
  EXPECT_EQ(0, seek.count());

}

TEST(ListeningRoomTest, VolumeIsShownNotForcedAndUserChangesEmit) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  TransportBar *bar = room.transport();
  QSignalSpy volume(bar, &TransportBar::VolumeRequested);

  // Showing the user's volume (at start, or when the player reports a change) must never write it back.
  room.SetVolume(37);
  room.SetVolume(80);
  EXPECT_EQ(0, volume.count());
  EXPECT_EQ(80, bar->volume_slider()->value());

  // Only the user's own action changes it, in small steps.
  bar->volume_slider()->setFocus();
  QTest::keyClick(bar->volume_slider(), Qt::Key_Left);
  ASSERT_EQ(1, volume.count());
  EXPECT_EQ(75, volume.first().first().toInt());

  // Mute is shown without touching the volume or announcing a volume change.
  room.SetMuted(true);
  EXPECT_EQ(TransportButton::Kind::SpeakerMuted, bar->speaker_button()->kind());
  EXPECT_EQ(QStringLiteral("Unmute"), bar->speaker_button()->accessibleName());
  EXPECT_EQ(75, bar->volume_slider()->value());
  EXPECT_EQ(1, volume.count());

  room.SetVolumeEnabled(false);
  EXPECT_FALSE(bar->volume_slider()->isEnabled() && bar->volume_slider()->can_seek());

}

TEST(SeekSliderTest, TimeFormattingAndPositionMapping) {

  EXPECT_EQ(QStringLiteral("0:00"), SeekSlider::FormatTime(0));
  EXPECT_EQ(QStringLiteral("0:59"), SeekSlider::FormatTime(59999));
  EXPECT_EQ(QStringLiteral("1:00"), SeekSlider::FormatTime(60000));
  EXPECT_EQ(QStringLiteral("1:02:03"), SeekSlider::FormatTime(3723000));
  EXPECT_EQ(QStringLiteral("0:00"), SeekSlider::FormatTime(-5));

  SeekSlider slider;
  slider.resize(206, 24);
  slider.SetPosition(0, 100000);
  EXPECT_EQ(0, slider.PositionForX(0));
  EXPECT_EQ(100000, slider.PositionForX(10000));
  EXPECT_NEAR(50000, slider.PositionForX(103), 600);

}

// --------------------------------------------------------------------------------------------------------------- playlists and streaming

TEST(ListeningRoomTest, PlaylistsPageListsAndOpensPlaylists) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.nav_button(ListeningRoom::View::Playlists)->click();
  room.SetStreamingServices({ { QStringLiteral("tidal"), QStringLiteral("Tidal"), QStringLiteral("Signed in"), true } });

  // Empty: it explains itself.
  room.SetPlaylists({});
  EXPECT_FALSE(room.playlists_list()->isVisible());

  RoomPlaylistCard evening;
  evening.id = 7;
  evening.name = QStringLiteral("Evening");
  evening.track_count = 14;
  evening.is_current = true;
  evening.is_playing = true;
  RoomPlaylistCard snapshot;
  snapshot.id = 9;
  snapshot.name = QStringLiteral("Tidal: Night Drive");
  snapshot.track_count = 1;
  snapshot.kind = QStringLiteral("Tidal snapshot");
  room.SetPlaylists({ evening, snapshot });
  ASSERT_EQ(2, room.playlists_list()->count());
  EXPECT_TRUE(room.playlists_list()->isVisible());

  QSignalSpy opened(&room, &ListeningRoom::PlaylistActivated);
  room.playlists_list()->setCurrentRow(1);
  DoubleClick(room.playlists_list()->viewport(), room.playlists_list()->visualItemRect(room.playlists_list()->item(1)).center());
  ASSERT_EQ(1, opened.count());
  EXPECT_EQ(9, opened.first().first().toInt());
  // Opening a playlist goes to the queue of that playlist.
  EXPECT_EQ(ListeningRoom::View::Listening, room.view());

  // Refreshing the list keeps the selection on the same playlist.
  room.nav_button(ListeningRoom::View::Playlists)->click();
  room.playlists_list()->setCurrentRow(1);
  room.SetPlaylists({ snapshot, evening });
  EXPECT_EQ(9, room.playlists_list()->currentItem()->data(Qt::UserRole + 1).toInt());

}

TEST(ListeningRoomTest, ImportEntryPointsOnThePlaylistsPage) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  QSignalSpy import_requested(&room, &ListeningRoom::ImportPlaylistsRequested);

  // Without any streaming service the import entry points are not shown.
  room.SetStreamingServices({});
  EXPECT_FALSE(room.playlists_import_button()->isVisible() && room.view() == ListeningRoom::View::Playlists);
  room.nav_button(ListeningRoom::View::Playlists)->click();
  EXPECT_FALSE(room.playlists_import_button()->isVisible());

  room.SetStreamingServices({ { QStringLiteral("tidal"), QStringLiteral("Tidal"), QStringLiteral("Signed in"), true } });
  EXPECT_TRUE(room.playlists_import_button()->isVisible());
  room.playlists_import_button()->click();
  ASSERT_EQ(1, import_requested.count());
  EXPECT_TRUE(import_requested.first().first().toString().isEmpty());

}

TEST(ListeningRoomTest, StreamingPageShowsRealStateAndRoutesActions) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.nav_button(ListeningRoom::View::Streaming)->click();

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
  room.SetStreamingServices({ tidal, qobuz });
  QCoreApplication::processEvents();

  QWidget *tidal_card = room.streaming_cards()->findChild<QWidget*>(QStringLiteral("service_tidal"));
  QWidget *qobuz_card = room.streaming_cards()->findChild<QWidget*>(QStringLiteral("service_qobuz"));
  ASSERT_NE(nullptr, tidal_card);
  ASSERT_NE(nullptr, qobuz_card);
  EXPECT_EQ(QStringLiteral("Signed in"), tidal_card->findChild<QLabel*>(QStringLiteral("status"))->text());
  EXPECT_EQ(QStringLiteral("Not signed in"), qobuz_card->findChild<QLabel*>(QStringLiteral("status"))->text());

  // Browsing and importing need a signed-in account; signing in is always possible.
  EXPECT_TRUE(tidal_card->findChild<QPushButton*>(QStringLiteral("browse"))->isEnabled());
  EXPECT_TRUE(tidal_card->findChild<QPushButton*>(QStringLiteral("import"))->isEnabled());
  EXPECT_FALSE(qobuz_card->findChild<QPushButton*>(QStringLiteral("browse"))->isEnabled());
  EXPECT_FALSE(qobuz_card->findChild<QPushButton*>(QStringLiteral("import"))->isEnabled());
  EXPECT_TRUE(qobuz_card->findChild<QPushButton*>(QStringLiteral("settings"))->isEnabled());

  QSignalSpy browse(&room, &ListeningRoom::BrowseServiceRequested);
  QSignalSpy import(&room, &ListeningRoom::ImportPlaylistsRequested);
  QSignalSpy settings(&room, &ListeningRoom::ServiceSettingsRequested);
  tidal_card->findChild<QPushButton*>(QStringLiteral("browse"))->click();
  tidal_card->findChild<QPushButton*>(QStringLiteral("import"))->click();
  qobuz_card->findChild<QPushButton*>(QStringLiteral("settings"))->click();
  ASSERT_EQ(1, browse.count());
  ASSERT_EQ(1, import.count());
  ASSERT_EQ(1, settings.count());
  EXPECT_EQ(QStringLiteral("tidal"), browse.first().first().toString());
  EXPECT_EQ(QStringLiteral("tidal"), import.first().first().toString());
  EXPECT_EQ(QStringLiteral("qobuz"), settings.first().first().toString());

  // The state is rebuilt, never duplicated, when it changes (for example after signing in).
  qobuz.signed_in = true;
  qobuz.status = QStringLiteral("Signed in");
  room.SetStreamingServices({ tidal, qobuz });
  qobuz_card = room.streaming_cards()->findChild<QWidget*>(QStringLiteral("service_qobuz"));
  ASSERT_NE(nullptr, qobuz_card);
  EXPECT_TRUE(qobuz_card->findChild<QPushButton*>(QStringLiteral("import"))->isEnabled());
  EXPECT_EQ(2, room.streaming_cards()->findChildren<QWidget*>(QRegularExpression(QStringLiteral("^service_"))).size());

}

// --------------------------------------------------------------------------------------------------------------- responsiveness

TEST(ListeningRoomTest, LayoutAdaptsFromLargeToCompactWindows) {

  RoomFixture::QueueFixture fixture(14);
  struct Size {
    int width;
    int height;
  };
  for (const Size size : { Size{ 1920, 1080 }, Size{ 1440, 900 }, Size{ 1000, 700 }, Size{ 800, 560 } }) {
    ListeningRoom room;
    room.SetPlaybackState(TransportBar::State::Playing);
    room.SetNowPlaying(SampleTrack());
    room.SetCover(RoomFixture::Cover());
    room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));
    room.SetPosition(61000, 252000);
    ShowAt(room, size.width, size.height);
    // The requested size is honoured: no minimum of any page (or of the hidden classic window) pushes the window larger.
    EXPECT_EQ(QSize(size.width, size.height), room.size()) << size.width << "x" << size.height;
    const QRect bounds(QPoint(0, 0), room.size());

    // The side rail goes icon-only in narrow windows, and the Library/Listening destinations stay reachable.
    EXPECT_EQ(size.width < 1100, room.compact()) << size.width;
    EXPECT_TRUE(room.nav_button(ListeningRoom::View::Library)->isVisible());

    // The cover is large but bounded, and always square within its area.
    const QRect sleeve = room.cover()->SleeveRect();
    EXPECT_GE(sleeve.width(), 160) << size.width;
    EXPECT_LE(sleeve.width(), CoverArtWidget::kMaximumSide);
    EXPECT_EQ(sleeve.width(), sleeve.height());
    if (size.width >= 1440) EXPECT_GE(sleeve.width(), 400);

    // The queue and the transport are fully inside the window and usable.
    for (QWidget *widget : { static_cast<QWidget*>(room.queue_view()), static_cast<QWidget*>(room.transport()), static_cast<QWidget*>(room.cover()) }) {
      const QRect rect(widget->mapTo(&room, QPoint(0, 0)), widget->size());
      EXPECT_TRUE(bounds.contains(rect)) << size.width << "x" << size.height << " " << widget->metaObject()->className();
    }
    EXPECT_GE(room.queue_view()->width(), 280) << size.width;
    EXPECT_GE(room.queue_view()->height(), 120) << size.height;
    EXPECT_LE(room.transport()->height(), 120);

    // The play button and the seek bar are always reachable; the title is never wider than its area (it elides instead).
    EXPECT_TRUE(room.transport()->play_button()->isVisible());
    EXPECT_TRUE(room.transport()->seek_slider()->isVisible());
    EXPECT_LE(room.title_label()->width(), room.width());

    // Nothing overlaps the transport: the page above ends where the transport begins.
    const QRect page_rect(room.page(ListeningRoom::View::Listening)->mapTo(&room, QPoint(0, 0)), room.page(ListeningRoom::View::Listening)->size());
    const QRect transport_rect(room.transport()->mapTo(&room, QPoint(0, 0)), room.transport()->size());
    EXPECT_LE(page_rect.bottom(), transport_rect.top()) << size.width;
  }

}

TEST(ListeningRoomTest, VeryLongTagsElideInsteadOfStretchingTheWindow) {

  ListeningRoom room;
  RoomTrack track;
  track.title = QString(300, QLatin1Char('W'));
  track.artist = QString(200, QLatin1Char('M')) + QStringLiteral(" and a very long list of collaborators");
  track.album = QString(200, QLatin1Char('A'));
  track.detail = QString(200, QLatin1Char('D'));
  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetNowPlaying(track);
  room.SetCover(RoomFixture::Cover());
  ShowAt(room, 1000, 700);

  EXPECT_EQ(1000, room.width());
  EXPECT_LE(room.minimumSizeHint().width(), 800);
  EXPECT_TRUE(room.title_label()->IsElided());
  EXPECT_LE(room.title_label()->width(), room.width());
  EXPECT_TRUE(room.transport()->title_label()->IsElided() || !room.transport()->title_label()->isVisible());
  EXPECT_FALSE(room.title_label()->toolTip().isEmpty());  // the full text stays available

}

TEST(ListeningRoomTest, RemoteTextIsNeverInterpretedAsMarkup) {

  ListeningRoom room;
  RoomTrack track;
  track.title = QStringLiteral("<b>bold</b> & <i>italic</i>");
  track.artist = QStringLiteral("<a href=\"http://example.invalid\">link</a>");
  room.SetNowPlaying(track);
  ShowAt(room, 1440, 900);
  EXPECT_EQ(Qt::PlainText, room.title_label()->textFormat());
  EXPECT_EQ(Qt::PlainText, room.artist_label()->textFormat());
  EXPECT_EQ(Qt::PlainText, room.transport()->title_label()->textFormat());
  EXPECT_TRUE(room.title_label()->text().contains(QStringLiteral("<b>")));

}

TEST(ListeningRoomTest, SurvivesRuntimePaletteChanges) {

  RoomFixture::QueueFixture fixture(6);
  ListeningRoom room;
  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetNowPlaying(SampleTrack());
  room.SetCover(RoomFixture::Cover());
  room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));
  ShowAt(room, 1440, 900);

  const QPalette original = QApplication::palette();
  QPalette light;
  light.setColor(QPalette::Window, QColor(244, 241, 235));
  light.setColor(QPalette::WindowText, QColor(30, 28, 26));
  light.setColor(QPalette::Highlight, QColor(150, 90, 30));
  light.setColor(QPalette::HighlightedText, QColor(255, 255, 255));

  // A spot of plain page background (above the queue's right edge, in the room's margin).
  const QPoint spot(room.width() - 6, 6);

  QApplication::setPalette(light);
  QCoreApplication::processEvents();
  const QImage a = room.grab().toImage();
  QApplication::setPalette(original);
  QCoreApplication::processEvents();
  const QImage b = room.grab().toImage();
  ASSERT_FALSE(a.isNull());
  ASSERT_FALSE(b.isNull());

  // The room paints with whatever palette it is given, at once: the page follows the Window color of each palette.
  EXPECT_EQ(QColor(244, 241, 235), a.pixelColor(spot));
  EXPECT_EQ(original.color(QPalette::Window), b.pixelColor(spot));
  EXPECT_NE(a.pixelColor(spot), b.pixelColor(spot));
  // The accent follows the palette too: the playing row's bar uses the Highlight color of the palette in use.
  EXPECT_NE(a, b);

}

// --------------------------------------------------------------------------------------------------------------- review findings

TEST(ListeningRoomQueueChoiceTest, BrowsingAnotherPlaylistWhileOneIsPlayingShowsIt) {

  const QList<int> open = { 1, 2, 3 };

  // A (1) plays and the user chose B (2): the queue shows B, and says that A is what plays. Nothing about playback is decided here.
  ListeningRoom::QueueChoice choice = ListeningRoom::ChooseQueue(open, true, 1, 1, 2);
  EXPECT_EQ(2, choice.shown);
  EXPECT_EQ(1, choice.playing);
  EXPECT_EQ(2, choice.browsed);
  EXPECT_TRUE(choice.browsing());

  // No selection: the playing playlist is shown.
  choice = ListeningRoom::ChooseQueue(open, true, 1, 1, -1);
  EXPECT_EQ(1, choice.shown);
  EXPECT_FALSE(choice.browsing());

  // The selection is the playing playlist: it is not "browsing" any more.
  choice = ListeningRoom::ChooseQueue(open, true, 2, 2, 2);
  EXPECT_EQ(2, choice.shown);
  EXPECT_EQ(-1, choice.browsed);
  EXPECT_FALSE(choice.browsing());

  // Playback stops: the browsed selection ends and the queue follows the selected (current) playlist.
  choice = ListeningRoom::ChooseQueue(open, false, 1, 3, 2);
  EXPECT_EQ(3, choice.shown);
  EXPECT_EQ(-1, choice.browsed);
  EXPECT_EQ(-1, choice.playing);

  // A browsed playlist that was closed in the meantime is ignored, as is a playing one that no longer exists.
  choice = ListeningRoom::ChooseQueue({ 1, 3 }, true, 1, 1, 2);
  EXPECT_EQ(1, choice.shown);
  EXPECT_EQ(-1, choice.browsed);
  choice = ListeningRoom::ChooseQueue({ 3 }, true, 1, 3, -1);
  EXPECT_EQ(3, choice.shown);
  EXPECT_EQ(-1, choice.playing);

  // Nothing open at all.
  choice = ListeningRoom::ChooseQueue({}, false, -1, -1, -1);
  EXPECT_EQ(-1, choice.shown);

}

TEST(ListeningRoomTest, BrowsedQueueIsLabelledAndCanGoBackToWhatIsPlaying) {

  RoomFixture::QueueFixture playing(8, 1);
  RoomFixture::QueueFixture browsed(5, 2);
  ListeningRoom room;
  ShowAt(room, 1440, 900);
  room.SetPlaybackState(TransportBar::State::Playing);
  playing.SetCurrent(2);
  room.SetQueuePlaylist(playing.playlist(), QStringLiteral("Evening"));
  EXPECT_FALSE(room.queue_back_button()->isVisible());

  QSignalSpy back(&room, &ListeningRoom::ShowPlayingQueueRequested);
  QSignalSpy activated(&room, &ListeningRoom::QueueRowActivated);

  // B is chosen while A plays.
  room.SetQueuePlaylist(browsed.playlist(), QStringLiteral("Small Hours"));
  room.SetQueueBrowsing(QStringLiteral("Evening"));
  EXPECT_EQ(5, room.queue_model()->rowCount());
  EXPECT_EQ(browsed.playlist(), room.queue_model()->playlist());
  // The queue is not showing the playing track as "now playing" in B (B has no current track), and says what it is.
  for (int row = 0; row < room.queue_model()->rowCount(); ++row) EXPECT_FALSE(room.queue_model()->data(room.queue_model()->index(row), RoomQueueModel::IsCurrentRole).toBool());
  bool labelled = false;
  for (const QLabel *label : room.findChildren<QLabel*>()) {
    if (label->isVisible() && label->text().contains(QStringLiteral("Evening")) && label->text().contains(QStringLiteral("not the playlist that is playing"))) labelled = true;
  }
  EXPECT_TRUE(labelled);
  EXPECT_TRUE(room.queue_back_button()->isVisible());
  // The transport still names what plays.
  room.SetNowPlaying(SampleTrack());
  EXPECT_EQ(QStringLiteral("Salt Light"), room.transport()->title_label()->text());

  // Activating a row of the browsed playlist reports a row of that playlist (MainWindow makes it the playing one).
  room.queue_view()->setCurrentIndex(room.queue_model()->index(1));
  room.queue_view()->setFocus();
  QTest::keyClick(room.queue_view(), Qt::Key_Return);
  ASSERT_EQ(1, activated.count());
  EXPECT_EQ(1, activated.first().first().toInt());

  room.queue_back_button()->click();
  EXPECT_EQ(1, back.count());

  // Back at the playing queue the label and the button are gone.
  room.SetQueuePlaylist(playing.playlist(), QStringLiteral("Evening"));
  room.SetQueueBrowsing(QString());
  EXPECT_FALSE(room.queue_back_button()->isVisible());
  EXPECT_TRUE(room.queue_model()->data(room.queue_model()->index(2), RoomQueueModel::IsCurrentRole).toBool());

}

TEST(ListeningRoomTest, TransportButtonsFollowTheEnabledStateOfTheirActions) {

  ListeningRoom room;
  ShowAt(room, 1440, 900);
  TransportBar *bar = room.transport();
  QSignalSpy previous(bar, &TransportBar::PreviousRequested);
  QSignalSpy play(bar, &TransportBar::PlayPauseRequested);
  QSignalSpy next(bar, &TransportBar::NextRequested);
  QSignalSpy stop(bar, &TransportBar::StopRequested);

  // For example a stream that cannot be paused: play/pause and previous are off, stop is on.
  room.SetPlaybackState(TransportBar::State::Playing);
  room.SetTransportActionStates(false, false, true, true);
  EXPECT_FALSE(bar->previous_button()->isEnabled());
  EXPECT_FALSE(bar->play_button()->isEnabled());
  EXPECT_TRUE(bar->next_button()->isEnabled());
  EXPECT_TRUE(bar->stop_button()->isEnabled());
  bar->previous_button()->click();
  bar->play_button()->click();
  bar->next_button()->click();
  EXPECT_EQ(0, previous.count());
  EXPECT_EQ(0, play.count());
  EXPECT_EQ(1, next.count());

  // A change of playback state does not override the actions: stopped, the stop action is disabled and the button stays disabled.
  room.SetTransportActionStates(true, true, true, false);
  room.SetPlaybackState(TransportBar::State::Stopped);
  EXPECT_FALSE(bar->stop_button()->isEnabled());
  room.SetPlaybackState(TransportBar::State::Paused);
  EXPECT_FALSE(bar->stop_button()->isEnabled());
  bar->stop_button()->click();
  EXPECT_EQ(0, stop.count());

  room.SetTransportActionStates(true, true, true, true);
  EXPECT_TRUE(bar->stop_button()->isEnabled());
  EXPECT_TRUE(bar->play_button()->isEnabled());

}

TEST(ListeningRoomTest, WordmarkAlwaysFitsItsSpaceAtDifferentFontSizes) {

  for (const int point_size : { 8, 10, 12, 16, 20 }) {
    ListeningRoom room;
    QFont font = room.font();
    font.setPointSize(point_size);
    room.setFont(font);
    ShowAt(room, 1440, 900);

    QWidget *wordmark = room.wordmark();
    ASSERT_NE(nullptr, wordmark);
    // The name and the "Listening Room" line are both fully inside the widget: it is as tall as its own size hint asks.
    EXPECT_GE(wordmark->height(), wordmark->sizeHint().height()) << point_size;
    EXPECT_GE(wordmark->width(), 100) << point_size;
    // The navigation below it starts after it ends.
    const int wordmark_bottom = wordmark->mapTo(&room, QPoint(0, wordmark->height())).y();
    const int first_button_top = room.nav_button(ListeningRoom::View::Listening)->mapTo(&room, QPoint(0, 0)).y();
    EXPECT_LE(wordmark_bottom, first_button_top) << point_size;
  }

}

TEST(ListeningRoomTest, PageHeadingsLineUpAcrossPages) {

  ListeningRoom room;
  room.SetStreamingServices({ { QStringLiteral("tidal"), QStringLiteral("Tidal"), QStringLiteral("Signed in"), true } });
  ShowAt(room, 1440, 900);

  QLabel *playlists_title = room.page(ListeningRoom::View::Playlists)->findChild<QLabel*>(QStringLiteral("page_title"));
  QLabel *streaming_title = room.page(ListeningRoom::View::Streaming)->findChild<QLabel*>(QStringLiteral("page_title"));
  ASSERT_NE(nullptr, playlists_title);
  ASSERT_NE(nullptr, streaming_title);

  room.nav_button(ListeningRoom::View::Playlists)->click();
  const QPoint playlists_pos = playlists_title->mapTo(&room, QPoint(0, 0));
  room.nav_button(ListeningRoom::View::Streaming)->click();
  const QPoint streaming_pos = streaming_title->mapTo(&room, QPoint(0, 0));

  // Same place on both pages, and not jammed against the top edge.
  EXPECT_EQ(playlists_pos, streaming_pos);
  EXPECT_GE(playlists_pos.y(), 24);
  EXPECT_GE(streaming_pos.x(), room.nav_button(ListeningRoom::View::Listening)->width());

  // The import button of the Playlists page sits at the top of the same heading row, not above it.
  room.nav_button(ListeningRoom::View::Playlists)->click();
  const QPoint button_pos = room.playlists_import_button()->mapTo(&room, QPoint(0, 0));
  EXPECT_GE(button_pos.y(), playlists_pos.y() - 4);
  EXPECT_LE(button_pos.y(), playlists_pos.y() + 12);

  // The manual refresh hint belongs to the Streaming page only.
  room.nav_button(ListeningRoom::View::Streaming)->click();
  QLabel *manual = room.page(ListeningRoom::View::Streaming)->findChild<QLabel*>(QStringLiteral("manual_refresh"));
  ASSERT_NE(nullptr, manual);
  EXPECT_TRUE(manual->isVisible());

}

TEST(ListeningRoomTest, ClassicWindowMinimumOnlyAppliesOnTheAdvancedPage) {

  ListeningRoom room;
  // The classic window has a large minimum size (the stock window opens at 1131x685 and does not shrink below roughly that).
  QWidget *classic = new QWidget;
  classic->setMinimumSize(1131, 685);
  room.classic_container()->layout()->addWidget(classic);

  // In the Listening Room, the native Library and the other views the compact size is honoured.
  ShowAt(room, 1000, 700);
  EXPECT_EQ(QSize(1000, 700), room.size());
  for (const ListeningRoom::View view : { ListeningRoom::View::Playlists, ListeningRoom::View::Streaming, ListeningRoom::View::Library, ListeningRoom::View::Listening }) {
    room.nav_button(view)->click();
    QCoreApplication::processEvents();
    room.layout()->activate();
    EXPECT_EQ(QSize(1000, 700), room.size());
  }

  // Only the advanced page asks for the classic window's size.
  room.SetView(ListeningRoom::View::Classic);
  QCoreApplication::processEvents();
  room.layout()->activate();
  EXPECT_GE(room.minimumSizeHint().width(), 720);
  EXPECT_GE(room.width(), 1131);

}

TEST(ListeningRoomTest, QueueScrollsToWholeRowsWithTheCurrentTrackNearTheTop) {

  // A long playlist, playing a track in the middle: the row before it is the first visible row, and no row is cut off at the top.
  {
    RoomFixture::QueueFixture fixture(40);
    ListeningRoom room;
    room.SetPlaybackState(TransportBar::State::Playing);
    room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));
    fixture.SetCurrent(9);
    room.SetNowPlaying(SampleTrack());
    ShowAt(room, 1000, 700);
    QCoreApplication::processEvents();

    RoomQueueView *view = room.queue_view();
    ASSERT_EQ(9, room.queue_model()->current_row());
    EXPECT_EQ(0, view->visualRect(room.queue_model()->index(8)).top());
    EXPECT_EQ(0, view->verticalScrollBar()->value() % std::max(1, view->sizeHintForRow(0)));
    EXPECT_TRUE(view->viewport()->rect().contains(view->visualRect(room.queue_model()->index(9)).center()));

    // The user can still scroll away, and the view stays where they put it while playback goes on.
    view->verticalScrollBar()->setValue(0);
    room.SetPosition(5000, 200000);
    EXPECT_EQ(0, view->verticalScrollBar()->value());
  }

  // Near the end of the list the current row cannot be at the top; the scroll still lands on whole rows and the current row is visible.
  {
    RoomFixture::QueueFixture fixture(40);
    ListeningRoom room;
    room.SetPlaybackState(TransportBar::State::Playing);
    room.SetQueuePlaylist(fixture.playlist(), QStringLiteral("Evening"));
    fixture.SetCurrent(38);
    room.SetNowPlaying(SampleTrack());
    ShowAt(room, 1000, 700);
    QCoreApplication::processEvents();

    RoomQueueView *view = room.queue_view();
    EXPECT_EQ(0, view->verticalScrollBar()->value() % std::max(1, view->sizeHintForRow(0)));
    EXPECT_TRUE(view->viewport()->rect().contains(view->visualRect(room.queue_model()->index(38)).center()));
  }

}
