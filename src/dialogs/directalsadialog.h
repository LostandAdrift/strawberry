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

#ifndef DIRECTALSADIALOG_H
#define DIRECTALSADIALOG_H

#include "config.h"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include "engine/directalsa.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Guided setup for sending audio straight to a hardware ALSA device with Adrift's own processing switched off.
// Nothing happens until a device is chosen and the user presses Apply. The dialog changes the Backend settings page (which still needs OK/Apply
// to save), keeps a restorable backup of the previous values, and never touches the volume.
class DirectAlsaDialog : public QDialog {
  Q_OBJECT

 public:
  // devices: (description, value) pairs; only hw: devices are offered.
  explicit DirectAlsaDialog(const QList<QPair<QString, QString>> &devices, const DirectAlsa::Settings &current, const QStringList &warnings, const bool has_backup, QWidget *parent = nullptr);

  QComboBox *device_combo() const { return combo_device_; }
  QPushButton *apply_button() const { return button_apply_; }
  QPushButton *restore_button() const { return button_restore_; }
  QPlainTextEdit *changes_view() const { return changes_; }
  QLabel *message_label() const { return label_message_; }
  const DirectAlsa::Plan &plan() const { return plan_; }

 Q_SIGNALS:
  void ApplyRequested(const DirectAlsa::Plan &plan);
  void RestoreRequested();

 private Q_SLOTS:
  void DeviceChanged();
  void Apply();

 private:
  DirectAlsa::Settings current_;
  DirectAlsa::Plan plan_;
  QComboBox *combo_device_;
  QPlainTextEdit *changes_;
  QLabel *label_message_;
  QPushButton *button_apply_;
  QPushButton *button_restore_;
  QPushButton *button_cancel_;
};

#endif  // DIRECTALSADIALOG_H
