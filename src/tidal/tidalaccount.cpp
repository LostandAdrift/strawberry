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

#include "tidalservice.h"
#include "tidalaccount.h"

using namespace Qt::Literals::StringLiterals;

TidalAccount::TidalAccount(TidalService *service, QObject *parent)
    : AccountBackend(parent),
      service_(service) {

  // Both end in AuthenticationFinished(); the account session ignores whatever does not belong to a sign-in it started.
  QObject::connect(service_, &StreamingService::LoginSuccess, this, &AccountBackend::SignInSucceeded);
  QObject::connect(service_, &StreamingService::LoginFailure, this, &AccountBackend::SignInFailed);
  QObject::connect(service_, &TidalService::AuthorizationLinkReady, this, &AccountBackend::LinkReady);
  QObject::connect(service_, &TidalService::SessionChanged, this, &AccountBackend::SessionChanged);

}

QString TidalAccount::key() const { return u"tidal"_s; }

QString TidalAccount::service_name() const { return u"Tidal"_s; }

bool TidalAccount::authenticated() const { return service_->authenticated(); }

bool TidalAccount::configured() const { return service_->has_client_id(); }

QString TidalAccount::account_key() const {
  return service_->authenticated() ? QString::number(service_->user_id()) : QString();
}

QList<AccountBackend::SetupField> TidalAccount::setup_fields() const {

  SetupField client_id;
  client_id.key = u"client_id"_s;
  client_id.label = tr("Tidal client ID");
  client_id.placeholder = tr("Client ID of your Tidal developer app");
  return QList<SetupField>() << client_id;

}

QString TidalAccount::setup_help() const {

  return tr("Tidal only lets apps sign in with a client ID, and Adrift does not ship one. Create an app in the Tidal developer portal (developer.tidal.com), "
            "add exactly this redirect address to it: tidal://login/auth, and paste the app's client ID below. "
            "The client ID is not a secret. Whether Tidal accepts a desktop app with that redirect address is decided by Tidal, not by Adrift.");

}

QString TidalAccount::signin_help() const {

  return tr("Your browser opens Tidal's own sign-in page. Adrift never sees your password. After you approve, the browser hands you back to Adrift.");

}

QUrl TidalAccount::pending_url() const { return service_->pending_authorization_url(); }

bool TidalAccount::SaveSetup(const QMap<QString, QString> &values, QString *error) {

  const QString client_id = values.value(u"client_id"_s).trimmed();
  if (client_id.isEmpty()) {
    if (error) *error = tr("Enter the client ID of your Tidal developer app.");
    return false;
  }
  if (client_id.contains(u' ') || client_id.size() < 8) {
    if (error) *error = tr("That does not look like a client ID. It is a single string without spaces.");
    return false;
  }
  service_->SaveClientId(client_id);
  return true;

}

void TidalAccount::StartSignIn() { service_->StartAuthorization(service_->client_id()); }

void TidalAccount::CancelSignIn() { service_->CancelAuthorization(); }

void TidalAccount::SignOut() { service_->ClearSession(); }
