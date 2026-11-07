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
#include <cmath>

#include <QColor>
#include <QEvent>
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
#include "roommotion.h"
#include "roomstyle.h"

CoverArtWidget::CoverArtWidget(QWidget *parent)
    : QWidget(parent), mode_(Mode::Idle), idle_text_(), settle_timer_(new QTimer(this)), spin_timer_(new QTimer(this)), fade_(new RoomTween(this, 1.0, 340, QEasingCurve::InOutCubic)), record_(new RoomTween(this, 0.0, 560, QEasingCurve::OutCubic)) {

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName(tr("Album cover"));

  settle_timer_->setSingleShot(true);
  settle_timer_->setInterval(kSettleMs);
  QObject::connect(settle_timer_, &QTimer::timeout, this, &CoverArtWidget::SettleNow);

  // The record turns by repainting its own corner, and only while it is out and turning.
  spin_timer_->setInterval(40);
  QObject::connect(spin_timer_, &QTimer::timeout, this, [this]() {
    angle_ = std::fmod(angle_ + 2.4, 360.0);
    update(RecordBounds(SleeveRect()));
  });

  QObject::connect(fade_, &RoomTween::Changed, this, [this]() {
    if (fade_->value() >= 1.0) outgoing_ = QPixmap();
    update();
  });
  QObject::connect(record_, &RoomTween::Changed, this, [this]() {
    update(RecordBounds(SleeveRect()));
    SyncSpin();
  });
  QObject::connect(RoomMotion::Instance(), &RoomMotion::ReducedChanged, this, [this]() {
    outgoing_ = QPixmap();
    SyncSpin();
    update();
  });

}

double CoverArtWidget::record_amount() const {
  return record_->value();
}

double CoverArtWidget::fade_amount() const {
  return fade_->value();
}

bool CoverArtWidget::fading() const {
  return !outgoing_.isNull() && fade_->value() < 1.0;
}

void CoverArtWidget::SetImage(const QImage &image, const bool has_track) {

  // A cover that is replaced fades into the new one (when the old one was on screen and motion is allowed).
  const bool fade = !sharp_.isNull() && isVisible() && !RoomMotion::Instance()->reduced() && (!image.isNull() || mode_ == Mode::Artwork);
  if (fade) outgoing_ = sharp_;
  else outgoing_ = QPixmap();

  // Reduce once. The original (which can be a 4000 px scan) is not kept.
  if (image.width() > kMasterSide || image.height() > kMasterSide) master_ = image.scaled(kMasterSide, kMasterSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  else master_ = image;

  mode_ = !image.isNull() ? Mode::Artwork : (has_track ? Mode::NoArtwork : Mode::Idle);
  ++image_generation_;
  settle_timer_->stop();
  setAccessibleDescription(mode_ == Mode::Artwork ? tr("Cover of the current track") : (mode_ == Mode::NoArtwork ? tr("This track has no artwork") : tr("Nothing playing")));

  if (fade) {
    fade_->Jump(0.0);
    fade_->SetTarget(1.0);
  }
  else {
    fade_->Jump(1.0);
  }
  record_->SetTarget(playing_ && mode_ != Mode::Idle ? 1.0 : 0.0);
  SyncSpin();
  update();

}

void CoverArtWidget::SetPlaying(const bool playing) {

  playing_ = playing;
  record_->SetTarget(playing_ && mode_ != Mode::Idle ? 1.0 : 0.0);
  SyncSpin();

}

void CoverArtWidget::SyncSpin() {

  const bool want = playing_ && mode_ != Mode::Idle && isVisible() && !RoomMotion::Instance()->reduced() && record_->value() > 0.6;
  if (want && !spin_timer_->isActive()) spin_timer_->start();
  else if (!want && spin_timer_->isActive()) spin_timer_->stop();

}

void CoverArtWidget::showEvent(QShowEvent *e) {

  QWidget::showEvent(e);
  SyncSpin();

}

void CoverArtWidget::hideEvent(QHideEvent *e) {

  QWidget::hideEvent(e);
  SyncSpin();

}

void CoverArtWidget::changeEvent(QEvent *e) {

  QWidget::changeEvent(e);
  // Colors are baked into the rendered sleeve, so a palette or font change renders it again.
  if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::FontChange) {
    sharp_ = QPixmap();
    record_cache_ = QPixmap();
    update();
  }

}

