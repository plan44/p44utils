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


  /// expression value, consisting of a value and an error to indicate non-value and reason for it
  class ExpressionValue {
  public:
    double v;
    ErrorPtr err;
    ExpressionValue() { v = 0; };
    ExpressionValue(ErrorPtr aError, double aValue = 0) { err = aError; v = aValue; };
    ExpressionValue(double aValue) { v = aValue; };
    bool isOk() const { return Error::isOK(err); }
    string stringValue();
  };


  /// Expression Error
  class ExpressionError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Null,
      Syntax,
      DivisionByZero,
      NotFound, ///< variable, object, function not found (for callback)
    } ErrorCodes;
    static const char *domain() { return "ExpressionError"; }
    virtual const char *getErrorDomain() const { return ExpressionError::domain(); };
    ExpressionError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    /// factory method to create string error fprint style
    static ErrorPtr err(ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
    static ExpressionValue errValue(ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
  };

  /// callback function for obtaining numeric variables
  /// @param aName the name of the value/variable to look up
  /// @return Expression value (with error when value is not available)
  typedef boost::function<ExpressionValue (const string aName)> ValueLookupCB;

  /// callback function for function evaluation
  typedef std::vector<ExpressionValue> FunctionArgumentVector;
  typedef boost::function<ExpressionValue (const string aFunctionName, const FunctionArgumentVector &aArguments)> FunctionLookupCB;

  /// evaluate expression with numeric result
  /// @param aExpression the expression text
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @return the result of the expression
  ExpressionValue evaluateExpression(const string &aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB);

  /// substitute "@{xxx}" type expression placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to get variables resolved into values
  ErrorPtr substituteExpressionPlaceholders(string &aString, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB);


} // namespace p44



#endif // defined(__p44utils__expressions__)
