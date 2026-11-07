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

#include <QAbstractButton>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QString>

#include "signalpathwidget.h"
#include "engine/signalpath.h"

namespace {

QColor Mix(const QColor &a, const QColor &b, const qreal amount_of_b) {

  return QColor::fromRgbF(static_cast<float>(a.redF() * (1.0 - amount_of_b) + b.redF() * amount_of_b),
                          static_cast<float>(a.greenF() * (1.0 - amount_of_b) + b.greenF() * amount_of_b),
                          static_cast<float>(a.blueF() * (1.0 - amount_of_b) + b.blueF() * amount_of_b));

}

}  // namespace

SignalPathWidget::SignalPathWidget(QWidget *parent) : QAbstractButton(parent), has_report_(false) {

  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setAccessibleName(tr("Audio signal path"));
  setVisible(false);

}

void SignalPathWidget::SetReport(const SignalPath::Report &report) {

  report_ = report;
  has_report_ = true;

  setText(report.badge + QStringLiteral(", ") + report.dsp_text);
  setToolTip(report.description.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
  setAccessibleName(tr("Audio signal path: %1").arg(text()));
  setAccessibleDescription(report.verdict);
  updateGeometry();
  setVisible(true);
  update();

}

void SignalPathWidget::Clear() {

  has_report_ = false;
  report_ = SignalPath::Report();
  setText(QString());
  setToolTip(QString());
  setAccessibleDescription(QString());
  setVisible(false);
  updateGeometry();

}

int SignalPathWidget::HorizontalPadding() const {
  return std::max(8, fontMetrics().height() / 2);
}

int SignalPathWidget::DotDiameter() const {
  return std::max(8, fontMetrics().height() / 2);
}

int SignalPathWidget::ChipWidth() const {
  if (!has_report_) return 0;
  // Measured with the bold font so the chip has the same size whether or not app DSP is on and its text never overflows.
  QFont bold = font();
  bold.setBold(true);
  return QFontMetrics(bold).horizontalAdvance(report_.dsp_text) + HorizontalPadding() * 2;
}

int SignalPathWidget::MainTextAvailableWidth() const {

  const int fixed = HorizontalPadding() + DotDiameter() + HorizontalPadding() + HorizontalPadding() + ChipWidth() + HorizontalPadding() / 2;
  return std::max(0, width() - fixed);

}

QString SignalPathWidget::ElidedMainText() const {

  if (!has_report_) return QString();
  return fontMetrics().elidedText(report_.badge, Qt::ElideRight, MainTextAvailableWidth());

}

QSize SignalPathWidget::sizeHint() const {

  if (!has_report_) return QSize(0, 0);
  const QFontMetrics fm = fontMetrics();
  const int text_width = std::min(fm.horizontalAdvance(report_.badge), kMaximumTextWidth);
  const int width = HorizontalPadding() + DotDiameter() + HorizontalPadding() + text_width + HorizontalPadding() + ChipWidth() + HorizontalPadding() / 2;
  return QSize(width, fm.height() + 10);

}

QSize SignalPathWidget::minimumSizeHint() const {

  if (!has_report_) return QSize(0, 0);
  const QFontMetrics fm = fontMetrics();
  const int width = HorizontalPadding() + DotDiameter() + HorizontalPadding() + kMinimumTextWidth + HorizontalPadding() + ChipWidth() + HorizontalPadding() / 2;
  return QSize(width, fm.height() + 10);

}

void SignalPathWidget::changeEvent(QEvent *e) {

  QAbstractButton::changeEvent(e);
  if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange) updateGeometry();

}

void SignalPathWidget::paintEvent(QPaintEvent *e) {

  Q_UNUSED(e)
  if (!has_report_) return;

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QPalette &pal = palette();
  const bool enabled = isEnabled();
  const QColor window = pal.color(QPalette::Window);
  const QColor text = pal.color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
  const QColor highlight = pal.color(QPalette::Highlight);

  // The pill is a subtle lift from the window color; text keeps the full contrast of WindowText on Window.
  QColor fill = Mix(window, text, 0.07);
  if (underMouse() || isDown()) fill = Mix(window, text, 0.14);
  const QColor border = Mix(window, text, hasFocus() ? 0.55 : 0.22);

  const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  const qreal radius = pill.height() / 2.0;
  p.setPen(QPen(hasFocus() ? highlight : border, hasFocus() ? 2.0 : 1.0));
  p.setBrush(fill);
  p.drawRoundedRect(pill, radius, radius);

  const int pad = HorizontalPadding();
  const int dot = DotDiameter();

  // Quality marker: filled for lossless/hi-res/DSD, ring for lossy, dotted ring for unknown. The text carries the same information.
  const QRectF dot_rect(pad, (height() - dot) / 2.0, dot, dot);
  QColor marker = highlight;
  if (report_.tier == SignalPath::Tier::Lossy || report_.tier == SignalPath::Tier::Unknown) marker = Mix(window, text, 0.65);
  p.setBrush(Qt::NoBrush);
  QPen marker_pen(marker, 1.6);
  if (report_.tier == SignalPath::Tier::Unknown) marker_pen.setStyle(Qt::DotLine);
  p.setPen(marker_pen);
  if (report_.tier == SignalPath::Tier::Lossless || report_.tier == SignalPath::Tier::HiRes || report_.tier == SignalPath::Tier::DSD) {
    p.setBrush(marker);
    p.setPen(Qt::NoPen);
  }
  p.drawEllipse(dot_rect.adjusted(1, 1, -1, -1));

  // Main text (elided)
  const int text_left = pad + dot + pad;
  const int chip_width = ChipWidth();
  const QRect text_rect(text_left, 0, std::max(0, width() - text_left - chip_width - pad / 2 - pad), height());
  p.setPen(text);
  p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(report_.badge, Qt::ElideRight, text_rect.width()));

  // DSP chip. When app DSP is on it uses the accent so it is noticeable; the words still say it, color is not the only cue.
  const QRectF chip(width() - chip_width - pad / 2.0, 3, chip_width, height() - 6);
  QColor chip_fill = report_.app_dsp_active ? Mix(window, highlight, 0.30) : Mix(window, text, 0.05);
  p.setPen(Qt::NoPen);
  p.setBrush(chip_fill);
  p.drawRoundedRect(chip, chip.height() / 2.0, chip.height() / 2.0);
  QFont chip_font = font();
  chip_font.setBold(report_.app_dsp_active);
  p.setFont(chip_font);
  p.setPen(report_.app_dsp_active ? text : Mix(window, text, 0.8));
  p.drawText(chip, Qt::AlignCenter, report_.dsp_text);

}
