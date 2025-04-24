//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__logger__
#define __p44utils__logger__

#include "p44utils_minimal.hpp"

#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
  #define  LOG_EMERG  0  /* system is unusable */
  #define  LOG_ALERT  1  /* action must be taken immediately */
  #define  LOG_CRIT  2  /* critical conditions */
  #define  LOG_ERR    3  /* error conditions */
  #define  LOG_WARNING  4  /* warning conditions */
  #define  LOG_NOTICE  5  /* normal but significant condition */
  #define  LOG_INFO  6  /* informational */
  #define  LOG_DEBUG  7  /* debug-level messages */
#else
  #include <syslog.h>
#endif

#ifndef __printflike
#define __printflike(...)
#endif

#if REDUCED_FOOTPRINT
  #define IFNOTREDUCEDFOOTPRINT(lvl) (0)
#else
  #define IFNOTREDUCEDFOOTPRINT(lvl) (lvl)
#endif

#ifndef ENABLE_LOG_COLORS
  #define ENABLE_LOG_COLORS 1
#endif


#include "p44obj.hpp"
#include <boost/function.hpp>

// global object independent logging
#define LOGENABLED(lvl) globalLogger.logEnabled(lvl)
#define LOG(lvl,...) { if (globalLogger.logEnabled(lvl)) globalLogger.log(lvl,##__VA_ARGS__); }
#define SETLOGLEVEL(lvl) globalLogger.setLogLevel(lvl)
#define SETERRLEVEL(lvl, dup) globalLogger.setErrLevel(lvl, dup)
#define SETDELTATIME(dt) globalLogger.setDeltaTime(dt)
#define LOGLEVEL (globalLogger.getLogLevel())
#define SETLOGHANDLER(lh,allowother) globalLogger.setLogHandler(lh,allowother)
#define DAEMONMODE globalLogger.getDaemonMode()
#define SETDAEMONMODE(d) globalLogger.setDaemonMode(d)
#if ENABLE_LOG_COLORS
#define SETLOGSYMBOLS(s) globalLogger.setSymbols(s)
#define SETLOGCOLORING(c) globalLogger.setColoring(c)
#else
#define SETLOGSYMBOLS(s)
#define SETLOGCOLORING(c)
#endif

// logging from within a P44LoggingObj (messages prefixed with object's logContextPrefix(), object's logoffset applied)
#define OLOGENABLED(lvl) logEnabled(lvl)
#define OLOG(lvl,...) { if (logEnabled(lvl)) log(lvl,##__VA_ARGS__); }
// logging via a specified P44LoggingObj (messages prefixed with object's logContextPrefix(), object's logoffset applied)
#define SOLOGENABLED(obj,lvl) ((obj).logEnabled(lvl))
#define SOLOG(obj,lvl,...) { if ((obj).logEnabled(lvl)) (obj).log(lvl,##__VA_ARGS__); }
// logging via a pointed-to P44LoggingObj (messages prefixed with object's logContextPrefix(), object's logoffset applied)
#define POLOGENABLED(obj,lvl) ((obj) ? (obj)->logEnabled(lvl) : LOGENABLED(lvl))
#define POLOG(obj,lvl,...) { if (POLOGENABLED(obj,lvl)) { if (obj) (obj)->log(lvl,##__VA_ARGS__); else globalLogger.log(lvl,##__VA_ARGS__); }}

// debug build extra logging (not included in release code unless ALWAYS_DEBUG is set)
#if defined(DEBUG) || ALWAYS_DEBUG
#define DEBUGLOGGING 1
#define DBGLOGENABLED(lvl) LOGENABLED(lvl)
#define DBGLOG(lvl,...) LOG(lvl,##__VA_ARGS__)
#define DBGFOCUSLOG FOCUSLOG
#define DBGFOCUSOLOG FOCUSOLOG
#define DBGFOCUSSOLOG FOCUSSOLOG
#define DBGFOCUSPOLOG FOCUSPOLOG
#define DBGOLOGENABLED(lvl) OLOGENABLED(lvl)
#define DBGOLOG(lvl,...) OLOG(lvl,##__VA_ARGS__)
#define DBGSOLOGENABLED(obj,lvl) SOLOGENABLED(obj,lvl)
#define DBGSOLOG(obj,lvl,...) SOLOG(obj,lvl,##__VA_ARGS__)
#define LOGGER_DEFAULT_LOGLEVEL LOG_DEBUG
#else
#define DEBUGLOGGING 0
#define DBGLOGENABLED(lvl) false
#define DBGLOG(lvl,...)
#define DBGFOCUSLOG(...)
#define DBGFOCUSOLOG(...)
#define DBGFOCUSSOLOG(...)
#define DBGFOCUSPOLOG(...)
#define DBGOLOGENABLED(lvl) false
#define DBGOLOG(lvl,...)
#define DBGSOLOGENABLED(obj,lvl) false
#define DBGSOLOG(obj,lvl,...)
#define LOGGER_DEFAULT_LOGLEVEL LOG_NOTICE
#endif

