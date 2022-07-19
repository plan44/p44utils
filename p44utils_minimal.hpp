//
//  Copyright (c) 2014-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

/// This header file introduces no dependencies on code, just defines the
/// bare minimum for using independent p44utils routines and types

#ifndef __p44utils__minimal__
#define __p44utils__minimal__

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif
#ifdef ESP_PLATFORM
  #include "sdkconfig.h"
#endif

#if __cplusplus >= 201103L
  // we have C++ 11
  #define P44_FINAL final
  #define P44_OVERRIDE override
  #define P44_CPP11_FEATURE 1
  #if __cplusplus >= 201703L
    // we have C++ 17
    #define P44_CPP17_FEATURE 1
    #if __cplusplus >= 202002L
      // we have C++ 20
      #define P44_CPP20_FEATURE 1
    #else
      #define P44_CPP20_FEATURE 0
    #endif
  #else
    #define P44_CPP17_FEATURE 0
    #define P44_CPP20_FEATURE 0
  #endif
#else
  #define P44_FINAL
  #define P44_OVERRIDE
  #define P44_CPP11_FEATURE 0
  #define P44_CPP17_FEATURE 0
  #define P44_CPP20_FEATURE 0
#endif

#include "p44utils_config.hpp"

// some minimal defs
#include <stddef.h>
#include <stdint.h>

#endif /* __p44utils__minimal__ */
