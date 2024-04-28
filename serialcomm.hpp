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

#ifndef __p44utils__serialcomm__
#define __p44utils__serialcomm__

#include "p44utils_main.hpp"

#include "fdcomm.hpp"

// unix I/O and network
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/param.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#if ENABLE_P44SCRIPT && !defined(ENABLE_SERIAL_SCRIPT_FUNCS)
  #define ENABLE_SERIAL_SCRIPT_FUNCS 1
#endif
#if ENABLE_SERIAL_SCRIPT_FUNCS && !ENABLE_P44SCRIPT
  #error "ENABLE_P44SCRIPT required when ENABLE_SERIAL_SCRIPT_FUNCS is set"
#endif

#if ENABLE_SERIAL_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif


using namespace std;

namespace p44 {

  class SerialCommError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      InvalidHost,
      UnknownBaudrate,
      numErrorCodes
    } ErrorCodes;
    
    static const char *domain() { return "SerialComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return SerialCommError::domain(); };
    SerialCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "InvalidHost",
      "UnknownBaudrate",
    };
    #endif // ENABLE_NAMED_ERRORS
  };



  class SerialComm;
  typedef boost::intrusive_ptr<SerialComm> SerialCommPtr;

  #if ENABLE_SERIAL_SCRIPT_FUNCS
  namespace P44Script {
    class SerialCommObj;
    typedef boost::intrusive_ptr<SerialCommObj> SerialCommObjPtr;
  }
  #endif

  /// A class providing serialized access to a serial device attached directly or via a TCP proxy
  class SerialComm : public FdComm
  {
    typedef FdComm inherited;

    #if ENABLE_SERIAL_SCRIPT_FUNCS
    friend class P44Script::SerialCommObj;
    #endif

    // serial connection
    string mConnectionPath;
    uint16_t mConnectionPort;
    int mBaudRate;
    int mCharSize; // character size 5..8 bits
    bool mParityEnable;
    bool mEvenParity;
    bool mTwoStopBits;
    bool mHardwareHandshake;
    bool mConnectionOpen;
    int mConnectionFd;
    int mDeviceOpenFlags;
    bool mUnknownReadyBytes;
    #if defined(TCGETS2)
    struct termios2 mOldTermIO;
    #else
    struct termios mOldTermIO;
    #endif
    bool mDeviceConnection;
    bool mReconnecting;
    MLTicket mReconnectTicket;

  public:

    SerialComm(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~SerialComm();

    /// Specify the serial connection parameters as single string
    /// @param aConnectionSpec "/dev[:commParams]" or "hostname[:port]"
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    /// @param aDefaultCommParams default communication parameters (in case spec does not contain :commParams)
    /// @note commParams syntax is: `[baud rate][,[bits][,[parity][,[stopbits][,[H]]]]]`
    ///   - parity can be O, E or N
    ///   - H means hardware handshake enabled
    /// @note commParams can also be set to `none` to use the device without applying any termios settings
    void setConnectionSpecification(const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams);

    /// @return true if local serial port, false otherwise (none or IP)
    static bool parseConnectionSpecification(
      const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
      string &aConnectionPath,
      int &aBaudRate,
      int &aCharSize,
      bool &aParityEnable,
      bool &aEvenParity,
      bool &aTwoStopBits,
      bool &aHardwareHandshake,
      uint16_t &aConnectionPort
    );

    /// set special operation parameters
    /// @param aOpenFlags these are the flags passed to open() when connection specification is a OS device (e.g. /dev/xx).
    ///   Default open flags are O_RDWR (O_NOCTTY is always added implicitly)
    ///   If 0 is passed, the default flags are set
    /// @param aUnknownReadyBytes if set, this indicates the FD might not be able to correctly
    ///   report the number of bytes ready to read, and the receive handler will be called even if
    ///   the number of bytes reported by FIONREAD at POLLIN is zero. The handler must NOT
    ///   rely on numBytesReady() and just read what's available (non-blocking, see `FdComm::makeNonBlocking()`).
    void setDeviceOpParams(int aDeviceOpenFlags, bool aUnknownReadyBytes);

    /// @return connection path (IP address or device path)
    string getConnectionPath() { return mConnectionPath; };

    /// establish the serial connection
    /// @note can be called multiple times, opens connection only if not already open
    /// @return error in case connection cannot be opened
    ErrorPtr establishConnection();

    /// tries to establish the connection, and will retry if opening fails right now
    /// @return true if connection is open now
    bool requestConnection();

    /// close the current connection, if any
    void closeConnection();

    /// check if connection is currently open
    bool connectionIsOpen();

    /// control DTR
    /// @param aActive if set DTR will set active, otherwise DTR will be made inactive
    void setDTR(bool aActive);

    /// control RTS
    /// @param aActive if set RTS will set active, otherwise DTR will be made inactive
    void setRTS(bool aActive);

    /// send BREAK
    void sendBreak();

  protected:

    /// This is called when
    /// an exception (HUP or error) occurs on the file descriptor
    virtual void dataExceptionHandler(int aFd, int aPollFlags) P44_OVERRIDE;

  private:

    void reconnectHandler();
    bool nativeSerialPort() { return mDeviceConnection && mBaudRate>0; }

  };


  #if ENABLE_SERIAL_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a serial interface bus
    class SerialCommObj : public StructuredLookupObject, public EventSource
    {
      typedef StructuredLookupObject inherited;
      friend class p44::SerialComm;

      SerialCommPtr mSerialComm;
    public:
      SerialCommObj(SerialCommPtr aSerialComm, char aSeparator);
      virtual ~SerialCommObj();
      virtual void deactivate() P44_OVERRIDE;
      virtual string getAnnotation() const P44_OVERRIDE { return "serial interface"; };
      SerialCommPtr serialComm() { return mSerialComm; }
    private:
      void hasData(ErrorPtr aStatus);
    };


    /// represents the global objects related to serial interfaces
    class SerialLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      SerialLookup();
    };

  } // namespace P44Script
  #endif // ENABLE_SERIAL_SCRIPT_FUNCS

} // namespace p44

#endif /* defined(__p44utils__serialcomm__) */
