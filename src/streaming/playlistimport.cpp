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

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>

#include "playlistimport.h"

using namespace Qt::Literals::StringLiterals;

namespace PlaylistImport {

namespace {

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
  track.artist = item["artist"_L1].toObject()["name"_L1].toString();
  track.album = item["album"_L1].toObject()["title"_L1].toString();
  track.duration_sec = static_cast<qint64>(item["duration"_L1].toDouble());
  track.available = !item.contains("allowStreaming"_L1) || item["allowStreaming"_L1].toBool(true);
  return track;

}

Track ParseQobuzTrack(const QJsonObject &item) {

  Track track;
  track.id = IdToString(item["id"_L1]);
  track.title = item["title"_L1].toString();
  track.artist = item["performer"_L1].toObject()["name"_L1].toString();
  if (track.artist.isEmpty()) track.artist = item["album"_L1].toObject()["artist"_L1].toObject()["name"_L1].toString();
  track.album = item["album"_L1].toObject()["title"_L1].toString();
  track.duration_sec = static_cast<qint64>(item["duration"_L1].toDouble());
  track.available = !item.contains("streamable"_L1) || item["streamable"_L1].toBool(true);
  return track;

}

}  // namespace

Page<PlaylistInfo> ParsePlaylistList(const Service service, const QJsonObject &json) {

  Page<PlaylistInfo> page;

  // Tidal: { totalNumberOfItems, offset, items: [ { uuid, title, numberOfTracks } ] }
  // Qobuz: { playlists: { total, offset, items: [ { id, name, tracks_count } ] } }
  const QJsonObject container = service == Service::Qobuz ? json["playlists"_L1].toObject() : json;
  if (!container["items"_L1].isArray()) return page;

  page.valid = true;
  page.offset = container["offset"_L1].toInt();
  const QString total_key = service == Service::Qobuz ? u"total"_s : u"totalNumberOfItems"_s;
  page.total = container.contains(total_key) ? container[total_key].toInt() : -1;

  const QJsonArray items = container["items"_L1].toArray();
  for (const QJsonValue &value : items) {
    const QJsonObject object = value.toObject();
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

  page.valid = true;
  page.offset = container["offset"_L1].toInt();
  const QString total_key = service == Service::Qobuz ? u"total"_s : u"totalNumberOfItems"_s;
  page.total = container.contains(total_key) ? container[total_key].toInt() : -1;

  const QJsonArray items = container["items"_L1].toArray();
  for (const QJsonValue &value : items) {
    QJsonObject object = value.toObject();
    Track track;
    if (service == Service::Qobuz) {
      track = ParseQobuzTrack(object);
    }
    else {
      // Skip videos and other non-track entries, which the player cannot play as audio.
      const QString type = object["type"_L1].toString();
      if (!type.isEmpty() && type != "track"_L1) continue;
      track = ParseTidalTrack(object["item"_L1].toObject());
    }
    if (track.id.isEmpty()) continue;
    page.items << track;
  }

  return page;

}

bool Paginator::Advance(const int items_in_page, const int reported_total) {

  if (finished_) return false;

  ++pages_;
  offset_ += items_in_page;

  if (items_in_page <= 0) {
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

MergeResult MergeNewTracks(const QSet<QString> &existing_ids, const QList<Track> &remote) {

  MergeResult result;
  QSet<QString> seen = existing_ids;

  for (const Track &track : remote) {
    if (!track.available) {
      ++result.unavailable_skipped;
      continue;
    }
    if (track.id.isEmpty()) continue;
    if (seen.contains(track.id)) {
      ++result.duplicates_skipped;
      continue;
    }
    seen.insert(track.id);
    result.to_append << track;
  }

  return result;

}

}  // namespace PlaylistImport
