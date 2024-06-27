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


#include "serialqueue.hpp"

using namespace p44;


#define DEFAULT_RECEIVE_TIMEOUT (3*Second) // [uS] = 3 seconds


// MARK: - SerialOperation


// set transmitter
void SerialOperation::setTransmitter(SerialOperationTransmitter aTransmitter)
{
  mTransmitter = aTransmitter;
}


// call to deliver received bytes
ssize_t SerialOperation::acceptBytes(size_t aNumBytes, uint8_t *aBytes)
{
  return 0; // accept none, expect none
}


// MARK: - SerialOperationSend


SerialOperationSend::SerialOperationSend() :
  mDataP(NULL),
  mDataSize(0),
  mAppendIndex(0)
{
}


SerialOperationSend::~SerialOperationSend()
{
  clearData();
}




void SerialOperationSend::clearData()
{
  if (mDataP) {
    delete [] mDataP;
    mDataP = NULL;
  }
  mDataSize = 0;
  mAppendIndex = 0;
}


void SerialOperationSend::setDataSize(size_t aDataSize)
{
  clearData();
  if (aDataSize>0) {
    mDataSize = aDataSize;
    mDataP = new uint8_t[mDataSize];
  }
}


void SerialOperationSend::appendData(size_t aNumBytes, uint8_t *aBytes)
{
  if (mAppendIndex+aNumBytes>mDataSize)
    aNumBytes = mDataSize-mAppendIndex;
  if (aNumBytes>0) {
    memcpy(mDataP+mAppendIndex, aBytes, aNumBytes);
    mAppendIndex += aNumBytes;
  }
}


void SerialOperationSend::appendByte(uint8_t aByte)
{
  appendData(1, &aByte);
}



bool SerialOperationSend::initiate()
{
  FOCUSLOG("SerialOperationSend::initiate: sending %zd bytes now", dataSize);
  size_t res;
  if (mDataP && mTransmitter) {
    // transmit
    res = mTransmitter(mDataSize,mDataP);
    if (res!=mDataSize) {
      // error
      abortOperation(ErrorPtr(new SQError(SQError::Transmit)));
    }
    // early release
    clearData();
  }
  // executed
  return inherited::initiate();
}



// MARK: - SerialOperationReceive


SerialOperationReceive::SerialOperationReceive() :
  mDataP(NULL),
  mExpectedBytes(0),
  mDataIndex(0)
{
  // allocate buffer
  setTimeout(DEFAULT_RECEIVE_TIMEOUT);
};


SerialOperationReceive::~SerialOperationReceive()
{
  clearData();
}


void SerialOperationReceive::setExpectedBytes(size_t aExpectedBytes)
{
  mExpectedBytes = aExpectedBytes;
  mDataP = new uint8_t[mExpectedBytes];
  mDataIndex = 0;
}


void SerialOperationReceive::clearData()
{
  if (mDataP) {
    delete [] mDataP;
    mDataP = NULL;
  }
  mExpectedBytes = 0;
  mDataIndex = 0;
}


ssize_t SerialOperationReceive::acceptBytes(size_t aNumBytes, uint8_t *aBytes)
{
  // append bytes into buffer
  if (!mInitiated)
    return 0; // cannot accept bytes when not yet initiated
  if (aNumBytes>mExpectedBytes)
    aNumBytes = mExpectedBytes;
  if (aNumBytes>0) {
    memcpy(mDataP+mDataIndex, aBytes, aNumBytes);
    mDataIndex += aNumBytes;
    mExpectedBytes -= aNumBytes;
  }
  // return number of bytes actually accepted
  return aNumBytes;
}


bool SerialOperationReceive::hasCompleted()
{
  // completed if all expected bytes received
  return mExpectedBytes<=0;
}


void SerialOperationReceive::abortOperation(ErrorPtr aError)
{
  clearData(); // don't expect any more, early release
  inherited::abortOperation(aError);
}


// MARK: - SerialOperationQueue


// Link into mainloop
SerialOperationQueue::SerialOperationQueue(MainLoop &aMainLoop) :
  inherited(aMainLoop),
  mAcceptBufferP(NULL),
  mAcceptBufferSize(0),
  mBufferedBytes(0)
{
  // Set handlers for FdComm
  mSerialComm = SerialCommPtr(new SerialComm(aMainLoop));
  mSerialComm->setReceiveHandler(boost::bind(&SerialOperationQueue::receiveHandler, this, _1));
  // TODO: once we implement buffered write, install the ready-for-transmission handler here
  //serialComm.setTransmitHandler(boost::bind(&SerialOperationQueue::transmitHandler, this, _1));
  // Set standard transmitter and receiver for operations
  setTransmitter(boost::bind(&SerialOperationQueue::standardTransmitter, this, _1, _2));
	setReceiver(boost::bind(&SerialOperationQueue::standardReceiver, this, _1, _2));
}


