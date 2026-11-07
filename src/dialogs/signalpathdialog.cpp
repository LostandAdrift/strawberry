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

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "signalpathdialog.h"
#include "engine/signalpath.h"

SignalPathDialog::SignalPathDialog(QWidget *parent)
    : QDialog(parent),
      sections_layout_(new QVBoxLayout),
      label_dsp_(new QLabel),
      label_output_(new QLabel),
      label_caveats_(new QLabel),
      technical_(new QPlainTextEdit),
      copy_button_(new QPushButton(tr("Copy details"))),
      output_settings_button_(new QPushButton(tr("Output settings…"))),
      close_button_(new QPushButton(tr("Close"))) {

  setWindowTitle(tr("Audio signal path"));
  setMinimumWidth(460);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setSpacing(10);

  // Two separate statements: what Adrift does to the samples, and what is known about the output.
  for (QLabel *label : { label_dsp_, label_output_, label_caveats_ }) {
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  }
  QFont bold = label_dsp_->font();
  bold.setBold(true);
  label_dsp_->setFont(bold);
  label_output_->setFont(bold);
  layout->addWidget(label_dsp_);
  layout->addWidget(label_output_);

  layout->addLayout(sections_layout_);
  layout->addWidget(label_caveats_);

  technical_->setReadOnly(true);
  technical_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  technical_->setLineWrapMode(QPlainTextEdit::NoWrap);
  technical_->setMinimumHeight(110);
  technical_->setAccessibleName(tr("Technical details"));
  layout->addWidget(technical_, 1);

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

}

QWidget *SignalPathDialog::BuildSection(const QString &title, const SignalPath::Report::Rows &rows) {

  QGroupBox *box = new QGroupBox(title);
  QFormLayout *form = new QFormLayout(box);
  form->setLabelAlignment(Qt::AlignRight);
  for (const auto &row : rows) {
    QLabel *value = new QLabel(row.second);
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    form->addRow(row.first + QLatin1Char(':'), value);
  }
  return box;

}

void SignalPathDialog::SetReport(const SignalPath::Report &report) {

  qDeleteAll(section_widgets_);
  section_widgets_.clear();

  label_dsp_->setText(report.dsp_verdict);
  label_output_->setText(report.output_verdict);
  label_caveats_->setText(report.caveats.join(QLatin1Char('\n')));
  label_caveats_->setVisible(!report.caveats.isEmpty());

  const QList<QPair<QString, SignalPath::Report::Rows>> sections = {
    { tr("Source"), report.source_rows },
    { tr("Processing"), report.processing_rows },
    { tr("Output"), report.output_rows },
  };
  for (const auto &section : sections) {
    QWidget *widget = BuildSection(section.first, section.second);
    sections_layout_->addWidget(widget);
    section_widgets_ << widget;
  }

  technical_->setPlainText(report.technical_text);

}

QString SignalPathDialog::technical_text() const {
  return technical_->toPlainText();
}

void SignalPathDialog::CopyDetails() {
  QApplication::clipboard()->setText(technical_->toPlainText());
}
