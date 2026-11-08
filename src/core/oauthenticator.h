/*
 * Strawberry Music Player
 * Copyright 2022-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef OAUTHENTICATOR_H
#define OAUTHENTICATOR_H

#include "config.h"

#include <QObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QSslError>

#include "includes/shared_ptr.h"

class QTimer;
class QNetworkReply;
class NetworkAccessManager;
class LocalRedirectServer;

class OAuthenticator : public QObject {
  Q_OBJECT

 public:
  explicit OAuthenticator(const SharedPtr<NetworkAccessManager> network, QObject *parent = nullptr);
  ~OAuthenticator() override;

  enum class Type {
    Authorization_Code,
    Client_Credentials
  };

  enum class PortType {
    SetToRedirectURL,
    PassAsUrlParam,
    PassInState
  };

  void set_settings_group(const QString &settings_group);
  void set_type(const Type type);
  void set_authorize_url(const QUrl &authorize_url);
  void set_redirect_url(const QUrl &redirect_url);
  void set_access_token_url(const QUrl &access_token_url);
  void set_client_id(const QString &client_id);
  void set_client_secret(const QString &client_secret);
  void set_scope(const QString &scope);
  void set_use_local_redirect_server(const bool use_local_redirect_server);
  void set_port_type(const PortType port_type);
  void set_use_pkce(const bool use_pkce);

  QString token_type() const { return token_type_; }
  QString access_token() const { return access_token_; }
  qint64 expires_in() const { return expires_in_; }
  QString country_code() const { return country_code_; }
  quint64 user_id() const { return user_id_; }
  bool authenticated() const { return !token_type_.isEmpty() && !access_token_.isEmpty(); }

  QByteArray authorization_header() const;

  // Starts a sign-in. Each call to Authenticate() of the authorization code flow is one interactive attempt with its own number (see attempt()).
  // A new attempt abandons the one before it: its pending state, local redirect server and code exchange are dropped.
  void Authenticate();
  // The number of the latest interactive attempt (0 before the first). Only InteractiveFinished() events carry it.
  quint64 attempt() const { return attempt_; }
  // True from Authenticate() until the flow finished, failed or was cancelled.
  bool authentication_pending() const { return !state_.isEmpty(); }
  // The address that was opened in the browser for the pending sign-in, empty when none is pending.
  QUrl pending_authorize_url() const { return pending_authorize_url_; }
  // Abandons a pending sign-in without reporting a failure: a callback that arrives later is refused because its state is no longer known.
  void CancelAuthentication();
  void ClearSession();
  void LoadSession();
  void ExternalAuthorizationUrlReceived(const QUrl &request_url);

  // Requests a new access token with the refresh token, for when the server rejected the access token.
  // Returns false if there is no refresh token, so the session can't be refreshed.
  bool RefreshAccessToken();

 private:
  using Param = QPair<QString, QString>;
  using ParamList = QList<Param>;

  QString GrantType() const;
  void StartRefreshLoginTimer();
  void ScheduleRefreshRetry();
  void AbortReplies();
  void AccessTokenRequestFailed(const bool refresh_token, const QString &error, const bool clear_session = false);
  QNetworkReply *CreateAccessTokenRequest(const ParamList &params, const bool refresh_token);
  void RequestAccessToken(const QString &code = QString(), const QUrl &redirect_url = QUrl());
  void RerefreshAccessToken();
  void AuthorizationUrlReceived(const QUrl &request_url, const QUrl &redirect_url);
  QUrl EffectiveRedirectUrl() const;

 Q_SIGNALS:
  void Error(const QString &error);
  // The sign-in address was handed to the desktop. `opened` is false when no browser could be started, then the user has to open it by hand.
  void AuthorizationUrlReady(const QUrl &url, const bool opened);
  // The stored session was removed (logout, refused refresh token, expired grant).
  void SessionCleared();
  // Reported for every sign-in outcome AND for background token refreshes (a refresh that succeeds or whose session is cleared). Callers that must tell
  // their own interactive sign-in apart from a refresh use InteractiveFinished instead.
  void AuthenticationFinished(const bool success, const QString &error = QString(), const bool invalid_grant = false);
  // The outcome of one interactive attempt, and only of that: the code exchange of attempt `attempt`, or a provider error returned for it.
  // Background refreshes, callbacks of other attempts and callbacks that cannot be attributed to the pending attempt never produce it.
  void InteractiveFinished(const quint64 attempt, const bool success, const QString &error = QString(), const bool invalid_grant = false);
  // A browser reply was refused because it cannot be attributed to the pending sign-in (no sign-in waiting, wrong or missing state, a repeat of a callback
  // that is already being exchanged). It never changes the pending sign-in. `reason` is plain text without any state or code.
  void CallbackIgnored(const QString &reason);

 private Q_SLOTS:
  void RefreshLoginTimerTimeout();
  void RedirectArrived();
  void HandleSSLErrors(const QList<QSslError> &ssl_errors);
  void AccessTokenRequestFinished(QNetworkReply *reply, const bool refresh_token);

 private:
  const SharedPtr<NetworkAccessManager> network_;
  QScopedPointer<LocalRedirectServer, QScopedPointerDeleteLater> local_redirect_server_;
  QTimer *timer_refresh_login_;

  QString settings_group_;
  Type type_;
  QUrl authorize_url_;
  QUrl redirect_url_;
  QUrl access_token_url_;
  QString client_id_;
  QString client_secret_;
  QString scope_;
  bool use_local_redirect_server_;
  PortType port_type_;
  bool use_pkce_;

  QString state_;
  QUrl pending_authorize_url_;
  quint64 attempt_ = 0;
  // The attempt the reply being processed belongs to (0 for a background refresh).
  quint64 finishing_attempt_ = 0;
  void FinishInteractive(const bool success, const QString &error, const bool invalid_grant);
  void AbandonAttempt();

  QString code_verifier_;
  QString code_challenge_;

  QString token_type_;
  QString access_token_;
  QString refresh_token_;
  qint64 expires_in_;
  qint64 login_time_;
  QString country_code_;
  quint64 user_id_;

  bool refresh_in_progress_;
  qint64 refresh_retry_interval_;

  QList<QNetworkReply*> replies_;
};

#endif  // OAUTHENTICATOR_H
