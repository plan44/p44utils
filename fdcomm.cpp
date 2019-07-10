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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "fdcomm.hpp"

#include <sys/ioctl.h>
#include <sys/poll.h>

using namespace p44;

FdComm::FdComm(MainLoop &aMainLoop) :
  dataFd(-1),
  mainLoop(aMainLoop),
  delimiter(0),
  delimiterPos(string::npos)
{
}


FdComm::~FdComm()
{
  // unregister handlers
  setFd(-1);
}


void FdComm::setFd(int aFd)
{
  if (dataFd!=aFd) {
    if (dataFd>=0) {
      // unregister previous fd
      mainLoop.unregisterPollHandler(dataFd);
      dataFd = -1;
    }
    dataFd = aFd;
    if (dataFd>=0) {
      // register new fd
      mainLoop.registerPollHandler(
        dataFd,
        (receiveHandler ? POLLIN : 0) | // report ready to read if we have a handler
        (transmitHandler ? POLLOUT : 0), // report ready to transmit if we have a handler
        boost::bind(&FdComm::dataMonitorHandler, this, _1, _2)
      );
    }
  }
}


void FdComm::stopMonitoringAndClose()
{
  if (dataFd>=0) {
    mainLoop.unregisterPollHandler(dataFd);
    close(dataFd);
    dataFd = -1;
  }
}



void FdComm::dataExceptionHandler(int aFd, int aPollFlags)
{
  FOCUSLOG("FdComm::dataExceptionHandler(fd==%d, pollflags==0x%X)", aFd, aPollFlags);
}



bool FdComm::dataMonitorHandler(int aFd, int aPollFlags)
{
  FdCommPtr keepMeAlive(this); // make sure this object lives until routine terminates
  FOCUSLOG("FdComm::dataMonitorHandler(time==%lld, fd==%d, pollflags==0x%X)", MainLoop::now(), aFd, aPollFlags);
  // Note: test POLLIN first, because we might get a POLLHUP in parallel - so make sure we process data before hanging up
  if ((aPollFlags & POLLIN) && receiveHandler) {
    // Note: on linux a socket closed server side does not return POLLHUP, but POLLIN with no data
    size_t bytes = numBytesReady();
    FOCUSLOG("- POLLIN with %zd bytes ready", bytes);
    if (bytes>0) {
      // check if in delimited mode (e.g. line by line)
      if (delimiter) {
        // receive into buffer
        receiveAndAppendToString(receiveBuffer);
        // check data and call back if we have collected a delimited string already
        checkReceiveData();
      }
      else {
        FOCUSLOG("- calling receive handler");
        receiveHandler(ErrorPtr());
      }
    }
    else {
      // alerted for read, but nothing to read any more - is also an exception
      FOCUSLOG("- POLLIN with no data - calling data exception handler");
      dataExceptionHandler(aFd, aPollFlags);
      aPollFlags = 0; // handle only once
    }
  }
  if (aPollFlags & POLLHUP) {
    // other end has closed connection
    FOCUSLOG("- POLLHUP - calling data exception handler");
    dataExceptionHandler(aFd, aPollFlags);
  }
  else if ((aPollFlags & POLLOUT)) {
    if (!sendBufferedData() && transmitHandler) {
      FOCUSLOG("- POLLOUT - calling data transmit handler");
      transmitHandler(ErrorPtr());
    }
  }
  else if (aPollFlags & POLLERR) {
    // error
    FOCUSLOG("- POLLERR - calling data exception handler");
    dataExceptionHandler(aFd, aPollFlags);
  }
  // handled
  return true;
}


void FdComm::checkReceiveData()
{
  if (delimiterPos==string::npos) {
    // no delimiter pending
    delimiterPos = receiveBuffer.find(delimiter);
    if (delimiterPos!=string::npos) {
      FOCUSLOG("- found delimiter, calling receive handler");
      receiveHandler(ErrorPtr());
    }
  }
}


bool FdComm::sendBufferedData()
{
  size_t toSend = transmitBuffer.size();
  if (toSend==0) {
    if (transmitHandler.empty())
      mainLoop.changePollFlags(dataFd, 0, POLLOUT); // done, we don't need POLLOUT any more
    return false;
  }
  // send as much as possible
  ErrorPtr err;
  size_t sent = transmitBytes(toSend, (const uint8_t *)transmitBuffer.c_str(), err);
  if (sent<toSend)
    transmitBuffer.erase(0,sent);
  else
    transmitBuffer.clear();
  return true; // buffered send still in progress
}


bool FdComm::receiveDelimitedString(string &aString)
{
  if (delimiterPos==string::npos) return false; // none ready
  // also remove CR if delimiter is LF
  size_t eraseSz = delimiterPos+1;
  if (delimiter=='\n' && delimiterPos>0 && receiveBuffer[delimiterPos-1]=='\r') {
    delimiterPos--;
  }
  aString.assign(receiveBuffer, 0, delimiterPos);
  receiveBuffer.erase(0, eraseSz);
  delimiterPos = string::npos; // consumed this one, ready for next
  // check for more delimited strings that might already be in the buffer
  mainLoop.executeNow(boost::bind(&FdComm::checkReceiveData, this));
  return true;
}


void FdComm::sendString(const string &aString)
{
  if (transmitBuffer.empty()) {
    if (transmitHandler.empty())
      mainLoop.changePollFlags(dataFd, POLLOUT, 0); // we need POLLOUT even if no transmit handler is set
    transmitBuffer = aString;
    sendBufferedData();
  }
  else {
    transmitBuffer.append(aString);
  }
}



