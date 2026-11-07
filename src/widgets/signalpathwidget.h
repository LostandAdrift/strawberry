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

#ifndef SIGNALPATHWIDGET_H
#define SIGNALPATHWIDGET_H

#include "config.h"

#include <QAbstractButton>
#include <QSize>
#include <QString>

#include "engine/signalpath.h"

class QPaintEvent;
class QEvent;

// A compact, clickable badge that summarizes the audio signal path, for example "FLAC 16/44.1 · System mixer   App DSP off".
// The source/output text is elided when space is short, so long tags never force the window wider.
// The widget is a real button: keyboard focusable, activated with Space/Enter, and exposes an accessible name and description.
class SignalPathWidget : public QAbstractButton {
  Q_OBJECT

 public:
  explicit SignalPathWidget(QWidget *parent = nullptr);

  static constexpr int kMaximumTextWidth = 280;
  static constexpr int kMinimumTextWidth = 90;

  void SetReport(const SignalPath::Report &report);
  // Hides the badge when nothing is playing.
  void Clear();

  const SignalPath::Report &report() const { return report_; }
  bool has_report() const { return has_report_; }

  // The text actually drawn for the main part after eliding at the current width.
  QString ElidedMainText() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 protected:
  void paintEvent(QPaintEvent *e) override;
  void changeEvent(QEvent *e) override;

 private:
  int ChipWidth() const;
  int DotDiameter() const;
  int HorizontalPadding() const;
  int MainTextAvailableWidth() const;

  SignalPath::Report report_;
  bool has_report_;
};

#endif  // SIGNALPATHWIDGET_H
