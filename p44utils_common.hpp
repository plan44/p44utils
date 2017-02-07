//
//  Copyright (c) 2014-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include <list>
#include <vector>
#include <map>

#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

#ifndef __printflike
#define __printflike(...)
#endif

#if __cplusplus >= 201103L
  // we have C++ 11
  #define P44_FINAL final
  #define P44_OVERRIDE override
#else
  #define P44_FINAL
  #define P44_OVERRIDE
#endif

#include "p44obj.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "mainloop.hpp"

// build platform dependencies
#if P44_BUILD_DIGI
  #define DISABLE_I2C 1 // no user space I2C support
  #define DISABLE_SPI 1 // no user space SPI support
#endif


#endif /* __p44utils__common__ */
