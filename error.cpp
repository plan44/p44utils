//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "error.hpp"

#include <string.h>
#include <errno.h>

#include "utils.hpp"

using namespace p44;

// MARK: - error base class

Error::Error(ErrorCode aErrorCode) :
  errorCode(aErrorCode)
{
}


Error::Error(ErrorCode aErrorCode, const std::string &aErrorMessage) :
  errorCode(aErrorCode),
  errorMessage(aErrorMessage)
{
}


void Error::setFormattedMessage(const char *aFmt, va_list aArgs, bool aAppend)
{
  // now make the string
  textCache.clear();
  string_format_v(errorMessage, aAppend, aFmt, aArgs);
}


void Error::prefixMessage(const char *aFmt, ...)
{
  textCache.clear();
  string s;
  va_list args;
  va_start(args, aFmt);
  string_format_v(s, false, aFmt, args);
  va_end(args);
  errorMessage.insert(0, s);
}


ErrorPtr Error::withPrefix(const char *aFmt, ...)
{
  textCache.clear();
  string s;
  va_list args;
  va_start(args, aFmt);
  string_format_v(s, false, aFmt, args);
  va_end(args);
  errorMessage.insert(0, s);
  return ErrorPtr(this);
}


const char *Error::domain()
{
  return "Error_baseClass";
}


const char *Error::getErrorDomain() const
{
  return Error::domain();
}


const char *Error::getErrorMessage() const
{
  return errorMessage.c_str();
}


string Error::errorCodeText() const
{
  #if ENABLE_NAMED_ERRORS
  string errorText = string_format("(%s", getErrorDomain());
  const char *errName = errorName();
  if (!errName) errName = getErrorCode()==0 ? "OK" : "NotOK";
  // Note: errorName() returning empty string means error has no error codes to show, only domain
  if (*errName) {
    errorText += "::";
    errorText += errName;
    if (getErrorCode()!=0) {
      string_format_append(errorText, "[%ld]", getErrorCode());
    }
  }
  errorText += ')';
  return errorText;
  #else
  return string_format("(%s:%ld)", getErrorDomain() , errorCode);
  #endif
}


string Error::description() const
{
  string errorText;
  if (errorMessage.size()>0)
    errorText = errorMessage;
  else
    errorText = "Error";
  // Append domain and code to message text
  errorText += " " + errorCodeText();
  return errorText;
}


const char* Error::text()
{
  if (textCache.empty()) {
    textCache = description();
  }
  return textCache.c_str(); // is safe to return, as textCache lives as error object member
}


const char* Error::text(ErrorPtr aError)
{
  if (!aError) return "<none>";
  return aError->text();
}



bool Error::isError(const char *aDomain, ErrorCode aErrorCode) const
{
  return aErrorCode==errorCode && (aDomain==NULL || isDomain(aDomain));
}


bool Error::isDomain(const char *aDomain) const
{
  return strcmp(aDomain, getErrorDomain())==0;
}


bool Error::isError(ErrorPtr aError, const char *aDomain, ErrorCode aErrorCode)
{
  if (!aError) return false;
  return aError->isError(aDomain, aErrorCode);
}

bool Error::isDomain(ErrorPtr aError, const char *aDomain)
{
  if (!aError) return false;
  return aError->isDomain(aDomain);
}


ErrorPtr Error::ok(ErrorPtr aError)
{
  if (aError) return aError; // whatever it is, return it
  return ErrorPtr(new Error(OK, "OK")); // aError is NULL, return an explicit OK
}


// MARK: - system error


const char *SysError::domain()
{
  return "System";
}


const char *SysError::getErrorDomain() const
{
  return SysError::domain();
}


SysError::SysError(const char *aContextMessage) :
  Error(errno, string(nonNullCStr(aContextMessage)).append(nonNullCStr(strerror(errno))))
{
}

SysError::SysError(int aErrNo, const char *aContextMessage) :
  Error(aErrNo, string(nonNullCStr(aContextMessage)).append(nonNullCStr(strerror(aErrNo))))
{
}


ErrorPtr SysError::errNo(const char *aContextMessage)
{
  if (errno==0)
    return ErrorPtr(); // empty, no error
  return ErrorPtr(new SysError(aContextMessage));
}

ErrorPtr SysError::err(int aErrNo, const char *aContextMessage)
{
  if (aErrNo==0)
    return ErrorPtr(); // empty, no error
  return ErrorPtr(new SysError(aErrNo, aContextMessage));
}


ErrorPtr SysError::retErr(int aRet, const char *aContextMessage)
{
  if (aRet>=0) return nullptr;
  return SysError::errNo(aContextMessage);
}



#ifdef ESP_PLATFORM

// MARK: - ESP IDF error

const char *EspError::domain()
{
  return "ESP32";
}


const char *EspError::getErrorDomain() const
{
  return SysError::domain();
}


EspError::EspError(esp_err_t aEspError, const char *aContextMessage) :
  Error(aEspError, string(nonNullCStr(aContextMessage)).append(nonNullCStr(esp_err_to_name(aEspError))))
{
}


ErrorPtr EspError::err(esp_err_t aEspError, const char *aContextMessage)
{
  if (aEspError==ESP_OK)
    return ErrorPtr(); // empty, no error
  return ErrorPtr(new EspError(aEspError, aContextMessage));
}

#endif // ESP_PLATFORM


// MARK: - web error


ErrorPtr WebError::webErr(uint16_t aHTTPError, const char *aFmt, ... )
{
  if (aHTTPError==0)
    return ErrorPtr(); // empty, no error
  Error *errP = new WebError(aHTTPError);
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args, false);
  va_end(args);
  return ErrorPtr(errP);
}


// MARK: - text error


ErrorPtr TextError::err(const char *aFmt, ...)
{
  Error *errP = new TextError();
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args, false);
  va_end(args);
  return ErrorPtr(errP);
}



