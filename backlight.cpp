//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "backlight.hpp"

#if ENABLE_BACKLIGHT

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.hpp"

using namespace p44;

// MARK: - backlight via kernel support

#define BACKLIGHT_SYS_CLASS_PATH "/sys/class/backlight"


BacklightControl::BacklightControl(const char* aBacklightName) :
  mBacklightFD(-1),
  mMaxBrightness(255)
{
  string name;
  int tempFd;
  const size_t buflen = 10;
  char buf[buflen];
  // get the max brightness
  name = string_format("%s/%s/max_brightness", BACKLIGHT_SYS_CLASS_PATH, aBacklightName);
  tempFd = open(name.c_str(), O_RDONLY);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open backlight max_brightness file %s: %s", name.c_str(), strerror(errno)); return; }
  size_t rd = read(tempFd, buf, buflen);
  if (rd>0) {
    sscanf(buf, "%u", &mMaxBrightness);
  }
  close(tempFd);
  // open the actual brightness
  name = string_format("%s/%s/brightness", BACKLIGHT_SYS_CLASS_PATH, aBacklightName);
  mBacklightFD = open(name.c_str(), O_RDWR);
  if (mBacklightFD<0) { LOG(LOG_ERR, "Cannot open backlight brightness file %s: %s", name.c_str(), strerror(errno)); return; }
}


BacklightControl::~BacklightControl()
{
  if (mBacklightFD>=0) {
    close(mBacklightFD);
  }
}


void BacklightControl::setValue(double aValue)
{
  if (mBacklightFD<0 || mMaxBrightness<=0) return; // non-existing pins cannot be set
  if (aValue<0) aValue = 0; // limit to min
  else if (aValue>100) aValue = 100;
  string s = string_format("%d", (int)(aValue*mMaxBrightness/100) );
  write(mBacklightFD, s.c_str(), s.length());
}


double BacklightControl::getValue()
{
  if (mBacklightFD<0 || mMaxBrightness<=0) return 0;
  const size_t buflen = 10;
  char buf[buflen];
  buf[buflen-1] = 0; // safety
  lseek(mBacklightFD, 0, SEEK_SET);
  size_t rd = read(mBacklightFD, buf, buflen);
  uint32_t bri;
  if (rd>0 && sscanf(buf, "%u", &bri)==1) {
    return (double)bri*100/mMaxBrightness;
  }
  return 0;
}


bool BacklightControl::getRange(double &aMin, double &aMax, double &aResolution)
{
  // always scaled to 0..100
  aMin = 0;
  aMax = 100;
  aResolution = mMaxBrightness>0 ? 100/mMaxBrightness : 1;
  return true;
}

#endif // ENABLE_BACKLIGHT
