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

#ifndef PLAYLISTIMPORT_H
#define PLAYLISTIMPORT_H

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

// Read-only playlist import helpers for streaming services.
//
// This is a one way import: remote playlists are read and local copies can be refreshed.
// Nothing here writes to the remote service, and there is no bidirectional sync.
// The functions are pure (JSON in, values out) so they can be tested without a network or credentials.
namespace PlaylistImport {

struct Track {
  QString id;
  QString title;
  QString artist;
  QString album;
  qint64 duration_sec = 0;
  bool available = true;
};

struct PlaylistInfo {
  QString id;
  QString name;
  int track_count = 0;
};

// One page of a paginated listing.
template<typename T>
struct Page {
  QList<T> items;
  int offset = 0;
  int total = -1;  // -1 if the service did not report a total
  bool valid = false;
};

enum class Service {
  Tidal,
  Qobuz
};

Page<PlaylistInfo> ParsePlaylistList(const Service service, const QJsonObject &json);
Page<Track> ParsePlaylistTracks(const Service service, const QJsonObject &json);

// Plans the next request of a paginated listing and guarantees termination.
// Stops when the reported total is reached, when a page is empty, or after kMaxPages pages,
// so a misbehaving server can never make the importer loop forever.
class Paginator {
 public:
  static constexpr int kMaxPages = 500;

  explicit Paginator(const int page_size = 100) : page_size_(page_size > 0 ? page_size : 100) {}

  int offset() const { return offset_; }
  int limit() const { return page_size_; }
  bool finished() const { return finished_; }
  bool truncated() const { return truncated_; }

  // Feed each parsed page. Returns true if another page should be requested at offset().
  bool Advance(const int items_in_page, const int reported_total);

 private:
  int page_size_;
  int offset_ = 0;
  int pages_ = 0;
  bool finished_ = false;
  bool truncated_ = false;
};

struct MergeResult {
  QList<Track> to_append;  // New tracks, in remote order, each id at most once
  int duplicates_skipped = 0;
  int unavailable_skipped = 0;
};

// Returns the tracks that are not already in the local playlist.
// Repeated refreshes with the same remote data append nothing.
// Duplicates inside the remote data itself are only added once.
MergeResult MergeNewTracks(const QSet<QString> &existing_ids, const QList<Track> &remote);

}  // namespace PlaylistImport

#endif  // PLAYLISTIMPORT_H
