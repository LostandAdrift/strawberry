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
#include <QClipboard>
#include <QDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "signalpathdialog.h"
#include "engine/signalpath.h"

namespace {

QLabel *MakeLabel(const bool selectable = true) {

  QLabel *label = new QLabel;
  // Tags and sink names are data, never markup.
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  if (selectable) label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  return label;

}

QLabel *MakeHeading(const QString &text) {

  QLabel *label = new QLabel(text);
  label->setTextFormat(Qt::PlainText);
  QFont font = label->font();
  font.setBold(true);
  font.setCapitalization(QFont::AllUppercase);
  font.setLetterSpacing(QFont::PercentageSpacing, 108);
  font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.85));
  label->setFont(font);
  // Quieter than the content, but only a little: still comfortably readable.
  QPalette pal = label->palette();
  QColor text_color = pal.color(QPalette::WindowText);
  const QColor window = pal.color(QPalette::Window);
  pal.setColor(QPalette::WindowText, QColor::fromRgbF(text_color.redF() * 0.75 + window.redF() * 0.25, text_color.greenF() * 0.75 + window.greenF() * 0.25, text_color.blueF() * 0.75 + window.blueF() * 0.25));
  label->setPalette(pal);
  return label;

}

}  // namespace

SignalPathDialog::SignalPathDialog(QWidget *parent)
    : QDialog(parent),
      scroll_(new QScrollArea),
      label_source_(MakeLabel()),
      label_tier_(MakeLabel()),
      label_source_summary_(MakeLabel()),
      label_output_(MakeLabel()),
      label_output_detail_(MakeLabel()),
      label_processing_(MakeLabel()),
      label_processing_detail_(MakeLabel()),
      label_caveats_(MakeLabel()),
      details_toggle_(new QToolButton),
      technical_(new QPlainTextEdit),
      copy_button_(new QPushButton(tr("Copy details"))),
      output_settings_button_(new QPushButton(tr("Output settings…"))),
      close_button_(new QPushButton(tr("Close"))) {

  setWindowTitle(tr("Audio signal path"));
  setMinimumWidth(420);

  // Content, inside a scroll area so the buttons below are always reachable.
  QWidget *content = new QWidget;
  QVBoxLayout *content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(4, 4, 8, 4);
  content_layout->setSpacing(6);

  QFont headline = label_source_->font();
  headline.setPointSizeF(headline.pointSizeF() * 1.7);
  headline.setBold(true);
  label_source_->setFont(headline);

  QHBoxLayout *source_row = new QHBoxLayout;
  source_row->addWidget(label_source_, 0, Qt::AlignBaseline);
  source_row->addWidget(label_tier_, 0, Qt::AlignBaseline);
  source_row->addStretch(1);
  label_tier_->setWordWrap(false);

  content_layout->addWidget(MakeHeading(tr("Source")));
  content_layout->addLayout(source_row);
  content_layout->addWidget(label_source_summary_);

  content_layout->addSpacing(6);
  content_layout->addWidget(MakeHeading(tr("Output")));
  QFont medium = label_output_->font();
  medium.setBold(true);
  medium.setPointSizeF(medium.pointSizeF() * 1.15);
  label_output_->setFont(medium);
  content_layout->addWidget(label_output_);
  content_layout->addWidget(label_output_detail_);

  content_layout->addSpacing(6);
  content_layout->addWidget(MakeHeading(tr("Processing in Adrift")));
  label_processing_->setFont(medium);
  content_layout->addWidget(label_processing_);
  content_layout->addWidget(label_processing_detail_);

  content_layout->addSpacing(6);
  content_layout->addWidget(label_caveats_);

  // Advanced text is collapsed by default.
  details_toggle_->setText(tr("Technical details"));
  details_toggle_->setCheckable(true);
  details_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  details_toggle_->setArrowType(Qt::RightArrow);
  details_toggle_->setAutoRaise(true);
  details_toggle_->setAccessibleDescription(tr("Shows or hides the full technical text"));
  content_layout->addWidget(details_toggle_, 0, Qt::AlignLeft);

  technical_->setReadOnly(true);
  technical_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  technical_->setLineWrapMode(QPlainTextEdit::NoWrap);
  technical_->setMinimumHeight(140);
  technical_->setAccessibleName(tr("Technical details"));
  technical_->setVisible(false);
  content_layout->addWidget(technical_);
  content_layout->addStretch(1);

  scroll_->setWidget(content);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(scroll_, 1);

  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->addWidget(copy_button_);
  buttons->addWidget(output_settings_button_);
  buttons->addStretch(1);
  buttons->addWidget(close_button_);
  layout->addLayout(buttons);

  close_button_->setDefault(true);
  QObject::connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
  QObject::connect(copy_button_, &QPushButton::clicked, this, &SignalPathDialog::CopyDetails);
  QObject::connect(output_settings_button_, &QPushButton::clicked, this, &SignalPathDialog::OutputSettingsRequested);
  QObject::connect(details_toggle_, &QToolButton::toggled, this, &SignalPathDialog::SetDetailsExpanded);

}

void SignalPathDialog::SetDetailsExpanded(const bool expanded) {

  details_toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
  technical_->setVisible(expanded);

}

QSize SignalPathDialog::sizeHint() const {

  const QSize base = QDialog::sizeHint();
  return QSize(std::max(base.width(), 460), std::min(base.height(), kMaximumPreferredHeight));

}

void SignalPathDialog::SetReport(const SignalPath::Report &report) {

  label_source_->setText(report.source_text);
  label_tier_->setText(SignalPath::TierText(report.tier));
  label_source_summary_->setText(report.source_summary);

  label_output_->setText(report.output_text);
  label_output_detail_->setText(report.output_detail + QLatin1Char('\n') + report.output_verdict);

  if (report.processing_active.isEmpty()) {
    label_processing_->setText(tr("App DSP off"));
    label_processing_detail_->setText(report.processing_off.isEmpty() ? QString() : tr("Not active: %1.").arg(report.processing_off.join(QStringLiteral(", "))));
  }
  else {
    label_processing_->setText(tr("App DSP on"));
    QStringList active;
    for (const auto &row : report.processing_active) active << QStringLiteral("• %1: %2").arg(row.first, row.second);
    QString detail = active.join(QLatin1Char('\n'));
    if (!report.processing_off.isEmpty()) detail += QLatin1Char('\n') + tr("Not active: %1.").arg(report.processing_off.join(QStringLiteral(", ")));
    label_processing_detail_->setText(detail);
  }

  label_caveats_->setText(report.caveats.join(QLatin1Char('\n')));
  label_caveats_->setVisible(!report.caveats.isEmpty());

  technical_->setPlainText(report.technical_text);

}

QString SignalPathDialog::technical_text() const {
  return technical_->toPlainText();
}

void SignalPathDialog::CopyDetails() {
  // Copies the full technical text even while it is collapsed.
  QApplication::clipboard()->setText(technical_->toPlainText());
}
