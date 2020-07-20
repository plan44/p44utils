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

#include "logger.hpp"

#include "utils.hpp"

using namespace p44;

// MARK: - Logger

p44::Logger globalLogger;

Logger::Logger() :
  loggerCB(NULL),
  loggerContextPtr(NULL),
  logFILE(NULL)
{
  pthread_mutex_init(&reportMutex, NULL);
  gettimeofday(&lastLogTS, NULL);
  logLevel = LOGGER_DEFAULT_LOGLEVEL;
  stderrLevel = LOG_ERR;
  deltaTime = false;
  errToStdout = true;
  daemonMode = true;
}


Logger::~Logger()
{
  if (logFILE) {
    fclose(logFILE);
    logFILE = NULL;
  }
}

#define LOGBUFSIZ 8192


bool Logger::stdoutLogEnabled(int aErrLevel)
{
  return (aErrLevel<=logLevel);
}


bool Logger::logEnabled(int aErrLevel, int aLevelOffset)
{
  if (aLevelOffset && aErrLevel>=LOG_NOTICE) {
    aErrLevel -= aLevelOffset;
    if (aErrLevel<LOG_NOTICE) aErrLevel = LOG_NOTICE;
    else if (aErrLevel>LOG_DEBUG) aErrLevel = LOG_DEBUG;
  }
  return stdoutLogEnabled(aErrLevel) || (daemonMode && aErrLevel<=stderrLevel);
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


void Logger::log(int aErrLevel, const char *aFmt, ... )
{
  if (logEnabled(aErrLevel)) {
    va_list args;
    va_start(args, aFmt);
    // format the message
    string message;
    string_format_v(message, false, aFmt, args);
    va_end(args);
    logStr_always(aErrLevel, message);
  }
}


void Logger::log_always(int aErrLevel, const char *aFmt, ... )
{
  va_list args;
  va_start(args, aFmt);
  // format the message
  string message;
  string_format_v(message, false, aFmt, args);
  va_end(args);
  logStr_always(aErrLevel, message);
}



void Logger::logStr_always(int aErrLevel, string aMessage)
{
  pthread_mutex_lock(&reportMutex);
  // create date + level
  char tsbuf[42];
  char *p = tsbuf;
  struct timeval t;
  gettimeofday(&t, NULL);
  p += strftime(p, sizeof(tsbuf), "[%Y-%m-%d %H:%M:%S", localtime(&t.tv_sec));
  p += sprintf(p, ".%03d", t.tv_usec/1000);
  if (deltaTime) {
    long long millisPassed = ((t.tv_sec*1e6+t.tv_usec) - (lastLogTS.tv_sec*1e6+lastLogTS.tv_usec))/1000; // in mS
    p += sprintf(p, "%6lldmS", millisPassed);
  }
  lastLogTS = t;
  p += sprintf(p, " %c] ", levelChars[aErrLevel]);
  // generate empty leading lines, if any
  string::size_type i=0;
  while (i<aMessage.length() && aMessage[i]=='\n') {
    logOutput_always(aErrLevel, "", "");
    i++;
  }
  // now process message, possibly multi-lined
  const char *prefix = tsbuf;
  string::size_type linestart=i;
  while (i<aMessage.length()) {
    char c = aMessage[i];
    if (c=='\n') {
      // end of line
      aMessage[i] = 0; // terminate
      // print it
      logOutput_always(aErrLevel, prefix, aMessage.c_str()+linestart);
      // set indent instead of date prefix for subsequent lines: 28 chars
      //   01234567890123456789012345678
      prefix = deltaTime ? "                                    " : "                            ";
      // new line starts after terminator
      i++;
      linestart = i;
    }
    else if (!isprint(c) && (uint8_t)c<0x80) {
      // ASCII control character, but not bit 7 set (UTF8 component char)
      aMessage.replace(i, 1, string_format("\\x%02x", (unsigned)(c & 0xFF)));
      i += 4; // single char replaced by 4 chars: \xNN
    }
    else {
      i++;
    }
  }
  logOutput_always(aErrLevel, prefix, aMessage.c_str()+linestart);
  pthread_mutex_unlock(&reportMutex);
}


void Logger::logOutput_always(int aLevel, const char *aLinePrefix, const char *aLogMessage)
{
  // output
  if (loggerCB) {
    loggerCB(loggerContextPtr, aLevel, aLinePrefix, aLogMessage);
  }
  else if (logFILE) {
    fputs(aLinePrefix, logFILE);
    fputs(aLogMessage, logFILE);
    fputs("\n", logFILE);
    fflush(logFILE);
  }
  else {
    // normal logging to stdout/err
    // - in daemon mode, only level<=stderrLevel goes to stderr
    // - in cmdline tool mode all log goes to stderr
    if (aLevel<=stderrLevel || !daemonMode) {
      // must go to stderr anyway
      fputs(aLinePrefix, stderr);
      fputs(aLogMessage, stderr);
      fputs("\n", stderr);
      fflush(stderr);
    }
    // - in daemon mode only, normal log goes to stdout (and errors are duplicated to stdout as well)
    if (daemonMode && (aLevel>stderrLevel || errToStdout)) {
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
    logFILE = fopen(aLogFilePath, "a");
  }
  else {
    if (logFILE) {
      fclose(logFILE);
      logFILE = NULL;
    }
  }
}


void Logger::setLogLevel(int aLogLevel)
{
  if (aLogLevel<LOG_EMERG || aLogLevel>LOG_DEBUG) return;
  logLevel = aLogLevel;
}


void Logger::setErrLevel(int aStderrLevel, bool aErrToStdout)
{
  if (aStderrLevel<LOG_EMERG || aStderrLevel>LOG_DEBUG) return;
  stderrLevel = aStderrLevel;
  errToStdout = aErrToStdout;
}


void Logger::setLogHandler(LoggerCB aLoggerCB, void *aContextPtr)
{
  loggerCB = aLoggerCB;
  loggerContextPtr = aContextPtr;
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
    if (*aFmt!='\r') {
      message = logContextPrefix();
      if (!message.empty()) message+=": ";
    }
    else {
      aFmt++; // skip \r
    }
    // format the message
    string_format_v(message, true, aFmt, args);
    va_end(args);
    globalLogger.logStr_always(aErrLevel, message);
  }
}

int P44LoggingObj::getLogLevelOffset()
{
  return logLevelOffset;
}


void P44LoggingObj::setLogLevelOffset(int aLogLevelOffset)
{
  log(globalLogger.getLogLevel(), "### changed log level offset to %d", aLogLevelOffset);
  logLevelOffset = aLogLevelOffset;
}