int CoverArtWidget::ShadowMargin() const {
  return std::max(10, fontMetrics().height());
}

int CoverArtWidget::ShadowPad() const {
  return ShadowMargin() * 2;
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

void CoverArtWidget::RenderSharp() {

  const QRect sleeve = SleeveRect();
  if (sleeve.width() < 8) return;
  const int pad = ShadowPad();
  const qreal dpr = devicePixelRatioF();
  const QSize logical(sleeve.width() + pad * 2, sleeve.height() + pad * 2);

  QPixmap pixmap(QSize(static_cast<int>(std::ceil(logical.width() * dpr)), static_cast<int>(std::ceil(logical.height() * dpr))));
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);

  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const QRect local(pad, pad, sleeve.width(), sleeve.height());

  // Soft shadow: stacked translucent rounded rects, darker near the sleeve and offset downwards.
  const QColor shadow = palette().color(QPalette::Shadow);
  const int layers = std::max(6, ShadowMargin() / 2);
  p.setPen(Qt::NoPen);
  for (int i = layers; i >= 1; --i) {
    QColor c = shadow;
    c.setAlphaF(0.34 / layers * (layers - i + 1) / 2.4);
    const int grow = i * 2;
    p.setBrush(c);
    p.drawRoundedRect(local.adjusted(-grow, -grow / 2 + layers / 2, grow, grow + layers / 2), 14 + grow / 2, 14 + grow / 2);
  }

  if (mode_ != Mode::Artwork || master_.isNull()) {
    DrawPlaceholder(&p, local);
  }
  else {
    const QSize target(static_cast<int>(std::ceil(sleeve.width() * dpr)), static_cast<int>(std::ceil(sleeve.height() * dpr)));
    QImage scaled = master_.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - target.width()) / 2, (scaled.height() - target.height()) / 2, target.width(), target.height());
    QPixmap art = QPixmap::fromImage(scaled.copy(crop));
    art.setDevicePixelRatio(dpr);
    QPainterPath path;
    path.addRoundedRect(QRectF(local), 14, 14);
    p.save();
    p.setClipPath(path);
    p.drawPixmap(local.topLeft(), art);
    p.restore();
    // A hairline keeps very dark or very light covers from dissolving into the background.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(RoomStyle::Hairline(palette()), 1));
    p.drawRoundedRect(QRectF(local).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
  }
  p.end();

  sharp_ = pixmap;
  sharp_target_ = sleeve.size();
  sharp_generation_ = image_generation_;
  ++sharp_renders_;

}

void CoverArtWidget::SettleNow() {

  settle_timer_->stop();
  RenderSharp();
  update();

}

QRect CoverArtWidget::RecordBounds(const QRect &sleeve) const {

  const qreal r = sleeve.width() * 0.47;
  const qreal peek = std::max(0.0, std::min(sleeve.width() * 0.22, static_cast<qreal>(width() - sleeve.right() - 8)));
  const QPointF centre(sleeve.right() - r + peek, sleeve.center().y());
  return QRectF(centre.x() - r - 2, centre.y() - r - 2, r * 2 + 4, r * 2 + 4).toAlignedRect();

}

