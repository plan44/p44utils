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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "fdcomm.hpp"

#include <sys/ioctl.h>

#ifndef ESP_PLATFORM
#include <poll.h>
#endif

using namespace p44;

FdComm::FdComm(MainLoop &aMainLoop) :
  mDataFd(-1),
  mMainLoop(aMainLoop),
  mDelimiter(0),
  mDelimiterPos(string::npos),
  mUnknownReadyBytes(false)
{
}


FdComm::~FdComm()
{
  // unregister handlers
  setFd(-1);
}


void FdComm::setFd(int aFd, bool aUnknownReadyBytes)
{
  mUnknownReadyBytes = aUnknownReadyBytes;
  if (mDataFd!=aFd) {
    if (mDataFd>=0) {
      // unregister previous fd
      mMainLoop.unregisterPollHandler(mDataFd);
      mDataFd = -1;
    }
    mDataFd = aFd;
    if (mDataFd>=0) {
      // register new fd
      mMainLoop.registerPollHandler(
        mDataFd,
        (mReceiveHandler ? POLLIN : 0) | // report ready to read if we have a handler
        (mTransmitHandler ? POLLOUT : 0), // report ready to transmit if we have a handler
        boost::bind(&FdComm::dataMonitorHandler, this, _1, _2)
      );
    }
  }
}


