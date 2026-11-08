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

#ifndef TIDALACCOUNT_H
#define TIDALACCOUNT_H

#include "config.h"

#include "includes/shared_ptr.h"
#include "streaming/streamingaccount.h"

class TidalService;

class TidalAccount : public AccountBackend {
  Q_OBJECT

 public:
  explicit TidalAccount(TidalService *service, QObject *parent = nullptr);

  QString key() const override;
  QString service_name() const override;
  bool authenticated() const override;
  bool configured() const override;
  QString account_key() const override;
  QList<SetupField> setup_fields() const override;
  QString setup_help() const override;
  // The setup text without needing a service (used by tests).
  static QString SetupHelpText();
  QString signin_help() const override;
  QUrl pending_url() const override;
  bool SaveSetup(const QMap<QString, QString> &values, QString *error) override;
  void StartSignIn(const quint64 attempt) override;
  void CancelSignIn() override;
  void SignOut() override;

 private:
  TidalService *service_;
};

#endif  // TIDALACCOUNT_H
