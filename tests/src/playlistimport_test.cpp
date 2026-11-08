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

#include <algorithm>
#include <functional>

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QList>
#include <QListWidget>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QLabel>

#include "core/song.h"
#include "streaming/playlistimport.h"

using namespace PlaylistImport;

namespace {

QJsonObject Json(const char *text) {
  return QJsonDocument::fromJson(QByteArray(text)).object();
}

// A scripted source: every request is answered by a function of (kind, playlist id, offset, limit).
// Replies are delivered from the event loop, like real network replies. After Cancel() any pending reply is dropped, but a test can
// force a stale delivery to prove the importer ignores it.
class FakeSource : public PageSource {
 public:
  using Responder = std::function<PageResult(const bool tracks, const QString &playlist_id, const int offset, const int limit)>;

  explicit FakeSource(const Service service = Service::Tidal) : service_(service) {}

  Service service() const override { return service_; }

  void RequestPlaylists(const int offset, const int limit) override { Enqueue(false, QString(), offset, limit); }
  void RequestTracks(const QString &playlist_id, const int offset, const int limit) override { Enqueue(true, playlist_id, offset, limit); }
  void Cancel() override {
    ++cancel_calls;
    ++generation_;
  }

  // Delivers a reply regardless of cancellation, to simulate a late network reply.
  void DeliverStale(const PageResult &result) { Q_EMIT PageReady(result); }

  Responder responder;
  QList<int> requested_offsets;
  QList<int> requested_limits;
  int cancel_calls = 0;
  bool hold = false;  // when true, requests are never answered (still in flight)

 private:
  void Enqueue(const bool tracks, const QString &playlist_id, const int offset, const int limit) {
    requested_offsets << offset;
    requested_limits << limit;
    if (hold) return;
    const quint64 generation = generation_;
    QMetaObject::invokeMethod(this, [this, tracks, playlist_id, offset, limit, generation]() {
      if (generation != generation_) return;
      Q_EMIT PageReady(responder(tracks, playlist_id, offset, limit));
    }, Qt::QueuedConnection);
  }

  Service service_;
  quint64 generation_ = 0;
};

PageResult Ok(const QJsonObject &json) {
  PageResult result;
  result.ok = true;
  result.http_status = 200;
  result.json = json;
  return result;
}

PageResult HttpError(const int status, const QString &message = QString()) {
  PageResult result;
  result.ok = false;
  result.http_status = status;
  result.error = message;
  return result;
}

// Tidal-shaped track page
QJsonObject TidalTracksPage(const QList<int> &ids, const int offset, const int total, const int videos = 0, const bool unavailable_first = false) {
  QJsonArray items;
  bool first = true;
  for (const int id : ids) {
    QJsonObject item{ { QStringLiteral("id"), id }, { QStringLiteral("title"), QStringLiteral("T%1").arg(id) }, { QStringLiteral("duration"), 100 },
                      { QStringLiteral("artist"), QJsonObject{ { QStringLiteral("id"), 7 }, { QStringLiteral("name"), QStringLiteral("A") } } },
                      { QStringLiteral("album"), QJsonObject{ { QStringLiteral("id"), 8 }, { QStringLiteral("title"), QStringLiteral("B") } } } };
    if (unavailable_first && first) item.insert(QStringLiteral("allowStreaming"), false);
    first = false;
    items.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("track") }, { QStringLiteral("item"), item } });
  }
  for (int i = 0; i < videos; ++i) {
    items.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("video") }, { QStringLiteral("item"), QJsonObject{ { QStringLiteral("id"), 900000 + i } } } });
  }
  return QJsonObject{ { QStringLiteral("offset"), offset }, { QStringLiteral("totalNumberOfItems"), total }, { QStringLiteral("items"), items } };
}

QJsonObject TidalPlaylistsPage(const int first, const int count, const int offset, const int total) {
  QJsonArray items;
  for (int i = 0; i < count; ++i) {
    items.append(QJsonObject{ { QStringLiteral("uuid"), QStringLiteral("pl-%1").arg(first + i) }, { QStringLiteral("title"), QStringLiteral("List %1").arg(first + i) }, { QStringLiteral("numberOfTracks"), 3 } });
  }
  return QJsonObject{ { QStringLiteral("offset"), offset }, { QStringLiteral("totalNumberOfItems"), total }, { QStringLiteral("items"), items } };
}

