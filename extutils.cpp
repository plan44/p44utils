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

#include "extutils.hpp"

#include <string.h>
#include <stdio.h>
#include <sys/types.h> // for ssize_t, size_t etc.

using namespace p44;

#ifndef ESP_PLATFORM

ErrorPtr p44::string_fromfile(const string aFilePath, string &aData)
{
  ErrorPtr err;
  FILE* f = fopen(aFilePath.c_str(), "r");
  if (f==NULL) {
    err = SysError::errNo();
  }
  else {
    if (!string_fgetfile(f, aData)) {
      err = SysError::errNo();
    }
    fclose(f);
  }
  return err;
}


ErrorPtr p44::string_tofile(const string aFilePath, const string &aData)
{
  ErrorPtr err;
  FILE* f = fopen(aFilePath.c_str(), "w");
  if (f==NULL) {
    err = SysError::errNo();
  }
  else {
    if (fwrite(aData.c_str(), aData.size(), 1, f)<1) {
      err = SysError::errNo();
    }
    fclose(f);
  }
  return err;
}

#endif // !ESP_PLATFORM
