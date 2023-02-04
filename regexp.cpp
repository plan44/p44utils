//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "regexp.hpp"

using namespace p44;


RegExp::RegExp() :
  capturesP(NULL)
{
  memset(&slre_state, 0, sizeof(slre_state));
}


RegExp::~RegExp()
{
  if (capturesP) delete capturesP; capturesP = NULL;
}


ErrorPtr RegExp::compile(const string aRegExp)
{
  ErrorPtr err;
  if (!slre_compile(&slre_state, aRegExp.c_str())) {
    err = Error::err<RegExpError>(RegExpError::Syntax, "RegExp Syntaxt error: %s", slre_state.err_str);
    slre_state.num_caps = 0;
  }
  return err;
}


bool RegExp::match(const string aText, bool aCapture)
{
  typedef struct cap cap;
  cap *capsP = NULL;
  if (aCapture) {
    // we need captures
    capsP = new cap[slre_state.num_caps+1];
  }
  bool matched = slre_match(&slre_state, aText.c_str(), (int)aText.size(), capsP);
  if (matched && capsP) {
    if (!capturesP) {
      capturesP = new StringVector;
    }
    else {
      capturesP->clear();
    }
    for (int i=0; i<=slre_state.num_caps; i++) {
      capturesP->push_back(string(capsP[i].ptr, capsP[i].len));
    }
  }
  delete [] capsP;
  return matched;
}


string RegExp::getCapture(int aIndex) const
{
  if (!capturesP || aIndex>=capturesP->size())
    return "";
  return (*capturesP)[aIndex];
}
