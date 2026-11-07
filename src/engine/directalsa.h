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

#ifndef DIRECTALSA_H
#define DIRECTALSA_H

#include <QMetaType>
#include <QString>
#include <QStringList>

// Planning for the "Direct ALSA setup" helper.
//
// The helper only ever changes how Adrift routes and processes audio: output sink, device, and the optional software processing options.
// It never changes the volume level, never turns volume control off (which would play at full level) and never picks a device for the user:
// a hardware device (hw:) must be chosen explicitly before a plan exists. The previous values are kept so they can be restored.
namespace DirectAlsa {

// The subset of the Backend settings the helper may touch.
struct Settings {
  QString output;
  QString device;
  bool replaygain = false;
  bool ebur128 = false;
  bool bs2b = false;
  bool channels_forced = false;
  bool fadeout = false;
  bool crossfade = false;
  bool autocrossfade = false;
  bool fadeout_pause = false;

  bool operator==(const Settings &other) const;
};

struct Plan {
  bool valid = false;
  QString error;
  Settings before;
  Settings after;
  QStringList changes;  // Human readable "Option: old -> new" lines, only for things that actually change
};

// True for raw hardware devices ("hw:..."), false for plughw:, default, dmix, sound servers and everything else.
bool IsHardwareDevice(const QString &device);

// Builds the settings to apply for the chosen hardware device. Invalid (with an explanation) if the device is not a hw: device.
// `volume_percent` and `volume_control` are only read to produce the warning text, they are never modified.
Plan MakePlan(const Settings &current, const QString &hw_device);

// Notes the user must read before applying, derived from the current runtime state.
QStringList Warnings(const bool volume_control, const int volume_percent, const bool equalizer_enabled, const bool stereo_balancer_enabled);

// Serialization of a backup of the previous settings (JSON). An invalid backup yields a Settings with empty output and ok == false.
QString BackupToJson(const Settings &settings);
Settings BackupFromJson(const QString &json, bool *ok = nullptr);

}  // namespace DirectAlsa

Q_DECLARE_METATYPE(DirectAlsa::Plan)

#endif  // DIRECTALSA_H
