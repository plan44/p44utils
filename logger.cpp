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

#include "logger.hpp"

#include "utils.hpp"

using namespace p44;

// MARK: - Logger

p44::Logger globalLogger;

Logger::Logger() :
  mLoggerCB(NoOP),
  mLoggerContextPtr(NULL),
  mLogFILE(NULL)
{
  pthread_mutex_init(&mReportMutex, NULL);
  gettimeofday(&mLastLogTS, NULL);
  mLogLevel = LOGGER_DEFAULT_LOGLEVEL;
  mStderrLevel = LOG_ERR;
  mDeltaTime = false;
  mErrToStdout = true;
  mDaemonMode = true;
  #if ENABLE_LOG_COLORS
  mLogSymbols = false;
  mLogColors = false;
  #endif
}


Logger::~Logger()
{
  if (mLogFILE) {
    fclose(mLogFILE);
    mLogFILE = NULL;
  }
}

#define LOGBUFSIZ 8192


bool Logger::stdoutLogEnabled(int aErrLevel)
{
  return (aErrLevel<=mLogLevel);
}


bool Logger::logEnabled(int aErrLevel, int aLevelOffset)
{
  if (aLevelOffset && aErrLevel>=LOG_NOTICE) {
    aErrLevel -= aLevelOffset;
    if (aErrLevel<LOG_NOTICE) aErrLevel = LOG_NOTICE;
    else if (aErrLevel>LOG_DEBUG) aErrLevel = LOG_DEBUG;
  }
  return stdoutLogEnabled(aErrLevel) || (mDaemonMode && aErrLevel<=mStderrLevel);
}


const static char levelChars[8] = {
  '*', // LOG_EMERG	  - system is unusable
  '!', // LOG_ALERT   - action must be taken immediately
  'C', // LOG_CRIT    - critical conditions
  'E', // LOG_ERR     - error conditions
  'W', // LOG_WARNING - warning conditions
  'N', // LOG_NOTICE  - normal but significant condition
  'I', // LOG_INFO    - informational
  'D'  // LOG_DEBUG   - debug-level messages
};


#if ENABLE_LOG_COLORS

#define ESC "\x1B"
// Colors
#define NORMAL ESC "[m"
#define GRAY ESC "[90m"
#define BRIGHT_GREEN ESC "[92m"
#define BRIGHT_RED ESC "[91m"
#define BRIGHT_YELLOW ESC "[93m"
#define DARK_BLUE ESC "[34m"
#define DARK_CYAN ESC "[36m"


static const struct {
  const char* ansiColor;
  const char* symbol;
} levelColors[8] = {
  { .ansiColor = BRIGHT_RED,    .symbol =  "🚫" }, // LOG_EMERG    - system is unusable
  { .ansiColor = BRIGHT_RED,    .symbol =  "‼️" }, // LOG_ALERT   - action must be taken immediately
  { .ansiColor = BRIGHT_RED,    .symbol =  "⁉️" }, // LOG_CRIT    - critical conditions
  { .ansiColor = BRIGHT_RED,    .symbol =  "🔴" }, // LOG_ERR     - error conditions
  { .ansiColor = BRIGHT_YELLOW, .symbol =  "⚠️" }, // LOG_WARNING - warning conditions
  { .ansiColor = BRIGHT_GREEN,  .symbol =  "✅" }, // LOG_NOTICE  - normal but significant condition
  { .ansiColor = NORMAL,        .symbol =  "ℹ️" }, // LOG_INFO    - informational
  { .ansiColor = DARK_CYAN,     .symbol =  "🛠️" }  // LOG_DEBUG   - debug-level messages
};

static const char* gTextContextPostfix = ": ";
static const char* gSymbolContextPostfix = " ➡️ ";

static const char* gContextPrefixColor = GRAY;
static const char* gNormalColor = NORMAL;

#endif // ENABLE_LOG_COLORS




void Logger::logV(int aErrLevel, bool aAlways, const char *aFmt, va_list aArgs)
{
  // format the message
  string message;
  string_format_v(message, false, aFmt, aArgs);
  if (aAlways || logEnabled(aErrLevel)) {
    logStr_always(aErrLevel, message);
  }
}


void Logger::log(int aErrLevel, const char *aFmt, ... )
{
  if (logEnabled(aErrLevel)) {
    va_list args;
    va_start(args, aFmt);
    logV(aErrLevel, false, aFmt, args);
    va_end(args);
  }
}


void Logger::log_always(int aErrLevel, const char *aFmt, ... )
{
  va_list args;
  va_start(args, aFmt);
  logV(aErrLevel, false, aFmt, args);
  va_end(args);
}



void Logger::logStr_always(int aErrLevel, const string aMessage)
{
  contextLogStr_always(aErrLevel, "", aMessage);
}