void CoverArtWidget::DrawRecord(QPainter *p, const QRect &sleeve, const qreal amount) const {

  const QPalette &pal = palette();
  const qreal r = sleeve.width() * 0.47;
  const qreal peek = std::max(0.0, std::min(sleeve.width() * 0.22, static_cast<qreal>(width() - sleeve.right() - 8)));
  const QPointF centre(sleeve.right() - r + peek * amount, sleeve.center().y());

  // The disc body (black vinyl and grooves) does not move, so it is rendered once per size into a pixmap and blitted. Only the sheen and the label turn.
  const qreal dpr = devicePixelRatioF();
  const int side = static_cast<int>(std::ceil(r * 2 + 4));
  if (record_cache_.isNull() || record_cache_side_ != side || record_cache_dpr_ != dpr) {
    QPixmap body(QSize(static_cast<int>(std::ceil(side * dpr)), static_cast<int>(std::ceil(side * dpr))));
    body.setDevicePixelRatio(dpr);
    body.fill(Qt::transparent);
    QPainter bp(&body);
    bp.setRenderHint(QPainter::Antialiasing);
    const QPointF c(side / 2.0, side / 2.0);
    bp.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.24), 1));
    bp.setBrush(RoomStyle::Mix(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.07), pal.color(QPalette::Shadow), 0.25));
    bp.drawEllipse(c, r, r);
    bp.setBrush(Qt::NoBrush);
    for (int i = 1; i <= 6; ++i) {
      const qreal gr = r * (0.40 + i * 0.09);
      bp.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), i % 2 ? 0.10 : 0.06), 1));
      bp.drawEllipse(c, gr, gr);
    }
    bp.end();
    record_cache_ = body;
    record_cache_side_ = side;
    record_cache_dpr_ = dpr;
  }
  p->save();
  p->setRenderHint(QPainter::Antialiasing);
  p->drawPixmap(QPointF(centre.x() - side / 2.0, centre.y() - side / 2.0), record_cache_);
  p->translate(centre);
  p->rotate(angle_);
  // The sheen and the label are what turn.
  QColor sheen = RoomStyle::Text(pal);
  sheen.setAlphaF(0.16);
  p->setPen(QPen(sheen, 3, Qt::SolidLine, Qt::RoundCap));
  p->drawArc(QRectF(-r * 0.86, -r * 0.86, r * 1.72, r * 1.72), 18 * 16, 34 * 16);
  p->drawArc(QRectF(-r * 0.86, -r * 0.86, r * 1.72, r * 1.72), 198 * 16, 34 * 16);
  p->setPen(Qt::NoPen);
  p->setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.62));
  p->drawEllipse(QPointF(0, 0), r * 0.30, r * 0.30);
  p->setBrush(RoomStyle::Window(pal));
  p->drawEllipse(QPointF(r * 0.12, 0), r * 0.03, r * 0.03);
  p->drawEllipse(QPointF(0, 0), r * 0.03, r * 0.03);
  p->restore();

}

void CoverArtWidget::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)

  const QRect sleeve = SleeveRect();
  if (sleeve.width() < 8) return;

  // A new cover (or a changed palette) is rendered at once. A changed size only stretches what is there; the sharp render follows when the size settles.
  if (sharp_.isNull() || sharp_generation_ != image_generation_) {
    RenderSharp();
  }
  else if (sharp_target_ != sleeve.size()) {
    ++stretched_paints_;
    if (!settle_timer_->isActive()) settle_timer_->start();
    else settle_timer_->start();  // restarted: only the last size of a drag is rendered
  }
  if (sharp_.isNull()) return;

  QPainter p(this);
  const int pad = ShadowPad();
  const QRect target(sleeve.left() - pad, sleeve.top() - pad, sleeve.width() + pad * 2, sleeve.height() + pad * 2);

  if (mode_ != Mode::Idle && record_->value() > 0.005) DrawRecord(&p, sleeve, record_->value());

  // At rest the pixmap is drawn one to one; while resizing it is scaled by the painter, which is a single cheap blit.
  p.setRenderHint(QPainter::SmoothPixmapTransform, sharp_target_ == sleeve.size());
  p.drawPixmap(target, sharp_);

  if (fading()) {
    p.setOpacity(1.0 - fade_->value());
    p.drawPixmap(target, outgoing_);
  }

}
