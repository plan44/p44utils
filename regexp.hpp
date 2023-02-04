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

#ifndef __p44utils__regexp__
#define __p44utils__regexp__

#include "p44utils_common.hpp"

extern "C" {
  #include "slre/slre.h"
}

using namespace std;

namespace p44 {


  class RegExpError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Syntax
    } ErrorCodes;

    static const char *domain() { return "RegExp"; }
    virtual const char *getErrorDomain() const { return RegExpError::domain(); };
    RegExpError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };



  class RegExp : public P44Obj
  {
    struct slre slre_state;
    typedef vector<string> StringVector;

    StringVector *capturesP;

  public:

    RegExp();
    virtual ~RegExp();

    /// compile a regular expression (prepare for applying it with match())
    /// @param aRegExp to compile
    ErrorPtr compile(const string aRegExp);

    /// Match regular expression
    /// @param aCapture if true, results will be captured
    /// @return true if match, false if not
    bool match(const string aText, bool aCapture);

    /// get captured substring from last match()
    /// @param aIndex 0 for complete match string, 1..n for subcaptures
    /// @return capture string or empty string if aIndex is out of range
    string getCapture(int aIndex) const;

  };
  typedef boost::intrusive_ptr<RegExp> RegExpPtr;



} // namespace p44



#endif /* defined(__p44utils__regexp__) */