template<typename Predicate>
bool WaitFor(Predicate predicate, const int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate()) {
    if (timer.elapsed() > timeout_ms) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return true;
}

PlaylistInfo Info(const QString &id = QStringLiteral("pl-1"), const int count = 0) {
  PlaylistInfo info;
  info.id = id;
  info.name = QStringLiteral("Mix");
  info.track_count = count;
  return info;
}

struct Captured {
  QList<QList<Track>> imports;
  QList<int> skipped;
  QStringList failures;
  QList<QList<PlaylistInfo>> playlists;
  int cancelled = 0;
};

void Connect(Importer &importer, Captured &captured) {
  QObject::connect(&importer, &Importer::PlaylistImported, &importer, [&captured](const PlaylistInfo &, const QList<Track> &tracks, const int skipped) {
    captured.imports << tracks;
    captured.skipped << skipped;
  });
  QObject::connect(&importer, &Importer::Failed, &importer, [&captured](const QString &message) { captured.failures << message; });
  QObject::connect(&importer, &Importer::PlaylistsLoaded, &importer, [&captured](const QList<PlaylistInfo> &playlists) { captured.playlists << playlists; });
  QObject::connect(&importer, &Importer::Cancelled, &importer, [&captured]() { ++captured.cancelled; });
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------- parsing

TEST(PlaylistImportParseTest, TidalPlaylistListAndWrappedEntries) {

  const Page<PlaylistInfo> page = ParsePlaylistList(Service::Tidal, Json(R"({
    "offset": 0, "totalNumberOfItems": 3,
    "items": [
      { "uuid": "aaaa-1", "title": "Night Drive", "numberOfTracks": 12 },
      { "title": "No id, skipped" },
      { "playlist": { "uuid": "bbbb-2", "title": "Focus", "numberOfTracks": 40 } }
    ]})"));
  ASSERT_TRUE(page.valid);
  EXPECT_EQ(3, page.total);
  EXPECT_EQ(3, page.raw_count);  // raw size, including the entry that was dropped
  ASSERT_EQ(2, page.items.size());
  EXPECT_EQ(QStringLiteral("Night Drive"), page.items[0].name);
  EXPECT_EQ(QStringLiteral("bbbb-2"), page.items[1].id);

}

TEST(PlaylistImportParseTest, QobuzListAndTracks) {

  const Page<PlaylistInfo> lists = ParsePlaylistList(Service::Qobuz, Json(R"({
    "playlists": { "offset": 0, "total": 1, "items": [ { "id": 123456, "name": "Late Jazz", "tracks_count": 9 } ] } })"));
  ASSERT_TRUE(lists.valid);
  ASSERT_EQ(1, lists.items.size());
  EXPECT_EQ(QStringLiteral("123456"), lists.items[0].id);

  const Page<Track> tracks = ParsePlaylistTracks(Service::Qobuz, Json(R"({
    "id": 5, "tracks": { "offset": 0, "total": 2, "items": [
      { "id": 77, "title": "Alpha", "duration": 301, "track_number": 3, "performer": { "name": "P" }, "album": { "title": "Q", "id": "abc", "image": { "large": "https://example.invalid/c.jpg" } }, "streamable": true },
      { "id": 78, "title": "Beta", "streamable": false, "album": { "artist": { "name": "Fallback" } } }
    ] } })"));
  ASSERT_TRUE(tracks.valid);
  EXPECT_EQ(2, tracks.raw_count);
  EXPECT_EQ(QStringLiteral("P"), tracks.items[0].artist);
  EXPECT_EQ(3, tracks.items[0].track);
  EXPECT_EQ(QStringLiteral("abc"), tracks.items[0].album_id);
  EXPECT_TRUE(tracks.items[0].cover_url.isValid());
  EXPECT_EQ(QStringLiteral("Fallback"), tracks.items[1].artist);
  EXPECT_FALSE(tracks.items[1].available);

}

TEST(PlaylistImportParseTest, TidalTracksSkipVideosButCountThemInRawSize) {

  const Page<Track> page = ParsePlaylistTracks(Service::Tidal, TidalTracksPage({ 1, 2, 3 }, 0, 10, 2, true));
  ASSERT_TRUE(page.valid);
  EXPECT_EQ(5, page.raw_count);  // 3 tracks + 2 videos
  ASSERT_EQ(3, page.items.size());
  EXPECT_FALSE(page.items[0].available);
  EXPECT_TRUE(page.items[1].available);
  EXPECT_EQ(QStringLiteral("T2"), page.items[1].title);
  EXPECT_EQ(7, page.items[1].artist_id.toInt());

}

TEST(PlaylistImportParseTest, MalformedResponsesAreInvalidNotCrashes) {

  EXPECT_FALSE(ParsePlaylistList(Service::Tidal, QJsonObject()).valid);
  EXPECT_FALSE(ParsePlaylistList(Service::Qobuz, Json(R"({"playlists": "nope"})")).valid);
  EXPECT_FALSE(ParsePlaylistTracks(Service::Tidal, Json(R"({"items": 5})")).valid);
  EXPECT_FALSE(ParsePlaylistTracks(Service::Qobuz, Json(R"({"tracks": []})")).valid);

  const Page<Track> page = ParsePlaylistTracks(Service::Tidal, Json(R"({"items": []})"));
  EXPECT_TRUE(page.valid);
  EXPECT_EQ(-1, page.total);

}

TEST(PlaylistImportParseTest, TrackToSong) {

  Track track;
  track.id = QStringLiteral("42");
  track.title = QStringLiteral("Song");
  track.artist = QStringLiteral("Artist");
  track.album = QStringLiteral("Album");
  track.duration_sec = 200;
  track.track = 5;
  const Song song = TrackToSong(track, Song::Source::Tidal, QStringLiteral("tidal"));
  EXPECT_EQ(Song::Source::Tidal, song.source());
  EXPECT_EQ(QStringLiteral("tidal:42"), song.url().toString());
  EXPECT_EQ(QStringLiteral("42"), song.song_id());
  EXPECT_EQ(200LL * 1000000000LL, song.length_nanosec());
  EXPECT_EQ(Song::FileType::Stream, song.filetype());

}

// ------------------------------------------------------------------------------------------------------------- pagination

TEST(PlaylistImportPaginatorTest, WalksPagesUsingRawCount) {

  Paginator paginator(100);
  EXPECT_TRUE(paginator.Advance(100, 250));
  EXPECT_EQ(100, paginator.offset());
  EXPECT_TRUE(paginator.Advance(100, 250));
  EXPECT_EQ(200, paginator.offset());
  EXPECT_FALSE(paginator.Advance(50, 250));
  EXPECT_TRUE(paginator.finished());
  EXPECT_FALSE(paginator.truncated());
  EXPECT_FALSE(paginator.Advance(100, 1000));  // stays finished

}

TEST(PlaylistImportPaginatorTest, StopsOnEmptyPageAndCapsRunawayServers) {

  Paginator open_ended(50);
  EXPECT_TRUE(open_ended.Advance(50, -1));
  EXPECT_FALSE(open_ended.Advance(0, -1));

  Paginator liar(100);
  int pages = 0;
  while (liar.Advance(1, 1000000)) {
    ASSERT_LT(++pages, Paginator::kMaxPages + 1);
  }
  EXPECT_TRUE(liar.truncated());

}

// ----------------------------------------------------------------------------------------------------------- the importer

class PlaylistImporterTest : public ::testing::Test {
 protected:
  PlaylistImporterTest() : importer(&source) { Connect(importer, captured); }

  FakeSource source;
  Importer importer;
  Captured captured;
};

TEST_F(PlaylistImporterTest, LoadsAllPlaylistPagesInOrder) {

  source.responder = [](const bool, const QString &, const int offset, const int limit) {
    EXPECT_EQ(Importer::kPageSize, limit);
    const int total = 250;
    const int count = std::min(limit, total - offset);
    return Ok(TidalPlaylistsPage(offset, count, offset, total));
  };

  importer.LoadPlaylists();
  EXPECT_TRUE(importer.busy());
  ASSERT_TRUE(WaitFor([this]() { return !captured.playlists.isEmpty() || !captured.failures.isEmpty(); }));

  ASSERT_EQ(1, captured.playlists.size());
  ASSERT_EQ(250, captured.playlists[0].size());
  EXPECT_EQ(QStringLiteral("pl-0"), captured.playlists[0].first().id);
  EXPECT_EQ(QStringLiteral("pl-249"), captured.playlists[0].last().id);
  EXPECT_EQ((QList<int>{ 0, 100, 200 }), source.requested_offsets);
  EXPECT_FALSE(importer.busy());

}

TEST_F(PlaylistImporterTest, PaginatesTracksByRawCountNotFilteredCount) {

  // Each page has 100 raw entries, of which 10 are videos that are filtered out. The next offset must be 100, not 90.
  source.responder = [](const bool tracks, const QString &id, const int offset, const int) {
    EXPECT_TRUE(tracks);
    EXPECT_EQ(QStringLiteral("pl-1"), id);
    QList<int> ids;
    const int total = 200;
    for (int i = 0; i < 90; ++i) ids << offset + i;
    return Ok(TidalTracksPage(ids, offset, total, 10));
  };

  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.imports.isEmpty() || !captured.failures.isEmpty(); }));

  ASSERT_TRUE(captured.failures.isEmpty()) << captured.failures.join(QLatin1Char(';')).toStdString();
  EXPECT_EQ((QList<int>{ 0, 100 }), source.requested_offsets);
  ASSERT_EQ(1, captured.imports.size());
  EXPECT_EQ(180, captured.imports[0].size());

}

