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

#ifndef STREAMINGACCOUNT_H
#define STREAMINGACCOUNT_H

#include "config.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

// In-app account sign-in for a streaming service.
//
// AccountBackend is a thin adapter around the real service (TidalService, QobuzService). It never stores or exposes secrets.
// AccountSession is the state machine the UI talks to: it knows whether the service needs setup, is signed out, is waiting for the browser or is signed in,
// and it makes sure that a late or stale callback can never change the outcome of a sign-in that was cancelled, timed out or replaced.
class AccountBackend : public QObject {
  Q_OBJECT

 public:
  struct SetupField {
    QString key;
    QString label;
    QString placeholder;
    bool secret = false;  // shown as dots, never echoed back
  };

  explicit AccountBackend(QObject *parent = nullptr) : QObject(parent) {}

  virtual QString key() const = 0;           // "tidal"
  virtual QString service_name() const = 0;  // "Tidal"
  virtual bool authenticated() const = 0;
  // True when everything needed to start a sign-in is available (developer client id or app credentials).
  virtual bool configured() const = 0;
  // Identifies the signed-in account, empty when signed out. Used to detect an account switch.
  virtual QString account_key() const = 0;
  virtual QList<SetupField> setup_fields() const = 0;
  // Honest, plain explanation of what is missing and what to do about it.
  virtual QString setup_help() const = 0;
  // What the browser step does.
  virtual QString signin_help() const = 0;
  virtual QUrl pending_url() const { return QUrl(); }

  // Stores the values of setup_fields(). Returns false with an explanation when they are not usable.
  virtual bool SaveSetup(const QMap<QString, QString> &values, QString *error) = 0;
  virtual void StartSignIn() = 0;
  // Abandons a sign-in in progress. After this no callback may finish it.
  virtual void CancelSignIn() = 0;
  virtual void SignOut() = 0;

 Q_SIGNALS:
  void SignInSucceeded();
  void SignInFailed(const QString &message);
  // The stored session or the credentials changed behind the UI's back (logout, refused refresh token).
  void SessionChanged();
  // The browser address of the sign-in in progress. `opened` is false when no browser could be started.
  void LinkReady(const QUrl &url, const bool opened);
};

class AccountSession : public QObject {
  Q_OBJECT

 public:
  enum class State {
    NeedsSetup,  // not signed in and not able to start a sign-in: developer credentials are missing
    SignedOut,
    SigningIn,   // waiting for the browser and the answer of the service
    SignedIn
  };

  static constexpr int kDefaultTimeoutMs = 5 * 60 * 1000;

  explicit AccountSession(AccountBackend *backend, QObject *parent = nullptr, const int timeout_ms = kDefaultTimeoutMs);

  AccountBackend *backend() const { return backend_; }
  State state() const { return state_; }
  QString service_name() const { return backend_->service_name(); }
  QString key() const { return backend_->key(); }
  QString account_key() const { return backend_->account_key(); }
  QUrl link() const { return link_; }
  bool link_opened() const { return link_opened_; }
  // Counts every attempt. Used by callers and tests to tell attempts apart.
  int attempt() const { return attempt_; }
  // True when the last finished attempt ended because it ran out of time.
  bool last_attempt_timed_out() const { return timed_out_; }

 public Q_SLOTS:
  // Starts a sign-in. Works from SignedOut and from SignedIn (reconnect). Ignored while one is running or when setup is missing.
  void SignIn();
  void Cancel();
  void SignOut();
  // Re-reads the state of the service, for example after a setup was saved. Does not interrupt a sign-in.
  void Refresh();

 Q_SIGNALS:
  void StateChanged(const AccountSession::State state);
  // A sign-in ended. `message` is plain text and empty on success. `cancelled` is true when the user cancelled.
  void SignInFinished(const bool success, const QString &message, const bool cancelled);
  void LinkChanged();

 private Q_SLOTS:
  void BackendSucceeded();
  void BackendFailed(const QString &message);
  void BackendLink(const QUrl &url, const bool opened);
  void TimedOut();

 private:
  State Resting() const;
  void SetState(const State state);
  void EndAttempt();

  AccountBackend *backend_;
  QTimer *timer_;
  State state_;
  int attempt_;
  bool timed_out_ = false;
  QUrl link_;
  bool link_opened_;
};

Q_DECLARE_METATYPE(AccountSession::State)

#endif  // STREAMINGACCOUNT_H
