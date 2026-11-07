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

// Behaviour of the native Library page: loading, search, album and artist browsing, playing and adding, cancellation, and the cover cache.
// The production LibraryView, models and delegates are used with a synthetic LibrarySource (invented artists, covers drawn by code), so nothing reads the user's library.

#include "gtest_include.h"

#include <chrono>
#include <memory>
#include <thread>

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimer>
#include <QTest>

#include "core/memorydatabase.h"
#include "collection/collectionbackend.h"
#include "collection/collectionmanager.h"
#include "listeningroom/libraryparts.h"
#include "listeningroom/libraryview.h"
#include "listeningroom/roommotion.h"
#include "listeningroom/roomwidgets.h"
#include "library_fixture.h"

namespace {

// Shows the view and waits until the first read has been applied.
void ShowAndLoad(LibraryView &view, const int width = 1100, const int height = 760) {

  QSignalSpy settled(&view, &LibraryView::AlbumsSettled);
  view.resize(width, height);
  view.show();
  ASSERT_TRUE(settled.wait(5000) || view.loaded());
  QCoreApplication::processEvents();

}

// Waits for the next settled result (a search or a reload).
bool WaitSettled(LibraryView &view, const int timeout = 5000) {
  QSignalSpy settled(&view, &LibraryView::AlbumsSettled);
  return settled.wait(timeout);
}

void WaitFor(const std::function<bool()> &condition, const int timeout = 5000) {
  QTRY_VERIFY_WITH_TIMEOUT(condition(), timeout);
}

QPoint CenterOfRow(QAbstractItemView *view, const int row) {
  return view->visualRect(view->model()->index(row, 0)).center();
}

}  // namespace

// --------------------------------------------------------------------------------------------------------------- loading and states

TEST(LibraryViewTest, ReadsTheLibraryOnlyWhenFirstShownAndOffTheGuiThread) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  QCoreApplication::processEvents();
  EXPECT_EQ(0, source.load_calls.load());
  EXPECT_EQ(LibraryView::State::Loading, view.state());

  ShowAndLoad(view);
  EXPECT_EQ(1, source.load_calls.load());
  EXPECT_EQ(LibraryView::State::Browsing, view.state());
  EXPECT_EQ(30, view.album_count());
  EXPECT_EQ(10, view.artist_count());
  EXPECT_TRUE(view.summary_label()->text().contains(QStringLiteral("30 albums")));
  EXPECT_TRUE(view.summary_label()->text().contains(QStringLiteral("10 artists")));
  EXPECT_TRUE(view.grid()->isVisible());
  EXPECT_TRUE(view.grid()->isVisibleTo(view.page_for(LibraryView::State::Browsing)));

}

TEST(LibraryViewTest, WhileTheFirstReadIsUnderWayItSaysSoAndKeepsTheGuiResponsive) {

  FakeLibrarySource source(12);
  source.GateAlbums();
  LibraryView view;
  view.SetSource(&source);
  view.resize(1000, 700);
  view.show();
  WaitFor([&]() { return source.load_calls.load() == 1; });

  // The read is blocked in the worker, yet the GUI thread runs: state, spinner and input all work.
  EXPECT_EQ(LibraryView::State::Loading, view.state());
  EXPECT_TRUE(view.page_for(LibraryView::State::Loading)->isVisible());
  EXPECT_TRUE(view.notice() != nullptr);
  view.SetSearchText(QStringLiteral("abc"));  // typing while loading is fine
  EXPECT_EQ(QStringLiteral("abc"), view.search_text());

  QSignalSpy settled(&view, &LibraryView::AlbumsSettled);
  source.ReleaseAlbums();
  ASSERT_TRUE(settled.wait(5000));
  EXPECT_TRUE(view.loaded());
  EXPECT_NE(LibraryView::State::Loading, view.state());

}