TEST_F(PlaylistImporterTest, KeepsOrderAndIntentionalDuplicates) {

  // The same track 5 appears three times in one playlist: all of them are kept, in order.
  source.responder = [](const bool, const QString &, const int offset, const int) { return Ok(TidalTracksPage({ 5, 1, 5, 2, 5 }, offset, 5)); };

  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.imports.isEmpty() || !captured.failures.isEmpty(); }));

  ASSERT_EQ(1, captured.imports.size());
  QStringList ids;
  for (const Track &track : captured.imports[0]) ids << track.id;
  EXPECT_EQ(QStringLiteral("5,1,5,2,5"), ids.join(QLatin1Char(',')));

}

TEST_F(PlaylistImporterTest, UnavailableTracksAreLeftOutAndCounted) {

  source.responder = [](const bool, const QString &, const int, const int) { return Ok(TidalTracksPage({ 1, 2, 3 }, 0, 3, 0, true)); };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.imports.isEmpty(); }));
  EXPECT_EQ(2, captured.imports[0].size());
  EXPECT_EQ(1, captured.skipped[0]);

}

TEST_F(PlaylistImporterTest, ExpiredLoginMidwayFailsWithoutPartialImport) {

  int call = 0;
  source.responder = [&call](const bool, const QString &, const int offset, const int) {
    if (++call == 1) {
      QList<int> ids;
      for (int i = 0; i < 100; ++i) ids << offset + i;
      return Ok(TidalTracksPage(ids, offset, 250));
    }
    return HttpError(401);
  };

  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }));

  ASSERT_EQ(1, captured.failures.size());
  // Plain words, no status code: the code is in the technical details of the classified failure.
  EXPECT_FALSE(captured.failures[0].contains(QStringLiteral("401")));
  EXPECT_TRUE(captured.failures[0].contains(QStringLiteral("Reconnect")));
  EXPECT_TRUE(captured.imports.isEmpty());  // nothing from page 1 leaks out
  EXPECT_FALSE(importer.busy());

}

