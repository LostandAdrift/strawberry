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

#include <algorithm>

#include <QAbstractSlider>
#include <QEnterEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QRectF>
#include <QSize>
#include <QString>

#include "seekslider.h"
#include "roomstyle.h"

SeekSlider::SeekSlider(QWidget *parent) : QAbstractSlider(parent), can_seek_(false), dragging_(false), hovered_(false) {

  setOrientation(Qt::Horizontal);
  setRange(0, 0);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAccessibleName(tr("Seek"));
  UpdateAccessibleText();

}

QString SeekSlider::FormatTime(const int ms) {

  const int total = std::max(0, ms) / 1000;
  const int hours = total / 3600;
  const int minutes = (total / 60) % 60;
  const int seconds = total % 60;
  if (hours > 0) return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));

}

void SeekSlider::UpdateAccessibleText() {

  if (maximum() <= 0) setAccessibleDescription(tr("Nothing to seek"));
  else setAccessibleDescription(tr("%1 of %2").arg(FormatTime(value()), FormatTime(maximum())));

}

void SeekSlider::SetPosition(const int position_ms, const int length_ms) {

  if (dragging_) return;
  const int length = std::max(0, length_ms);
  if (maximum() != length) setMaximum(length);
  const int position = std::clamp(position_ms, 0, length);
  if (value() != position) {
    // Follows playback silently: valueChanged is for widgets that watch the slider, the user's action is SeekRequested.
    setValue(position);
  }
  UpdateAccessibleText();
  update();

}

void SeekSlider::SetCanSeek(const bool can_seek) {

  can_seek_ = can_seek;
  update();

}

int SeekSlider::TrackLeft() const {
  return 6;
}

int SeekSlider::TrackRight() const {
  return std::max(TrackLeft() + 1, width() - 6);
}

int SeekSlider::PositionForX(const int x) const {

  if (maximum() <= 0) return 0;
  const double fraction = static_cast<double>(std::clamp(x, TrackLeft(), TrackRight()) - TrackLeft()) / static_cast<double>(TrackRight() - TrackLeft());
  return static_cast<int>(fraction * maximum());

}

QSize SeekSlider::sizeHint() const {
  return QSize(240, std::max(22, fontMetrics().height()));
}

QSize SeekSlider::minimumSizeHint() const {
  return QSize(80, std::max(22, fontMetrics().height()));
}

void SeekSlider::mousePressEvent(QMouseEvent *e) {

  if (e->button() != Qt::LeftButton || maximum() <= 0 || !can_seek_) {
    e->ignore();
    return;
  }
  setFocus(Qt::MouseFocusReason);
  dragging_ = true;
  setValue(PositionForX(static_cast<int>(e->position().x())));
  update();
  if (live_) Q_EMIT SeekRequested(value());

}

void SeekSlider::mouseMoveEvent(QMouseEvent *e) {

  if (dragging_) {
    setValue(PositionForX(static_cast<int>(e->position().x())));
    UpdateAccessibleText();
    update();
    if (live_) Q_EMIT SeekRequested(value());
  }

}

void SeekSlider::mouseReleaseEvent(QMouseEvent *e) {

  if (!dragging_ || e->button() != Qt::LeftButton) return;
  setValue(PositionForX(static_cast<int>(e->position().x())));
  dragging_ = false;
  UpdateAccessibleText();
  update();
  if (!live_) Q_EMIT SeekRequested(value());

}

void SeekSlider::keyPressEvent(QKeyEvent *e) {

  if (maximum() <= 0 || !can_seek_) {
    QAbstractSlider::keyPressEvent(e);
    return;
  }

  int target = value();
  switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
      target -= step_;
      break;
    case Qt::Key_Right:
    case Qt::Key_Up:
      target += step_;
      break;
    case Qt::Key_PageDown:
      target -= page_step_;
      break;
    case Qt::Key_PageUp:
      target += page_step_;
      break;
    case Qt::Key_Home:
      target = 0;
      break;
    case Qt::Key_End:
      target = maximum();
      break;
    default:
      QAbstractSlider::keyPressEvent(e);
      return;
  }

  target = std::clamp(target, 0, maximum());
  setValue(target);
  UpdateAccessibleText();
  update();
  Q_EMIT SeekRequested(target);

}

void SeekSlider::enterEvent(QEnterEvent *e) {
  QAbstractSlider::enterEvent(e);
  hovered_ = true;
  update();
}

void SeekSlider::leaveEvent(QEvent *e) {
  QAbstractSlider::leaveEvent(e);
  hovered_ = false;
  update();
}

void SeekSlider::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QPalette &pal = palette();
  const qreal thickness = 4;
  const qreal y = (height() - thickness) / 2.0;
  const QRectF track(TrackLeft(), y, TrackRight() - TrackLeft(), thickness);

  p.setPen(Qt::NoPen);
  p.setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.16));
  p.drawRoundedRect(track, thickness / 2, thickness / 2);

  const bool active = maximum() > 0;
  const double fraction = active ? static_cast<double>(value()) / maximum() : 0.0;
  const qreal filled_width = track.width() * fraction;
  if (active && filled_width > 0) {
    p.setBrush(can_seek_ || !isEnabled() ? RoomStyle::Accent(pal) : RoomStyle::Muted(pal));
    p.drawRoundedRect(QRectF(track.left(), y, std::max<qreal>(thickness, filled_width), thickness), thickness / 2, thickness / 2);
  }

  // The handle appears on hover, drag and keyboard focus; the focus ring is part of it.
  if (active && (hovered_ || dragging_ || hasFocus())) {
    const qreal radius = hasFocus() || dragging_ ? 7 : 6;
    const QPointF centre(track.left() + filled_width, height() / 2.0);
    if (hasFocus()) {
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.6), 2));
      p.drawEllipse(centre, radius + 3, radius + 3);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(RoomStyle::Text(pal));
    p.drawEllipse(centre, radius, radius);
  }

}