SerialOperationQueue::~SerialOperationQueue()
{
  mSerialComm->closeConnection();
  setAcceptBuffer(0);
}


void SerialOperationQueue::setTransmitter(SerialOperationTransmitter aTransmitter)
{
  mTransmitter = aTransmitter;
}


void SerialOperationQueue::setReceiver(SerialOperationReceiver aReceiver)
{
  mReceiver = aReceiver;
}


void SerialOperationQueue::setExtraBytesHandler(SerialOperationExtraBytesHandler aExtraBytesHandler)
{
  mExtraBytesHandler = aExtraBytesHandler;
}


ssize_t SerialOperationQueue::acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes)
{
  if (mExtraBytesHandler) {
    return mExtraBytesHandler(aNumBytes, aBytes);
  }
  else {
    return 0; // base class does not accept any extra bytes by default
  }
}


#define RECBUFFER_SIZE 100


// handles incoming data from serial interface
void SerialOperationQueue::receiveHandler(ErrorPtr aError)
{
  if (mReceiver) {
    uint8_t buffer[RECBUFFER_SIZE];
    size_t numBytes = mReceiver(RECBUFFER_SIZE, buffer);
    FOCUSLOG("SerialOperationQueue::receiveHandler: got %zd bytes to accept", numBytes);
    if (numBytes>0) {
      acceptBytes(numBytes, buffer);
    }
  }
}


// queue a new serial I/O operation
void SerialOperationQueue::queueSerialOperation(SerialOperationPtr aOperation)
{
  aOperation->setTransmitter(mTransmitter);
  inherited::queueOperation(aOperation);
}



void SerialOperationQueue::setAcceptBuffer(size_t aBufferSize)
{
  if (mAcceptBufferP) {
    delete [] mAcceptBufferP;
    mAcceptBufferP = NULL;
  }
  mBufferedBytes = 0;
  mAcceptBufferSize = 0;
  if (aBufferSize>0) {
    mAcceptBufferSize = aBufferSize;
    mAcceptBufferP = new uint8_t[mAcceptBufferSize];
  }
}



// deliver bytes to the most recent waiting operation
size_t SerialOperationQueue::acceptBytes(size_t aNumBytes, uint8_t *aBytes)
{
  FOCUSLOG("Start of SerialOperationQueue::acceptBytes: received %zd new bytes to accept", aNumBytes);
  // first check if some operations still need processing
  size_t acceptedBytes = 0;
  uint8_t *bytes;
  size_t numBytes;
  while (aNumBytes>0) {
    FOCUSLOG("- %zd bytes left to process", aNumBytes);
    // buffered mode?
    if (mAcceptBufferSize>0) {
      // buffered mode - collect in buffer and then let operations process
      ssize_t by = mAcceptBufferSize - mBufferedBytes;
      if (by>0) {
        // still room in the buffer, append to buffer
        if (aNumBytes<by) by = aNumBytes; // buffer at most aNumBytes
        memcpy(mAcceptBufferP+mBufferedBytes, aBytes, by); // buffer
        mBufferedBytes += by;
        aNumBytes -= by;
        aBytes += by;
        FOCUSLOG("- %zd bytes buffered, %zd total buffered, %zd remaining", by, bufferedBytes, aNumBytes);
      }
      else {
        // buffer full, cannot store more
        LOG(LOG_DEBUG, "- %zu received bytes could neither be processed nor buffered -> ignored", aNumBytes);
        break; // no point in iterating
      }
      // initiate processing on buffered data
      bytes = mAcceptBufferP;
      numBytes = mBufferedBytes;
    }
    else {
      // unbuffered, directly process incoming data
      bytes = aBytes;
      numBytes = aNumBytes;
      aNumBytes = 0; // all must be consumed or are lost
    }
    // let operations process bytes now
    if (FOCUSLOGENABLED) {
      string s;
      for (size_t i=0; i<numBytes; i++) {
        string_format_append(s, " %02X", bytes[i]);
      }
      FOCUSLOG("- attempting to process %zd bytes: %s", numBytes, s.c_str());
    }
    ssize_t consumed = 0;
    for (OperationList::iterator pos = mOperationQueue.begin(); pos!=mOperationQueue.end(); ++pos) {
      FOCUSLOG("- offering %zd bytes to next operation to accept", numBytes);
      SerialOperationPtr sop = boost::dynamic_pointer_cast<SerialOperation>(*pos);
      if (sop) {
        consumed = sop->acceptBytes(numBytes, bytes);
        FOCUSLOG("- operation accepted %zd bytes (-1: not enough to process anything)", consumed);
      }
      if (consumed==NOT_ENOUGH_BYTES) {
        FOCUSLOG("- operation will accept bytes, but needs more at a time -> don't process more");
        break; // this operation would accept bytes, but needs more of them at a time
      }
      bytes += consumed; // advance pointer
      numBytes -= consumed; // count
      acceptedBytes += consumed;
      if (numBytes<=0)
        break; // all bytes consumed
    }
    while (numBytes>0 && consumed!=NOT_ENOUGH_BYTES) {
      // Still bytes left to accept, give chance to process these now
      FOCUSLOG("- %zd left after all pending operations asked, offer them to acceptExtraBytes()", numBytes);
      consumed = acceptExtraBytes(numBytes, bytes);
      FOCUSLOG("- acceptExtraBytes() accepted %zd bytes (-1: not enough to process anything)", consumed);
      if (consumed==0) {
        // acceptExtraBytes does not accept any bytes -> done
        break;
      }
      else if (consumed!=NOT_ENOUGH_BYTES) {
        bytes += consumed; // advance pointer
        numBytes -= consumed; // count
        acceptedBytes += consumed;
      }
    }
    // buffered mode?
    if (mAcceptBufferSize>0) {
      // in buffered mode, remove accepted bytes and keep rest for next run
      mBufferedBytes = numBytes;
      if (numBytes>0) {
        // still bytes left unprocessed, move them to the beginning of the buffer
        // Note: in buffered mode, numBytes is always less or equal acceptBufferSize, so we know we can move the rest
        memmove(mAcceptBufferP, bytes, numBytes);
      }
    }
    else {
      // unbuffered mode - bytes than cannot be processed are lost
      if (numBytes>0) {
        FOCUSLOG("SerialOperationQueue::acceptBytes - %zd unprocessed bytes -> ignored", aNumBytes);
      }
      break;
    }
  } // while bytes to process
  // final check if some operations might be complete now
  processOperations();
  // return number of accepted bytes
  FOCUSLOG("End of SerialOperationQueue::acceptBytes: accepted %zd bytes", acceptedBytes);
  return acceptedBytes;
};


