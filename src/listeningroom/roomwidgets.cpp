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

#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "roomwidgets.h"
#include "roomstyle.h"

// ------------------------------------------------------------------------------------------------------------ button

RoomButton::RoomButton(const QString &text, const bool primary, QWidget *parent)
    : QPushButton(text, parent), primary_(primary), hover_(new RoomTween(this, 0.0, 150)), press_(new RoomTween(this, 0.0, 90)) {

  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  QObject::connect(hover_, &RoomTween::Changed, this, [this]() { update(); });
  QObject::connect(press_, &RoomTween::Changed, this, [this]() { update(); });

}

void RoomButton::SetPrimary(const bool primary) {

  if (primary_ == primary) return;
  primary_ = primary;
  update();

}

double RoomButton::hover_amount() const {
  return hover_->value();
}

double RoomButton::press_amount() const {
  return press_->value();
}

QSize RoomButton::sizeHint() const {

  const QFontMetrics fm(font());
  return QSize(fm.horizontalAdvance(text()) + fm.height() * 2, std::max(32, fm.height() + 14));

}

void RoomButton::enterEvent(QEnterEvent *e) {

  QPushButton::enterEvent(e);
  if (isEnabled()) hover_->SetTarget(1.0);

}

void RoomButton::leaveEvent(QEvent *e) {

  QPushButton::leaveEvent(e);
  hover_->SetTarget(0.0);
  press_->SetTarget(0.0);

}

void RoomButton::mousePressEvent(QMouseEvent *e) {

  QPushButton::mousePressEvent(e);
  if (isDown()) press_->SetTarget(1.0);

}

void RoomButton::mouseReleaseEvent(QMouseEvent *e) {

  QPushButton::mouseReleaseEvent(e);
  press_->SetTarget(0.0);

}

void RoomButton::focusInEvent(QFocusEvent *e) {
  QPushButton::focusInEvent(e);
  update();
}

void RoomButton::focusOutEvent(QFocusEvent *e) {
  QPushButton::focusOutEvent(e);
  update();
}

void RoomButton::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  const bool on = isEnabled();
  const qreal hover = on ? hover_->value() : 0.0;
  const qreal press = on ? press_->value() : 0.0;
  // A press sinks the pill by a pixel on every side.
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1).adjusted(press * 1.2, press * 1.2, -press * 1.2, -press * 1.2);
  const qreal radius = r.height() / 2.0;

  QColor ink = on ? RoomStyle::Text(pal) : RoomStyle::Faint(pal);
  if (primary_) {
    QColor fill = RoomStyle::Accent(pal);
    if (!on) fill = RoomStyle::Mix(RoomStyle::Window(pal), fill, 0.3);
    else {
      fill = RoomStyle::Mix(fill, RoomStyle::Text(pal), 0.14 * hover);
      fill = RoomStyle::Mix(fill, RoomStyle::Window(pal), 0.22 * press);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(r, radius, radius);
    ink = RoomStyle::AccentText(pal);
  }
  else if (isCheckable() && isChecked()) {
    // A chosen option (the service switch): tinted with the accent, like the highlight of the segmented control.
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.5), 1));
    p.setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.28 + 0.06 * hover));
    p.drawRoundedRect(r, radius, radius);
  }
  else {
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.24 + 0.2 * hover), 1));
    p.setBrush(RoomStyle::Surface(pal, 0.07 * std::min(1.0, hover + press * 0.5)));
    p.drawRoundedRect(r, radius, radius);
  }
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.85), 2));
    p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), radius + 1, radius + 1);
  }
  p.setPen(ink);
  p.setFont(RoomStyle::Scaled(font(), 1.0, true));
  p.drawText(rect(), Qt::AlignCenter, text());

}

// ------------------------------------------------------------------------------------------------------------ labels

void ToneLabel::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  QColor color = RoomStyle::Text(palette());
  switch (tone_) {
    case ElidedLabel::Tone::Muted:  color = RoomStyle::Muted(palette()); break;
    case ElidedLabel::Tone::Faint:  color = RoomStyle::Faint(palette()); break;
    case ElidedLabel::Tone::Accent: color = RoomStyle::Accent(palette()); break;
    case ElidedLabel::Tone::Normal: break;
  }
  p.setPen(color);
  p.setFont(font());
  int flags = static_cast<int>(alignment());
  if (wordWrap()) flags |= Qt::TextWordWrap;
  p.drawText(contentsRect(), flags, text());

}

