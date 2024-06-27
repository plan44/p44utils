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

#ifndef __p44utils__serialqueue__
#define __p44utils__serialqueue__

#include "p44utils_main.hpp"

#include <time.h>

#include <list>

#include "operationqueue.hpp"

#include "serialcomm.hpp"

using namespace std;

namespace p44 {


  /// acceptBytes() can return this for a queue with an accept buffer to reject
  /// accepting bytes now because more are needed.
  #define NOT_ENOUGH_BYTES -1


  class SQError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Transmit,
    } ErrorCodes;
    
    static const char *domain() { return "SerialQueue"; };
    virtual const char *getErrorDomain() const { return SQError::domain(); };
    SQError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };



  class SerialOperation;
  class SerialOperationQueue;
  class SerialOperationSendAndReceive;

  typedef boost::intrusive_ptr<SerialOperation> SerialOperationPtr;

  /// SerialOperation transmitter
  typedef boost::function<size_t (size_t aNumBytes, const uint8_t *aBytes)> SerialOperationTransmitter;


  /// Serial operation
  class SerialOperation : public Operation
  {
    typedef Operation inherited;

  protected:

    SerialOperationTransmitter mTransmitter;

  public:

    /// set transmitter
    /// @param aTransmitter callback to be used to transmit data
    void setTransmitter(SerialOperationTransmitter aTransmitter);

    /// is called to deliver received bytes
    /// @param aNumBytes number of bytes ready for accepting
    /// @param aBytes pointer to bytes buffer
    /// @return number of bytes operation could accept, 0 if operation does accept none,
    ///   NOT_ENOUGH_BYTES if operation would accept bytes, but not enough of them are ready.
    ///   Note that NOT_ENOUGH_BYTES may only be used when the SerialQueue has a
    ///   buffer for re-assembling messages (see SerialQueue::setAcceptBuffer())
    virtual ssize_t acceptBytes(size_t aNumBytes, uint8_t *aBytes);

  };



  /// Send operation
  class SerialOperationSend : public SerialOperation
  {
    typedef SerialOperation inherited;

    size_t mDataSize;
    size_t mAppendIndex;
    uint8_t *mDataP;

  public:

    /// constructor
    SerialOperationSend();

    /// destructor
    virtual ~SerialOperationSend();

    /// set how much data to send
    /// @param aDataSize size of data buffer to prepare
    void setDataSize(size_t aDataSize);

    /// clear all data
    void clearData();

    /// append data to buffer
    /// @param aNumBytes number of bytes to add
    /// @param aBytes bytes to append
    void appendData(size_t aNumBytes, uint8_t *aBytes);

    /// append single byte to buffer
    /// @param aByte byte to append
    void appendByte(uint8_t aByte);

    /// will be called by queue when data can be sent
    virtual bool initiate();

  };
  typedef boost::intrusive_ptr<SerialOperationSend> SerialOperationSendPtr;


  /// receive operation
  class SerialOperationReceive : public SerialOperation
  {
    typedef SerialOperation inherited;

    size_t mExpectedBytes;
    uint8_t *mDataP;
    size_t mDataIndex;

  public:

    /// constructor
    SerialOperationReceive();

    /// destructor
    virtual ~SerialOperationReceive();

    /// set how much data to send
    /// @param aExpectedBytes how many bytes we expect to receive
    void setExpectedBytes(size_t aExpectedBytes);

    /// clear all data
    void clearData();

    /// get size of data received
    /// @return size of data accessible via getDataP()
    size_t getDataSize() { return mDataIndex; };

    /// get data buffer pointer
    /// @return pointer to data
    uint8_t *getDataP() { return mDataP; };

    /// accepts bytes into buffer until expected number
    virtual ssize_t acceptBytes(size_t aNumBytes, uint8_t *aBytes);

    /// returns true when enough bytes were received
    virtual bool hasCompleted();

    /// abort
    virtual void abortOperation(ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<SerialOperationReceive> SerialOperationReceivePtr;



  /// SerialOperation receiver
  typedef boost::function<size_t (size_t aMaxBytes, uint8_t *aBytes)> SerialOperationReceiver;

  /// SerialOperation extra bytes handler
  typedef boost::function<ssize_t (size_t aNumBytes, const uint8_t *aBytes)> SerialOperationExtraBytesHandler;


  /// Serial operation queue
  class SerialOperationQueue : public OperationQueue
  {
    typedef OperationQueue inherited;

    SerialOperationTransmitter mTransmitter;
    SerialOperationReceiver mReceiver;
    SerialOperationExtraBytesHandler mExtraBytesHandler;

    size_t mAcceptBufferSize;
    size_t mBufferedBytes;
    uint8_t *mAcceptBufferP;

	public:

    /// the serial communication channel
    SerialCommPtr mSerialComm;

    /// create operation queue linked into specified Synchronous IO mainloop
    SerialOperationQueue(MainLoop &aMainLoop = MainLoop::currentMainLoop());

    /// destructor
    virtual ~SerialOperationQueue();

    /// set transmitter to be used for all operations (for sending data to hardware)
    void setTransmitter(SerialOperationTransmitter aTransmitter);

    /// set receiver (which actually gets data from hardware)
    void setReceiver(SerialOperationReceiver aReceiver);

    /// set extra byte handler (which gets "transmitted" extra bytes received from hardware and not accepted by queued operations)
    void setExtraBytesHandler(SerialOperationExtraBytesHandler aExtraBytesHandler);

    /// set an accept buffer
    /// @param aBufferSize size of buffer that will hold received bytes until they can be processed.
    ///   setting a buffer size allows operations and acceptExtraBytes() to not accept bytes when there are to few bytes ready
    void setAcceptBuffer(size_t aBufferSize);

    /// queue a new operation
    /// @param aOperation the serial IO operation to queue
    void queueSerialOperation(SerialOperationPtr aOperation);

    /// called to process extra bytes after all pending operations have processed their bytes
    /// @param aNumBytes number of bytes ready to accept
    /// @param aBytes bytes ready to accept
    /// @return number of extra bytes that could be accepted, 0 if none, NOT_ENOUGH_BYTES if extra bytes would be accepted,
    ///   but not enough of them are ready. Note that NOT_ENOUGH_BYTES may only be used when the SerialQueue has a
    ///   buffer for re-assembling messages (see setAcceptBuffer())
    /// @note if setExtraBytesHandler() has set a handler, this is called here. Otherwise, the baseclass' implementation
    ///   always returns 0 (does not accept any extra bytes by default)
    virtual ssize_t acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes);

    /// standard transmitter
    size_t standardTransmitter(size_t aNumBytes, const uint8_t *aBytes);

    /// standard receiver
    size_t standardReceiver(size_t aMaxBytes, uint8_t *aBytes);

  private:
    /// base class implementation: deliver bytes to the most recent waiting operation,
    ///   call acceptExtraBytes if bytes are left after all operations had chance to accept bytes.
    virtual size_t acceptBytes(size_t aNumBytes, uint8_t *aBytes);

    /// FdComm handler
    void receiveHandler(ErrorPtr aError);

  };


} // namespace p44


#endif /* defined(__p44utils__serialqueue__) */
