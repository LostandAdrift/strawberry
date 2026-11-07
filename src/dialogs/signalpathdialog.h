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
#include <QSize>

#include "engine/signalpath.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;

// Explains the current audio signal path with a clear hierarchy: the source quality first, then the output, then only the processing
// that actually changes the signal (everything that is off is one summary line). Technical text is collapsed and can be copied.
// The content scrolls, so the Close button stays visible even in a small window.
class SignalPathDialog : public QDialog {
  Q_OBJECT

 public:
  // The largest height the dialog asks for, so it fits a 600 pixel high screen area with title bar and panels.
  static constexpr int kMaximumPreferredHeight = 520;

  explicit SignalPathDialog(QWidget *parent = nullptr);

  void SetReport(const SignalPath::Report &report);

  QSize sizeHint() const override;

  QString technical_text() const;
  QPushButton *output_settings_button() const { return output_settings_button_; }
  QPushButton *copy_button() const { return copy_button_; }
  QPushButton *close_button() const { return close_button_; }
  QToolButton *details_toggle() const { return details_toggle_; }
  QPlainTextEdit *technical_view() const { return technical_; }
  QScrollArea *scroll_area() const { return scroll_; }

 Q_SIGNALS:
  // The user asked to open the output (Backend) settings.
  void OutputSettingsRequested();

 private Q_SLOTS:
  void CopyDetails();
  void SetDetailsExpanded(const bool expanded);

 private:
  QScrollArea *scroll_;
  QLabel *label_source_;
  QLabel *label_tier_;
  QLabel *label_source_summary_;
  QLabel *label_output_;
  QLabel *label_output_detail_;
  QLabel *label_processing_;
  QLabel *label_processing_detail_;
  QLabel *label_caveats_;
  QToolButton *details_toggle_;
  QPlainTextEdit *technical_;
  QPushButton *copy_button_;
  QPushButton *output_settings_button_;
  QPushButton *close_button_;
};

#endif  // SIGNALPATHDIALOG_H
