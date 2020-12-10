//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#ifndef ESP_PLATFORM
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif
#include "logger.hpp"
#include "mainloop.hpp"

using namespace p44;


#ifdef ESP_PLATFORM

// MARK: - PWM via ESP32 LEDC PWM controller

PWMPin::PWMPin(int aPwmChip, int aPwmChannel, bool aInverted, double aInitialValue, uint32_t aPeriodInNs) :
  gpioNo((gpio_num_t)aPwmChip),
  ledcChannel((ledc_channel_t)aPwmChannel),
  inverted(aInverted),
  activeNs(0),
  periodNs(aPeriodInNs)
{
  esp_err_t ret;

  if (periodNs==0) periodNs = 200000; // 5kHz
  // timer params
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
    .duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty, 13bit is max for 5000 hz
    .timer_num = LEDC_TIMER_0,            // timer index
    .freq_hz = 5000,                      // frequency of PWM signal
    .clk_cfg = LEDC_AUTO_CLK              // Auto select the source clock
  };
  ledc_timer.freq_hz = 1e9/periodNs;
  // channel params
  ledc_channel_config_t ledc_channel = {
    .gpio_num   = GPIO_NUM_NC,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel    = LEDC_CHANNEL_0,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = LEDC_TIMER_0,
    .duty       = 0,
    .hpoint     = 0
  };
  ledc_channel.gpio_num = gpioNo;
  ledc_channel.channel = ledcChannel;
  ledc_channel.duty = aInitialValue/100*((1<<13)-1);
  // Set configuration of timer0 for high speed channels
  ret = ledc_timer_config(&ledc_timer);
  if (ret==ESP_OK) {
    ret = ledc_channel_config(&ledc_channel);
  }
  if (ret!=ESP_OK) {
    LOG(LOG_ERR,"LEDC PWM init error: %s", esp_err_to_name(ret));
    gpioNo = GPIO_NUM_NC; // signal "not connected"
  }
}


PWMPin::~PWMPin()
{
  if (gpioNo!=GPIO_NUM_NC) {
    ledc_stop(LEDC_HIGH_SPEED_MODE, ledcChannel, inverted);
  }
}


void PWMPin::setValue(double aValue)
{
  if (gpioNo==GPIO_NUM_NC) return; // non-existing pins cannot be set
  if (aValue<0) aValue = 0; // limit to min
  else if (aValue>100) aValue = 100; // limit to max
  if (inverted) aValue = 100-aValue; // inverted output: invert duty cycle
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, ledcChannel, aValue/100*((1<<13)-1));
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, ledcChannel);
}


double PWMPin::getValue()
{
  if (periodNs==0) return 0;
  return (double)activeNs/(double)periodNs*100;
}


bool PWMPin::getRange(double &aMin, double &aMax, double &aResolution)
{
  aMin = 0;
  aMax = 100;
  aResolution = periodNs>0 ? 1/periodNs : 1;
  return true;
}


#else

// MARK: - PWM via modern kernel support

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
  if (periodNs==0) periodNs = 20000; // 50kHz
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
  s = string_format("%u", periodNs);
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


bool PWMPin::getRange(double &aMin, double &aMax, double &aResolution)
{
  aMin = 0;
  aMax = 100;
  aResolution = periodNs>0 ? 1/periodNs : 1;
  return true;
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

#endif // !ESP_PLATFORM