QLabel *MakeLabel(const QString &text, const bool word_wrap) {

  ToneLabel *label = new ToneLabel(text);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(word_wrap);
  return label;

}

void StyleLabel(QLabel *label, const QFont &font, const ElidedLabel::Tone tone) {

  label->setFont(font);
  static_cast<ToneLabel*>(label)->SetTone(tone);

}

QLabel *AddPageHeader(QVBoxLayout *layout, const QFont &base, const QString &title, const QString &description, QWidget *trailing) {

  QHBoxLayout *row = new QHBoxLayout;
  row->setSpacing(16);
  QVBoxLayout *titles = new QVBoxLayout;
  titles->setSpacing(2);
  QLabel *title_label = MakeLabel(title, false);
  title_label->setObjectName(QStringLiteral("page_title"));
  StyleLabel(title_label, RoomStyle::Scaled(base, 1.9, true), ElidedLabel::Tone::Normal);
  QLabel *description_label = MakeLabel(description, false);
  description_label->setObjectName(QStringLiteral("page_description"));
  StyleLabel(description_label, base, ElidedLabel::Tone::Muted);
  titles->addWidget(title_label);
  titles->addWidget(description_label);
  row->addLayout(titles, 1);
  if (trailing) row->addWidget(trailing, 0, Qt::AlignTop);
  layout->addLayout(row);
  layout->addSpacing(14);
  return title_label;

}

// ------------------------------------------------------------------------------------------------------------ search field

RoomSearchField::RoomSearchField(QWidget *parent) : QWidget(parent), edit_(new QLineEdit(this)), focus_(new RoomTween(this, 0.0, 160)) {

  edit_->setFrame(false);
  edit_->setClearButtonEnabled(true);
  edit_->setPlaceholderText(tr("Search albums, artists, tracks"));
  edit_->setAccessibleName(tr("Search the library"));
  edit_->installEventFilter(this);
  setFocusProxy(edit_);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  ApplyEditPalette();
  QObject::connect(edit_, &QLineEdit::textChanged, this, &RoomSearchField::TextChanged);
  QObject::connect(edit_, &QLineEdit::returnPressed, this, &RoomSearchField::Submitted);
  QObject::connect(focus_, &RoomTween::Changed, this, [this]() { update(); });

}

QString RoomSearchField::text() const {
  return edit_->text();
}

void RoomSearchField::SetText(const QString &text) {
  edit_->setText(text);
}

void RoomSearchField::SetPlaceholder(const QString &text) {
  edit_->setPlaceholderText(text);
}

QSize RoomSearchField::sizeHint() const {
  return QSize(300, std::max(34, fontMetrics().height() + 16));
}

QSize RoomSearchField::minimumSizeHint() const {
  return QSize(160, sizeHint().height());
}

QRect RoomSearchField::EditRect() const {
  return QRect(34, 4, std::max(0, width() - 34 - 8), std::max(0, height() - 8));
}

void RoomSearchField::resizeEvent(QResizeEvent *e) {

  QWidget::resizeEvent(e);
  edit_->setGeometry(EditRect());

}

void RoomSearchField::ApplyEditPalette() {

  // The line edit must not paint its own background: the pill does.
  applying_palette_ = true;
  QPalette pal = edit_->palette();
  pal.setColor(QPalette::Base, Qt::transparent);
  pal.setColor(QPalette::Text, RoomStyle::Text(QApplication::palette()));
  pal.setColor(QPalette::PlaceholderText, RoomStyle::Faint(QApplication::palette()));
  edit_->setPalette(pal);
  applying_palette_ = false;

}

void RoomSearchField::changeEvent(QEvent *e) {

  QWidget::changeEvent(e);
  if ((e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange) && !applying_palette_) {
    ApplyEditPalette();
    update();
  }

}

bool RoomSearchField::eventFilter(QObject *watched, QEvent *e) {

  if (watched == edit_) {
    if (e->type() == QEvent::FocusIn) focus_->SetTarget(1.0);
    else if (e->type() == QEvent::FocusOut) focus_->SetTarget(0.0);
    else if (e->type() == QEvent::PaletteChange && !applying_palette_) ApplyEditPalette();
  }
  return QWidget::eventFilter(watched, e);

}

