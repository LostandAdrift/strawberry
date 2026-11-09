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

  // Not LoginSuccess/LoginFailure: those also report background token refreshes. InteractiveLoginFinished is the outcome of one numbered sign-in attempt.
  QObject::connect(service_, &TidalService::InteractiveLoginFinished, this, [this](const quint64 attempt, const bool success, const QString &error) { ReportFinished(attempt, success, error); });
  QObject::connect(service_, &TidalService::AuthorizationLinkReady, this, [this](const QUrl &url, const bool opened) { ReportLink(url, opened); });
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
  client_id.placeholder = tr("Client ID compatible with Strawberry's Tidal sign-in");
  return QList<SetupField>() << client_id;

}

QString TidalAccount::setup_help() const { return SetupHelpText(); }

QString TidalAccount::SetupHelpText() {

  // What this backend really is (checked against the code: legacy scopes "r_usr w_usr", token endpoint login.tidal.com/oauth2/token, API api.tidalhifi.com/v1,
  // redirect tidal://login/auth). Tidal's current developer dashboard issues IDs for a different sign-in (login.tidal.com/authorize + auth.tidal.com/v1 token
  // endpoint, PKCE, newer scopes), and the Strawberry maintainer states that those dashboard client IDs are not compatible with this integration.
  return tr("Adrift signs in to Tidal the way Strawberry's Tidal support always has, with Tidal's older sign-in and older API. "
            "Client IDs created in Tidal's public developer dashboard use Tidal's newer sign-in and are not compatible with it, so creating an app there will not make this work. "
            "Adrift does not include a client ID and does not take one from other apps. "
            "\n\nIf you already have a client ID that works with Strawberry's Tidal support, paste it below; it is kept exactly as you enter it and is not a secret. "
            "Without one, Tidal sign-in is not available in Adrift yet. Qobuz and your own music are not affected. "
            "Playlists through Tidal's newer official API would need a separate sign-in that Adrift does not have.");

}

QString TidalAccount::signin_help() const {

  return tr("Your browser opens Tidal's own sign-in page. Adrift never sees your password. After you approve, the browser hands you back to Adrift.");

}

QUrl TidalAccount::pending_url() const { return service_->pending_authorization_url(); }

bool TidalAccount::SaveSetup(const QMap<QString, QString> &values, QString *error) {

  const QString client_id = values.value(u"client_id"_s).trimmed();
  if (client_id.isEmpty()) {
    if (error) *error = tr("Enter a client ID that already works with Strawberry's Tidal sign-in. Your email, password and ordinary developer-dashboard client IDs cannot be used here.");
    return false;
  }
  if (client_id.contains(u' ') || client_id.size() < 8) {
    if (error) *error = tr("That does not look like a client ID. It is a single string without spaces.");
    return false;
  }
  service_->SaveClientId(client_id);
  return true;

}

void TidalAccount::StartSignIn(const quint64 attempt) {

  BeginAttempt(attempt);
  // The service may report at once (no client ID, redirect error): that is about this attempt and is passed on while it is being started.
  EndStart(service_->StartAuthorization(service_->client_id()));

}

void TidalAccount::CancelSignIn() {

  ReleaseAttempt();
  service_->CancelAuthorization();

}

void TidalAccount::SignOut() { service_->ClearSession(); }