// "focus" logging during development, additional logging that can be enabled per source file
// (define FOCUSLOGLEVEL before including logger.hpp)
#if FOCUSLOGLEVEL
#define FOCUSLOG(...) LOG(FOCUSLOGLEVEL,##__VA_ARGS__)
#define FOCUSOLOG(...) OLOG(FOCUSLOGLEVEL,##__VA_ARGS__)
#define FOCUSSOLOG(obj,...) SOLOG(obj,FOCUSLOGLEVEL,##__VA_ARGS__)
#define FOCUSPOLOG(obj,...) POLOG(obj,FOCUSLOGLEVEL,##__VA_ARGS__)
#define FOCUSLOGGING 1
#define FOCUSLOGENABLED LOGENABLED(FOCUSLOGLEVEL)
#define FOCUSOLOGENABLED OLOGENABLED(FOCUSLOGLEVEL)
#define FOCUSPLOGENABLED PLOGENABLED(FOCUSLOGLEVEL)
#if !(defined(DEBUG) || ALWAYS_DEBUG || FOCUSLOGLEVEL>=7)
#warning "**** FOCUSLOGLEVEL<7 enabled in non-DEBUG build ****"
#endif
#else
#define FOCUSLOGGING 0
#define FOCUSLOG(...)
#define FOCUSOLOG(...)
#define FOCUSSOLOG(obj,...)
#define FOCUSPOLOG(obj,...)
#define FOCUSLOGENABLED false
#define FOCUSOLOGENABLED false
#define FOCUSPLOGENABLED false
#endif


using namespace std;

namespace p44 {


  /// callback for logging lines somewhere else than stderr/stdout
  /// @param aLevel the log level
  /// @param aLinePrefix the line prefix (date and loglevel in square brackets, or indent for multilines)
  /// @param aLogMessage the log message itself
  typedef boost::function<void (int aLevel, const char *aLinePrefix, const char *aLogMessage)> LoggerCB;

  class Logger : public P44Obj
  {
    pthread_mutex_t mReportMutex;
    struct timeval mLastLogTS; ///< timestamp of last log line
    int mLogLevel; ///< log level
    int mStderrLevel; ///< lowest level that also goes to stderr
    bool mDeltaTime; ///< if set, log timestamps will show delta time relative to previous log line
    bool mErrToStdout; ///< if set, even log lines that go to stderr are still shown on stdout as well
    bool mDaemonMode; ///< if set, normal log goes to stdout and log<=mStderrLevel goes to stderr. If cleared, all log goes to stderr according to mLogLevel
    LoggerCB mLoggerCB; ///< custom logger output function to use (instead or in addition of stderr/stdout, see mLogCBOnly)
    bool mAllowOther; ///< if set, setting CB or file will still allow other logging to happen in parallel
    FILE *mLogFILE; ///< file to log to (instead of stderr/stdout)
    #if ENABLE_LOG_COLORS
    bool mLogColors; ///< if set, logger uses ANSI colors to differentiate levels
    bool mLogSymbols; ///< if set, logger uses UTF-8 color dots to differentiate levels
    #endif

  public:
    Logger();
    virtual ~Logger();

    /// test if log is enabled at a given level
    /// @param aLogLevel level to check
    /// @param aLevelOffset if aLogLevel is in the 5..7 (LOG_NOTICE..LOG_DEBUG) range, aLevelOffset is subtracted
    ///   and the result is limited to the 5..7 range.
    ///   This parameter can be fed from a property of a logging object to elevate (positive aLevelOffset)
    ///   or silence (negative aLevelOffset) its logging selectively.
    /// @return true if any logging (stderr or stdout) is enabled at the specified level
    bool logEnabled(int aLogLevel, int aLevelOffset = 0);

    /// test if log to std out is enabled at a given level
    /// @param aErrLevel level to check
    /// @return true if logging to stdout is enabled at this level.
    /// Note: logging might still occur on stderr, even if this function returns false
    bool stdoutLogEnabled(int aErrLevel);


    /// log a message if logging is enabled for the specified aErrLevel
    /// @param aErrLevel error level of the message
    /// @param aFmt ... printf style error message
    void log(int aErrLevel, const char *aFmt, ... ) __printflike(3,4);

    /// log a message
    /// @param aErrLevel error level of the message
    /// @param aAlways if set, always log the message without checking current loglevel, otherwise
    ///   only if logging is enabled for the specified aErrLevel
    /// @param aFmt printf-style format string
    /// @param aArgs va_list of argumments
    void logV(int aErrLevel, bool aAlways, const char *aFmt, va_list aArgs);

