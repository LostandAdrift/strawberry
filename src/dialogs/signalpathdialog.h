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

#ifndef SIGNALPATHDIALOG_H
#define SIGNALPATHDIALOG_H

#include "config.h"

#include <QDialog>

#include "engine/signalpath.h"

class QLabel;
class QVBoxLayout;
class QPlainTextEdit;
class QPushButton;
class QWidget;

// Explains the current audio signal path in three sections (source, processing, output), with copyable technical text.
class SignalPathDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SignalPathDialog(QWidget *parent = nullptr);

  void SetReport(const SignalPath::Report &report);

  QString technical_text() const;
  QPushButton *output_settings_button() const { return output_settings_button_; }
  QPushButton *copy_button() const { return copy_button_; }

 Q_SIGNALS:
  // The user asked to open the output (Backend) settings.
  void OutputSettingsRequested();

 private Q_SLOTS:
  void CopyDetails();

 private:
  QWidget *BuildSection(const QString &title, const SignalPath::Report::Rows &rows);

  QVBoxLayout *sections_layout_;
  QLabel *label_dsp_;
  QLabel *label_output_;
  QLabel *label_caveats_;
  QPlainTextEdit *technical_;
  QPushButton *copy_button_;
  QPushButton *output_settings_button_;
  QPushButton *close_button_;
  QList<QWidget*> section_widgets_;
};

#endif  // SIGNALPATHDIALOG_H
