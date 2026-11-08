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

#include <QMap>
#include <QString>

#include "qobuzservice.h"
#include "qobuzaccount.h"

using namespace Qt::Literals::StringLiterals;

QobuzAccount::QobuzAccount(QobuzService *service, QObject *parent)
    : AccountBackend(parent),
      service_(service) {

  QObject::connect(service_, &QobuzService::InteractiveLoginFinished, this, [this](const quint64 attempt, const bool success, const QString &error) { ReportFinished(attempt, success, error); });
  QObject::connect(service_, &QobuzService::AuthorizationLinkReady, this, [this](const QUrl &url, const bool opened) { ReportLink(url, opened); });
  QObject::connect(service_, &QobuzService::SessionChanged, this, &AccountBackend::SessionChanged);

}

QString QobuzAccount::key() const { return u"qobuz"_s; }

QString QobuzAccount::service_name() const { return u"Qobuz"_s; }

bool QobuzAccount::authenticated() const { return service_->authenticated(); }

bool QobuzAccount::configured() const { return service_->has_app_credentials(); }

QString QobuzAccount::account_key() const {
  return service_->authenticated() ? QString::number(service_->user_id()) : QString();
}

QList<AccountBackend::SetupField> QobuzAccount::setup_fields() const {

  SetupField app_id;
  app_id.key = u"app_id"_s;
  app_id.label = tr("App ID");
  SetupField app_secret;
  app_secret.key = u"app_secret"_s;
  app_secret.label = tr("App secret");
  app_secret.secret = true;
  SetupField private_key;
  private_key.key = u"private_key"_s;
  private_key.label = tr("Private key");
  private_key.secret = true;
  return QList<SetupField>() << app_id << app_secret << private_key;

}

QString QobuzAccount::setup_help() const {

  return tr("Qobuz only lets registered applications sign in, and Adrift does not ship credentials. "
            "Enter the app ID, app secret and private key of a Qobuz API application you are entitled to use. "
            "Adrift stores them in its settings and uses them only to sign you in. If you do not have such credentials, sign-in is not available for this account in Adrift.");

}

QString QobuzAccount::signin_help() const {

  return tr("Your browser opens Qobuz's own sign-in page. Adrift never sees your password. After you approve, the browser hands you back to Adrift.");

}

QUrl QobuzAccount::pending_url() const { return service_->pending_sign_in_url(); }

bool QobuzAccount::SaveSetup(const QMap<QString, QString> &values, QString *error) {

  const QString app_id = values.value(u"app_id"_s).trimmed();
  const QString app_secret = values.value(u"app_secret"_s).trimmed();
  const QString private_key = values.value(u"private_key"_s).trimmed();
  if (app_id.isEmpty() || app_secret.isEmpty() || private_key.isEmpty()) {
    if (error) *error = tr("All three values are needed: app ID, app secret and private key.");
    return false;
  }
  service_->SaveAppCredentials(app_id, app_secret, private_key);
  return true;

}

void QobuzAccount::StartSignIn(const quint64 attempt) {

  BeginAttempt(attempt);
  service_->Authenticate();
  // Authenticate() moved the service's attempt number on; that is the number of this sign-in unless it already ended in there.
  EndStart(service_->attempt());

}

void QobuzAccount::CancelSignIn() {

  ReleaseAttempt();
  service_->CancelAuthentication();

}

void QobuzAccount::SignOut() { service_->ClearSession(); }