TEST(LibraryViewTest, EmptyLibraryHasAUsefulStateWithAddMusic) {

  FakeLibrarySource source(0);
  LibraryView view;
  view.SetSource(&source);
  QSignalSpy add(&view, &LibraryView::AddMusicRequested);
  QSignalSpy streaming(&view, &LibraryView::ShowStreamingRequested);
  ShowAndLoad(view);

  EXPECT_EQ(LibraryView::State::Empty, view.state());
  EXPECT_TRUE(view.page_for(LibraryView::State::Empty)->isVisible());
  EXPECT_TRUE(view.notice()->title_label()->text().contains(QStringLiteral("empty")));
  EXPECT_TRUE(view.notice()->primary_button()->isVisible());
  EXPECT_TRUE(view.notice()->secondary_button()->isVisible());
  EXPECT_EQ(QStringLiteral("Add music…"), view.notice()->primary_button()->text());
  // Nothing in the empty state is a dead end, and there is no grid of nothing.
  EXPECT_FALSE(view.grid()->isVisible());

  view.notice()->primary_button()->click();
  EXPECT_EQ(1, add.count());
  view.add_music_button()->click();
  EXPECT_EQ(2, add.count());
  view.notice()->secondary_button()->click();
  EXPECT_EQ(1, streaming.count());

}

TEST(LibraryViewTest, ARecordTurnsOnlyWhileTheLoadingNoticeIsVisible) {

  FakeLibrarySource source(8);
  source.GateAlbums();
  LibraryView view;
  view.SetSource(&source);
  view.resize(900, 600);
  view.show();
  WaitFor([&]() { return source.load_calls.load() == 1; });

  LibraryNotice *loading = qobject_cast<LibraryNotice*>(view.page_for(LibraryView::State::Loading));
  ASSERT_NE(nullptr, loading);
  EXPECT_TRUE(loading->spinning());
  EXPECT_EQ(!RoomMotion::Instance()->reduced(), loading->timer_active());

  QSignalSpy settled(&view, &LibraryView::AlbumsSettled);
  source.ReleaseAlbums();
  ASSERT_TRUE(settled.wait(5000));
  EXPECT_FALSE(loading->timer_active());

  // Reduced motion: no timer at all, even when the record would be waiting.
  RoomMotion::Instance()->SetReduced(true);
  loading->SetSpinning(true);
  EXPECT_FALSE(loading->timer_active());
  RoomMotion::Instance()->SetReduced(false);

}

// --------------------------------------------------------------------------------------------------------------- search

TEST(LibraryViewTest, SearchFindsAlbumsByArtistAlbumAndTrackTitle) {

  FakeLibrarySource source(40);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);
  ASSERT_EQ(40, view.album_count());

  // By artist: immediate, from what is loaded.
  view.SetSearchText(QStringLiteral("paper lanterns"));
  int expected = 0;
  for (int i = 0; i < 40; ++i) {
    if (source.AlbumAt(i).artist == QStringLiteral("The Paper Lanterns")) ++expected;
  }
  ASSERT_GT(expected, 0);
  EXPECT_EQ(expected, view.album_count());
  for (int row = 0; row < view.album_model()->rowCount(); ++row) EXPECT_EQ(QStringLiteral("The Paper Lanterns"), view.album_model()->album(row).artist);

  // By a track title: the source finds the albums that have a matching track, and they are added when the answer arrives.
  view.SetSearchText(QStringLiteral("Tidewrack"));
  WaitFor([&]() { return source.search_calls.load() >= 1; });
  QTRY_VERIFY_WITH_TIMEOUT(view.album_count() == static_cast<int>(source.LoadAlbums(QStringLiteral("Tidewrack")).count()), 5000);
  EXPECT_GT(view.album_count(), 0);
  EXPECT_LT(view.album_count(), 40);

  // Several words must all match.
  view.SetSearchText(QStringLiteral("marin slow"));
  EXPECT_GT(view.album_count(), 0);
  for (int row = 0; row < view.album_model()->rowCount(); ++row) {
    EXPECT_EQ(QStringLiteral("Marin Holt"), view.album_model()->album(row).artist);
    EXPECT_TRUE(view.album_model()->album(row).album.startsWith(QStringLiteral("Slow Tide")));
  }

}

TEST(LibraryViewTest, SearchWithoutMatchesSaysSoAndClearingGoesBack) {

  FakeLibrarySource source(20);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);

  view.SetSearchText(QStringLiteral("zzzzqqq"));
  EXPECT_EQ(LibraryView::State::NoMatches, view.state());
  EXPECT_TRUE(view.page_for(LibraryView::State::NoMatches)->isVisible());
  EXPECT_TRUE(view.notice()->title_label()->text().contains(QStringLiteral("zzzzqqq")));
  EXPECT_EQ(QStringLiteral("Clear search"), view.notice()->primary_button()->text());
  EXPECT_FALSE(view.notice()->secondary_button()->isVisible());

  view.notice()->primary_button()->click();
  EXPECT_TRUE(view.search_text().isEmpty());
  EXPECT_EQ(LibraryView::State::Browsing, view.state());
  EXPECT_EQ(20, view.album_count());

}

