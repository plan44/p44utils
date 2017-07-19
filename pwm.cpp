//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44utils is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44utils is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44utils. If not, see <http://www.gnu.org/licenses/>.
//

#include "pwm.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "logger.hpp"
#include "mainloop.hpp"

using namespace p44;


// MARK: ===== PWM via modern kernel support

#define PWM_SYS_CLASS_PATH "/sys/class/pwm"


PWMPin::PWMPin(int aPwmChip, int aPwmChannel, bool aInverted, double aInitialValue, uint32_t aPeriodInNs) :
  pwmChip(aPwmChip),
  pwmChannel(aPwmChannel),
  inverted(aInverted),
  activeNs(0),
  periodNs(aPeriodInNs),
  pwmFD(-1)
{
  int tempFd;
  string name;
  string s = string_format("%d", pwmChannel);
  // have the kernel export the pwm channel
  name = string_format("%s/pwmchip%d/export", PWM_SYS_CLASS_PATH, pwmChip);
  tempFd = open(name.c_str(), O_WRONLY);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open PWM export file %s: %s", name.c_str(), strerror(errno)); return; }
  write(tempFd, s.c_str(), s.length());
  close(tempFd);
  // save base path
  string basePath = string_format("%s/pwmchip%d/pwm%d", PWM_SYS_CLASS_PATH, pwmChip, pwmChannel);
  // configure
  // - set polarity
  name = basePath + "/polarity";
  tempFd = open(name.c_str(), O_RDWR);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open PWM polarity file %s: %s", name.c_str(), strerror(errno)); return; }
  s = inverted ? "inverted" : "normal";
  write(tempFd, s.c_str(), s.length());
  close(tempFd);
  // - set period
  name = basePath + "/period";
  tempFd = open(name.c_str(), O_RDWR);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open PWM period file %s: %s", name.c_str(), strerror(errno)); return; }
  s = string_format("%u", aPeriodInNs);
  write(tempFd, s.c_str(), s.length());
  close(tempFd);
  // now keep the duty cycle FD open
  name = basePath + "/duty_cycle";
  pwmFD = open(name.c_str(), O_RDWR);
  if (pwmFD<0) { LOG(LOG_ERR, "Cannot open PWM duty_cycle file %s: %s", name.c_str(), strerror(errno)); return; }
  // - set the initial value
  setValue(aInitialValue);
  // - now enable
  name = basePath + "/enable";
  tempFd = open(name.c_str(), O_RDWR);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open PWM enable file %s: %s", name.c_str(), strerror(errno)); return; }
  s = "1";
  write(tempFd, s.c_str(), s.length());
  close(tempFd);
}


PWMPin::~PWMPin()
{
  if (pwmFD>0) {
    close(pwmFD);
  }
}


void PWMPin::setValue(double aValue)
{
  if (pwmFD<0) return; // non-existing pins cannot be set
  if (aValue<0) aValue = 0; // limit to min
  activeNs = periodNs*(aValue/100);
  if (activeNs>periodNs) activeNs = periodNs; // limit to max
  string s = string_format("%u", activeNs);
  write(pwmFD, s.c_str(), s.length());
}


double PWMPin::getValue()
{
  if (periodNs==0) return 0;
  return (double)activeNs/(double)periodNs*100;
}


//  Using PWMs with the sysfs interface
//  -----------------------------------
//
//  If CONFIG_SYSFS is enabled in your kernel configuration a simple sysfs
//  interface is provided to use the PWMs from userspace. It is exposed at
//  /sys/class/pwm/. Each probed PWM controller/chip will be exported as
//  pwmchipN, where N is the base of the PWM chip. Inside the directory you
//  will find:
//
//  npwm
//  The number of PWM channels this chip supports (read-only).
//
//  export
//  Exports a PWM channel for use with sysfs (write-only).
//
//  unexport
//  Unexports a PWM channel from sysfs (write-only).
//
//  The PWM channels are numbered using a per-chip index from 0 to npwm-1.
//
//  When a PWM channel is exported a pwmX directory will be created in the
//  pwmchipN directory it is associated with, where X is the number of the
//  channel that was exported. The following properties will then be available:
//
//  period
//  The total period of the PWM signal (read/write).
//  Value is in nanoseconds and is the sum of the active and inactive
//  time of the PWM.
//
//  duty_cycle
//  The active time of the PWM signal (read/write).
//  Value is in nanoseconds and must be less than the period.
//
//  polarity
//  Changes the polarity of the PWM signal (read/write).
//  Writes to this property only work if the PWM chip supports changing
//  the polarity. The polarity can only be changed if the PWM is not
//  enabled. Value is the string "normal" or "inversed".
//
//  enable
//  Enable/disable the PWM signal (read/write).
//
//  - 0 - disabled
//  - 1 - enabled
