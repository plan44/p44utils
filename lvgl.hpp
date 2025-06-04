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

#ifndef __p44utils__lvgl__
#define __p44utils__lvgl__

#ifndef ENABLE_LVGL
// We assume that including this file in a build usually means that LittlevGL support is actually needed.
// Still, ENABLE_LVGL can be set to 0 to create build variants w/o removing the file from the project/makefile
#define ENABLE_LVGL 1
#endif

#if ENABLE_LVGL

#include "p44utils_common.hpp"

#include "lvgl.h"

#ifndef MOUSE_CURSOR_SUPPORT
  #define MOUSE_CURSOR_SUPPORT 1
#endif

#ifndef ENABLE_IMAGE_SUPPORT
  #if !DEBUG
    #warning "tbd: re-enable"
  #endif
  #define ENABLE_IMAGE_SUPPORT 0
#endif


using namespace std;

namespace p44 {

  // singleton wrapper for LittlevGL
  class LvGL : public P44LoggingObj
  {
    lv_display_t *mDisplay; ///< the display
    MLTicket mLvglTicket; ///< the display tasks timer
    bool mWithKeyboard; ///< set if a keyboard should be attached (simulator only)
    uint32_t mLastActivity; ///< for activity detection
    SimpleCB mTaskCallback; ///< called when detecting user activity

    #if LV_USE_FILESYSTEM
    lv_fs_drv_t mPf_fs_drv;
    #endif

    LvGL();
    ~LvGL();

  public:

    static LvGL& lvgl();

    void init(const string aDispSpec);

    void setTaskCallback(SimpleCB aCallback);

    virtual string contextType() const P44_OVERRIDE { return "lvgl"; };

  private:

    void lvglTask(MLTimer &aTimer, MLMicroSeconds aNow);

  };
  typedef boost::intrusive_ptr<LvGL> LvGLPtr;


} // namespace p44

#endif // ENABLE_LVGL
#endif // __p44utils__lvgl__