void FdComm::stopMonitoringAndClose()
{
  if (mDataFd>=0) {
    mMainLoop.unregisterPollHandler(mDataFd);
    close(mDataFd);
    mDataFd = -1;
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
  if ((aPollFlags & POLLIN) && mReceiveHandler) {
    size_t bytes = 0;
    if (!mUnknownReadyBytes) {
      bytes = numBytesReady();
      FOCUSLOG("- POLLIN with %zd bytes ready", bytes);
    }
    else {
      FOCUSLOG("- POLLIN with UNKNOWN amount of data ready");
    }
    // Note: on linux a socket closed server side does not return POLLHUP, but POLLIN with no data
    if (bytes>0 || mUnknownReadyBytes) {
      // check if in delimited mode (e.g. line by line)
      if (mDelimiter) {
        // receive into buffer
        receiveAndAppendToString(mReceiveBuffer);
        // check data and call back if we have collected a delimited string already
        checkReceiveData();
      }
      else {
        FOCUSLOG("- calling receive handler");
        mReceiveHandler(ErrorPtr());
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
    if (!sendBufferedData() && mTransmitHandler) {
      FOCUSLOG("- POLLOUT - calling data transmit handler");
      mTransmitHandler(ErrorPtr());
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
  if (mDelimiterPos==string::npos) {
    // no delimiter pending
    mDelimiterPos = mReceiveBuffer.find(mDelimiter);
    if (mDelimiterPos!=string::npos) {
      FOCUSLOG("- found delimiter, calling receive handler");
      mReceiveHandler(ErrorPtr());
    }
  }
}


bool FdComm::sendBufferedData()
{
  size_t toSend = mTransmitBuffer.size();
  if (toSend==0) {
    if (mTransmitHandler.empty())
      mMainLoop.changePollFlags(mDataFd, 0, POLLOUT); // done, we don't need POLLOUT any more
    return false;
  }
  // send as much as possible
  ErrorPtr err;
  size_t sent = transmitBytes(toSend, (const uint8_t *)mTransmitBuffer.c_str(), err);
  if (sent<toSend)
    mTransmitBuffer.erase(0,sent);
  else
    mTransmitBuffer.clear();
  return true; // buffered send still in progress
}


bool FdComm::receiveDelimitedString(string &aString)
{
  if (mDelimiterPos==string::npos) return false; // none ready
  // also remove CR if delimiter is LF
  size_t eraseSz = mDelimiterPos+1;
  if (mDelimiter=='\n' && mDelimiterPos>0 && mReceiveBuffer[mDelimiterPos-1]=='\r') {
    mDelimiterPos--;
  }
  aString.assign(mReceiveBuffer, 0, mDelimiterPos);
  mReceiveBuffer.erase(0, eraseSz);
  mDelimiterPos = string::npos; // consumed this one, ready for next
  // check for more delimited strings that might already be in the buffer
  mMainLoop.executeNow(boost::bind(&FdComm::checkReceiveData, this));
  return true;
}


void FdComm::sendString(const string &aString)
{
  if (mTransmitBuffer.empty()) {
    if (mTransmitHandler.empty())
      mMainLoop.changePollFlags(mDataFd, POLLOUT, 0); // we need POLLOUT even if no transmit handler is set
    mTransmitBuffer = aString;
    sendBufferedData();
  }
  else {
    mTransmitBuffer.append(aString);
  }
}



void FdComm::setReceiveHandler(StatusCB aReceiveHandler, char aDelimiter)
{
  if (mReceiveHandler.empty()!=aReceiveHandler.empty()) {
    mReceiveHandler = aReceiveHandler;
    if (mDataFd>=0) {
      // If connected already, update poll flags to include data-ready-to-read
      // (otherwise, flags will be set when connection opens)
      if (mReceiveHandler.empty())
        mMainLoop.changePollFlags(mDataFd, 0, POLLIN); // clear POLLIN
      else
        mMainLoop.changePollFlags(mDataFd, POLLIN, 0); // set POLLIN
    }
  }
  mDelimiter = aDelimiter;
  mDelimiterPos = string::npos;
  mReceiveHandler = aReceiveHandler;
}


void FdComm::setTransmitHandler(StatusCB aTransmitHandler)
{
  if (mTransmitHandler.empty()!=aTransmitHandler.empty()) {
    mTransmitHandler = aTransmitHandler;
    if (mDataFd>=0) {
      // If connected already, update poll flags to include ready-for-transmit
      // (otherwise, flags will be set when connection opens)
      if (mTransmitHandler.empty())
        mMainLoop.changePollFlags(mDataFd, 0, POLLOUT); // clear POLLOUT
      else
        mMainLoop.changePollFlags(mDataFd, POLLOUT, 0); // set POLLOUT
    }
  }
}


size_t FdComm::transmitBytes(size_t aNumBytes, const uint8_t *aBytes, ErrorPtr &aError)
{
  // if not connected now, we can't write
  if (mDataFd<0) {
    // waiting for connection to open
    return 0; // cannot transmit data yet
  }
  // connection is open, write now
  ssize_t res = write(mDataFd,aBytes,aNumBytes);
  if (res<0) {
    aError = SysError::errNo("FdComm::transmitBytes: ");
    return 0; // nothing transmitted
  }
  return (size_t)res;
}


bool FdComm::transmitString(const string &aString)
{
  ErrorPtr err;
  size_t res = transmitBytes(aString.length(), (uint8_t *)aString.c_str(), err);
  if (Error::notOK(err)) {
    FOCUSLOG("FdComm: Error sending data: %s", err->text());
  }
  return Error::isOK(err) && res==aString.length(); // ok if no error and all bytes sent
}




size_t FdComm::receiveBytes(size_t aNumBytes, uint8_t *aBytes, ErrorPtr &aError)
{
  if (mDataFd>=0) {
		// read
    ssize_t res = 0;
		if (aNumBytes>0) {
			res = read(mDataFd,aBytes,aNumBytes); // read
      if (res<0) {
        if (errno==EWOULDBLOCK)
          return 0; // nothing received
        else {
          aError = SysError::errNo("FdComm::receiveBytes: ");
          return 0; // nothing received
        }
      }
      return (size_t)res;
    }
  }
	return 0; // no fd set, nothing to read
}


ErrorPtr FdComm::receiveAndAppendToString(string &aString, ssize_t aMaxBytes)
{
  ErrorPtr err;
  size_t max;
  if (mUnknownReadyBytes) {
    max = aMaxBytes;
  }
  else {
    max = numBytesReady();
    if (aMaxBytes>0 && max>(size_t)aMaxBytes) max = (size_t)aMaxBytes;
  }
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
  if (mDataFd>=0) {
    // get number of bytes ready for reading
    int numBytes; // must be int!! FIONREAD defines parameter as *int
    int res = ioctl(mDataFd, FIONREAD, &numBytes);
    return (size_t)(res!=0 ? 0 : numBytes);
  }
	return 0; // no fd set, nothing to read
}


void FdComm::makeNonBlocking(int aFd)
{
  if (aFd<0) aFd = mDataFd;
  int flags;
  if ((flags = fcntl(aFd, F_GETFL, 0))==-1)
    flags = 0;
  fcntl(aFd, F_SETFL, flags | O_NONBLOCK);
}


// MARK: - FdStringCollector


FdStringCollector::FdStringCollector(MainLoop &aMainLoop) :
  FdComm(aMainLoop),
  mEnded(false)
{
  setReceiveHandler(boost::bind(&FdStringCollector::gotData, this, _1));
}


void FdStringCollector::gotData(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    receiveAndAppendToString(mCollectedData);
  }
  else {
    // error ends collecting
    mEnded = true;
  }
}



void FdStringCollector::dataExceptionHandler(int aFd, int aPollFlags)
{
  FdCommPtr keepMeAlive(this); // make sure this object lives until routine terminates
  FOCUSLOG("FdStringCollector::dataExceptionHandler(fd==%d, pollflags==0x%X), numBytesReady()=%d", aFd, aPollFlags, numBytesReady());
  if ((aPollFlags & (POLLHUP|POLLIN|POLLERR)) != 0) {
    // - other end has closed connection (POLLHUP)
    // - linux socket was closed server side and does not return POLLHUP, but POLLIN with no data (and mUnknownReceivedBytes is not set)
    // - error (POLLERR)
    // end polling for data
    setReceiveHandler(NoOP);
    // if ending first time, call back
    if (!mEnded && mEndedCallback) {
      mEndedCallback(ErrorPtr());
      // Note: we do not clear the callback here, as it might hold references which are not cleanly disposable right now
    }
    // anyway, ended now
    mEnded = true;
  }
}


void FdStringCollector::collectToEnd(StatusCB aEndedCallback)
{
  FdCommPtr keepMeAlive(this); // make sure this object lives until routine terminates
  mEndedCallback = aEndedCallback;
  if (mEnded) {
    // if already ended when called, end right away
    if (mEndedCallback) {
      mEndedCallback(ErrorPtr());
      mEndedCallback = NoOP;
    }
  }
}