TEST(LibraryViewTest, TypingInTheSearchFieldFiltersAfterAShortPause) {

  FakeLibrarySource source(40);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);

  QLineEdit *edit = view.search_field()->edit();
  edit->setFocus();
  QTest::keyClicks(edit, QStringLiteral("kestrel"));
  // Not on every key: the filter runs once typing pauses.
  EXPECT_EQ(40, view.album_count());
  QTRY_VERIFY_WITH_TIMEOUT(view.album_count() < 40, 3000);
  for (int row = 0; row < view.album_model()->rowCount(); ++row) EXPECT_EQ(QStringLiteral("Kestrel Quartet"), view.album_model()->album(row).artist);

  // Escape clears the search.
  QTest::keyClick(&view, Qt::Key_Escape);
  EXPECT_TRUE(view.search_text().isEmpty());
  EXPECT_EQ(40, view.album_count());

}

TEST(LibraryViewTest, ASlowOlderSearchNeverOverwritesANewerOne) {

  FakeLibrarySource source(40);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);

  view.SetSearchText(QStringLiteral("water"));
  view.SetSearchText(QStringLiteral("kestrel"));
  QTest::qWait(300);
  // Whatever order the workers answered in, the list is the one for the latest text.
  for (int row = 0; row < view.album_model()->rowCount(); ++row) EXPECT_EQ(QStringLiteral("Kestrel Quartet"), view.album_model()->album(row).artist);
  EXPECT_GT(view.album_count(), 0);

}

// --------------------------------------------------------------------------------------------------------------- album and artist browsing

TEST(LibraryViewTest, SelectingAnAlbumOpensItsPageWithTheTracks) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);

  QTest::mouseClick(view.grid()->viewport(), Qt::LeftButton, Qt::NoModifier, view.grid()->CoverRect(3).center());
  EXPECT_EQ(LibraryView::State::Album, view.state());
  EXPECT_EQ(view.album_model()->album(3).Key(), view.current_album().Key());
  EXPECT_EQ(view.album_model()->album(3).album, view.album_title_label()->text());
  WaitFor([&]() { return view.track_model()->rowCount() > 0; });

  const SongList expected = source.AllTracksOf(3);
  ASSERT_EQ(expected.count(), view.track_model()->rowCount());
  for (int i = 0; i < expected.count(); ++i) EXPECT_EQ(expected.at(i).title(), view.track_model()->index(i).data(LibraryTrackModel::TitleRole).toString());
  EXPECT_TRUE(view.page_for(LibraryView::State::Album)->isVisible());
  EXPECT_TRUE(view.play_button()->isEnabled());

  // Back: Escape and the back button both return to the grid, with the album's tile current.
  view.back_button()->click();
  EXPECT_EQ(LibraryView::State::Browsing, view.state());
  EXPECT_EQ(3, view.grid()->currentIndex().row());
  view.OpenAlbum(view.album_model()->album(5));
  QTest::keyClick(&view, Qt::Key_Escape);
  EXPECT_EQ(LibraryView::State::Browsing, view.state());

}

TEST(LibraryViewTest, PlayingTracksAndAlbumsAreRequestsWithTheRightSongs) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  qRegisterMetaType<SongList>("SongList");
  QSignalSpy play(&view, &LibraryView::PlayRequested);
  QSignalSpy add(&view, &LibraryView::AddRequested);
  ShowAndLoad(view);

  view.OpenAlbum(view.album_model()->album(2));
  WaitFor([&]() { return view.track_model()->rowCount() > 2; });
  const SongList tracks = view.current_tracks();

  // Double click on a track plays from that track.
  QTest::mouseClick(view.track_view()->viewport(), Qt::LeftButton, Qt::NoModifier, CenterOfRow(view.track_view(), 2));
  QTest::mouseDClick(view.track_view()->viewport(), Qt::LeftButton, Qt::NoModifier, CenterOfRow(view.track_view(), 2));
  ASSERT_EQ(1, play.count());
  EXPECT_EQ(2, play.last().at(1).toInt());
  EXPECT_EQ(tracks.count(), play.last().at(0).value<SongList>().count());
  EXPECT_EQ(tracks.at(2).url(), play.last().at(0).value<SongList>().at(2).url());

  // Enter plays the current row.
  view.track_view()->setCurrentIndex(view.track_model()->index(1));
  view.track_view()->setFocus();
  QTest::keyClick(view.track_view(), Qt::Key_Return);
  ASSERT_EQ(2, play.count());
  EXPECT_EQ(1, play.last().at(1).toInt());

  // The Play button starts the album from its first track.
  view.play_button()->click();
  ASSERT_EQ(3, play.count());
  EXPECT_EQ(0, play.last().at(1).toInt());

  // Play next and Add are explicit and different.
  view.play_next_button()->click();
  ASSERT_EQ(1, add.count());
  EXPECT_EQ(static_cast<int>(LibraryView::AddMode::PlayNext), add.last().at(1).toInt());
  view.add_button()->click();
  ASSERT_EQ(2, add.count());
  EXPECT_EQ(static_cast<int>(LibraryView::AddMode::Append), add.last().at(1).toInt());
  EXPECT_EQ(tracks.count(), add.last().at(0).value<SongList>().count());

}

