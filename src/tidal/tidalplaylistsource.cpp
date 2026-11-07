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

#include <algorithm>

#include <QCoreApplication>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>

#include "includes/shared_ptr.h"
#include "core/networkaccessmanager.h"
#include "tidalservice.h"
#include "tidalplaylistsource.h"

using namespace Qt::Literals::StringLiterals;

TidalPlaylistSource::TidalPlaylistSource(TidalService *service, const SharedPtr<NetworkAccessManager> network, QObject *parent)
    : PlaylistImport::PageSource(parent),
      service_(service),
      request_(new Request(service, network, this)),
      generation_(0) {}

void TidalPlaylistSource::RequestPlaylists(const int offset, const int limit) {

  Start(QStringLiteral("users/%1/playlists").arg(service_->user_id()),
        Request::ParamList() << Request::Param(u"limit"_s, QString::number(limit)) << Request::Param(u"offset"_s, QString::number(offset)) << Request::Param(u"order"_s, u"DATE"_s) << Request::Param(u"orderDirection"_s, u"DESC"_s));

}

void TidalPlaylistSource::RequestTracks(const QString &playlist_id, const int offset, const int limit) {

  Start(QStringLiteral("playlists/%1/items").arg(playlist_id),
        Request::ParamList() << Request::Param(u"limit"_s, QString::number(limit)) << Request::Param(u"offset"_s, QString::number(offset)) << Request::Param(u"order"_s, u"INDEX"_s) << Request::Param(u"orderDirection"_s, u"ASC"_s));

}

void TidalPlaylistSource::Start(const QString &resource, const Request::ParamList &params) {

  if (!service_->authenticated()) {
    EmitFailure(tr("Not signed in"));
    return;
  }

  const quint64 generation = ++generation_;
  QNetworkReply *reply = request_->Get(resource, params);
  reply_ = reply;
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() { ReplyFinished(reply, generation); });

}

void TidalPlaylistSource::ReplyFinished(QNetworkReply *reply, const quint64 generation) {

  reply->deleteLater();
  // Ignore replies of requests that were cancelled or replaced.
  if (generation != generation_) return;
  reply_.clear();

  const TidalBaseRequest::JsonObjectResult result = request_->Parse(reply);

  PlaylistImport::PageResult page;
  page.http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  page.network_error = static_cast<int>(reply->error());
  page.retry_after_sec = std::clamp(reply->rawHeader("Retry-After").toInt(), 0, 3600);
  if (result.error_code == TidalBaseRequest::ErrorCode::Success) {
    page.ok = true;
    page.json = result.json_object;
  }
  else {
    page.ok = false;
    page.error = result.error_message;
  }
  Q_EMIT PageReady(page);

}

void TidalPlaylistSource::EmitFailure(const QString &message) {

  const quint64 generation = ++generation_;
  QMetaObject::invokeMethod(this, [this, message, generation]() {
    if (generation != generation_) return;
    PlaylistImport::PageResult page;
    page.error = message;
    page.not_signed_in = true;
    Q_EMIT PageReady(page);
  }, Qt::QueuedConnection);

}

void TidalPlaylistSource::Cancel() {

  ++generation_;
  if (reply_) {
    QNetworkReply *reply = reply_;
    reply_.clear();
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }

}
