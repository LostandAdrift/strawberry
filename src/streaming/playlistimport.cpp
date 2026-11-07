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
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUrl>

#include "core/song.h"
#include "playlistimport.h"

using namespace Qt::Literals::StringLiterals;

namespace PlaylistImport {

namespace {

constexpr qint64 kNsecPerSec = 1000000000LL;

QString Tr(const char *text) {
  return QCoreApplication::translate("PlaylistImport", text);
}

// JSON ids can be numbers or strings depending on the service.
QString IdToString(const QJsonValue &value) {

  if (value.isString()) return value.toString();
  if (value.isDouble()) return QString::number(static_cast<qint64>(value.toDouble()));
  return QString();

}

Track ParseTidalTrack(const QJsonObject &item) {

  Track track;
  track.id = IdToString(item["id"_L1]);
  track.title = item["title"_L1].toString();
  const QJsonObject artist = item["artist"_L1].toObject();
  track.artist = artist["name"_L1].toString();
  track.artist_id = IdToString(artist["id"_L1]);
  const QJsonObject album = item["album"_L1].toObject();
  track.album = album["title"_L1].toString();
  track.album_id = IdToString(album["id"_L1]);
  const QString cover = album["cover"_L1].toString().replace(u'-', u'/');
  if (!cover.isEmpty()) track.cover_url = QUrl(QStringLiteral("https://resources.tidal.com/images/%1/320x320.jpg").arg(cover));
  track.track = item["trackNumber"_L1].toInt();
  track.disc = item["volumeNumber"_L1].toInt();
  track.duration_sec = static_cast<qint64>(item["duration"_L1].toDouble());
  track.available = item["allowStreaming"_L1].toBool(true) && item["streamReady"_L1].toBool(true);
  return track;

}

Track ParseQobuzTrack(const QJsonObject &item) {

  Track track;
  track.id = IdToString(item["id"_L1]);
  track.title = item["title"_L1].toString();
  const QJsonObject performer = item["performer"_L1].toObject();
  const QJsonObject album = item["album"_L1].toObject();
  const QJsonObject album_artist = album["artist"_L1].toObject();
  track.artist = performer["name"_L1].toString();
  track.artist_id = IdToString(performer["id"_L1]);
  if (track.artist.isEmpty()) {
    track.artist = album_artist["name"_L1].toString();
    track.artist_id = IdToString(album_artist["id"_L1]);
  }
  track.album = album["title"_L1].toString();
  track.album_id = IdToString(album["id"_L1]);
  const QString image = album["image"_L1].toObject()["large"_L1].toString();
  if (!image.isEmpty()) track.cover_url = QUrl(image);
  track.track = item["track_number"_L1].toInt();
  track.disc = item["media_number"_L1].toInt();
  track.duration_sec = static_cast<qint64>(item["duration"_L1].toDouble());
  track.available = item["streamable"_L1].toBool(true);
  return track;

}

template<typename T>
void ParseEnvelope(const QJsonObject &container, const QString &total_key, Page<T> &page) {

  page.valid = true;
  page.offset = container["offset"_L1].toInt();
  page.total = container.contains(total_key) ? container[total_key].toInt() : -1;
  page.raw_count = static_cast<int>(container["items"_L1].toArray().size());

}

}  // namespace

Page<PlaylistInfo> ParsePlaylistList(const Service service, const QJsonObject &json) {

  Page<PlaylistInfo> page;

  // Tidal: { totalNumberOfItems, offset, items: [ { uuid, title, numberOfTracks } ] } (entries may also be wrapped as { playlist: {...} })
  // Qobuz: { playlists: { total, offset, items: [ { id, name, tracks_count } ] } }
  const QJsonObject container = service == Service::Qobuz ? json["playlists"_L1].toObject() : json;
  if (!container["items"_L1].isArray()) return page;

  ParseEnvelope(container, service == Service::Qobuz ? u"total"_s : u"totalNumberOfItems"_s, page);

  const QJsonArray items = container["items"_L1].toArray();
  for (const QJsonValue &value : items) {
    QJsonObject object = value.toObject();
    if (object.contains("playlist"_L1)) object = object["playlist"_L1].toObject();
    PlaylistInfo info;
    if (service == Service::Qobuz) {
      info.id = IdToString(object["id"_L1]);
      info.name = object["name"_L1].toString();
      info.track_count = object["tracks_count"_L1].toInt();
    }
    else {
      info.id = object["uuid"_L1].toString();
      info.name = object["title"_L1].toString();
      info.track_count = object["numberOfTracks"_L1].toInt();
    }
    if (info.id.isEmpty()) continue;
    if (info.name.isEmpty()) info.name = info.id;
    page.items << info;
  }

  return page;

}

Page<Track> ParsePlaylistTracks(const Service service, const QJsonObject &json) {

  Page<Track> page;

  // Tidal: { totalNumberOfItems, offset, items: [ { type: "track", item: { id, title, ... } } ] }
  // Qobuz: { tracks: { total, offset, items: [ { id, title, performer, album } ] } }
  const QJsonObject container = service == Service::Qobuz ? json["tracks"_L1].toObject() : json;
  if (!container["items"_L1].isArray()) return page;

  ParseEnvelope(container, service == Service::Qobuz ? u"total"_s : u"totalNumberOfItems"_s, page);

  const QJsonArray items = container["items"_L1].toArray();
  for (const QJsonValue &value : items) {
    const QJsonObject object = value.toObject();
    Track track;
    if (service == Service::Qobuz) {
      track = ParseQobuzTrack(object);
    }
    else {
      // Skip videos and other non-track entries, which the player cannot play as audio. They still count towards the raw page size.
      const QString type = object["type"_L1].toString();
      if (!type.isEmpty() && type != "track"_L1) continue;
      track = ParseTidalTrack(object["item"_L1].toObject());
    }
    if (track.id.isEmpty()) continue;
    page.items << track;
  }

  return page;

}

Song TrackToSong(const Track &track, const Song::Source source, const QString &url_scheme) {

  Song song(source);
  QUrl url;
  url.setScheme(url_scheme);
  url.setPath(track.id);

  song.set_source(source);
  song.set_song_id(track.id);
  song.set_album_id(track.album_id);
  song.set_artist_id(track.artist_id);
  song.set_album(track.album);
  song.set_artist(track.artist);
  song.set_title(track.title);
  song.set_track(track.track);
  song.set_disc(track.disc);
  song.set_url(url);
  song.set_length_nanosec(track.duration_sec * kNsecPerSec);
  if (track.cover_url.isValid()) song.set_art_automatic(track.cover_url);
  song.set_directory_id(0);
  song.set_filetype(Song::FileType::Stream);
  song.set_filesize(0);
  song.set_mtime(0);
  song.set_ctime(0);
  song.set_valid(true);
  return song;

}

bool Paginator::Advance(const int raw_items_in_page, const int reported_total) {

  if (finished_) return false;

  ++pages_;
  offset_ += raw_items_in_page > 0 ? raw_items_in_page : 0;

  if (raw_items_in_page <= 0) {
    finished_ = true;
  }
  else if (reported_total >= 0 && offset_ >= reported_total) {
    finished_ = true;
  }
  else if (pages_ >= kMaxPages) {
    finished_ = true;
    truncated_ = true;
  }

  return !finished_;

}

Importer::Importer(PageSource *source, QObject *parent)
    : QObject(parent),
      source_(source),
      state_(State::Idle),
      paginator_(kPageSize),
      received_(0),
      expected_total_(-1) {

  qRegisterMetaType<PlaylistImport::PageResult>();
  qRegisterMetaType<PlaylistImport::PlaylistInfo>();
  qRegisterMetaType<QList<PlaylistImport::PlaylistInfo>>();
  qRegisterMetaType<PlaylistImport::Track>();
  qRegisterMetaType<QList<PlaylistImport::Track>>();
  qRegisterMetaType<PlaylistImport::Importer::State>();

  QObject::connect(source_, &PageSource::PageReady, this, &Importer::PageReceived);

}

void Importer::SetState(const State state) {

  if (state_ == state) return;
  state_ = state;
  Q_EMIT StateChanged(state);

}

void Importer::Reset() {

  paginator_ = Paginator(kPageSize);
  playlists_.clear();
  tracks_.clear();
  received_ = 0;
  expected_total_ = -1;

}

void Importer::LoadPlaylists() {

  if (busy()) return;
  Reset();
  SetState(State::LoadingPlaylists);
  Q_EMIT Progress(0, -1);
  RequestNext();

}

void Importer::ImportPlaylist(const PlaylistInfo &playlist) {

  if (busy() || playlist.id.isEmpty()) return;
  Reset();
  current_playlist_ = playlist;
  SetState(State::LoadingTracks);
  Q_EMIT Progress(0, playlist.track_count > 0 ? playlist.track_count : -1);
  RequestNext();

}

void Importer::Cancel() {

  if (!busy()) return;
  source_->Cancel();
  Reset();
  SetState(State::Idle);
  Q_EMIT Cancelled();

}

void Importer::RequestNext() {

  if (state_ == State::LoadingPlaylists) {
    source_->RequestPlaylists(paginator_.offset(), paginator_.limit());
  }
  else if (state_ == State::LoadingTracks) {
    source_->RequestTracks(current_playlist_.id, paginator_.offset(), paginator_.limit());
  }

}

void Importer::Fail(const QString &message) {

  // Everything read so far is discarded: a failed import never yields a partial list.
  Reset();
  SetState(State::Idle);
  Q_EMIT Failed(message);

}

void Importer::PageReceived(const PageResult &result) {

  // A reply that arrives after Cancel() or after a failure is ignored.
  if (!busy()) return;

  if (!result.ok) {
    if (result.http_status == 401 || result.http_status == 403) {
      Fail(Tr("The service rejected the request (HTTP %1). Your login may have expired, log in again in the settings.").arg(result.http_status));
    }
    else if (!result.error.isEmpty()) {
      Fail(result.error);
    }
    else {
      Fail(Tr("The request failed."));
    }
    return;
  }

  const Service service = source_->service();
  int raw_count = 0;
  int total = -1;

  if (state_ == State::LoadingPlaylists) {
    const Page<PlaylistInfo> page = ParsePlaylistList(service, result.json);
    if (!page.valid) {
      Fail(Tr("The service returned an unexpected response."));
      return;
    }
    raw_count = page.raw_count;
    total = page.total;
    playlists_ << page.items;
  }
  else {
    const Page<Track> page = ParsePlaylistTracks(service, result.json);
    if (!page.valid) {
      Fail(Tr("The service returned an unexpected response."));
      return;
    }
    raw_count = page.raw_count;
    total = page.total;
    tracks_ << page.items;
  }

  // The total must stay the same while paging, otherwise the list changed under us and pages may overlap or skip entries.
  if (total >= 0) {
    if (expected_total_ >= 0 && total != expected_total_) {
      Fail(Tr("The list changed while it was being loaded. Please try again."));
      return;
    }
    expected_total_ = total;
  }

  received_ += raw_count;
  Q_EMIT Progress(received_, expected_total_);

  if (paginator_.Advance(raw_count, total)) {
    RequestNext();
    return;
  }

  if (paginator_.truncated()) {
    Fail(Tr("The list is too long to load safely."));
    return;
  }
  if (expected_total_ >= 0 && received_ < expected_total_) {
    Fail(Tr("The service returned fewer entries (%1) than it reported (%2).").arg(received_).arg(expected_total_));
    return;
  }

  Finish();

}

void Importer::Finish() {

  if (state_ == State::LoadingPlaylists) {
    const QList<PlaylistInfo> playlists = playlists_;
    Reset();
    SetState(State::Idle);
    Q_EMIT PlaylistsLoaded(playlists);
    return;
  }

  // Keep order and intentional duplicates; leave out only unavailable (unplayable) entries.
  QList<Track> playable;
  int skipped = 0;
  for (const Track &track : std::as_const(tracks_)) {
    if (track.available) playable << track;
    else ++skipped;
  }
  const PlaylistInfo playlist = current_playlist_;
  Reset();
  SetState(State::Idle);
  Q_EMIT PlaylistImported(playlist, playable, skipped);

}

}  // namespace PlaylistImport
