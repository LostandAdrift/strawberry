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

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "directalsadialog.h"
#include "engine/directalsa.h"

DirectAlsaDialog::DirectAlsaDialog(const QList<QPair<QString, QString>> &devices, const DirectAlsa::Settings &current, const QStringList &warnings, const bool has_backup, QWidget *parent)
    : QDialog(parent),
      current_(current),
      combo_device_(new QComboBox),
      changes_(new QPlainTextEdit),
      label_message_(new QLabel),
      button_apply_(new QPushButton(tr("Apply to settings"))),
      button_restore_(new QPushButton(tr("Restore previous settings"))),
      button_cancel_(new QPushButton(tr("Cancel"))) {

  setWindowTitle(tr("Direct ALSA setup"));
  setMinimumWidth(520);

  QVBoxLayout *layout = new QVBoxLayout(this);

  QLabel *intro = new QLabel(tr("Send audio straight to your DAC or sound card, with Adrift's own processing turned off. Nothing changes until you choose a device and press Apply, and you still have to press OK or Apply in Settings to save it."));
  intro->setWordWrap(true);
  layout->addWidget(intro);

  QHBoxLayout *device_row = new QHBoxLayout;
  QLabel *device_label = new QLabel(tr("Hardware device:"));
  device_label->setBuddy(combo_device_);
  device_row->addWidget(device_label);
  device_row->addWidget(combo_device_, 1);
  layout->addLayout(device_row);

  // The first entry is a prompt, not a device: the user must choose their hardware, Adrift never guesses.
  combo_device_->addItem(tr("Choose your device…"), QString());
  for (const auto &device : devices) {
    if (DirectAlsa::IsHardwareDevice(device.second)) combo_device_->addItem(device.first, device.second);
  }

  QLabel *warnings_title = new QLabel(tr("Before you start"));
  QFont bold = warnings_title->font();
  bold.setBold(true);
  warnings_title->setFont(bold);
  layout->addWidget(warnings_title);
  for (const QString &warning : warnings) {
    QLabel *label = new QLabel(QStringLiteral("• ") + warning);
    label->setWordWrap(true);
    layout->addWidget(label);
  }

  QLabel *changes_title = new QLabel(tr("What will change"));
  changes_title->setFont(bold);
  layout->addWidget(changes_title);
  changes_->setReadOnly(true);
  changes_->setMaximumHeight(130);
  changes_->setAccessibleName(tr("Planned changes"));
  layout->addWidget(changes_);

  label_message_->setWordWrap(true);
  layout->addWidget(label_message_);

  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->addWidget(button_restore_);
  buttons->addStretch(1);
  buttons->addWidget(button_apply_);
  buttons->addWidget(button_cancel_);
  layout->addLayout(buttons);

  button_restore_->setEnabled(has_backup);
  button_restore_->setToolTip(has_backup ? tr("Put back the output, device and processing options you had before using this helper") : tr("No previous settings have been saved by this helper"));
  button_cancel_->setDefault(true);

  QObject::connect(combo_device_, &QComboBox::currentIndexChanged, this, &DirectAlsaDialog::DeviceChanged);
  QObject::connect(button_apply_, &QPushButton::clicked, this, &DirectAlsaDialog::Apply);
  QObject::connect(button_restore_, &QPushButton::clicked, this, [this]() {
    Q_EMIT RestoreRequested();
    accept();
  });
  QObject::connect(button_cancel_, &QPushButton::clicked, this, &QDialog::reject);

  if (devices.isEmpty() || combo_device_->count() <= 1) {
    label_message_->setText(tr("No hardware (hw:) ALSA devices were found. Connect your DAC and reopen Settings."));
  }
  DeviceChanged();

}

void DirectAlsaDialog::DeviceChanged() {

  const QString device = combo_device_->currentData().toString();
  plan_ = DirectAlsa::MakePlan(current_, device);

  if (!plan_.valid) {
    changes_->setPlainText(tr("Choose your device to see what will change."));
    button_apply_->setEnabled(false);
    return;
  }

  changes_->setPlainText(plan_.changes.isEmpty() ? tr("Nothing needs to change: these settings are already direct.") : plan_.changes.join(QLatin1Char('\n')));
  label_message_->setText(tr("The volume level is not changed."));
  button_apply_->setEnabled(true);

}

void DirectAlsaDialog::Apply() {

  if (!plan_.valid) return;
  Q_EMIT ApplyRequested(plan_);
  accept();

}