void FdComm::setReceiveHandler(StatusCB aReceiveHandler, char aDelimiter)
{
  if (receiveHandler.empty()!=aReceiveHandler.empty()) {
    receiveHandler = aReceiveHandler;
    if (dataFd>=0) {
      // If connected already, update poll flags to include data-ready-to-read
      // (otherwise, flags will be set when connection opens)
      if (receiveHandler.empty())
        mainLoop.changePollFlags(dataFd, 0, POLLIN); // clear POLLIN
      else
        mainLoop.changePollFlags(dataFd, POLLIN, 0); // set POLLIN
    }
  }
  delimiter = aDelimiter;
  delimiterPos = string::npos;
  receiveHandler = aReceiveHandler;
}


void FdComm::setTransmitHandler(StatusCB aTransmitHandler)
{
  if (transmitHandler.empty()!=aTransmitHandler.empty()) {
    transmitHandler = aTransmitHandler;
    if (dataFd>=0) {
      // If connected already, update poll flags to include ready-for-transmit
      // (otherwise, flags will be set when connection opens)
      if (transmitHandler.empty())
        mainLoop.changePollFlags(dataFd, 0, POLLOUT); // clear POLLOUT
      else
        mainLoop.changePollFlags(dataFd, POLLOUT, 0); // set POLLOUT
    }
  }
}


size_t FdComm::transmitBytes(size_t aNumBytes, const uint8_t *aBytes, ErrorPtr &aError)
{
  // if not connected now, we can't write
  if (dataFd<0) {
    // waiting for connection to open
    return 0; // cannot transmit data yet
  }
  // connection is open, write now
  ssize_t res = write(dataFd,aBytes,aNumBytes);
  if (res<0) {
    aError = SysError::errNo("FdComm::transmitBytes: ");
    return 0; // nothing transmitted
  }
  return res;
}


bool FdComm::transmitString(const string &aString)
{
  ErrorPtr err;
  size_t res = transmitBytes(aString.length(), (uint8_t *)aString.c_str(), err);
  if (!Error::isOK(err)) {
    FOCUSLOG("FdComm: Error sending data: %s", err->text());
  }
  return Error::isOK(err) && res==aString.length(); // ok if no error and all bytes sent
}




size_t FdComm::receiveBytes(size_t aNumBytes, uint8_t *aBytes, ErrorPtr &aError)
{
  if (dataFd>=0) {
		// read
    ssize_t res = 0;
		if (aNumBytes>0) {
			res = read(dataFd,aBytes,aNumBytes); // read
      if (res<0) {
        if (errno==EWOULDBLOCK)
          return 0; // nothing received
        else {
          aError = SysError::errNo("FdComm::receiveBytes: ");
          return 0; // nothing received
        }
      }
      return res;
    }
  }
	return 0; // no fd set, nothing to read
}


ErrorPtr FdComm::receiveAndAppendToString(string &aString, ssize_t aMaxBytes)
{
  ErrorPtr err;
  size_t max = numBytesReady();
  if (aMaxBytes>0 && max>aMaxBytes) max = aMaxBytes;
  uint8_t *buf = new uint8_t[max];
  size_t b = receiveBytes(max, buf, err);
  if (Error::isOK(err)) {
    // received
    aString.append((char *)buf, b);
  }
  delete[] buf;
  return err;
}


ErrorPtr FdComm::receiveIntoString(string &aString, ssize_t aMaxBytes)
{
  aString.erase();
  return receiveAndAppendToString(aString, aMaxBytes);
}



size_t FdComm::numBytesReady()
{
  if (dataFd>=0) {
    // get number of bytes ready for reading
    int numBytes; // must be int!! FIONREAD defines parameter as *int
    int res = ioctl(dataFd, FIONREAD, &numBytes);
    return res!=0 ? 0 : numBytes;
  }
	return 0; // no fd set, nothing to read
}


void FdComm::makeNonBlocking(int aFd)
{
  if (aFd<0) aFd = dataFd;
  int flags;
  if ((flags = fcntl(aFd, F_GETFL, 0))==-1)
    flags = 0;
  fcntl(aFd, F_SETFL, flags | O_NONBLOCK);
}


// MARK: - FdStringCollector


FdStringCollector::FdStringCollector(MainLoop &aMainLoop) :
  FdComm(aMainLoop),
  ended(false)
{
  setReceiveHandler(boost::bind(&FdStringCollector::gotData, this, _1));
}


void FdStringCollector::gotData(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    receiveAndAppendToString(collectedData);
  }
  else {
    // error ends collecting
    ended = true;
  }
}



void FdStringCollector::dataExceptionHandler(int aFd, int aPollFlags)
{
  FdCommPtr keepMeAlive(this); // make sure this object lives until routine terminates
  FOCUSLOG("FdStringCollector::dataExceptionHandler(fd==%d, pollflags==0x%X), numBytesReady()=%d", aFd, aPollFlags, numBytesReady());
  if ((aPollFlags & (POLLHUP|POLLIN|POLLERR)) != 0) {
    // - other end has closed connection (POLLHUP)
    // - linux socket was closed server side and does not return POLLHUP, but POLLIN with no data
    // - error (POLLERR)
    // end polling for data
    setReceiveHandler(NULL);
    // if ending first time, call back
    if (!ended && endedCallback) {
      endedCallback(ErrorPtr());
      // Note: we do not clear the callback here, as it might hold references which are not cleanly disposable right now
    }
    // anyway, ended now
    ended = true;
  }
}


void FdStringCollector::collectToEnd(StatusCB aEndedCallback)
{
  FdCommPtr keepMeAlive(this); // make sure this object lives until routine terminates
  endedCallback = aEndedCallback;
  if (ended) {
    // if already ended when called, end right away
    if (endedCallback) {
      endedCallback(ErrorPtr());
      endedCallback = NULL;
    }
  }
}



