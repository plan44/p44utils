//
//  Copyright (c) 2014-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__config__
#define __p44utils__config__

// NOTE: This is a default (template) config file only
//                 ******************
//       Usually, copy this file into a location outside p44utils in your project,
//       rename it to p44utils_config.hpp and modify it according to
//       your needs.

#ifndef ENABLE_NAMED_ERRORS
  #define ENABLE_NAMED_ERRORS P44_CPP11_FEATURE // Enable if compiler can do C++11
#endif
#ifndef ENABLE_EXPRESSIONS
  #define ENABLE_EXPRESSIONS 1 // Expression/Script engine support in some of the p44utils components
#endif
#ifndef ENABLE_P44LRGRAPHICS
  #define ENABLE_P44LRGRAPHICS 0 // p44lrgraphics support in some of the p44utils components
#endif
#ifndef ENABLE_APPLICATION_SUPPORT
  #define ENABLE_APPLICATION_SUPPORT 1 // support for Application (e.g. domain specific commandline options) in other parts of P44 utils
#endif
#ifndef ENABLE_JSON_APPLICATION
  #define ENABLE_JSON_APPLICATION 0 // enables JSON utilities in Application, requires json-c
#endif


#endif // __p44utils__config__
