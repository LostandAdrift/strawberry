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

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "directalsa.h"

using namespace Qt::Literals::StringLiterals;

namespace DirectAlsa {

namespace {

QString Tr(const char *text) {
  return QCoreApplication::translate("DirectAlsa", text);
}

QString OnOff(const bool value) {
  return value ? Tr("on") : Tr("off");
}

void AddBoolChange(QStringList &changes, const QString &name, const bool before, const bool after) {
  if (before != after) changes << QStringLiteral("%1: %2 → %3").arg(name, OnOff(before), OnOff(after));
}

}  // namespace

bool Settings::operator==(const Settings &other) const {

  return output == other.output && device == other.device && replaygain == other.replaygain && ebur128 == other.ebur128 && bs2b == other.bs2b &&
         channels_forced == other.channels_forced && fadeout == other.fadeout && crossfade == other.crossfade && autocrossfade == other.autocrossfade && fadeout_pause == other.fadeout_pause;

}

bool IsHardwareDevice(const QString &device) {
  return device.trimmed().startsWith("hw:"_L1, Qt::CaseInsensitive);
}

Plan MakePlan(const Settings &current, const QString &hw_device) {

  Plan plan;
  plan.before = current;

  if (hw_device.trimmed().isEmpty()) {
    plan.error = Tr("Choose your audio device first.");
    return plan;
  }
  if (!IsHardwareDevice(hw_device)) {
    plan.error = Tr("Direct output needs a raw hardware device (its name starts with hw:). plughw:, default and sound-server devices can convert or mix.");
    return plan;
  }

  plan.after = current;
  plan.after.output = u"alsasink"_s;
  plan.after.device = hw_device.trimmed();
  plan.after.replaygain = false;
  plan.after.ebur128 = false;
  plan.after.bs2b = false;
  plan.after.channels_forced = false;
  plan.after.fadeout = false;
  plan.after.crossfade = false;
  plan.after.autocrossfade = false;
  plan.after.fadeout_pause = false;

  if (current.output != plan.after.output) plan.changes << Tr("Output: %1 → %2").arg(current.output.isEmpty() ? Tr("automatic") : current.output, plan.after.output);
  if (current.device != plan.after.device) plan.changes << Tr("Device: %1 → %2").arg(current.device.isEmpty() ? Tr("automatic") : current.device, plan.after.device);
  AddBoolChange(plan.changes, Tr("ReplayGain"), current.replaygain, false);
  AddBoolChange(plan.changes, Tr("Loudness normalization"), current.ebur128, false);
  AddBoolChange(plan.changes, Tr("Crossfeed"), current.bs2b, false);
  AddBoolChange(plan.changes, Tr("Forced channel count"), current.channels_forced, false);
  AddBoolChange(plan.changes, Tr("Fade out on stop"), current.fadeout, false);
  AddBoolChange(plan.changes, Tr("Crossfade on track change"), current.crossfade, false);
  AddBoolChange(plan.changes, Tr("Automatic crossfade"), current.autocrossfade, false);
  AddBoolChange(plan.changes, Tr("Fade on pause"), current.fadeout_pause, false);

  plan.valid = true;
  return plan;

}

QStringList Warnings(const bool volume_control, const int volume_percent, const bool equalizer_enabled, const bool stereo_balancer_enabled) {

  QStringList warnings;

  warnings << Tr("Adrift will not change your volume. Before you play, set your amplifier or DAC volume low, then raise it slowly: a direct hardware connection can be much louder than a sound server.");

  if (volume_control && volume_percent < 100) {
    warnings << Tr("Software volume is at %1%. Anything below 100% changes the samples. Raise it to 100% yourself and control loudness at the DAC or amplifier.").arg(volume_percent);
  }
  if (!volume_control) {
    warnings << Tr("Volume control is turned off, so playback is always at the full level of the file.");
  }
  if (equalizer_enabled) warnings << Tr("The equalizer is on and changes the samples. Turn it off in the equalizer window.");
  if (stereo_balancer_enabled) warnings << Tr("Stereo balance is on and changes the samples. Turn it off in the equalizer window.");

  warnings << Tr("A hw: device is usually exclusive: other programs cannot play sound at the same time, and the device may refuse sample rates or formats it does not support.");

  return warnings;

}

QString BackupToJson(const Settings &settings) {

  QJsonObject object;
  object[u"output"_s] = settings.output;
  object[u"device"_s] = settings.device;
  object[u"replaygain"_s] = settings.replaygain;
  object[u"ebur128"_s] = settings.ebur128;
  object[u"bs2b"_s] = settings.bs2b;
  object[u"channels_forced"_s] = settings.channels_forced;
  object[u"fadeout"_s] = settings.fadeout;
  object[u"crossfade"_s] = settings.crossfade;
  object[u"autocrossfade"_s] = settings.autocrossfade;
  object[u"fadeout_pause"_s] = settings.fadeout_pause;
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));

}

Settings BackupFromJson(const QString &json, bool *ok) {

  if (ok) *ok = false;
  Settings settings;

  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return settings;

  const QJsonObject object = document.object();
  // All keys must be present: a partial backup would silently restore the wrong thing.
  for (const QString &key : { u"output"_s, u"device"_s, u"replaygain"_s, u"ebur128"_s, u"bs2b"_s, u"channels_forced"_s, u"fadeout"_s, u"crossfade"_s, u"autocrossfade"_s, u"fadeout_pause"_s }) {
    if (!object.contains(key)) return Settings();
  }

  settings.output = object[u"output"_s].toString();
  settings.device = object[u"device"_s].toString();
  settings.replaygain = object[u"replaygain"_s].toBool();
  settings.ebur128 = object[u"ebur128"_s].toBool();
  settings.bs2b = object[u"bs2b"_s].toBool();
  settings.channels_forced = object[u"channels_forced"_s].toBool();
  settings.fadeout = object[u"fadeout"_s].toBool();
  settings.crossfade = object[u"crossfade"_s].toBool();
  settings.autocrossfade = object[u"autocrossfade"_s].toBool();
  settings.fadeout_pause = object[u"fadeout_pause"_s].toBool();
  if (ok) *ok = true;
  return settings;

}

}  // namespace DirectAlsa