void RoomSearchField::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
  const qreal radius = r.height() / 2.0;
  const qreal focus = focus_->value();

  p.setBrush(RoomStyle::Surface(pal, 0.05 + 0.02 * focus));
  p.setPen(QPen(RoomStyle::Mix(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.2), RoomStyle::Accent(pal), focus), 1.0 + focus * 0.6));
  p.drawRoundedRect(r, radius, radius);

  // The magnifier.
  const QPointF centre(r.left() + 18, r.center().y() - 1);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(RoomStyle::Muted(pal), 1.6, Qt::SolidLine, Qt::RoundCap));
  p.drawEllipse(centre, 5.2, 5.2);
  p.drawLine(centre + QPointF(3.8, 3.8), centre + QPointF(8.0, 8.0));

}

// ------------------------------------------------------------------------------------------------------------ segmented control

RoomSegmented::RoomSegmented(const QStringList &labels, QWidget *parent) : QWidget(parent), labels_(labels), glide_(new RoomTween(this, 0.0, 220, QEasingCurve::OutCubic)) {

  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setAccessibleName(tr("View"));
  QObject::connect(glide_, &RoomTween::Changed, this, [this]() { update(); });

}

double RoomSegmented::highlight_position() const {
  return glide_->value();
}

int RoomSegmented::SegmentWidth() const {

  int widest = 0;
  const QFontMetrics fm(RoomStyle::Scaled(font(), 1.0, true));
  for (const QString &label : labels_) widest = std::max(widest, fm.horizontalAdvance(label));
  return widest + fm.height() * 2;

}

QSize RoomSegmented::sizeHint() const {
  return QSize(SegmentWidth() * static_cast<int>(labels_.count()) + 8, std::max(34, fontMetrics().height() + 16));
}

void RoomSegmented::SetCurrent(const int index) {

  if (index < 0 || index >= labels_.count() || index == current_) return;
  current_ = index;
  glide_->SetTarget(index);
  update();
  Q_EMIT CurrentChanged(index);

}

void RoomSegmented::mousePressEvent(QMouseEvent *e) {

  const int segment = SegmentWidth();
  if (segment <= 0) return;
  SetCurrent(std::clamp((static_cast<int>(e->position().x()) - 4) / segment, 0, static_cast<int>(labels_.count()) - 1));

}

void RoomSegmented::keyPressEvent(QKeyEvent *e) {

  if (e->key() == Qt::Key_Left) SetCurrent(current_ - 1);
  else if (e->key() == Qt::Key_Right) SetCurrent(current_ + 1);
  else QWidget::keyPressEvent(e);

}

void RoomSegmented::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette &pal = palette();
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
  const qreal radius = r.height() / 2.0;
  p.setPen(QPen(RoomStyle::Hairline(pal), 1));
  p.setBrush(RoomStyle::Surface(pal, 0.04));
  p.drawRoundedRect(r, radius, radius);

  const int segment = SegmentWidth();
  const QRectF highlight(4 + glide_->value() * segment, r.top() + 3, segment, r.height() - 6);
  p.setPen(Qt::NoPen);
  p.setBrush(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Accent(pal), 0.28));
  p.drawRoundedRect(highlight, highlight.height() / 2, highlight.height() / 2);

  p.setFont(RoomStyle::Scaled(font(), 1.0, true));
  for (int i = 0; i < labels_.count(); ++i) {
    const QRectF cell(4 + i * segment, r.top(), segment, r.height());
    // Text over the highlight keeps full contrast: the nearer the highlight, the stronger the text.
    const qreal nearness = std::max(0.0, 1.0 - std::abs(glide_->value() - i));
    p.setPen(RoomStyle::Mix(RoomStyle::Muted(pal), RoomStyle::Text(pal), nearness));
    p.drawText(cell, Qt::AlignCenter, labels_.at(i));
  }
  if (hasFocus()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(RoomStyle::Mix(RoomStyle::Window(pal), RoomStyle::Text(pal), 0.8), 1.5));
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius, radius);
  }

}
