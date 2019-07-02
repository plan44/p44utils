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

#ifndef __p44utils__serialcomm__
#define __p44utils__serialcomm__

#include "p44utils_common.hpp"

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
    } ErrorCodes;
    
    static const char *domain() { return "SerialComm"; }
    virtual const char *getErrorDomain() const { return SerialCommError::domain(); };
    SerialCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };



  class SerialComm;
  typedef boost::intrusive_ptr<SerialComm> SerialCommPtr;

  /// A class providing serialized access to a serial device attached directly or via a TCP proxy
  class SerialComm : public FdComm
  {
    typedef FdComm inherited;

    // serial connection
    string connectionPath;
    uint16_t connectionPort;
    int baudRate;
    int charSize; // character size 5..8 bits
    bool parityEnable;
    bool evenParity;
    bool twoStopBits;
    bool hardwareHandshake;
    bool connectionOpen;
    int connectionFd;
    struct termios oldTermIO;
    bool serialConnection;
    bool reconnecting;
    MLTicket reconnectTicket;

  public:

    SerialComm(MainLoop &aMainLoop);
    virtual ~SerialComm();

    /// Specify the serial connection parameters as single string
    /// @param aConnectionSpec "/dev[:commParams]" or "hostname[:port]"
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    /// @param aDefaultCommParams default communication parameters (in case spec does not contain :commParams)
    /// @note commParams syntax is: [baud rate][,[bits][,[parity][,[stopbits][,[H]]]]]
    ///   - parity can be O, E or N
    ///   - H means hardware handshake enabled
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

    /// @return connection path (IP address or device path)
    string getConnectionPath() { return connectionPath; };

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
    virtual void dataExceptionHandler(int aFd, int aPollFlags);


  private:

    void reconnectHandler();

  };

} // namespace p44

#endif /* defined(__p44utils__serialcomm__) */