// MARK: - standard transmitter and receivers




size_t SerialOperationQueue::standardTransmitter(size_t aNumBytes, const uint8_t *aBytes)
{
  FOCUSLOG("SerialOperationQueue::standardTransmitter(%zd bytes) called", aNumBytes);
  ssize_t res = 0;
  size_t numWritten = 0;
  ErrorPtr err = mSerialComm->establishConnection();
  if (Error::isOK(err)) {
    while (aNumBytes>0) {
      res = mSerialComm->transmitBytes(aNumBytes, aBytes+numWritten, err);
      if (Error::notOK(err)) {
        FOCUSLOG("Error writing serial data: %s", err->text());
        break;
      }
      else if (res<=0) {
        // 0 can happen when connection is not open, minus should not but is catched here to safegard against loop
        FOCUSLOG("transmitBytes returned res<=0 -> end transmitting");
        break;
      }
      else {
        // written some
        numWritten += res;
        aNumBytes -= res;
        if (FOCUSLOGENABLED) {
          std::string s;
          for (ssize_t i=0; i<res; i++) {
            string_format_append(s, "%02X ",aBytes[i]);
          }
          FOCUSLOG("Transmitted %zd bytes: %s", res, s.c_str());
        }
      }
    }
  }
  else {
    LOG(LOG_DEBUG, "SerialOperationQueue::standardTransmitter error - connection could not be established!");
  }
  return numWritten;
}



size_t SerialOperationQueue::standardReceiver(size_t aMaxBytes, uint8_t *aBytes)
{
  FOCUSLOG("SerialOperationQueue::standardReceiver(%zd bytes) called", aMaxBytes);
  size_t gotBytes = 0;
  if (mSerialComm->connectionIsOpen()) {
		// get number of bytes available
    ErrorPtr err;
    gotBytes = mSerialComm->receiveBytes(aMaxBytes, aBytes, err);
    if (Error::notOK(err)) {
      FOCUSLOG("- Error reading serial: %s", err->text());
      return 0;
    }
    else {
      if (FOCUSLOGENABLED) {
        if (gotBytes>0) {
          std::string s;
          for (size_t i=0; i<gotBytes; i++) {
            string_format_append(s, "%02X ",aBytes[i]);
          }
          FOCUSLOG("- Received %zd bytes: %s", gotBytes, s.c_str());
        }
      }
      else {
        FOCUSLOG("Received %zd bytes", gotBytes);
      }
    }
  }
  else {
    LOG(LOG_DEBUG, "SerialOperationQueue::standardReceiver error - connection is not open!");
  }
  return gotBytes;
}



