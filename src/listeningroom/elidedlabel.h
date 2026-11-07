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

#ifndef ELIDEDLABEL_H
#define ELIDEDLABEL_H

#include <algorithm>

#include <QColor>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QString>

#include "roomstyle.h"

// A single-line, plain-text label that elides instead of widening its layout. Tags and remote names are data, never markup.
// The full text is kept as the tooltip when it had to be elided.
class ElidedLabel : public QLabel {
 public:
  explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(0);
  }

  // The text color is derived from the palette at paint time, so runtime palette changes apply.
  enum class Tone {
    Normal,
    Muted,
    Faint,
    Accent
  };
  void SetTone(const Tone tone) { tone_ = tone; update(); }
  Tone tone() const { return tone_; }
  void SetElideMode(const Qt::TextElideMode mode) { mode_ = mode; update(); }
  QString ElidedText() const { return fontMetrics().elidedText(text(), mode_, width()); }
  bool IsElided() const { return fontMetrics().horizontalAdvance(text()) > width(); }

  QSize minimumSizeHint() const override { return QSize(0, fontMetrics().height()); }
  QSize sizeHint() const override { return QSize(std::min(fontMetrics().horizontalAdvance(text()) + 2, 4000), fontMetrics().height()); }

 protected:
  void paintEvent(QPaintEvent *e) override {
    Q_UNUSED(e)
    QPainter p(this);
    QColor color = RoomStyle::Text(palette());
    switch (tone_) {
      case Tone::Muted:  color = RoomStyle::Muted(palette()); break;
      case Tone::Faint:  color = RoomStyle::Faint(palette()); break;
      case Tone::Accent: color = RoomStyle::Accent(palette()); break;
      case Tone::Normal: break;
    }
    p.setPen(color);
    p.setFont(font());
    p.drawText(rect(), static_cast<int>(alignment()) | Qt::AlignVCenter, ElidedText());
    setToolTip(IsElided() ? text() : QString());
  }

 private:
  Tone tone_ = Tone::Normal;
  Qt::TextElideMode mode_ = Qt::ElideRight;
};

#endif  // ELIDEDLABEL_H