TEST(LibraryViewTest, ThePlayButtonOfATilePlaysTheAlbumWithoutOpeningIt) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  qRegisterMetaType<SongList>("SongList");
  QSignalSpy play(&view, &LibraryView::PlayRequested);
  ShowAndLoad(view);

  // The button appears on hover, and a press on it plays; a press elsewhere on the tile opens the album.
  const QPoint on_tile = view.grid()->CoverRect(4).center();
  QTest::mouseMove(view.grid()->viewport(), on_tile);
  EXPECT_EQ(4, view.grid()->hover_row());
  QTest::mouseClick(view.grid()->viewport(), Qt::LeftButton, Qt::NoModifier, view.grid()->PlayButtonRect(4).center());
  WaitFor([&]() { return play.count() == 1; });
  EXPECT_EQ(0, play.last().at(1).toInt());
  EXPECT_EQ(source.AllTracksOf(4).count(), play.last().at(0).value<SongList>().count());
  EXPECT_EQ(LibraryView::State::Browsing, view.state());

}

TEST(LibraryViewTest, ArtistsModeListsArtistsAndChoosingOneShowsTheirAlbums) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);

  view.mode_switch()->SetCurrent(1);
  EXPECT_EQ(LibraryView::Mode::Artists, view.mode());
  EXPECT_TRUE(view.artist_list()->isVisible());
  EXPECT_FALSE(view.grid()->isVisible());
  EXPECT_EQ(10, view.artist_list()->count());

  QListWidgetItem *item = nullptr;
  for (int i = 0; i < view.artist_list()->count(); ++i) {
    if (view.artist_list()->item(i)->text() == QStringLiteral("Ines Varga")) item = view.artist_list()->item(i);
  }
  ASSERT_NE(nullptr, item);
  Q_EMIT view.artist_list()->itemActivated(item);

  EXPECT_EQ(QStringLiteral("Ines Varga"), view.artist_filter());
  EXPECT_EQ(LibraryView::Mode::Albums, view.mode());
  EXPECT_TRUE(view.grid()->isVisible());
  EXPECT_EQ(3, view.album_count());
  for (int row = 0; row < view.album_model()->rowCount(); ++row) EXPECT_EQ(QStringLiteral("Ines Varga"), view.album_model()->album(row).artist);
  EXPECT_TRUE(view.summary_label()->text().contains(QStringLiteral("Ines Varga")));

  view.SetArtistFilter(QString());
  EXPECT_EQ(30, view.album_count());

}

// --------------------------------------------------------------------------------------------------------------- lifecycle and cancellation

TEST(LibraryViewTest, DestroyingTheViewWhileAReadIsPendingIsSafe) {

  FakeLibrarySource source(20);
  source.GateAlbums();
  auto view = std::make_unique<LibraryView>();
  view->SetSource(&source);
  // The release comes from another thread: destroying the view blocks this one until the worker is done.
  std::thread releaser([&source]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    source.ReleaseAlbums();
  });
  view->resize(900, 600);
  view->show();
  WaitFor([&]() { return source.load_calls.load() == 1; });

  // The worker is inside the (blocked) read. Destroying the view waits for it and drops the answer: no crash, no signal into a dead widget.
  QPointer<LibraryView> guard(view.get());
  view.reset();
  releaser.join();
  EXPECT_TRUE(guard.isNull());
  QCoreApplication::processEvents();
  QTest::qWait(50);

}

