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

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include "streamingaccount.h"

using namespace Qt::Literals::StringLiterals;

AccountSession::AccountSession(AccountBackend *backend, QObject *parent, const int timeout_ms)
    : QObject(parent),
      backend_(backend),
      timer_(new QTimer(this)),
      state_(State::SignedOut),
      attempt_(0),
      link_opened_(true) {

  timer_->setSingleShot(true);
  timer_->setInterval(timeout_ms);
  QObject::connect(timer_, &QTimer::timeout, this, &AccountSession::TimedOut);

  QObject::connect(backend_, &AccountBackend::SignInSucceeded, this, &AccountSession::BackendSucceeded);
  QObject::connect(backend_, &AccountBackend::SignInFailed, this, &AccountSession::BackendFailed);
  QObject::connect(backend_, &AccountBackend::LinkReady, this, &AccountSession::BackendLink);
  QObject::connect(backend_, &AccountBackend::SessionChanged, this, &AccountSession::Refresh);

  state_ = Resting();

}

AccountSession::State AccountSession::Resting() const {

  if (backend_->authenticated()) return State::SignedIn;
  return backend_->configured() ? State::SignedOut : State::NeedsSetup;

}

void AccountSession::SetState(const State state) {

  if (state == state_) return;
  state_ = state;
  Q_EMIT StateChanged(state_);

}

void AccountSession::EndAttempt() {

  timer_->stop();
  link_ = QUrl();
  link_opened_ = true;
  Q_EMIT LinkChanged();

}

void AccountSession::Refresh() {

  if (state_ == State::SigningIn) return;
  SetState(Resting());

}

void AccountSession::SignIn() {

  if (state_ == State::SigningIn) return;
  if (!backend_->configured()) {
    SetState(State::NeedsSetup);
    return;
  }

  ++attempt_;
  timed_out_ = false;
  SetState(State::SigningIn);
  timer_->start();
  // The backend may report right away (for example when it can't even start); that report is handled like any other.
  backend_->StartSignIn();

}

void AccountSession::Cancel() {

  if (state_ != State::SigningIn) return;

  ++attempt_;  // anything that still belongs to the abandoned attempt no longer matches
  backend_->CancelSignIn();
  EndAttempt();
  SetState(Resting());
  Q_EMIT SignInFinished(false, tr("Sign-in cancelled."), true);

}

void AccountSession::SignOut() {

  if (state_ == State::SigningIn) Cancel();
  backend_->SignOut();
  SetState(Resting());

}

void AccountSession::BackendSucceeded() {

  if (state_ != State::SigningIn) {
    // Not part of a sign-in of ours, for example a background token refresh. Only the state is brought up to date.
    Refresh();
    return;
  }

  // Success is only believed when the service really holds a session. A success report that arrives without one belongs to an attempt
  // that was abandoned earlier, the current attempt keeps waiting for its own answer.
  if (!backend_->authenticated()) return;

  EndAttempt();
  SetState(State::SignedIn);
  Q_EMIT SignInFinished(true, QString(), false);

}

void AccountSession::BackendFailed(const QString &message) {

  // A failure that is not about a sign-in of ours (stale callback, background refresh) must not show up as a new failure.
  if (state_ != State::SigningIn) return;

  EndAttempt();
  backend_->CancelSignIn();
  SetState(Resting());
  Q_EMIT SignInFinished(false, message, false);

}

void AccountSession::BackendLink(const QUrl &url, const bool opened) {

  if (state_ != State::SigningIn) return;
  link_ = url;
  link_opened_ = opened;
  Q_EMIT LinkChanged();

}

void AccountSession::TimedOut() {

  if (state_ != State::SigningIn) return;

  ++attempt_;
  timed_out_ = true;
  backend_->CancelSignIn();
  EndAttempt();
  SetState(Resting());
  Q_EMIT SignInFinished(false, tr("The sign-in was not completed in time. If the browser did not open or you closed it, start the sign-in again."), false);

}
