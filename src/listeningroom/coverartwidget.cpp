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

#include <QColor>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

#include "coverartwidget.h"
#include "roomstyle.h"

CoverArtWidget::CoverArtWidget(QWidget *parent) : QWidget(parent), mode_(Mode::Idle), idle_text_(), cache_key_(0) {

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName(tr("Album cover"));

}

void CoverArtWidget::SetImage(const QImage &image, const bool has_track) {

  image_ = image;
  cache_ = QPixmap();
  mode_ = !image.isNull() ? Mode::Artwork : (has_track ? Mode::NoArtwork : Mode::Idle);
  setAccessibleDescription(mode_ == Mode::Artwork ? tr("Cover of the current track") : (mode_ == Mode::NoArtwork ? tr("This track has no artwork") : tr("Nothing playing")));
  update();

}

int CoverArtWidget::ShadowMargin() const {
  return std::max(10, fontMetrics().height());
}

QRect CoverArtWidget::SleeveRect() const {

  const int margin = ShadowMargin();
  const int side = std::clamp(std::min(width(), height()) - margin * 2, std::min(kMinimumSide, std::min(width(), height())), kMaximumSide);
  const int clamped = std::max(0, std::min(side, std::min(width(), height()) - 2));
  // Left aligned, so the title below the cover starts exactly where the sleeve starts.
  return QRect(margin, (height() - clamped) / 2 - margin / 4, clamped, clamped);

}

QSize CoverArtWidget::sizeHint() const {
  return QSize(420, 420);
}

QSize CoverArtWidget::minimumSizeHint() const {
  return QSize(kMinimumSide + ShadowMargin() * 2, kMinimumSide + ShadowMargin() * 2);
}

void CoverArtWidget::DrawPlaceholder(QPainter *p, const QRect &rect) const {

  const QPalette &pal = palette();
  const QColor window = RoomStyle::Window(pal);
  const QColor text = RoomStyle::Text(pal);
  const QColor accent = RoomStyle::Accent(pal);

  // The sleeve itself: a quiet raised surface with a hairline edge.
  p->setPen(QPen(RoomStyle::Hairline(pal), 1));
  p->setBrush(RoomStyle::Surface(pal, 0.07));
  p->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);

  // A record, a little to the right of centre, with grooves and a label in the accent color.
  const QPointF centre(rect.center().x() + rect.width() * 0.04, rect.center().y() - rect.height() * 0.06);
  const qreal radius = rect.width() * 0.34;
  p->setPen(Qt::NoPen);
  p->setBrush(RoomStyle::Mix(window, text, 0.03));
  p->drawEllipse(centre, radius, radius);
  p->setBrush(Qt::NoBrush);
  for (int i = 1; i <= 5; ++i) {
    const qreal r = radius * (0.38 + i * 0.115);
    p->setPen(QPen(RoomStyle::Mix(window, text, i % 2 ? 0.07 : 0.045), 1));
    p->drawEllipse(centre, r, r);
  }
  p->setPen(Qt::NoPen);
  p->setBrush(RoomStyle::Mix(window, accent, 0.55));
  p->drawEllipse(centre, radius * 0.30, radius * 0.30);
  p->setBrush(window);
  p->drawEllipse(centre, radius * 0.035, radius * 0.035);

  // One line of text under the record, only when it says something the page does not already say.
  const QString line = mode_ == Mode::NoArtwork ? tr("No artwork for this track") : idle_text_;
  if (line.isEmpty()) return;
  p->setFont(RoomStyle::Scaled(font(), 1.05));
  p->setPen(RoomStyle::Muted(pal));
  const QRect text_rect(rect.left() + 12, static_cast<int>(centre.y() + radius + rect.height() * 0.05), rect.width() - 24, fontMetrics().height() * 2);
  p->drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, line);

}

void CoverArtWidget::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  const QRect sleeve = SleeveRect();
  if (sleeve.width() < 8) return;

  // Soft shadow: stacked translucent rounded rects, darker near the sleeve and offset downwards.
  const QColor shadow = palette().color(QPalette::Shadow);
  const int layers = std::max(6, ShadowMargin() / 2);
  p.setPen(Qt::NoPen);
  for (int i = layers; i >= 1; --i) {
    QColor c = shadow;
    c.setAlphaF(0.34 / layers * (layers - i + 1) / 2.4);
    const int grow = i * 2;
    p.setBrush(c);
    p.drawRoundedRect(sleeve.adjusted(-grow, -grow / 2 + layers / 2, grow, grow + layers / 2), 14 + grow / 2, 14 + grow / 2);
  }

  if (mode_ != Mode::Artwork) {
    DrawPlaceholder(&p, sleeve);
    return;
  }

  // Scale once per size and cache, so repainting during seeking and animation is cheap.
  const qreal dpr = devicePixelRatioF();
  const QSize target = QSize(static_cast<int>(sleeve.width() * dpr), static_cast<int>(sleeve.height() * dpr));
  if (cache_.isNull() || cache_size_ != target) {
    QImage scaled = image_.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - target.width()) / 2, (scaled.height() - target.height()) / 2, target.width(), target.height());
    cache_ = QPixmap::fromImage(scaled.copy(crop));
    cache_.setDevicePixelRatio(dpr);
    cache_size_ = target;
  }

  QPainterPath path;
  path.addRoundedRect(QRectF(sleeve), 14, 14);
  p.save();
  p.setClipPath(path);
  p.drawPixmap(sleeve.topLeft(), cache_);
  p.restore();

  // A hairline keeps very dark or very light covers from dissolving into the background.
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(RoomStyle::Hairline(palette()), 1));
  p.drawRoundedRect(QRectF(sleeve).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);

}
