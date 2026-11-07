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

#ifndef ROOMSTYLE_H
#define ROOMSTYLE_H

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QPalette>

// Small helpers shared by the Listening Room widgets. Everything is derived from the application palette at paint time,
// so the Adrift Dark, Omarchy and system themes (and runtime palette changes) all work, and text keeps the contrast the palette gives it.
namespace RoomStyle {

inline QColor Mix(const QColor &a, const QColor &b, const qreal amount_of_b) {

  const qreal t = std::clamp(amount_of_b, 0.0, 1.0);
  return QColor::fromRgbF(static_cast<float>(a.redF() * (1.0 - t) + b.redF() * t), static_cast<float>(a.greenF() * (1.0 - t) + b.greenF() * t), static_cast<float>(a.blueF() * (1.0 - t) + b.blueF() * t));

}

// Main text.
inline QColor Text(const QPalette &p) { return p.color(QPalette::WindowText); }
// Secondary text. Mostly the text color, so it stays well above AA contrast on the window color.
inline QColor Muted(const QPalette &p) { return Mix(p.color(QPalette::Window), p.color(QPalette::WindowText), 0.72); }
// Tertiary text (labels, hints). Still readable, deliberately quieter.
inline QColor Faint(const QPalette &p) { return Mix(p.color(QPalette::Window), p.color(QPalette::WindowText), 0.58); }
inline QColor Accent(const QPalette &p) { return p.color(QPalette::Highlight); }
inline QColor AccentText(const QPalette &p) { return p.color(QPalette::HighlightedText); }
inline QColor Window(const QPalette &p) { return p.color(QPalette::Window); }
// The side rail sits slightly apart from the content.
inline QColor Rail(const QPalette &p) { return Mix(p.color(QPalette::Window), p.color(QPalette::Shadow), 0.22); }
// Raised surfaces (cards, queue rows on hover).
inline QColor Surface(const QPalette &p, const qreal amount = 0.06) { return Mix(p.color(QPalette::Window), p.color(QPalette::WindowText), amount); }
inline QColor Hairline(const QPalette &p) { return Mix(p.color(QPalette::Window), p.color(QPalette::WindowText), 0.14); }

// A scaled copy of a font. Sizes follow the user's font setting, so text size stays dynamic.
inline QFont Scaled(const QFont &base, const qreal factor, const bool bold = false, const QFont::Weight weight = QFont::Normal) {

  QFont font = base;
  if (base.pointSizeF() > 0) font.setPointSizeF(base.pointSizeF() * factor);
  else if (base.pixelSize() > 0) font.setPixelSize(static_cast<int>(base.pixelSize() * factor));
  if (bold) font.setWeight(QFont::DemiBold);
  else font.setWeight(weight);
  return font;

}

// Small caps-style section label font.
inline QFont Label(const QFont &base) {

  QFont font = Scaled(base, 0.82, true);
  font.setCapitalization(QFont::AllUppercase);
  font.setLetterSpacing(QFont::PercentageSpacing, 112);
  return font;

}

}  // namespace RoomStyle

#endif  // ROOMSTYLE_H
