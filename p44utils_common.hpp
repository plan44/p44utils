//
//  Copyright (c) 2014-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__common__
#define __p44utils__common__

#include "p44utils_minimal.hpp"

#include <list>
#include <vector>
#include <map>

#ifdef ESP_PLATFORM
  #define BOOST_NO_EXCEPTIONS
  #include <boost/throw_exception.hpp>
#endif

#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

#ifndef __printflike
#define __printflike(...)
#endif
#ifdef ESP_PLATFORM
  #define __printflike_template(...)
#else
  #define __printflike_template(...) __printflike(__VA_ARGS__)
#endif

#include "p44obj.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "extutils.hpp"
#include "mainloop.hpp"

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
