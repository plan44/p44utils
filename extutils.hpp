//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__extutils__
#define __p44utils__extutils__

#include "p44utils_common.hpp"

using namespace std;

/// Extended utilities that have dependencies on other p44utils classes (such as p44::Error)
/// @note utilities that DO NOT depends on other p44utils classes are in "utils"

namespace p44 {

  #ifndef ESP_PLATFORM

  /// reads string from file
  /// @param aFilePath the path of the file to read
  /// @param aData the string to store the contents of the file to
  /// @return ok or error
  ErrorPtr string_fromfile(const string aFilePath, string &aData);

  /// saves string to file
  /// @param aFilePath the path of the file to write
  /// @param aData the string to store in the file
  /// @return ok or error
  ErrorPtr string_tofile(const string aFilePath, const string &aData);

  #endif

} // namespace p44

#endif /* defined(__p44utils__extutils__) */
