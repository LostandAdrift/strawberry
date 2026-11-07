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

#ifndef TIDALPLAYLISTSOURCE_H
#define TIDALPLAYLISTSOURCE_H

#include "config.h"

#include <QPointer>
#include <QString>

#include "includes/shared_ptr.h"
#include "streaming/playlistimport.h"
#include "tidalbaserequest.h"

class QNetworkReply;
class NetworkAccessManager;
class TidalService;

// Read-only GET access to the signed-in user's Tidal playlists, using the same session and request conventions as the other Tidal requests.
// Only GET requests are made; nothing is ever written to the service.
class TidalPlaylistSource : public PlaylistImport::PageSource {
  Q_OBJECT

 public:
  explicit TidalPlaylistSource(TidalService *service, const SharedPtr<NetworkAccessManager> network, QObject *parent = nullptr);

  PlaylistImport::Service service() const override { return PlaylistImport::Service::Tidal; }
  void RequestPlaylists(const int offset, const int limit) override;
  void RequestTracks(const QString &playlist_id, const int offset, const int limit) override;
  void Cancel() override;

 private:
  class Request : public TidalBaseRequest {
   public:
    using TidalBaseRequest::TidalBaseRequest;
    QNetworkReply *Get(const QString &resource, const ParamList &params) { return CreateRequest(resource, params); }
    JsonObjectResult Parse(QNetworkReply *reply) { return ParseJsonObject(reply); }
  };

  void Start(const QString &resource, const Request::ParamList &params);
  void ReplyFinished(QNetworkReply *reply, const quint64 generation);
  void EmitFailure(const QString &message);

  TidalService *service_;
  Request *request_;
  QPointer<QNetworkReply> reply_;
  quint64 generation_;
};

#endif  // TIDALPLAYLISTSOURCE_H
