//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2014-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

/// This header file introduces dependencies on other p44utils classes, but no
/// "main" program dependencies like mainloop

#ifndef __p44utils__common__
#define __p44utils__common__

#include "p44utils_minimal.hpp"

#include <list>
#include <vector>
#include <map>

// derived definitions
#ifdef ESP_PLATFORM
  #define BOOST_NO_EXCEPTIONS
  #include <boost/throw_exception.hpp>
#endif
#if ENABLE_UWSC
  // libuwsc (websockets) needs libev based main loop
  #define MAINLOOP_LIBEV_BASED 1
#endif

#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

#if P44_BUILD_DIGI
  // old build environment needs old-style bind with global namespace placeholders
  #include <boost/bind.hpp>
#else
  // including this instead of boost/bind.hpp puts argument placeholder into boost::placeholders namespace
  #include <boost/bind/bind.hpp>
  // to still allow using the placeholders w/o qualifier:
  using namespace boost::placeholders;
#endif

#include "p44utils_defs.hpp"
#include "p44obj.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "extutils.hpp"

// build platform dependencies
#if P44_BUILD_DIGI
  #define DISABLE_I2C 1 // no user space I2C support
  #define DISABLE_SPI 1 // no user space SPI support
#endif
#if P44_BUILD_WIN
  #define DISABLE_I2C 1
  #define DISABLE_SPI 1
  #define DISABLE_GPIO 1
  #define DISABLE_PWM 1
  #define DISABLE_SYSCMDIO 1
#endif

#endif /* __p44utils__common__ */
