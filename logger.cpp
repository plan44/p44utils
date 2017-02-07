//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

p44::Logger globalLogger;

Logger::Logger() :
  loggerCB(NULL),
  loggerContextPtr(NULL),
  logFILE(NULL)
{
  pthread_mutex_init(&reportMutex, NULL);
  logLevel = LOGGER_DEFAULT_LOGLEVEL;
  stderrLevel = LOG_ERR;
  errToStdout = true;
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


bool Logger::logEnabled(int aErrLevel)
{
  return stdoutLogEnabled(aErrLevel) || aErrLevel<=stderrLevel;
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
    pthread_mutex_lock(&reportMutex);
    va_list args;
    va_start(args, aFmt);
    // format the message
    string message;
    string_format_v(message, false, aFmt, args);
    va_end(args);
    logStr(aErrLevel, message);
  }
}


void Logger::logStr(int aErrLevel, string aMessage)
{
  if (logEnabled(aErrLevel)) {
    // create date + level
    char tsbuf[42];
    char *p = tsbuf;
    struct timeval t;
    gettimeofday(&t, NULL);
    p += strftime(p, sizeof(tsbuf), "[%Y-%m-%d %H:%M:%S", localtime(&t.tv_sec));
    p += sprintf(p, ".%03d %c] ", t.tv_usec/1000, levelChars[aErrLevel]);
    // generate empty leading lines, if any
    string::size_type i=0;
    while (i<aMessage.length() && aMessage[i]=='\n') {
      logOutput(aErrLevel, "", "");
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
        logOutput(aErrLevel, prefix, aMessage.c_str()+linestart);
        // set indent instead of date prefix for subsequent lines: 28 chars
        //   01234567890123456789012345678
        prefix = "                            ";
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
    logOutput(aErrLevel, prefix, aMessage.c_str()+linestart);
    pthread_mutex_unlock(&reportMutex);
  }
}


void Logger::logOutput(int aLevel, const char *aLinePrefix, const char *aLogMessage)
{
  // output
  if (loggerCB) {
    loggerCB(loggerContextPtr, aLevel, aLinePrefix, aLogMessage);
  }
  else if (logFILE) {
    if (stdoutLogEnabled(aLevel)) {
      fputs(aLinePrefix, logFILE);
      fputs(aLogMessage, logFILE);
      fputs("\n", logFILE);
      fflush(logFILE);
    }
  }
  else {
    // normal logging to stdout/err
    if (aLevel<=stderrLevel) {
      // must go to stderr anyway
      fputs(aLinePrefix, stderr);
      fputs(aLogMessage, stderr);
      fputs("\n", stderr);
      fflush(stderr);
    }
    if (stdoutLogEnabled(aLevel) && (aLevel>stderrLevel || errToStdout)) {
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



void Logger::logSysError(int aErrLevel, int aErrNum)
{
  if (logEnabled(aErrLevel)) {
    // obtain error number if none specified
    if (aErrNum==0)
      aErrNum = errno;
    // obtain error message
    char buf[LOGBUFSIZ];
    strerror_r(aErrNum, buf, LOGBUFSIZ);
    // show it
    log(aErrLevel, "System error message: %s\n", buf);
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

