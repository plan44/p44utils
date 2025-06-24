//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "lvgl.hpp"

#if ENABLE_LVGL

#include "application.hpp"

#if ENABLE_IMAGE_SUPPORT
  #include <png.h>
#endif

using namespace p44;

LvGL::LvGL() :
  mDisplay(nullptr),
  mWithKeyboard(false)
{
}


LvGL::~LvGL()
{
}


static LvGLPtr lvglP;

LvGL& LvGL::lvgl()
{
  if (!lvglP) {
    lvglP = new LvGL;
  }
  return *lvglP;
}

// MARK: - logging

#if LV_USE_LOG

extern "C" void lvgl_log_cb(lv_log_level_t aLevel, const char *aMsg)
{
  int logLevel = LOG_WARNING;
  switch (aLevel) {
    case LV_LOG_LEVEL_TRACE: logLevel = LOG_DEBUG; break; // A lot of logs to give detailed information
    case LV_LOG_LEVEL_INFO: logLevel = LOG_INFO; break; // Log important events
    case LV_LOG_LEVEL_WARN: logLevel = LOG_WARNING; break; // Log if something unwanted happened but didn't caused problem
    case LV_LOG_LEVEL_ERROR: logLevel = LOG_ERR; break; // Only critical issue, when the system may fail
  }
  POLOG(lvglP, logLevel, "%s", aMsg);
}

#endif // LV_USE_LOG



// MARK: - get current millis

static inline uint32_t getmillis(void)
{
  return (uint32_t)_p44_millis();
}



// MARK: - littlevGL initialisation

void LvGL::init(const string aDispSpec)
{
  // defaults
  string dispdev = "/dev/fb0";
  int colorformat = 0;
  int32_t dx = 0; // default
  int32_t dy = 0; // default
  mWithKeyboard = false;
  lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0; // default
  string evdev = "/dev/input/event0";
  // aDispSpec:
  //   [<display device>[:<evdev device>]][<dx>:<dy>[:<colorformat>]][:<options>]
  //   - display device name, defaults to /dev/fb0, irrelevant for SDL sim
  //   - dx,dy: integers
  //   - options: characters
  //     - C: show cursor
  //     - L: rotate left
  //     - R: rotate right
  //     - U: upside down
  string part;
  const char *p = aDispSpec.c_str();
  int nmbrcnt = 0;
  int txtcnt = 0;
  while (nextPart(p, part, ':')) {
    if (!isdigit(part[0])) {
      // text
      if (nmbrcnt==0) {
        // texts before first number
        if (txtcnt==0) {
          dispdev = part;
          txtcnt++;
        }
        else if (txtcnt==1) {
          evdev = part;
          txtcnt++;
        }
      }
      else {
        // text after first number are options
        for (size_t i=0; i<part.size(); i++) {
          switch (part[i]) {
            case 'K': mWithKeyboard = true; break;
            case 'R': rotation = LV_DISPLAY_ROTATION_90; break;
            case 'U': rotation = LV_DISPLAY_ROTATION_180; break;
            case 'L': rotation = LV_DISPLAY_ROTATION_270; break;
          }
        }
      }
    }
    else {
      // number
      int n = atoi(part.c_str());
      switch (nmbrcnt) {
        case 0: dx = n; break;
        case 1: dy = n; break;
        default: break;
      }
      nmbrcnt++;
    }
  }
  // init library
  lv_init();
  #if LV_USE_LOG
  lv_log_register_print_cb(lvgl_log_cb);
  #endif
  // init tick getter
  lv_tick_set_cb(getmillis);
  // init display
  #if defined(__APPLE__)
  // - SDL2
  if (dx<=0) dx = 720;
  if (dy<=0) dy = 720;
  mDisplay = lv_sdl_window_create(dx, dy);
  #else
  // - Linux frame buffer
  mDisplay = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(mDisplay, dispdev.c_str()); // will read fb properties from device
  #endif
  if (dx>0 && dy>0) {
    // manual resolution
    lv_display_set_resolution(mDisplay, dx, dy);
  }
  if (colorformat>0) {
    // manual color format
    lv_display_set_color_format(mDisplay, (lv_color_format_t)colorformat);
  }
  if (rotation!=LV_DISPLAY_ROTATION_0) {
    lv_display_set_rotation(mDisplay, rotation);
  }
  // init input devices
  #if defined(__APPLE__)
  lv_indev_t *touch = lv_sdl_mouse_create();
  lv_indev_set_display(touch, mDisplay);
  if (mWithKeyboard) {
    lv_indev_t *kbd = lv_sdl_keyboard_create();
    lv_indev_set_display(kbd, mDisplay);
  }
  #else
  lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, evdev.c_str());
  lv_indev_set_display(touch, mDisplay);
  #endif
  // - schedule updates
  mLvglTicket.executeOnce(boost::bind(&LvGL::lvglTask, this, _1, _2));
}


#define LVGL_TICK_PERIOD (5*MilliSecond)

void LvGL::lvglTask(MLTimer &aTimer, MLMicroSeconds aNow)
{
  lv_task_handler();
  #if defined(__APPLE__)
  // also need to update SDL2
  //monitor_sdl_refr_core();
  if (mDisplay) lv_refr_now(mDisplay);
  #endif
  if (mTaskCallback && mDisplay) {
    mTaskCallback();
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, LVGL_TICK_PERIOD);
}


void LvGL::setTaskCallback(SimpleCB aCallback)
{
  mTaskCallback = aCallback;
}




#endif // ENABLE_LVGL
