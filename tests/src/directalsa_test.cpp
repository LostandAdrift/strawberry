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

#include "gtest_include.h"

#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>

#include "dialogs/directalsadialog.h"
#include "engine/directalsa.h"

using namespace DirectAlsa;

namespace {

Settings Busy() {
  Settings s;
  s.output = QStringLiteral("pulsesink");
  s.device = QStringLiteral("alsa_output.pci-0000_00_1f.3.analog-stereo");
  s.replaygain = true;
  s.ebur128 = false;
  s.bs2b = true;
  s.channels_forced = true;
  s.fadeout = true;
  s.crossfade = true;
  s.autocrossfade = false;
  s.fadeout_pause = true;
  return s;
}

QList<QPair<QString, QString>> Devices() {
  return {
    { QStringLiteral("Example DAC (hw)"), QStringLiteral("hw:CARD=Example,DEV=0") },
    { QStringLiteral("Example DAC (plughw)"), QStringLiteral("plughw:CARD=Example,DEV=0") },
    { QStringLiteral("Default"), QStringLiteral("default") },
    { QStringLiteral("Onboard (hw)"), QStringLiteral("hw:CARD=PCH,DEV=0") },
  };
}

}  // namespace

TEST(DirectAlsaTest, OnlyRawHardwareDevicesCount) {

  EXPECT_TRUE(IsHardwareDevice(QStringLiteral("hw:CARD=X,DEV=0")));
  EXPECT_TRUE(IsHardwareDevice(QStringLiteral("HW:0,0")));
  EXPECT_FALSE(IsHardwareDevice(QStringLiteral("plughw:0,0")));
  EXPECT_FALSE(IsHardwareDevice(QStringLiteral("default")));
  EXPECT_FALSE(IsHardwareDevice(QStringLiteral("dmix:CARD=X")));
  EXPECT_FALSE(IsHardwareDevice(QString()));

}

TEST(DirectAlsaTest, NoPlanWithoutAnExplicitHardwareChoice) {

  Plan empty = MakePlan(Busy(), QString());
  EXPECT_FALSE(empty.valid);
  EXPECT_FALSE(empty.error.isEmpty());
  EXPECT_TRUE(empty.changes.isEmpty());

  EXPECT_FALSE(MakePlan(Busy(), QStringLiteral("plughw:0,0")).valid);
  EXPECT_FALSE(MakePlan(Busy(), QStringLiteral("default")).valid);

}

TEST(DirectAlsaTest, PlanSwitchesRoutingAndTurnsProcessingOff) {

  const Plan plan = MakePlan(Busy(), QStringLiteral("hw:CARD=Example,DEV=0"));
  ASSERT_TRUE(plan.valid);
  EXPECT_EQ(QStringLiteral("alsasink"), plan.after.output);
  EXPECT_EQ(QStringLiteral("hw:CARD=Example,DEV=0"), plan.after.device);
  EXPECT_FALSE(plan.after.replaygain);
  EXPECT_FALSE(plan.after.ebur128);
  EXPECT_FALSE(plan.after.bs2b);
  EXPECT_FALSE(plan.after.channels_forced);
  EXPECT_FALSE(plan.after.fadeout);
  EXPECT_FALSE(plan.after.crossfade);
  EXPECT_FALSE(plan.after.autocrossfade);
  EXPECT_FALSE(plan.after.fadeout_pause);

  // The previous values are kept untouched for restoring.
  EXPECT_TRUE(plan.before == Busy());
  // Every change is listed; unchanged options are not.
  const QString changes = plan.changes.join(QLatin1Char('\n'));
  EXPECT_TRUE(changes.contains(QStringLiteral("pulsesink")));
  EXPECT_TRUE(changes.contains(QStringLiteral("ReplayGain")));
  EXPECT_TRUE(changes.contains(QStringLiteral("Crossfeed")));
  EXPECT_FALSE(changes.contains(QStringLiteral("Automatic crossfade")));
  EXPECT_FALSE(changes.contains(QStringLiteral("Loudness normalization")));

}

TEST(DirectAlsaTest, AlreadyDirectSettingsProduceNoChanges) {

  Settings s;
  s.output = QStringLiteral("alsasink");
  s.device = QStringLiteral("hw:CARD=Example,DEV=0");
  const Plan plan = MakePlan(s, s.device);
  ASSERT_TRUE(plan.valid);
  EXPECT_TRUE(plan.changes.isEmpty());
  EXPECT_TRUE(plan.after == s);

}

TEST(DirectAlsaTest, PlanDoesNotCarryAnyVolumeSetting) {

  // The Settings struct has no volume or volume-control field at all, so a plan cannot change them.
  const Plan plan = MakePlan(Busy(), QStringLiteral("hw:0,0"));
  for (const QString &change : plan.changes) {
    EXPECT_FALSE(change.contains(QStringLiteral("olume"))) << change.toStdString();
  }

}