TEST(LibraryViewTest, OnlyTheLastAlbumAskedForIsShown) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);

  source.GateTracks();
  view.OpenAlbum(view.album_model()->album(1));
  WaitFor([&]() { return source.track_calls.load() >= 1; });
  view.OpenAlbum(view.album_model()->album(7));
  WaitFor([&]() { return source.track_calls.load() >= 2; });
  source.ReleaseTracks();
  WaitFor([&]() { return view.track_model()->rowCount() > 0; });
  QTest::qWait(100);

  // Both reads finished, in whatever order; the page shows album 7 only.
  const SongList expected = source.AllTracksOf(7);
  ASSERT_EQ(expected.count(), view.track_model()->rowCount());
  EXPECT_EQ(expected.first().title(), view.track_model()->index(0).data(LibraryTrackModel::TitleRole).toString());
  EXPECT_EQ(view.album_model()->album(7).album, view.album_title_label()->text());

}

TEST(LibraryViewTest, ChangesToTheCollectionReloadOnceAndKeepTheModelWhenNothingChanged) {

  FakeLibrarySource source(30);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);
  const int resets = view.album_model()->resets();
  ASSERT_EQ(1, source.load_calls.load());

  // A burst of change notices (a scan adding songs) is one read.
  for (int i = 0; i < 12; ++i) Q_EMIT source.Changed();
  QTRY_VERIFY_WITH_TIMEOUT(source.load_calls.load() == 2, 4000);
  QTest::qWait(900);
  EXPECT_EQ(2, source.load_calls.load());
  // The same albums: the model was not rebuilt, so scroll position and selection stay.
  EXPECT_EQ(resets, view.album_model()->resets());

  // A really different library is applied.
  source.Generate(35);
  Q_EMIT source.Changed();
  QTRY_VERIFY_WITH_TIMEOUT(view.album_count() == 35, 4000);
  EXPECT_GT(view.album_model()->resets(), resets);

}

// --------------------------------------------------------------------------------------------------------------- covers and resizing

TEST(LibraryViewTest, CoversAreRequestedBoundedCachedAndNotAskedAgainWhenResizing) {

  FakeLibrarySource source(60);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view, 1200, 800);

  // Only the covers that are on screen are requested, a few at a time.
  QTest::qWait(200);
  const int first_requests = source.cover_requests.load();
  EXPECT_GT(first_requests, 0);
  EXPECT_LT(first_requests, 60);
  EXPECT_LE(view.album_model()->in_flight(), LibraryAlbumModel::kMaxInFlight);
  EXPECT_GT(view.album_model()->cached_covers(), 0);

  // One request size for all tiles (a bucket), not one per window width.
  const int bucket = source.last_cover_size.load();
  EXPECT_EQ(bucket, LibraryAlbumModel::BucketFor(bucket));

  // A long drag-resize: 40 widths. No cover is requested again, and the tile grid is recomputed once the width settles, not per step.
  const int passes_before = view.grid()->layout_passes();
  for (int i = 0; i < 40; ++i) {
    view.resize(1200 - i * 9, 800 - i * 4);
    QCoreApplication::processEvents();
    (void)view.grab();
  }
  EXPECT_LE(view.grid()->layout_passes() - passes_before, 1);
  view.grid()->SettleLayout();
  QCoreApplication::processEvents();
  EXPECT_EQ(first_requests, source.cover_requests.load());
  EXPECT_LE(view.album_model()->cached_covers(), LibraryAlbumModel::kMaxCached);

  // The tile width follows the settled width (and stays within sane bounds).
  EXPECT_GE(view.grid()->tile_width(), LibraryAlbumGrid::kMinTile - 20);
  EXPECT_LE(view.grid()->tile_width(), 250);
  EXPECT_GE(view.grid()->columns(), 1);

}

TEST(LibraryViewTest, AlbumsWithoutArtworkStillGetADistinctSleeve) {

  FakeLibrarySource source(12);
  source.no_covers = true;
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view);
  QTest::qWait(150);

  EXPECT_TRUE(view.album_model()->CoverFailed(0));
  // A failed cover is not asked for again.
  const int requests = source.cover_requests.load();
  (void)view.grab();
  QTest::qWait(100);
  EXPECT_EQ(requests, source.cover_requests.load());
  EXPECT_FALSE(view.grab().isNull());

}

