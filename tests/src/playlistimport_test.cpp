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

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include "streaming/playlistimport.h"

using namespace PlaylistImport;

namespace {

QJsonObject Json(const char *text) {
  return QJsonDocument::fromJson(QByteArray(text)).object();
}

}  // namespace

TEST(PlaylistImportTest, ParseTidalPlaylistList) {

  const Page<PlaylistInfo> page = ParsePlaylistList(Service::Tidal, Json(R"({
    "limit": 50, "offset": 0, "totalNumberOfItems": 2,
    "items": [
      { "uuid": "aaaa-1", "title": "Night Drive", "numberOfTracks": 12 },
      { "title": "No id, skipped" },
      { "uuid": "bbbb-2", "title": "Focus", "numberOfTracks": 40 }
    ]})"));
  ASSERT_TRUE(page.valid);
  EXPECT_EQ(2, page.total);
  ASSERT_EQ(2, page.items.size());
  EXPECT_EQ(QStringLiteral("aaaa-1"), page.items[0].id);
  EXPECT_EQ(QStringLiteral("Night Drive"), page.items[0].name);
  EXPECT_EQ(12, page.items[0].track_count);
  EXPECT_EQ(QStringLiteral("Focus"), page.items[1].name);

}

TEST(PlaylistImportTest, ParseQobuzPlaylistList) {

  const Page<PlaylistInfo> page = ParsePlaylistList(Service::Qobuz, Json(R"({
    "playlists": { "offset": 0, "limit": 50, "total": 1,
      "items": [ { "id": 123456, "name": "Late Jazz", "tracks_count": 9 } ] } })"));
  ASSERT_TRUE(page.valid);
  EXPECT_EQ(1, page.total);
  ASSERT_EQ(1, page.items.size());
  EXPECT_EQ(QStringLiteral("123456"), page.items[0].id);
  EXPECT_EQ(QStringLiteral("Late Jazz"), page.items[0].name);
  EXPECT_EQ(9, page.items[0].track_count);

}

TEST(PlaylistImportTest, ParseTidalTracksSkipsVideosAndMarksUnavailable) {

  const Page<Track> page = ParsePlaylistTracks(Service::Tidal, Json(R"({
    "offset": 0, "totalNumberOfItems": 4,
    "items": [
      { "type": "track", "item": { "id": 1, "title": "One", "duration": 200, "artist": { "name": "A" }, "album": { "title": "X" } } },
      { "type": "video", "item": { "id": 2, "title": "Clip" } },
      { "type": "track", "item": { "id": 3, "title": "Gone", "allowStreaming": false } },
      { "type": "track", "item": { "title": "No id" } }
    ]})"));
  ASSERT_TRUE(page.valid);
  EXPECT_EQ(4, page.total);
  ASSERT_EQ(2, page.items.size());
  EXPECT_EQ(QStringLiteral("1"), page.items[0].id);
  EXPECT_EQ(QStringLiteral("A"), page.items[0].artist);
  EXPECT_EQ(QStringLiteral("X"), page.items[0].album);
  EXPECT_EQ(200, page.items[0].duration_sec);
  EXPECT_TRUE(page.items[0].available);
  EXPECT_FALSE(page.items[1].available);

}

TEST(PlaylistImportTest, ParseQobuzTracks) {

  const Page<Track> page = ParsePlaylistTracks(Service::Qobuz, Json(R"({
    "id": 5, "tracks": { "offset": 0, "total": 2, "items": [
      { "id": 77, "title": "Alpha", "duration": 301, "performer": { "name": "P" }, "album": { "title": "Q" }, "streamable": true },
      { "id": 78, "title": "Beta", "streamable": false, "album": { "artist": { "name": "Fallback" } } }
    ] } })"));
  ASSERT_TRUE(page.valid);
  ASSERT_EQ(2, page.items.size());
  EXPECT_EQ(QStringLiteral("P"), page.items[0].artist);
  EXPECT_EQ(QStringLiteral("Fallback"), page.items[1].artist);
  EXPECT_FALSE(page.items[1].available);

}

