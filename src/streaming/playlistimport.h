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

#include "config.h"

#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

#include "core/song.h"

// Read-only playlist import from streaming services.
//
// A remote playlist is read page by page and delivered as one complete, ordered list of tracks.
// The local playlist is a snapshot: refreshing replaces it with the latest complete remote list.
// Nothing here writes to the remote service and nothing is merged or "synced" in two directions.
//
// The importer talks to a PageSource, which hides the network. Tests use a scripted fake source; the app uses
// TidalPlaylistSource and QobuzPlaylistSource.
namespace PlaylistImport {

struct Track {
  QString id;
  QString title;
  QString artist;
  QString artist_id;
  QString album;
  QString album_id;
  QUrl cover_url;
  int track = 0;
  int disc = 0;
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
  int raw_count = 0;  // Number of entries the server returned, before any filtering. Pagination uses this, not items.size().
  int offset = 0;
  bool has_offset = false;  // true only if the response stated its offset explicitly
  int total = -1;           // -1 if the service did not report a total
  bool valid = false;
};

enum class Service {
  Tidal,
  Qobuz
};

Page<PlaylistInfo> ParsePlaylistList(const Service service, const QJsonObject &json);
Page<Track> ParsePlaylistTracks(const Service service, const QJsonObject &json);

// Builds a playable song for a track. `url_scheme` is the streaming service's URL scheme (e.g. from StreamingService::url_scheme()).
Song TrackToSong(const Track &track, const Song::Source source, const QString &url_scheme);

// Plans the next request of a paginated listing and guarantees termination.
// Stops when the reported total is reached, when a page is empty, or after kMaxPages pages.
// The offset advances by the number of entries actually received.
class Paginator {
 public:
  static constexpr int kMaxPages = 500;

  explicit Paginator(const int page_size = 100) : page_size_(page_size > 0 ? page_size : 100) {}

  int offset() const { return offset_; }
  int limit() const { return page_size_; }
  bool finished() const { return finished_; }
  bool truncated() const { return truncated_; }

  // Feed each page. Returns true if another page should be requested at offset().
  bool Advance(const int raw_items_in_page, const int reported_total);

 private:
  int page_size_;
  int offset_ = 0;
  int pages_ = 0;
  bool finished_ = false;
  bool truncated_ = false;
};

// Result of one request, as delivered by a PageSource. Never contains secrets.
struct PageResult {
  bool ok = false;
  QString error;           // Message from the request layer when !ok. May be technical; shown only under "Technical details".
  int http_status = 0;     // 0 if unknown
  int network_error = 0;   // QNetworkReply::NetworkError of the reply, 0 (NoError) if unknown
  int retry_after_sec = 0; // From a Retry-After header, 0 if absent
  bool not_signed_in = false;  // The source did not even send the request because there is no session
  QJsonObject json;
};

// Why a read failed, in the terms the user can act on.
enum class FailureKind {
  NotSignedIn,       // there is no session: sign in
  AuthExpired,       // the session was rejected (HTTP 401): reconnect
  PermissionDenied,  // the account is not allowed to read this (HTTP 403): permissions or subscription
  Offline,           // no connection to the service
  Timeout,
  RateLimited,       // HTTP 429
  ServerError,       // HTTP 5xx
  BadResponse,       // the reply could not be understood or is inconsistent
  Cancelled
};

struct Failure {
  FailureKind kind = FailureKind::BadResponse;
  QString title;        // short, plain text
  QString message;      // one or two human sentences, says what happened and that nothing was imported
  QString details;      // technical text (status codes, reply text) for the collapsible details
  int http_status = 0;
  int retry_after_sec = 0;
  bool retryable = false;  // Repeating the same read can reasonably help
  bool needs_signin = false;
};

// Turns a failed page into a Failure. `service_name` is used in the wording.
Failure ClassifyFailure(const PageResult &result, const QString &service_name);
// Failures that the importer itself detects (inconsistent paging and so on).
Failure BadResponseFailure(const QString &message, const QString &details = QString());

// Asynchronous page access. One request at a time. Cancel() must make sure no further PageReady is emitted for the cancelled request.
class PageSource : public QObject {
  Q_OBJECT

 public:
  explicit PageSource(QObject *parent = nullptr) : QObject(parent) {}

  virtual Service service() const = 0;
  virtual void RequestPlaylists(const int offset, const int limit) = 0;
  virtual void RequestTracks(const QString &playlist_id, const int offset, const int limit) = 0;
  virtual void Cancel() = 0;

 Q_SIGNALS:
  void PageReady(const PlaylistImport::PageResult &result);
};

// Drives a PageSource to read all playlists, or one complete playlist.
// A result is delivered only after every page was received and verified; any error, cancellation, page cap or inconsistent total
// discards everything read so far, so a partial list is never imported.
class Importer : public QObject {
  Q_OBJECT

 public:
  enum class State {
    Idle,
    LoadingPlaylists,
    LoadingTracks
  };

  static constexpr int kPageSize = 100;

  explicit Importer(PageSource *source, QObject *parent = nullptr);

  State state() const { return state_; }
  bool busy() const { return state_ != State::Idle; }

 public Q_SLOTS:
  // Both are ignored while busy.
  void LoadPlaylists();
  void ImportPlaylist(const PlaylistImport::PlaylistInfo &playlist);
  void Cancel();

 Q_SIGNALS:
  void StateChanged(const PlaylistImport::Importer::State state);
  // loaded entries (raw count) and the reported total, or -1 when unknown
  void Progress(const int loaded, const int total);
  void PlaylistsLoaded(const QList<PlaylistImport::PlaylistInfo> &playlists);
  // `skipped` is the number of entries left out because they are unavailable (not playable); order and duplicates of the others are kept.
  void PlaylistImported(const PlaylistImport::PlaylistInfo &playlist, const QList<PlaylistImport::Track> &tracks, const int skipped);
  // Emitted together with Failed(), carries the classification.
  void FailureOccurred(const PlaylistImport::Failure &failure);
  void Failed(const QString &message);
  void Cancelled();

 private Q_SLOTS:
  void PageReceived(const PlaylistImport::PageResult &result);

 private:
  void SetState(const State state);
  void Reset();
  void Fail(const QString &message);
  void Fail(const Failure &failure);
  void RequestNext();
  void Finish();

  PageSource *source_;
  State state_;
  Paginator paginator_;
  PlaylistInfo current_playlist_;
  QList<PlaylistInfo> playlists_;
  QList<Track> tracks_;
  int received_;
  int expected_total_;
};

}  // namespace PlaylistImport

Q_DECLARE_METATYPE(PlaylistImport::PageResult)
Q_DECLARE_METATYPE(PlaylistImport::Failure)
Q_DECLARE_METATYPE(PlaylistImport::PlaylistInfo)
Q_DECLARE_METATYPE(PlaylistImport::Track)
Q_DECLARE_METATYPE(PlaylistImport::Importer::State)

#endif  // PLAYLISTIMPORT_H