TEST_F(PlaylistImporterTest, ServerErrorsAndMalformedBodiesFailCleanly) {

  source.responder = [](const bool, const QString &, const int, const int) { return HttpError(503, QStringLiteral("Received HTTP code 503")); };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty(); }));
  EXPECT_TRUE(captured.failures[0].contains(QStringLiteral("problem on its side")));
  EXPECT_TRUE(captured.failures[0].contains(QStringLiteral("Nothing was imported")));
  EXPECT_FALSE(captured.failures[0].contains(QStringLiteral("503")));

  captured.failures.clear();
  source.responder = [](const bool, const QString &, const int, const int) { return Ok(Json(R"({"unexpected": true})")); };
  importer.LoadPlaylists();
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty(); }));
  EXPECT_TRUE(captured.failures[0].contains(QStringLiteral("unexpected")));
  EXPECT_TRUE(captured.playlists.isEmpty());

  // The importer is usable again after a failure.
  captured.failures.clear();
  source.responder = [](const bool, const QString &, const int offset, const int) { return Ok(TidalPlaylistsPage(0, 2, offset, 2)); };
  importer.LoadPlaylists();
  ASSERT_TRUE(WaitFor([this]() { return !captured.playlists.isEmpty(); }));
  EXPECT_EQ(2, captured.playlists[0].size());

}

