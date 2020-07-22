//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__error__
#define __p44utils__error__

#include "p44utils_minimal.hpp"

#include <string>
#include <stdint.h>
#include <stdarg.h>
#include "p44obj.hpp"

#ifndef __printflike
#define __printflike(...)
#endif

using namespace std;

namespace p44 {

  typedef long ErrorCode;



  /// error base class
  class Error;
  typedef boost::intrusive_ptr<Error> ErrorPtr;
  class Error : public P44Obj
  {
    ErrorCode errorCode;
    string errorMessage;
    string textCache; // only created on demand
  public:

    enum {
      OK,
      NotOK,
      numErrorCodes
    };
    typedef ErrorCode ErrorCodes;

    static const char *domain();

    /// create error with error code
    /// @param aErrorCode error code. aErrorCode==0 from any domain means OK.
    Error(ErrorCode aErrorCode);

    /// create error with error code and message
    /// @param aErrorCode error code. aErrorCode==0 from any domain means OK.
    /// @param aErrorMessage error message
    Error(ErrorCode aErrorCode, const std::string &aErrorMessage);


    /// create a Error subclass object
    /// @param aErrorCode error code. aErrorCode==0 from any domain means OK.
    template<typename T> static ErrorPtr err(ErrorCode aErrorCode)
    {
      Error *errP = new T(static_cast<typename T::ErrorCodes>(aErrorCode));
      return ErrorPtr(errP);
    };


    /// create a Error subclass object with printf-style formatted error
    /// @param aErrorCode error code. aErrorCode==0 from any domain means OK.
    /// @param aFmt ... error message format string and arguments
    template<typename T> static ErrorPtr err(ErrorCode aErrorCode, const char *aFmt, ...)
    #if !P44_BUILD_WIN
    __printflike(2,3)
    #endif
    {
      Error *errP = new T(static_cast<typename T::ErrorCodes>(aErrorCode));
      va_list args;
      va_start(args, aFmt);
      errP->setFormattedMessage(aFmt, args, false);
      va_end(args);
      return ErrorPtr(errP);
    };

    /// create a Error subclass object with message
    /// @param aErrorCode error code. aErrorCode==0 from any domain means OK.
    /// @param aMessage error message
    template<typename T> static ErrorPtr err_str(ErrorCode aErrorCode, const string aMessage)
    {
      Error *errP = new T(static_cast<typename T::ErrorCodes>(aErrorCode));
      errP->errorMessage = aMessage;
      return ErrorPtr(errP);
    };

    /// create a Error subclass object with message
    /// @param aErrorCode error code. aErrorCode==0 from any domain means OK.
    /// @param aMessage error message
    template<typename T> static ErrorPtr err_cstr(ErrorCode aErrorCode, const char *aMessage)
    {
      Error *errP = new T(static_cast<typename T::ErrorCodes>(aErrorCode));
      if (aMessage) errP->errorMessage = aMessage;
      return ErrorPtr(errP);
    };

    /// set formatted error message
    /// @param aFmt error message format string
    /// @param aArgs argument list for formatting
    /// @param aAppend if set, append to message rather than replacing it
    void setFormattedMessage(const char *aFmt, va_list aArgs, bool aAppend = true);

    /// insert additional message context
    void prefixMessage(const char *aFmt, ...) __printflike(2,3);

    /// return error object with inserted additional message context
    ErrorPtr withPrefix(const char *aFmt, ...) __printflike(2,3);

    /// get error code
    /// @return the error code. Note that error codes are unique only within the same error domain.
    ///   error code 0 from any domain means OK.
    inline ErrorCode getErrorCode() const { return errorCode; }

    /// @return true if error is OK code (= no error)
    inline bool isOK() { return errorCode==OK; };

    /// @return true if error is a real error (not the OK code)
    inline bool notOK() { return errorCode!=OK; };


    /// get error message
    /// @return the explicitly set error message, empty string if none is set.
    /// @note use description() to get a text at least showing the error domain and code if no message is set
    virtual const char *getErrorMessage() const;

    /// get error domain
    /// @return the error domain constant string
    virtual const char *getErrorDomain() const;

