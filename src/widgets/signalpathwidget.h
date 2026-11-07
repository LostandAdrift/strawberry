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

// A compact, clickable badge that summarizes the audio signal path, for example "FLAC 16/44.1   System mixer   App DSP off".
//
// The source (codec, bit depth, sample rate) is the most important part and is kept as long as possible.
// When the widget is narrow, the secondary parts are dropped first: the "App DSP" chip, then the output label, and only then is the source text elided.
// The full details are always in the tooltip and the details dialog. A long tag can never widen the window.
// The widget is a real button: keyboard focusable, activated with Space/Enter, and exposes an accessible name and description.
class SignalPathWidget : public QAbstractButton {
  Q_OBJECT

 public:
  explicit SignalPathWidget(QWidget *parent = nullptr);

  // The text and the parts that are shown at a given width.
  struct Layout {
    QString source;   // elided only when even the source alone does not fit
    QString output;   // empty when dropped
    bool show_chip = false;
  };

  static constexpr int kMaximumTextWidth = 260;  // cap for source + output text, whatever the tags say
  static constexpr int kMaximumSourceWidth = 150;  // the source text is never wider than this in the minimum size

  void SetReport(const SignalPath::Report &report);
  // Hides the badge when nothing is playing.
  void Clear();

  const SignalPath::Report &report() const { return report_; }
  bool has_report() const { return has_report_; }

  // What would be drawn at the given widget width.
  Layout ComputeLayout(const int width) const;
  // The text actually drawn for the main part at the current width: the source, then the output if it fits.
  QString ElidedMainText() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 protected:
  void paintEvent(QPaintEvent *e) override;
  void changeEvent(QEvent *e) override;

 private:
  int Padding() const;
  int DotDiameter() const;
  int Gap() const;
  int ChipWidth() const;
  int LeftEdge() const;
  int TextWidth(const QString &text) const;

  SignalPath::Report report_;
  bool has_report_;
};

#endif  // SIGNALPATHWIDGET_H