TEST(DirectAlsaTest, WarningsAboutVolumeAndExclusiveUse) {

  const QString text = Warnings(true, 55, false, false).join(QLatin1Char('\n'));
  EXPECT_TRUE(text.contains(QStringLiteral("will not change your volume")));
  EXPECT_TRUE(text.contains(QStringLiteral("55%")));
  EXPECT_TRUE(text.contains(QStringLiteral("exclusive")));
  EXPECT_FALSE(text.contains(QStringLiteral("equalizer is on")));

  const QString off = Warnings(false, 100, true, true).join(QLatin1Char('\n'));
  EXPECT_TRUE(off.contains(QStringLiteral("full level")));
  EXPECT_TRUE(off.contains(QStringLiteral("equalizer is on")));
  EXPECT_TRUE(off.contains(QStringLiteral("Stereo balance is on")));

  // At 100% with volume control on there is no software-volume warning.
  EXPECT_FALSE(Warnings(true, 100, false, false).join(QLatin1Char('\n')).contains(QStringLiteral("Software volume")));

}

TEST(DirectAlsaTest, BackupRoundTripsAndRejectsDamagedData) {

  bool ok = false;
  const Settings restored = BackupFromJson(BackupToJson(Busy()), &ok);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(restored == Busy());

  BackupFromJson(QString(), &ok);
  EXPECT_FALSE(ok);
  BackupFromJson(QStringLiteral("not json"), &ok);
  EXPECT_FALSE(ok);
  BackupFromJson(QStringLiteral("{\"output\":\"alsasink\"}"), &ok);  // partial backups are rejected
  EXPECT_FALSE(ok);
  BackupFromJson(QStringLiteral("[1,2]"), &ok);
  EXPECT_FALSE(ok);

}

// ------------------------------------------------------------------------------------------------------------------ dialog

TEST(DirectAlsaDialogTest, ApplyIsDisabledUntilAHardwareDeviceIsChosen) {

  DirectAlsaDialog dialog(Devices(), Busy(), Warnings(true, 80, false, false), false);

  // Only hw: devices are offered, plus the prompt entry.
  ASSERT_EQ(3, dialog.device_combo()->count());
  EXPECT_EQ(0, dialog.device_combo()->currentIndex());
  EXPECT_TRUE(dialog.device_combo()->currentData().toString().isEmpty());
  EXPECT_FALSE(dialog.apply_button()->isEnabled());
  EXPECT_FALSE(dialog.restore_button()->isEnabled());

  QSignalSpy applied(&dialog, &DirectAlsaDialog::ApplyRequested);
  dialog.apply_button()->click();
  EXPECT_EQ(0, applied.count());  // a disabled button cannot apply anything

  dialog.device_combo()->setCurrentIndex(1);
  EXPECT_TRUE(dialog.apply_button()->isEnabled());
  EXPECT_TRUE(dialog.changes_view()->toPlainText().contains(QStringLiteral("hw:CARD=Example,DEV=0")));
  EXPECT_TRUE(dialog.message_label()->text().contains(QStringLiteral("volume level is not changed")));

  dialog.apply_button()->click();
  ASSERT_EQ(1, applied.count());
  const Plan plan = applied.first().first().value<Plan>();
  EXPECT_TRUE(plan.valid);
  EXPECT_EQ(QStringLiteral("hw:CARD=Example,DEV=0"), plan.after.device);

}

TEST(DirectAlsaDialogTest, ReturningToThePromptDisablesApplyAgain) {

  DirectAlsaDialog dialog(Devices(), Busy(), QStringList(), false);
  dialog.device_combo()->setCurrentIndex(2);
  EXPECT_TRUE(dialog.apply_button()->isEnabled());
  dialog.device_combo()->setCurrentIndex(0);
  EXPECT_FALSE(dialog.apply_button()->isEnabled());

}

TEST(DirectAlsaDialogTest, RestoreIsOfferedOnlyWithABackupAndEmitsRequest) {

  DirectAlsaDialog dialog(Devices(), Busy(), QStringList(), true);
  EXPECT_TRUE(dialog.restore_button()->isEnabled());
  QSignalSpy restored(&dialog, &DirectAlsaDialog::RestoreRequested);
  dialog.restore_button()->click();
  EXPECT_EQ(1, restored.count());

}

TEST(DirectAlsaDialogTest, NoHardwareDevicesExplainsWhy) {

  DirectAlsaDialog dialog({ { QStringLiteral("Default"), QStringLiteral("default") } }, Busy(), QStringList(), false);
  EXPECT_EQ(1, dialog.device_combo()->count());
  EXPECT_FALSE(dialog.apply_button()->isEnabled());
  EXPECT_TRUE(dialog.message_label()->text().contains(QStringLiteral("No hardware")));

}
