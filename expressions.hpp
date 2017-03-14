//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__expressions__
#define __p44utils__expressions__

#include "p44utils_common.hpp"
#include <string>

using namespace std;

namespace p44 {

/// callback function for obtaining string variables
/// @param aValue the contents of this is looked up and possibly replaced
/// @return ok or error
typedef boost::function<ErrorPtr (const string aName, string &aValue)> StringValueLookupCB;

/// substitute "@{xxx}" type placeholders in string
/// @param aString string to replace placeholders in
/// @param aValueLookupCB this will be called to get variables resolved into values
ErrorPtr substitutePlaceholders(string &aString, StringValueLookupCB aValueLookupCB);


/// callback function for obtaining numeric variables
/// @param aValue the contents of this is looked up and possibly replaced
/// @return ok or error
typedef boost::function<ErrorPtr (const string aName, double &aValue)> DoubleValueLookupCB;

/// evaluate expression with numeric result
/// @param aExpression the expression text
/// @param aResult the numeric result
  /// @param aValueLookupCB this will be called to get variables resolved into values
ErrorPtr evaluateExpression(const string &aExpression, double &aResult, DoubleValueLookupCB aValueLookupCB);


} // namespace p44



#endif // defined(__p44utils__expressions__)