TEST_F(PlaylistImporterTest, ShortResultBeforeReportedTotalIsAnError) {

  // Claims 150 entries, but the second page is empty: do not import a truncated list.
  source.responder = [](const bool, const QString &, const int offset, const int) {
    if (offset == 0) {
      QList<int> ids;
      for (int i = 0; i < 100; ++i) ids << i;
      return Ok(TidalTracksPage(ids, 0, 150));
    }
    return Ok(TidalTracksPage({}, offset, 150));
  };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }));
  ASSERT_EQ(1, captured.failures.size());
  EXPECT_TRUE(captured.imports.isEmpty());

}

TEST_F(PlaylistImporterTest, ChangingTotalMidwayIsAnError) {

  int call = 0;
  source.responder = [&call](const bool, const QString &, const int offset, const int) {
    QList<int> ids;
    for (int i = 0; i < 100; ++i) ids << offset + i;
    return Ok(TidalTracksPage(ids, offset, ++call == 1 ? 250 : 260));
  };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }));
  ASSERT_EQ(1, captured.failures.size());
  EXPECT_TRUE(captured.failures[0].contains(QStringLiteral("changed")));
  EXPECT_TRUE(captured.imports.isEmpty());

}

TEST_F(PlaylistImporterTest, PageCapFailsInsteadOfImportingPartialList) {

  // A server that always returns one entry and claims a huge total.
  source.responder = [](const bool, const QString &, const int offset, const int) { return Ok(TidalTracksPage({ offset + 1 }, offset, 100000000)); };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }, 20000));
  ASSERT_EQ(1, captured.failures.size());
  EXPECT_TRUE(captured.imports.isEmpty());
  EXPECT_EQ(Paginator::kMaxPages, source.requested_offsets.size());

}

TEST_F(PlaylistImporterTest, CancelStopsTheRequestAndIgnoresLateReplies) {

  source.hold = true;
  QList<QPair<int, int>> progress;
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(importer.busy());
  ASSERT_EQ(1, source.requested_offsets.size());

  importer.Cancel();
  EXPECT_FALSE(importer.busy());
  EXPECT_EQ(1, source.cancel_calls);
  EXPECT_EQ(1, captured.cancelled);

  // A reply that was already in flight arrives afterwards: it must not produce an import or a failure.
  source.DeliverStale(Ok(TidalTracksPage({ 1, 2, 3 }, 0, 3)));
  QCoreApplication::processEvents();
  EXPECT_TRUE(captured.imports.isEmpty());
  EXPECT_TRUE(captured.failures.isEmpty());

  // Cancel while idle does nothing.
  importer.Cancel();
  EXPECT_EQ(1, captured.cancelled);

}

TEST_F(PlaylistImporterTest, IgnoresRequestsWhileBusy) {

  source.hold = true;
  importer.LoadPlaylists();
  importer.LoadPlaylists();
  importer.ImportPlaylist(Info());
  EXPECT_EQ(1, source.requested_offsets.size());
  EXPECT_EQ(Importer::State::LoadingPlaylists, importer.state());

}

TEST_F(PlaylistImporterTest, RepeatedRefreshDeliversTheCompleteCurrentListEachTime) {

  QList<int> remote = { 1, 2, 3 };
  source.responder = [&remote](const bool, const QString &, const int offset, const int) { return Ok(TidalTracksPage(remote, offset, static_cast<int>(remote.size()))); };

  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return captured.imports.size() == 1; }));

  // Unchanged remote: the second refresh delivers the same complete list again (the caller replaces, so nothing is appended).
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return captured.imports.size() == 2; }));
  EXPECT_EQ(captured.imports[0].size(), captured.imports[1].size());

  // Remote changed (one removed, one added, order changed): the new list is delivered as-is.
  remote = { 3, 4, 1 };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return captured.imports.size() == 3; }));
  QStringList ids;
  for (const Track &track : captured.imports[2]) ids << track.id;
  EXPECT_EQ(QStringLiteral("3,4,1"), ids.join(QLatin1Char(',')));

}

