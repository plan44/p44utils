//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "serialcomm.hpp"

using namespace p44;

#if ENABLE_SERIAL_SCRIPT_FUNCS
  #include "application.hpp" // for userlevel check
  using namespace P44Script;
#endif

#if defined(__linux__)
  #include <linux/serial.h>
#endif

#ifdef __APPLE__
  #include <sys/ioctl.h>
  #include <IOKit/serial/ioss.h> // for IOSSIOSPEED
#endif


#define DEFAULT_OPEN_FLAGS (O_RDWR)

SerialComm::SerialComm(MainLoop &aMainLoop) :
  inherited(aMainLoop),
  mConnectionPort(0),
  mBaudRate(9600),
  mCharSize(8),
  mParityEnable(false),
  mEvenParity(false),
  mTwoStopBits(false),
  mHardwareHandshake(false),
  mTxOnly(false),
  mConnectionOpen(false),
  mReconnecting(false),
  mUnknownReadyBytes(false),
  mDeviceOpenFlags(DEFAULT_OPEN_FLAGS)
{
}


SerialComm::~SerialComm()
{
  closeConnection();
}



bool SerialComm::parseConnectionSpecification(
  const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
  string &aConnectionPath,
  int &aBaudRate,
  int &aCharSize,
  bool &aParityEnable,
  bool &aEvenParity,
  bool &aTwoStopBits,
  bool &aHardwareHandshake,
  bool &aTxOnly,
  uint16_t &aConnectionPort
)
{
  // device or IP host?
  aConnectionPort = 0; // means: serial
  aConnectionPath.clear();
  aBaudRate = 9600;
  aCharSize = 8;
  aParityEnable = false;
  aEvenParity = false;
  aTwoStopBits = false;
  aHardwareHandshake = false;
  aTxOnly = false;
  if (aConnectionSpec && *aConnectionSpec) {
    aConnectionPath = aConnectionSpec;
    if (aConnectionSpec[0]=='/') {
      // serial device
      string opt = nonNullCStr(aDefaultCommParams);
      size_t n = aConnectionPath.find(":");
      if (n!=string::npos) {
        // explicit specification of communication params: baudrate, bits, parity
        opt = aConnectionPath.substr(n+1,string::npos);
        aConnectionPath.erase(n,string::npos);
      }
      if (opt.size()>0) {
        // get communication options           : [baud rate][,[bits][,[parity][,[stopbits][,[H]]]]]
        // or for not doing any termio setting : none
        string part;
        const char *p = opt.c_str();
        if (nextPart(p, part, ',')) {
          // baud rate
          if (uequals("none", part)) {
            // just char device, do not do any termios
            aBaudRate = -1; // signal "no termios" via negative baud rate
          }
          else {
            sscanf(part.c_str(), "%d", &aBaudRate);
            if (nextPart(p, part, ',')) {
              // bits
              sscanf(part.c_str(), "%d", &aCharSize);
              if (nextPart(p, part, ',')) {
                // parity: O,E,N
                if (part.size()>0) {
                  aParityEnable = false;
                  if (part[0]=='E') {
                    aParityEnable = true;
                    aEvenParity = true;
                  }
                  else if (part[0]=='O') {
                    aParityEnable = false;
                    aEvenParity = false;
                  }
                }
                if (nextPart(p, part, ',')) {
                  // stopbits: 1 or 2
                  if (part.size()>0) {
                    aTwoStopBits = part[0]=='2';
                  }
                  if (nextPart(p, part, ',')) {
                    // more options:
                    for (size_t i=0; i<part.size(); i++) {
                      switch (part[i]) {
                        case 'H': aHardwareHandshake = true; break;
                        case 'T': aTxOnly = true; break;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      return true; // real serial
    }
    else {
      // IP host
      aConnectionPort = aDefaultPort; // set default in case aConnectionSpec does not have a path number
      splitHost(aConnectionSpec, &aConnectionPath, &aConnectionPort);
      return false; // no real serial
    }
  }
  return false; // no real serial either
}



void SerialComm::setConnectionSpecification(const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams)
{
  parseConnectionSpecification(
    aConnectionSpec, aDefaultPort, aDefaultCommParams,
    mConnectionPath,
    mBaudRate,
    mCharSize,
    mParityEnable,
    mEvenParity,
    mTwoStopBits,
    mHardwareHandshake,
    mTxOnly,
    mConnectionPort
  );
  closeConnection();
}


void SerialComm::setDeviceOpParams(int aDeviceOpenFlags, bool aUnknownReadyBytes)
{
  mDeviceOpenFlags = aDeviceOpenFlags!=0 ? aDeviceOpenFlags : DEFAULT_OPEN_FLAGS;
  mUnknownReadyBytes = aUnknownReadyBytes;
}

// Setting baud rate on Linux, see sample code at the bottom of the tty_ioctl man page:
// https://www.man7.org/linux/man-pages/man4/tty_ioctl.4.html

#ifdef __APPLE__
#define OTHERBAUDRATE (999999) // one that does not exist
#elif defined(BOTHER)
#define OTHERBAUDRATE (BOTHER)
#elif P44_BUILD_OW
#define OTHERBAUDRATE (CBAUDEX)
#endif

ErrorPtr SerialComm::establishConnection()
{
  if (!mConnectionOpen) {
    // Open connection to bridge
    mConnectionFd = 0;
    int res;
    #if USE_TERMIOS2
    struct termios2 newTermIO;
    #else
    struct termios newTermIO;
    #endif
    // check type of connection
    mDeviceConnection = mConnectionPath[0]=='/';
    if (mDeviceConnection) {
      // char device code
      int baudRateCode = -1; // invalid
      if (nativeSerialPort()) {
        // actual serial port we want to set termios params
        // - convert the baudrate
        switch (mBaudRate) {
          // standard baud rates that should be available everywhere
          case 50 : baudRateCode = B50; break;
          case 75 : baudRateCode = B75; break;
          case 110 : baudRateCode = B110; break;
          case 134 : baudRateCode = B134; break;
          case 150 : baudRateCode = B150; break;
          case 200 : baudRateCode = B200; break;
          case 300 : baudRateCode = B300; break;
          case 600 : baudRateCode = B600; break;
          case 1200 : baudRateCode = B1200; break;
          case 1800 : baudRateCode = B1800; break;
          case 2400 : baudRateCode = B2400; break;
          case 4800 : baudRateCode = B4800; break;
          case 9600 : baudRateCode = B9600; break;
          case 19200 : baudRateCode = B19200; break;
          case 38400 : baudRateCode = B38400; break;
          case 57600 : baudRateCode = B57600; break;
          case 115200 : baudRateCode = B115200; break;
          case 230400 : baudRateCode = B230400; break;
          #if defined(__linux__)
          // linux-only baud rate codes
          case 460800 : baudRateCode = B460800; break;
          case 500000 : baudRateCode = B500000; break;
          case 576000 : baudRateCode = B576000; break;
          case 921600 : baudRateCode = B921600; break;
          case 1000000 : baudRateCode = B1000000; break;
          case 1152000 : baudRateCode = B1152000; break;
          case 1500000 : baudRateCode = B1500000; break;
          case 2000000 : baudRateCode = B2000000; break;
          case 2500000 : baudRateCode = B2500000; break;
          case 3000000 : baudRateCode = B3000000; break;
          case 3500000 : baudRateCode = B3500000; break;
          case 4000000 : baudRateCode = B4000000; break;
          #endif // defined(__linux__)
          #ifdef OTHERBAUDRATE
          // platform supports other (=custom) baudrates
          default : baudRateCode = OTHERBAUDRATE; break;
          #endif
        }
        if (baudRateCode<=0) {
          return ErrorPtr(new SerialCommError(SerialCommError::UnknownBaudrate));
        }
      }
      // assume it's a serial port
      mConnectionFd = open(mConnectionPath.c_str(), mDeviceOpenFlags|O_NOCTTY|O_NONBLOCK);
      if (mConnectionFd<0) {
        return SysError::errNo("Cannot open serial port: ");
      }
      if (nativeSerialPort()) {
        // actual serial port we want to set termios params
        // - save current port settings
        #if USE_TERMIOS2
        ioctl(mConnectionFd, TCGETS2, &mOldTermIO);
        #elif defined(TCGETS)
        ioctl(mConnectionFd, TCGETS, &mOldTermIO);
        #else
        tcgetattr(mConnectionFd, &mOldTermIO); // save current port settings
        #endif
        // see "man termios" for details
        memset(&newTermIO, 0, sizeof(newTermIO));
        // - 8-N-1,
        newTermIO.c_cflag =
          CLOCAL | // no modem control lines (local)
          (mTxOnly ? 0 : CREAD) | // rx enable/disable
          (mCharSize==5 ? CS5 : (mCharSize==6 ? CS6 : (mCharSize==7 ? CS7 : CS8))) | // char size
          (mTwoStopBits ? CSTOPB : 0) | // stop bits
          (mParityEnable ? PARENB | (mEvenParity ? 0 : PARODD) : 0) | // parity
          (mHardwareHandshake ? CRTSCTS : 0); // hardware handshake
        // - ignore parity errors
        newTermIO.c_iflag =
        mParityEnable ? INPCK : IGNPAR; // check or ignore parity
        // - no output control
        newTermIO.c_oflag = 0;
        // - no input control (non-canonical)
        newTermIO.c_lflag = 0;
        // - no inter-char time
        newTermIO.c_cc[VTIME]    = 0;   /* inter-character timer unused */
        // - receive every single char seperately
        newTermIO.c_cc[VMIN]     = 1;   /* blocking read until 1 chars received */
        // - baud rate
        #ifdef OTHERBAUDRATE
        if (baudRateCode==OTHERBAUDRATE) {
          // we have a non-standard baudrate
          #if defined(BOTHER)
          FOCUSLOG("SerialComm: requested custom baud rate = %d -> setting with BOTHER to c_ispeed/c_ospeed", mBaudRate);
          // Linux: set the baudrate directly via BOTHER
          // - output
          newTermIO.c_cflag &= ~CBAUD; // not necessary because we cleared all flags
          newTermIO.c_cflag |= BOTHER;
          newTermIO.c_ospeed = mBaudRate;
          // - input
          #ifdef IBSHIFT
          newTermIO.c_cflag &= ~(CBAUD << IBSHIFT); // not necessary because we cleared all flags
          newTermIO.c_cflag |= BOTHER << IBSHIFT;
          #endif
          newTermIO.c_ispeed = mBaudRate;
          #elif P44_BUILD_OW
          // try the alias trick
          newTermIO.c_cflag &= ~(CBAUD | CBAUDEX);
          newTermIO.c_cflag |= B38400;
          // - actual baud rate magic happens after tcsetattr(), see below
          #elif defined(__APPLE__)
          // first, set a dummy baudrate, actual apple specific setting follows below
          cfsetspeed(&newTermIO, B9600);
          #endif // __APPLE__, !BOTHER
        }
        else
        #endif // OTHERBAUDRATE
        {
          // Most compatible way to set standard baudrates
          // Note: as this ors into c_cflag, this must be after setting c_cflag initial value
          cfsetspeed(&newTermIO, baudRateCode);
        }
        // - set new params
        tcflush(mConnectionFd, TCIFLUSH);
        #if USE_TERMIOS2
        res = ioctl(mConnectionFd, TCSETS2, &newTermIO);
        #elif defined(TCGETS)
        res = ioctl(mConnectionFd, TCSETS, &newTermIO);
        #else
        res = tcsetattr(mConnectionFd, TCSANOW, &newTermIO);
        #endif
        if (res<0) {
          return SysError::errNo("Error setting serial port parameters: ");
        }
        // - baud rate settings needed after tcsetattr()
        #ifdef OTHERBAUDRATE
        if (baudRateCode==OTHERBAUDRATE && mBaudRate>0) {
          #if P44_BUILD_OW
          struct serial_struct serial;
          if (ioctl(mConnectionFd, TIOCGSERIAL, &serial)) return SysError::errNo("Error preparing for custom baudrate by getting TIOCGSERIAL: ");
          serial.flags &= ~ASYNC_SPD_MASK;
          serial.flags |= ASYNC_SPD_CUST;
          serial.custom_divisor = serial.baud_base / mBaudRate;
          FOCUSLOG("SerialComm: requested custom baud rate = %d, serial.baud_base = %d -> serial.custom_divisor = %d, ACTUAL baud = %d",
            mBaudRate, serial.baud_base, serial.custom_divisor, serial.baud_base/serial.custom_divisor
          );
          if (ioctl(mConnectionFd, TIOCSSERIAL, &serial)<0) return SysError::errNo("Error setting custom baud rate with TIOCSSERIAL: ");
          #elif defined(__APPLE__)
          speed_t speed = mBaudRate;
          if (ioctl(mConnectionFd, IOSSIOSPEED, &speed)<0) return SysError::errNo("Error setting custom baud rate with IOSSIOSPEED: ");
          #endif // __APPLE__, !P44_BUILD_OW
        }
        #endif // OTHERBAUDRATE
      }
    }
    else {
      // assume it's an IP address or hostname
      struct sockaddr_in conn_addr;
      if ((mConnectionFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return SysError::errNo("Cannot create socket: ");
      }
      // prepare IP address
      memset(&conn_addr, '0', sizeof(conn_addr));
      conn_addr.sin_family = AF_INET;
      conn_addr.sin_port = htons(mConnectionPort);
      struct hostent *server;
      server = gethostbyname(mConnectionPath.c_str());
      if (server == NULL) {
        close(mConnectionFd);
        return ErrorPtr(new SerialCommError(SerialCommError::InvalidHost));
      }
      memcpy((void *)&conn_addr.sin_addr.s_addr, (void *)(server->h_addr), sizeof(in_addr_t));
      if ((res = connect(mConnectionFd, (struct sockaddr *)&conn_addr, sizeof(conn_addr))) < 0) {
        close(mConnectionFd);
        return SysError::errNo("Cannot open socket: ");
      }
    }
    // successfully opened
    mConnectionOpen = true;
    // now set FD for FdComm to monitor
    setFd(mConnectionFd, mUnknownReadyBytes);
  }
  mReconnecting = false; // successfully opened, don't try to reconnect any more
  return ErrorPtr(); // ok
}


bool SerialComm::requestConnection()
{
  ErrorPtr err = establishConnection();
  if (Error::notOK(err)) {
    if (!mReconnecting) {
      LOG(LOG_ERR, "SerialComm: requestConnection() could not open connection now: %s -> entering background retry mode", err->text());
      mReconnecting = true;
      mReconnectTicket.executeOnce(boost::bind(&SerialComm::reconnectHandler, this), 5*Second);
    }
    return false;
  }
  return true;
}




void SerialComm::closeConnection()
{
  mReconnecting = false; // explicit close, don't try to reconnect any more
  if (mConnectionOpen) {
    // stop monitoring
    setFd(-1);
    // restore IO settings
    if (nativeSerialPort()) {
      #if USE_TERMIOS2
      ioctl(mConnectionFd, TCSETS2, &mOldTermIO);
      #elif defined(TCGETS)
      ioctl(mConnectionFd, TCSETS, &mOldTermIO);
      #else
      tcsetattr(mConnectionFd, TCSANOW, &mOldTermIO);
      #endif
    }
    // close
    close(mConnectionFd);
    // closed
    mConnectionOpen = false;
  }
}


bool SerialComm::connectionIsOpen()
{
  return mConnectionOpen;
}


// MARK: - break

#ifndef PLATFORM_HAS_SHORTBREAK
#define PLATFORM_HAS_SHORTBREAK (!P44_BUILD_OW) // assume it does, normally - but does not on OpenWrt Linux for MT7688 SoC
#endif

void SerialComm::sendBreak(MLMicroSeconds aDuration)
{
  if (!connectionIsOpen() || !nativeSerialPort()) return; // ignore
  #if PLATFORM_HAS_SHORTBREAK || !defined(TIOCGSERIAL)
  // tcsendbreak accepts duration (or we don't have means to mess with baud rate anyway)
  int breaklen = 0; // standard break, which should be >=0.25sec and <=0.5sec
  if (aDuration>0) breaklen = static_cast<int>((aDuration+MilliSecond-1)/MilliSecond);
  FOCUSLOG("- tcsendbreak with duration=%d mS", breaklen);
  tcsendbreak(mConnectionFd, breaklen);
  #else
  if (aDuration==0) {
    // standard break, which should be >=0.25sec and <=0.5sec
    DBGFOCUSLOG("- tcsendbreak with standard duration");
    tcsendbreak(mConnectionFd, 0);
  }
  else {
    // non-standard duration, need to fake it
    // - drain
    FOCUSLOG("- emulating break with custom duration=%lld by sending NUL with very low baud rate", aDuration/MilliSecond);
    tcdrain(mConnectionFd);
    DBGFOCUSLOG("- did drain before break");
    // - manipulate baud rate
    struct serial_struct oldserial;
    struct serial_struct serial;
    if (ioctl(mConnectionFd, TIOCGSERIAL, &serial)) return;
    memcpy(&oldserial, &serial, sizeof(struct serial_struct));
    serial.flags &= ~ASYNC_SPD_MASK;
    serial.flags |= ASYNC_SPD_CUST;
    // start
    serial.custom_divisor = (int)((MLMicroSeconds)serial.baud_base / 9 * aDuration / Second);
    DBGFOCUSLOG("- will set fake baud for break: serial.custom_divisor = %d", serial.custom_divisor);
    if (ioctl(mConnectionFd, TIOCSSERIAL, &serial)) return;
    DBGFOCUSLOG("- did set fake baud rate");
    // send one 0x00 with slower baudrate to fake a BREAK
    uint8_t b = 0;
    write(mConnectionFd, &b, 1);
    DBGFOCUSLOG("- did write a 0x00 byte");
    // Note: do NOT drain here, it consumes enormous time on MT7688. Just wait long enough, we drained
    //   already above, so timing should be ok with just waiting
    // await break to go out
    MainLoop::sleep(aDuration);
    DBGFOCUSLOG("- did sleep for the break duration");
    // restore actual baud rate
    DBGFOCUSLOG("- will restore actual baud rate after break: serial.custom_divisor = %d", oldserial.custom_divisor)
    if (ioctl(mConnectionFd, TIOCSSERIAL, &oldserial)) return;
    DBGFOCUSLOG("- did restore the original baud rate");
  }
  #endif
}


// MARK: - handshake signal control

void SerialComm::setDTR(bool aActive)
{
  if (!connectionIsOpen() || !nativeSerialPort()) return; // ignore
  int iFlags = TIOCM_DTR;
  ioctl(mConnectionFd, aActive ? TIOCMBIS : TIOCMBIC, &iFlags);
}


void SerialComm::setRTS(bool aActive)
{
  if (!connectionIsOpen() || !nativeSerialPort()) return; // ignore
  int iFlags = TIOCM_RTS;
  ioctl(mConnectionFd, aActive ? TIOCMBIS : TIOCMBIC, &iFlags);
}


// MARK: - handling data exception


void SerialComm::dataExceptionHandler(int aFd, int aPollFlags)
{
  DBGLOG(LOG_DEBUG, "SerialComm::dataExceptionHandler(fd==%d, pollflags==0x%X)", aFd, aPollFlags);
  bool reEstablish = false;
  if (aPollFlags & POLLHUP) {
    // other end has closed connection
    LOG(LOG_ERR, "SerialComm: serial connection was hung up unexpectely");
    reEstablish = true;
  }
  else if (aPollFlags & POLLIN) {
    // Note: on linux a socket closed server side does not return POLLHUP, but POLLIN with no data
    // alerted for read, but nothing to read any more: assume connection closed
    LOG(LOG_ERR, "SerialComm: serial connection returns POLLIN with no data: assuming connection broken");
    reEstablish = true;
  }
  else if (aPollFlags & POLLERR) {
    // error
    LOG(LOG_ERR, "SerialComm: error on serial connection: assuming connection broken");
    reEstablish = true;
  }
  // in case of error, close and re-open connection
  if (reEstablish && !mReconnecting) {
    LOG(LOG_ERR, "SerialComm: closing and re-opening connection in attempt to re-establish it after error");
    closeConnection();
    // try re-opening right now
    mReconnecting = true;
    reconnectHandler();
  }
}


void SerialComm::reconnectHandler()
{
  if (mReconnecting) {
    ErrorPtr err = establishConnection();
    if (Error::notOK(err)) {
      LOG(LOG_ERR, "SerialComm: re-connect failed: %s -> retry again later", err->text());
      mReconnecting = true;
      mReconnectTicket.executeOnce(boost::bind(&SerialComm::reconnectHandler, this), 15*Second);
    }
    else {
      LOG(LOG_NOTICE, "SerialComm: successfully reconnected to %s", mConnectionPath.c_str());
    }
  }
}


#if ENABLE_SERIAL_SCRIPT_FUNCS

// MARK: - midi scripting

// received()
static void received_func(BuiltinFunctionContextPtr f)
{
  EventSource* es = dynamic_cast<EventSource*>(f->thisObj().get());
  assert(es);
  f->finish(new OneShotEventNullValue(es, "serial data"));
}

// send(senddata)
FUNC_ARG_DEFS(send, { anyvalid });
static void send_func(BuiltinFunctionContextPtr f)
{
  SerialCommObj* o = dynamic_cast<SerialCommObj*>(f->thisObj().get());
  assert(o);
  o->serialComm()->sendString(f->arg(0)->stringValue());
  f->finish();
}

// rts(on)
// dtr(on)
FUNC_ARG_DEFS(boolarg, { numeric } );
static void rts_func(BuiltinFunctionContextPtr f)
{
  SerialCommObj* o = dynamic_cast<SerialCommObj*>(f->thisObj().get());
  assert(o);
  o->serialComm()->setRTS(f->arg(0)->boolValue());
  f->finish();
}
static void dtr_func(BuiltinFunctionContextPtr f)
{
  SerialCommObj* o = dynamic_cast<SerialCommObj*>(f->thisObj().get());
  assert(o);
  o->serialComm()->setDTR(f->arg(0)->boolValue());
  f->finish();
}

// sendbreak()
static void sendbreak_func(BuiltinFunctionContextPtr f)
{
  SerialCommObj* o = dynamic_cast<SerialCommObj*>(f->thisObj().get());
  assert(o);
  o->serialComm()->sendBreak();
  f->finish();
}

static const BuiltinMemberDescriptor serialCommMembers[] = {
  FUNC_DEF_W_ARG(send, executable|null),
  FUNC_DEF_NOARG(received, executable|null),
  FUNC_DEF_C_ARG(dtr, executable|null, boolarg),
  FUNC_DEF_C_ARG(rts, executable|null, boolarg),
  FUNC_DEF_NOARG(sendbreak, executable|null),
  BUILTINS_TERMINATOR
};

static BuiltInMemberLookup* sharedSerialCommFunctionLookupP = NULL;

SerialCommObj::SerialCommObj(SerialCommPtr aSerialComm, char aSeparator) :
  mSerialComm(aSerialComm)
{
  // install the input handler
  mSerialComm->setReceiveHandler(boost::bind(&SerialCommObj::hasData, this, _1), aSeparator);

  registerSharedLookup(sharedSerialCommFunctionLookupP, serialCommMembers);
}


void SerialCommObj::deactivate()
{
  if (mSerialComm) {
    mSerialComm->closeConnection();
    mSerialComm.reset();
  }
}


SerialCommObj::~SerialCommObj()
{
  deactivate();
}


void SerialCommObj::hasData(ErrorPtr aStatus)
{
  if (Error::isOK(aStatus)) {
    // get the data
    string data;
    if (mSerialComm->mDelimiter) {
      // receiving delimited chunks
      if (mSerialComm->receiveDelimitedString(data)) {
        sendEvent(new StringValue(data));
      }
      return;
    }
    else {
      // no delimiter, report available data
      aStatus = mSerialComm->receiveIntoString(data, 4096);
      if (Error::isOK(aStatus)) {
        sendEvent(new StringValue(data));
        return;
      }
    }
  }
  // failed
  sendEvent(new ErrorValue(aStatus));
}


// serial(serialconnectionspec)
// serial(serialconnectionspec, delimiter)
FUNC_ARG_DEFS(serial, { text }, { text|numeric|optionalarg } );
static void serial_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  SerialCommPtr serialComm = new SerialComm;
  serialComm->setConnectionSpecification(f->arg(0)->stringValue().c_str(), 2101, "none");
  char delimiter = 0;
  if (f->arg(1)->hasType(text)) delimiter = *(f->arg(1)->stringValue().c_str()); // one char or NUL
  else if (f->arg(1)->boolValue()) delimiter = '\n'; // just true means LF (and removing CRs, too)
  SerialCommObjPtr serialObj = new SerialCommObj(serialComm, delimiter);
  ErrorPtr err = serialObj->serialComm()->establishConnection();
  if (Error::isOK(err)) {
    f->finish(serialObj);
  }
  else {
    f->finish(new ErrorValue(err));
  }
}


static const BuiltinMemberDescriptor cSerialGlobals[] = {
  FUNC_DEF_W_ARG(serial, executable|null),
  BUILTINS_TERMINATOR
};

const BuiltinMemberDescriptor* p44::P44Script::serialGlobals()
{
  return cSerialGlobals;
}


#endif // ENABLE_SERIAL_SCRIPT_FUNCS