    /// get the description of the error
    /// @return a description string. If an error message was not set, a standard string with the error domain and number will be shown
    virtual std::string description() const;

    /// returns the error text c_str which is safe to use as long as the error object lives
    const char* text();

    /// check for a specific error
    /// @param aDomain the domain or NULL to match any domain
    /// @param aErrorCode the error code to match
    /// @return true if the error matches domain and code
    bool isError(const char *aDomain, ErrorCode aErrorCode) const;
    static bool isError(ErrorPtr aError, const char *aDomain, ErrorCode aErrorCode);

    /// @return true if the error matches given domain
    bool isDomain(const char *aDomain) const;

    /// checks for OK condition, which means either no error object assigned at all to the smart ptr, or ErrorCode==0
    /// @param aError error pointer to check
    /// @return true if OK
    static inline bool isOK(ErrorPtr aError) { return (aError==NULL || aError->isOK()); };

    /// checks for error (not OK) condition, which means aError assigned but not == 0 == OK
    /// @param aError error pointer to check
    /// @return true if this is a real error
    static inline bool notOK(ErrorPtr aError) { return aError && aError->notOK(); }

    /// returns a error description text in all cases, even if no error object is passed
    /// @param aError error pointer to check
    /// @return "<none>" if NULL error object, text() of error otherwise
    static const char* text(ErrorPtr aError);

    /// factory function to create a explicit OK error (object with ErrorCode==0)
    /// @param aError if set and is !isOK(), then aError will be returned instead of OK
    ///    This can be used to make sure an explicit OK is returned even of aError is NULL
    /// @return OK error object if aError is OK or NULL, aError otherwise
    static ErrorPtr ok(ErrorPtr aError = ErrorPtr());

    #if ENABLE_NAMED_ERRORS
  protected:
    /// return name of the error
    /// @return error name or NULL if subclass has no named error, empty string for
    ///   subclasses without meaningful error codes beyond OK/notOK
    virtual const char* errorName() const { return NULL; }
    #endif // ENABLE_NAMED_ERRORS
  };


  /// macro to create convenient factory method



  /// C errno based system error
  class SysError : public Error
  {
  public:
    static const char *domain();
    virtual const char *getErrorDomain() const P44_OVERRIDE;

    /// create system error from current errno and set message to strerror() text
    SysError(const char *aContextMessage = NULL);

    /// create system error from passed errno and set message to strerror() text
    /// @param aErrNo a errno error number
    SysError(int aErrNo, const char *aContextMessage = NULL);

    /// factory function to create a ErrorPtr either containing NULL (if errno indicates OK)
    /// or a SysError (if errno indicates error)
    static ErrorPtr errNo(const char *aContextMessage = NULL);

    /// factory function to create a ErrorPtr either containing NULL (if aErrNo indicates OK)
    /// or a SysError (if aErrNo indicates error)
    static ErrorPtr err(int aErrNo, const char *aContextMessage = NULL);
  };


  /// Web/HTTP error code based error
  class WebError : public Error
  {
  public:
    static const char *domain() { return "WebError"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return WebError::domain(); };
    WebError(uint16_t aHTTPError) : Error(ErrorCode(aHTTPError)) {};
    WebError(uint16_t aHTTPError, std::string aErrorMessage) : Error(ErrorCode(aHTTPError), aErrorMessage) {};

    /// factory function to create a ErrorPtr either containing NULL (if aErrNo indicates OK)
    /// or a SysError (if aErrNo indicates error)
    static ErrorPtr webErr(uint16_t aHTTPError, const char *aFmt, ... ) __printflike(2,3);
  };


  /// Text message based error
  class TextError : public Error
  {
  public:
    static const char *domain() { return "TextError"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return TextError::domain(); };
    TextError() : Error(Error::NotOK) {};

    /// factory method to create string error fprint style
    static ErrorPtr err(const char *aFmt, ...) __printflike(1,2);
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return ""; };
    #endif // ENABLE_NAMED_ERRORS
  };


} // namespace p44


#endif /* defined(__p44utils__error__) */