void Logger::contextLogStr_always(int aErrLevel, const string& aContext, const string& aMessage)
{
  pthread_mutex_lock(&mReportMutex);
  // create date + level
  struct timeval t;
  gettimeofday(&t, NULL);
  string prefix = string_ftime("[%Y-%m-%d %H:%M:%S", localtime(&t.tv_sec));
  string_format_append(prefix, ".%03d", (int)(t.tv_usec/1000));
  if (mDeltaTime) {
    long long millisPassed = (long long)(((t.tv_sec*1000000ll+t.tv_usec) - (mLastLogTS.tv_sec*1000000ll+mLastLogTS.tv_usec))/1000); // in mS
    string_format_append(prefix, "%6lldmS", millisPassed);
  }
  mLastLogTS = t;
  string_format_append(prefix, " %c] ", levelChars[aErrLevel]);
  // generate empty leading lines, if any
  string::size_type i=0;
  while (i<aMessage.length() && aMessage[i]=='\n') {
    logOutput_always(aErrLevel, "", "");
    i++;
  }
  string msg;
  // create message
  #if ENABLE_LOG_COLORS
  if (mLogSymbols) {
    msg += levelColors[aErrLevel].symbol;
    msg += " ";
  }
  #endif
  if (!aContext.empty()) {
    #if ENABLE_LOG_COLORS
    if (mLogColors) msg += gContextPrefixColor;
    #endif
    msg += aContext;
    #if ENABLE_LOG_COLORS
    if (mLogSymbols) {
      msg += gSymbolContextPostfix;
    }
    else
    #endif
    {
      msg += gTextContextPostfix;
    }
  }
  #if ENABLE_LOG_COLORS
  // Switch to loglevel color
  if (mLogColors) msg += levelColors[aErrLevel].ansiColor;
  #endif
  // now process input message, possibly multi-lined
  while (i<aMessage.length()) {
    char c = aMessage[i];
    if (c=='\n') {
      // end of line
      logOutput_always(aErrLevel, prefix.c_str(), msg.c_str());
      msg.clear();
      prefix.assign(prefix.size(), ' '); // replace by spaces
    }
    else if (!isprint(c) && (uint8_t)c<0x80) {
      // ASCII control character, but not bit 7 set (UTF8 component char)
      string_format_append(msg, "\\x%02x", (unsigned)(c & 0xFF));
    }
    else {
      msg += c;
    }
    i++;
  }
  #if ENABLE_LOG_COLORS
  // Switch to loglevel color
  if (mLogColors) msg += gNormalColor;
  #endif
  logOutput_always(aErrLevel, prefix.c_str(), msg.c_str());
  pthread_mutex_unlock(&mReportMutex);
}



void Logger::logOutput_always(int aLevel, const char *aLinePrefix, const char *aLogMessage)
{
  // output
  if (mLoggerCB) {
    mLoggerCB(mLoggerContextPtr, aLevel, aLinePrefix, aLogMessage);
  }
  else if (mLogFILE) {
    fputs(aLinePrefix, mLogFILE);
    fputs(aLogMessage, mLogFILE);
    fputs("\n", mLogFILE);
    fflush(mLogFILE);
  }
  else {
    // normal logging to stdout/err
    // - in daemon mode, only level<=mStderrLevel goes to stderr
    // - in cmdline tool mode all log goes to stderr
    if (aLevel<=mStderrLevel || !mDaemonMode) {
      // must go to stderr anyway
      fputs(aLinePrefix, stderr);
      fputs(aLogMessage, stderr);
      fputs("\n", stderr);
      fflush(stderr);
    }
    // - in daemon mode only, normal log goes to stdout (and errors are duplicated to stdout as well)
    if (mDaemonMode && (aLevel>mStderrLevel || mErrToStdout)) {
      // must go to stdout as well
      fputs(aLinePrefix, stdout);
      fputs(aLogMessage, stdout);
      fputs("\n", stdout);
      fflush(stdout);
    }
  }
}


void Logger::setLogFile(const char *aLogFilePath)
{
  if (aLogFilePath) {
    mLogFILE = fopen(aLogFilePath, "a");
  }
  else {
    if (mLogFILE) {
      fclose(mLogFILE);
      mLogFILE = NULL;
    }
  }
}


void Logger::setLogLevel(int aLogLevel)
{
  if (aLogLevel<LOG_EMERG || aLogLevel>LOG_DEBUG) return;
  mLogLevel = aLogLevel;
}


void Logger::setErrLevel(int aStderrLevel, bool aErrToStdout)
{
  if (aStderrLevel<LOG_EMERG || aStderrLevel>LOG_DEBUG) return;
  mStderrLevel = aStderrLevel;
  mErrToStdout = aErrToStdout;
}


void Logger::setLogHandler(LoggerCB aLoggerCB, void *aContextPtr)
{
  mLoggerCB = aLoggerCB;
  mLoggerContextPtr = aContextPtr;
}


// MARK: - P44LoggingObj

P44LoggingObj::P44LoggingObj() :
  logLevelOffset(0)
{
}


string P44LoggingObj::logContextPrefix()
{
  return string_format("P44LoggingObj @%p", this);
}


bool P44LoggingObj::logEnabled(int aLogLevel)
{
  return globalLogger.logEnabled(aLogLevel, getLogLevelOffset());
}

void P44LoggingObj::log(int aErrLevel, const char *aFmt, ... )
{
  if (logEnabled(aErrLevel)) {
    va_list args;
    va_start(args, aFmt);
    // get the prefix (can be disabled by starting log line with \r)
    string message;
    string context;
    if (*aFmt!='\r') {
      context = logContextPrefix();
    }
    else {
      // prefix disabled, skip marker
      aFmt++; // skip \r
    }
    // format the message
    string_format_v(message, true, aFmt, args);
    va_end(args);
    globalLogger.contextLogStr_always(aErrLevel, context, message);
  }
}

int P44LoggingObj::getLogLevelOffset()
{
  return logLevelOffset;
}


void P44LoggingObj::setLogLevelOffset(int aLogLevelOffset)
{
  if (aLogLevelOffset!=logLevelOffset) {
    log(globalLogger.getLogLevel(), "### changed log level offset from %d to %d", logLevelOffset, aLogLevelOffset);
    logLevelOffset = aLogLevelOffset;
  }
}