TEST(PlaylistImportTest, MalformedResponsesAreInvalidNotCrashes) {

  EXPECT_FALSE(ParsePlaylistList(Service::Tidal, QJsonObject()).valid);
  EXPECT_FALSE(ParsePlaylistList(Service::Qobuz, Json(R"({"playlists": "nope"})")).valid);
  EXPECT_FALSE(ParsePlaylistTracks(Service::Tidal, Json(R"({"items": 5})")).valid);
  EXPECT_FALSE(ParsePlaylistTracks(Service::Qobuz, Json(R"({"tracks": []})")).valid);

  // A page with a missing total is still usable.
  const Page<Track> page = ParsePlaylistTracks(Service::Tidal, Json(R"({"items": []})"));
  EXPECT_TRUE(page.valid);
  EXPECT_EQ(-1, page.total);

}

TEST(PlaylistImportTest, PaginatorWalksAllPages) {

  Paginator paginator(100);
  EXPECT_EQ(0, paginator.offset());
  EXPECT_TRUE(paginator.Advance(100, 250));
  EXPECT_EQ(100, paginator.offset());
  EXPECT_TRUE(paginator.Advance(100, 250));
  EXPECT_EQ(200, paginator.offset());
  EXPECT_FALSE(paginator.Advance(50, 250));
  EXPECT_TRUE(paginator.finished());
  EXPECT_FALSE(paginator.truncated());

  // Finished paginators stay finished.
  EXPECT_FALSE(paginator.Advance(100, 1000));

}

TEST(PlaylistImportTest, PaginatorStopsOnEmptyPageWithoutTotal) {

  Paginator paginator(50);
  EXPECT_TRUE(paginator.Advance(50, -1));
  EXPECT_TRUE(paginator.Advance(10, -1));
  EXPECT_FALSE(paginator.Advance(0, -1));
  EXPECT_TRUE(paginator.finished());

}

TEST(PlaylistImportTest, PaginatorTerminatesWhenServerLiesAboutTotal) {

  // The server claims a huge total but keeps returning one item: we must still stop.
  Paginator paginator(100);
  int pages = 0;
  while (paginator.Advance(1, 1000000)) {
    ++pages;
    ASSERT_LT(pages, Paginator::kMaxPages + 1);
  }
  EXPECT_TRUE(paginator.truncated());
  EXPECT_TRUE(paginator.finished());

}

TEST(PlaylistImportTest, PaginatorHandlesShortPageUsingActualCount) {

  // The offset advances by what was actually received, not by the requested page size.
  Paginator paginator(100);
  EXPECT_TRUE(paginator.Advance(40, 100));
  EXPECT_EQ(40, paginator.offset());

}

TEST(PlaylistImportTest, MergeAppendsOnlyNewTracks) {

  QList<Track> remote;
  for (const char *id : { "1", "2", "3" }) {
    Track track;
    track.id = QString::fromLatin1(id);
    remote << track;
  }

  MergeResult first = MergeNewTracks(QSet<QString>(), remote);
  EXPECT_EQ(3, first.to_append.size());

  // Refresh with identical remote data appends nothing.
  QSet<QString> local;
  for (const Track &track : first.to_append) local.insert(track.id);
  MergeResult second = MergeNewTracks(local, remote);
  EXPECT_TRUE(second.to_append.isEmpty());
  EXPECT_EQ(3, second.duplicates_skipped);

  // Remote gains one track: only that one is appended, order preserved.
  Track extra;
  extra.id = QStringLiteral("4");
  remote << extra;
  MergeResult third = MergeNewTracks(local, remote);
  ASSERT_EQ(1, third.to_append.size());
  EXPECT_EQ(QStringLiteral("4"), third.to_append[0].id);

}

TEST(PlaylistImportTest, MergeDedupesWithinRemoteAndSkipsUnavailable) {

  Track a;
  a.id = QStringLiteral("1");
  Track gone;
  gone.id = QStringLiteral("2");
  gone.available = false;
  Track no_id;

  const MergeResult result = MergeNewTracks(QSet<QString>(), { a, a, gone, no_id });
  ASSERT_EQ(1, result.to_append.size());
  EXPECT_EQ(1, result.duplicates_skipped);
  EXPECT_EQ(1, result.unavailable_skipped);

}