TEST_F(PlaylistImporterTest, RepeatedPageWithWrongOffsetIsRejectedWithoutASnapshot) {

  // Raw counts add up to the total, but the second page claims to start at offset 0 again: a stale or repeated page.
  source.responder = [](const bool, const QString &, const int, const int) {
    QList<int> ids;
    for (int i = 0; i < 100; ++i) ids << i;
    return Ok(TidalTracksPage(ids, 0, 200));
  };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }));

  ASSERT_EQ(1, captured.failures.size());
  EXPECT_TRUE(captured.failures[0].contains(QStringLiteral("unexpected page")));
  EXPECT_TRUE(captured.imports.isEmpty());
  EXPECT_EQ((QList<int>{ 0, 100 }), source.requested_offsets);
  EXPECT_FALSE(importer.busy());

}

TEST_F(PlaylistImporterTest, RepeatedPlaylistListPageIsRejectedToo) {

  source.responder = [](const bool, const QString &, const int, const int) { return Ok(TidalPlaylistsPage(0, 100, 0, 200)); };
  importer.LoadPlaylists();
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.playlists.isEmpty(); }));
  ASSERT_EQ(1, captured.failures.size());
  EXPECT_TRUE(captured.playlists.isEmpty());

}

TEST_F(PlaylistImporterTest, PagesThatOmitTheOffsetUseTheRequestedOrder) {

  source.responder = [](const bool, const QString &, const int offset, const int) {
    QList<int> ids;
    const int total = 150;
    for (int i = offset; i < std::min(offset + 100, total); ++i) ids << i;
    QJsonObject page = TidalTracksPage(ids, offset, total);
    page.remove(QStringLiteral("offset"));
    return Ok(page);
  };
  importer.ImportPlaylist(Info());
  ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }));
  ASSERT_EQ(1, captured.imports.size());
  EXPECT_EQ(150, captured.imports[0].size());

}

TEST_F(PlaylistImporterTest, MalformedPaginationMetadataFailsCleanly) {

  const QList<QJsonObject> bad_pages = {
    Json(R"({"offset": "0", "totalNumberOfItems": 1, "items": []})"),
    Json(R"({"offset": -5, "totalNumberOfItems": 1, "items": []})"),
    Json(R"({"offset": 0.5, "totalNumberOfItems": 1, "items": []})"),
    Json(R"({"offset": 0, "totalNumberOfItems": "many", "items": []})"),
    Json(R"({"offset": 0, "totalNumberOfItems": -1, "items": []})"),
    Json(R"({"offset": null, "items": []})"),
    Json(R"({"offset": 0, "totalNumberOfItems": 3.7, "items": []})"),
  };
  for (const QJsonObject &bad : bad_pages) {
    EXPECT_FALSE(ParsePlaylistTracks(Service::Tidal, bad).valid) << QJsonDocument(bad).toJson().toStdString();
    EXPECT_FALSE(ParsePlaylistList(Service::Tidal, bad).valid) << QJsonDocument(bad).toJson().toStdString();

    captured = Captured();
    source.requested_offsets.clear();
    source.responder = [bad](const bool, const QString &, const int, const int) { return Ok(bad); };
    importer.ImportPlaylist(Info());
    ASSERT_TRUE(WaitFor([this]() { return !captured.failures.isEmpty() || !captured.imports.isEmpty(); }));
    EXPECT_EQ(1, captured.failures.size());
    EXPECT_TRUE(captured.imports.isEmpty());
  }

  // The same for the Qobuz envelope.
  EXPECT_FALSE(ParsePlaylistTracks(Service::Qobuz, Json(R"({"tracks": {"offset": "x", "total": 1, "items": []}})")).valid);
  EXPECT_FALSE(ParsePlaylistList(Service::Qobuz, Json(R"({"playlists": {"offset": 0, "total": -2, "items": []}})")).valid);

}
