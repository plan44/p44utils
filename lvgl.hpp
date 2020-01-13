//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__lvgl__
#define __p44utils__lvgl__

#ifndef ENABLE_LVGL
// We assume that including this file in a build usually means that LittlevGL support is actually needed.
// Still, ENABLE_LVGL can be set to 0 to create build variants w/o removing the file from the project/makefile
#define ENABLE_LVGL 1
#endif

#if ENABLE_LVGL

#include "p44utils_common.hpp"

#include "lvgl/lvgl.h"

#if defined(__APPLE__)
  // test&debugging build on MacOS, using SDL2 for graphics
  // - littlevGL
  #include "lv_drivers/display/monitor.h"
  #include "lv_drivers/indev/mouse.h"
#else
  // target platform build
  // - littlevGL
  #include "lv_drivers/display/fbdev.h"
  #include "lv_drivers/indev/evdev.h"
#endif

#ifndef MOUSE_CURSOR_SUPPORT
  #define MOUSE_CURSOR_SUPPORT 1
#endif

#ifndef ENABLE_IMAGE_SUPPORT
  #define ENABLE_IMAGE_SUPPORT 1
#endif


using namespace std;

namespace p44 {

  // singleton wrapper for LittlevGL
  class LvGL
  {
    lv_disp_t *dispdev; ///< the display device
    lv_indev_t *pointer_indev; ///< the input device for pointer (touch, mouse)
    lv_indev_t *keyboard_indev; ///< the input device for keyboard
    MLTicket lvglTicket; ///< the display tasks timer
    bool showCursor; ///< set if a cursor should be shown (for debug)
    lv_disp_buf_t disp_buf; ///< the display buffer descriptors
    lv_color_t *buf1; ///< the buffer
    uint32_t lastActivity; ///< for activity detection
    SimpleCB taskCallback; ///< called when detecting user activity

    #if LV_USE_FILESYSTEM
    lv_fs_drv_t pf_fs_drv;
    #endif

    LvGL();
    ~LvGL();

  public:

    static LvGL& lvgl();

    void init(bool aShowCursor);

    void setTaskCallback(SimpleCB aCallback);

  private:

    void lvglTask(MLTimer &aTimer, MLMicroSeconds aNow);

  };





} // namespace p44

#endif // ENABLE_LVGL
#endif // __p44utils__lvgl__