    /// log a message unconditionally (even if aErrLevel is not enabled)
    /// @param aErrLevel error level of the message, for inclusion into log message prefix
    /// @param aFmt ... printf style error message
    void log_always(int aErrLevel, const char *aFmt, ... ) __printflike(3,4);

    /// log a message (unconditionally)
    /// @param aErrLevel error level of the message, for inclusion into log message prefix
    /// @param aMessage the message string
    void logStr_always(int aErrLevel, std::string aMessage);

    /// log a message with context prefix (unconditionally)
    /// @param aErrLevel error level of the message, for inclusion into log message prefix
    /// @param aContext a context string, to be inserted before the message itself
    /// @param aMessage the message string
    void contextLogStr_always(int aErrLevel, const std::string& aContext, const std::string& aMessage);

    /// set log file
    /// @param aLogFilePath file to write log to instead of stdout
    /// @param aAllowOther if set,  stdout/stderr still happens
    /// @note if a callback is set with setLogHandler, it overrides logging to a file if aAllowOther is not set
    void setLogFile(const char *aLogFilePath, bool aAllowOther = false);

    /// set log level
    /// @param aLogLevel the new log level
    /// @note even if aLogLevel is set to suppress messages, messages that qualify for going to stderr
    ///   (see setErrorLevel) will still be shown on stderr, but not on stdout.
    void setLogLevel(int aLogLevel);

    /// return current log level
    /// @return current log level
    int getLogLevel() { return mLogLevel; }

    /// @return true if log symbols are enabled
    bool logSymbols() { return mLogSymbols; }

    /// set level required to send messages to stderr
    /// @param aStderrLevel any messages with this or a lower (=higher priority) level will be sent to stderr (default = LOG_ERR)
    /// @param aErrToStdout if set, messages that qualify for stderr will STILL be duplicated to stdout as well (default = true)
    void setErrLevel(int aStderrLevel, bool aErrToStdout);

    /// set handler for outputting log lines
    /// @param aLoggerCB callback to be called whenever
    /// @param aAllowOther if set, other log methods (file and stdout/stderr still happen)
    void setLogHandler(LoggerCB aLoggerCB, bool aAllowOther = false);

    /// set delta time display
    /// @param aDeltaTime if set, time passed since last log line will be displayed
    void setDeltaTime(bool aDeltaTime) { mDeltaTime = aDeltaTime; };

    // @return true if in daemonmode (log goes to stdout, only higher importance errors to stderr)
    bool getDaemonMode() { return mDaemonMode; }

    // @param true to enable daemon mode (on by default)
    void setDaemonMode(bool aDaemonMode) { mDaemonMode = aDaemonMode; }

    /// @param aSymbols if set, logger uses UTF-8 symbols to differentiate levels and separate prefix from actual log content
    void setSymbols(bool aSymbols) { mLogSymbols = aSymbols; };

    /// @param aColoring if set, ANSI terminal colors are used to differentiate levels and separate prefix from actual log content
    void setColoring(bool aColoring) { mLogColors = aColoring; };

  private:

    void logOutput_always(int aLevel, const char *aLinePrefix, const char *aLogMessage);

  };


  class P44LoggingObj : public P44Obj
  {
    typedef P44Obj inherited;

  protected:
    
    int mLogLevelOffset; ///< will be subtracted from log level for checking (in 7..5 range only)

  public:

    P44LoggingObj();

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix();

    /// @return name (usually user-defined) of the context object
    virtual string contextName() const;

    /// @return type (such as: device, element, vdc, trigger) of the context object
    virtual string contextType() const;

    /// @return id identifying the context object
    virtual string contextId() const;

    /// test if log is enabled from this object at a given level
    /// @param aLogLevel level to check
    bool logEnabled(int aLogLevel);

    /// log a message from this object if logging is enabled for the specified aErrLevel adjusted by local logLevelOffset
    /// @param aErrLevel error level of the message
    /// @param aFmt ... printf style error message
    void log(int aErrLevel, const char *aFmt, ... ) __printflike(3,4);

    /// @return the per-instance log level offset
    /// @note is virtual because some objects might want to use the log level offset of another object
    virtual int getLogLevelOffset();

    /// @return always locally stored offset, even when getLogLevelOffset() returns something else
    int getLocalLogLevelOffset() { return mLogLevelOffset; }

    /// set the log level offset on this logging object (and possibly contained sub-objects)
    /// @param aLogLevelOffset the new log level offset
    virtual void setLogLevelOffset(int aLogLevelOffset);

  };

} // namespace p44


extern p44::Logger globalLogger;



#endif /* defined(__p44utils__logger__) */
