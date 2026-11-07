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
#include <QMetaObject>
#include <QNetworkReply>
#include <QString>

#include "includes/shared_ptr.h"
#include "core/networkaccessmanager.h"
#include "qobuzservice.h"
#include "qobuzplaylistsource.h"

using namespace Qt::Literals::StringLiterals;

QobuzPlaylistSource::QobuzPlaylistSource(QobuzService *service, const SharedPtr<NetworkAccessManager> network, QObject *parent)
    : PlaylistImport::PageSource(parent),
      service_(service),
      request_(new Request(service, network, this)),
      generation_(0) {}

void QobuzPlaylistSource::RequestPlaylists(const int offset, const int limit) {

  Start(u"playlist/getUserPlaylists"_s,
        Request::ParamList() << Request::Param(u"limit"_s, QString::number(limit)) << Request::Param(u"offset"_s, QString::number(offset)));

}

void QobuzPlaylistSource::RequestTracks(const QString &playlist_id, const int offset, const int limit) {

  Start(u"playlist/get"_s,
        Request::ParamList() << Request::Param(u"playlist_id"_s, playlist_id) << Request::Param(u"extra"_s, u"tracks"_s) << Request::Param(u"limit"_s, QString::number(limit)) << Request::Param(u"offset"_s, QString::number(offset)));

}

void QobuzPlaylistSource::Start(const QString &resource, Request::ParamList params) {

  if (!service_->authenticated()) {
    EmitFailure(tr("You are not logged in to Qobuz. Log in from the settings first."));
    return;
  }

  params << Request::Param(u"user_auth_token"_s, service_->user_auth_token());

  const quint64 generation = ++generation_;
  QNetworkReply *reply = request_->Get(resource, params);
  reply_ = reply;
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() { ReplyFinished(reply, generation); });

}

void QobuzPlaylistSource::ReplyFinished(QNetworkReply *reply, const quint64 generation) {

  reply->deleteLater();
  // Ignore replies of requests that were cancelled or replaced.
  if (generation != generation_) return;
  reply_.clear();

  const QobuzBaseRequest::JsonObjectResult result = request_->Parse(reply);

  PlaylistImport::PageResult page;
  page.http_status = result.http_status_code;
  if (result.error_code == QobuzBaseRequest::ErrorCode::Success) {
    page.ok = true;
    page.json = result.json_object;
  }
  else {
    page.ok = false;
    page.error = result.error_message;
  }
  Q_EMIT PageReady(page);

}

void QobuzPlaylistSource::EmitFailure(const QString &message) {

  const quint64 generation = ++generation_;
  QMetaObject::invokeMethod(this, [this, message, generation]() {
    if (generation != generation_) return;
    PlaylistImport::PageResult page;
    page.error = message;
    Q_EMIT PageReady(page);
  }, Qt::QueuedConnection);

}

void QobuzPlaylistSource::Cancel() {

  ++generation_;
  if (reply_) {
    QNetworkReply *reply = reply_;
    reply_.clear();
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }

}