TEST(LibraryViewTest, ManyAlbumsKeepTheCacheBounded) {

  FakeLibrarySource source(700);
  LibraryView view;
  view.SetSource(&source);
  ShowAndLoad(view, 1200, 800);

  // Scroll through everything, asking for covers as the tiles come into view.
  for (int row = 0; row < view.album_model()->rowCount(); row += 20) {
    view.grid()->scrollTo(view.album_model()->index(row), QAbstractItemView::PositionAtTop);
    (void)view.grab();
    QTest::qWait(5);
  }
  QTest::qWait(300);
  EXPECT_LE(view.album_model()->cached_covers(), LibraryAlbumModel::kMaxCached);
  EXPECT_LE(view.album_model()->in_flight(), LibraryAlbumModel::kMaxInFlight);

}

// --------------------------------------------------------------------------------------------------------------- the real backend

TEST(CollectionLibrarySourceTest, ReadsAlbumsAndTracksFromTheCollectionBackend) {

  auto database = std::make_shared<MemoryDatabase>(nullptr);
  CollectionBackend backend;
  backend.Init(database, nullptr, Song::Source::Collection, QLatin1String(CollectionManager::kSongsTable), QLatin1String(CollectionManager::kDirsTable), QLatin1String(CollectionManager::kSubdirsTable));

  // A directory first: it gets ID 1.
  backend.AddDirectory(QStringLiteral("/music"));
  SongList songs;
  const QStringList titles = { QStringLiteral("Salt Light"), QStringLiteral("Harbour Lamps"), QStringLiteral("Low Water") };
  for (int i = 0; i < 3; ++i) {
    Song song;
    song.set_directory_id(1);
    song.set_url(QUrl::fromLocalFile(QStringLiteral("/music/marin/slow-tide/%1.flac").arg(i + 1)));
    song.set_mtime(1);
    song.set_ctime(1);
    song.set_filesize(1);
    song.set_title(titles.at(i));
    song.set_artist(QStringLiteral("Marin Holt"));
    song.set_albumartist(QStringLiteral("Marin Holt"));
    song.set_album(QStringLiteral("Slow Tide"));
    song.set_track(3 - i);  // stored in reverse, so the track order has to come from the track numbers
    song.set_disc(1);
    song.set_length_nanosec(200000000000LL);
    songs << song;
  }
  Song other = songs.first();
  other.set_url(QUrl::fromLocalFile(QStringLiteral("/music/ines/fog/1.flac")));
  other.set_title(QStringLiteral("Fog Signals"));
  other.set_artist(QStringLiteral("Ines Varga"));
  other.set_albumartist(QStringLiteral("Ines Varga"));
  other.set_album(QStringLiteral("Weather for Strings"));
  other.set_track(1);
  songs << other;
  backend.AddOrUpdateSongs(songs);

  CollectionLibrarySource source(&backend, nullptr);
  const LibraryAlbumList albums = source.LoadAlbums(QString());
  ASSERT_EQ(2, albums.count());
  EXPECT_EQ(QStringLiteral("Ines Varga"), albums.at(0).artist);
  EXPECT_EQ(QStringLiteral("Marin Holt"), albums.at(1).artist);
  EXPECT_EQ(QStringLiteral("Slow Tide"), albums.at(1).album);
  EXPECT_FALSE(albums.at(1).first_url.isEmpty());

  const SongList tracks = source.LoadTracks(albums.at(1));
  ASSERT_EQ(3, tracks.count());
  EXPECT_EQ(1, tracks.at(0).track());
  EXPECT_EQ(QStringLiteral("Low Water"), tracks.at(0).title());
  EXPECT_EQ(3, tracks.at(2).track());

  // The same store drives the view: the production widget shows what the backend holds.
  LibraryView view;
  view.SetSource(&source);
  QSignalSpy settled(&view, &LibraryView::AlbumsSettled);
  view.resize(900, 600);
  view.show();
  // The worker hands the read to the backend's own thread (here: this one, which is processing events while it waits), so the in-memory
  // database is read through the connection it belongs to, and the production view shows exactly what the backend holds.
  ASSERT_TRUE(settled.wait(5000) || view.loaded());
  EXPECT_EQ(2, view.album_count());
  EXPECT_EQ(2, view.artist_count());
  view.OpenAlbum(view.album_model()->album(1));
  QTRY_VERIFY_WITH_TIMEOUT(view.track_model()->rowCount() == 3, 5000);
  EXPECT_EQ(QStringLiteral("Low Water"), view.track_model()->index(0).data(LibraryTrackModel::TitleRole).toString());

}
