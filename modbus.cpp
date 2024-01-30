//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "modbus.hpp"

#if ENABLE_MODBUS

#include "application.hpp" // for temp dir path
#include "serialcomm.hpp" // for parameter parsing
#include "crc32.hpp"
#include "utils.hpp"

using namespace p44;

#if ENABLE_MODBUS_SCRIPT_FUNCS
using namespace P44Script;
#endif

// MARK: - ModbusConnection

ModbusConnection::ModbusConnection() :
  mModbus(NULL),
  mIsTcp(false),
  mDoAcceptConnections(false),
  mServerSocket(-1),
  mSlaveAddress(-1), // none
  mConnected(false),
  mFloatMode(float_dcba) // this was the standard mode in older libmodbus
{
}


ModbusConnection::~ModbusConnection()
{
  clearModbusContext();
}


void ModbusConnection::clearModbusContext()
{
  close();
  if (mModbus) {
    modbus_free(mModbus);
    mModbus = NULL;
  }
}


void ModbusConnection::setDebug(bool aDebugEnabled)
{
  if (mModbus) {
    modbus_set_debug(mModbus, aDebugEnabled);
  }
}


extern "C" {

  void setRts(modbus_t* ctx, int on, void* cbctx)
  {
    ModbusConnection* modbusConnection = (ModbusConnection*)cbctx;
    if (modbusConnection && modbusConnection->mModbusTxEnable) {
      modbusConnection->mModbusTxEnable->set(on);
      if (modbusConnection->mModbusRxEnable) {
        // we have separate Rx enable, set this to the inverse of Tx enable
        modbusConnection->mModbusRxEnable->set(!on);
      }
    }
  }

}



ErrorPtr ModbusConnection::setConnectionSpecification(
  const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
  const char *aTransmitEnableSpec, MLMicroSeconds aTxDisableDelay,
  const char *aReceiveEnableSpec,
  int aByteTimeNs,
  modbus_error_recovery_mode aRecoveryMode
)
{
  ErrorPtr err;

  // get rid of old context
  clearModbusContext();
  // parse the connection spec
  string connectionPath;
  uint16_t connectionPort;
  int baudRate;
  int charSize; // character size 5..8 bits
  bool parityEnable;
  bool evenParity;
  bool twoStopBits;
  bool hardwareHandshake;
  mIsTcp = !SerialComm::parseConnectionSpecification(
    aConnectionSpec, aDefaultPort, aDefaultCommParams,
    connectionPath,
    baudRate,
    charSize,
    parityEnable,
    evenParity,
    twoStopBits,
    hardwareHandshake,
    connectionPort
  );
  int mberr = 0;
  if (!mIsTcp) {
    bool rs232 = aTransmitEnableSpec && strcasecmp("RS232", aTransmitEnableSpec)==0;
    if (!rs232) {
      if (aTransmitEnableSpec!=NULL && *aTransmitEnableSpec!=0 && strcasecmp("RTS", aTransmitEnableSpec)!=0) {
        // not using native RTS, but digital IO specification (or * as placeholder for pinspec to be set separately with setDriverEnablePins())
        if (strcmp(aTransmitEnableSpec,"*")!=0) mModbusTxEnable = DigitalIoPtr(new DigitalIo(aTransmitEnableSpec, true, false));
      }
      if (aReceiveEnableSpec) {
        mModbusRxEnable = DigitalIoPtr(new DigitalIo(aReceiveEnableSpec, true, true));
      }
    }
    if (baudRate==0 || connectionPath.empty()) {
      err = Error::err<ModBusError>(ModBusError::InvalidConnParams, "invalid RTU connection params");
    }
    else {
      mModbus = modbus_new_rtu(
        connectionPath.c_str(),
        baudRate,
        parityEnable ? (evenParity ? 'E' : 'O') : 'N',
        charSize,
        twoStopBits ? 2 : 1
      );
      if (mModbus==0) {
        mberr = errno;
      }
      else {
        if (aByteTimeNs>0) {
          setByteTimeNs(aByteTimeNs);
        }
        if (rs232) {
          if (modbus_rtu_set_serial_mode(mModbus, MODBUS_RTU_RS232)<0) mberr = errno;
        }
        else {
          // set custom RTS if needed (FIRST, otherwise modbus_rtu_set_serial_mode() might fail when TIOCSRS485 does not work)
          if (mberr==0 && mModbusTxEnable) {
            if (modbus_rtu_set_custom_rts_ex(mModbus, setRts, this)<0) mberr = errno;
          }
          if (mberr==0) {
            if (modbus_rtu_set_serial_mode(mModbus, MODBUS_RTU_RS485)<0) mberr = errno;
          }
          if (mberr==0) {
            if (modbus_rtu_set_rts(mModbus, MODBUS_RTU_RTS_UP)<0) mberr = errno;
          }
        }
        if (mberr==0) {
          if (aTxDisableDelay!=Never) {
            if (modbus_rtu_set_rts_delay(mModbus, (int)aTxDisableDelay)<0) mberr = errno;
          }
        }
        if (mberr==0) {
          if (modbus_set_error_recovery(mModbus, aRecoveryMode)) mberr = errno;
        }
      }
    }
  }
  else {
    if (!aConnectionSpec || *aConnectionSpec==0) {
      err = Error::err<ModBusError>(ModBusError::InvalidConnParams, "invalid TCP connection params");
    }
    mModbus = modbus_new_tcp(connectionPath.c_str(), connectionPort);
    if (mModbus==0) mberr = errno;
  }
  if (Error::isOK(err) && mberr!=0) {
    err = ModBusError::err<ModBusError>(mberr);
    clearModbusContext();
  }
  if (Error::isOK(err)) {
    mbContextReady();
  }
  return err;
}


ErrorPtr ModbusConnection::setByteTimeNs(int aByteTimeNs)
{
  ErrorPtr err;
  OLOG(LOG_DEBUG, "Setting explicit byte time: %d nS, calculated value is %d nS", aByteTimeNs, modbus_rtu_get_byte_time(mModbus));
  if (modbus_rtu_set_byte_time(mModbus, (int)aByteTimeNs)) err = ModBusError::err<ModBusError>(errno);
  return err;
}


ErrorPtr ModbusConnection::setRecoveryMode(modbus_error_recovery_mode aRecoveryMode)
{
  ErrorPtr err;
  if (modbus_set_error_recovery(mModbus, aRecoveryMode)) err = ModBusError::err<ModBusError>(errno);
  return err;
}




void ModbusConnection::mbContextReady()
{
  if (mSlaveAddress>=0) {
    modbus_set_slave(mModbus, mSlaveAddress);
  }
}


bool ModbusConnection::isCommErr(ErrorPtr aError)
{
  if (aError && aError->isDomain(ModBusError::domain())) {
    long err = aError->getErrorCode();
    if (
      err==ModBusError::SysErr+ETIMEDOUT ||
      err==ModBusError::SysErr+ECONNRESET ||
      err==ModBusError::MBErr+EMBBADDATA
    )
      return true;
  }
  return false;
}




void ModbusConnection::setSlaveAddress(int aSlaveAddress)
{
  if (aSlaveAddress!=mSlaveAddress) {
    mSlaveAddress = aSlaveAddress;
    if (mModbus && mSlaveAddress>=0) {
      modbus_set_slave(mModbus, aSlaveAddress);
    }
  }
}


ErrorPtr ModbusConnection::connect(bool aAutoFlush)
{
  ErrorPtr err;
  if (!mModbus) {
    err = Error::err<ModBusError>(ModBusError::InvalidConnParams, "no valid connection parameters - cannot open connection");
  }
  if (!mConnected) {
    if (mIsTcp && mDoAcceptConnections) {
      // act as TCP server, waiting for connections
      mServerSocket = modbus_tcp_listen(mModbus, 1);
      if (mServerSocket<0) {
        return Error::err<ModBusError>(errno)->withPrefix("cannot listen: ");
      }
      // - install connection watcher
      MainLoop::currentMainLoop().registerPollHandler(mServerSocket, POLLIN, boost::bind(&ModbusConnection::connectionAcceptHandler, this, _1, _2));
      mConnected = true;
    }
    else {
      // act as TCP client or just serial connection
      if (modbus_connect(mModbus)<0) {
        if (errno!=EINPROGRESS) {
          err = ModBusError::err<ModBusError>(errno)->withPrefix("connecting: ");
        }
      }
      if (Error::isOK(err)) {
        if (aAutoFlush) {
          flush(); // flush garbage possibly already in communication device buffers
        }
        startServing(); // start serving in case this is a Modbus slave
        mConnected = true;
      }
    }
  }
  return err;
}


bool ModbusConnection::connectionAcceptHandler(int aFd, int aPollFlags)
{
  if (aPollFlags & POLLIN) {
    // server socket has data, means connection waiting to get accepted
    modbus_tcp_accept(mModbus, &mServerSocket);
    startServing();
  }
  // handled
  return true;

}


void ModbusConnection::close()
{
  if (mModbus && mConnected) {
    if (mServerSocket>=0) {
      MainLoop::currentMainLoop().unregisterPollHandler(mServerSocket);
      ::close(mServerSocket);
    }
    modbus_close(mModbus);
  }
  mConnected = false;
}


int ModbusConnection::flush()
{
  int flushed = 0;
  if (mModbus) {
    flushed = modbus_flush(mModbus);
    FOCUSOLOG("flushed, %d bytes", flushed);
  }
  return flushed;
}



double ModbusConnection::getAsDouble(const uint16_t *aTwoRegs)
{
  switch (mFloatMode) {
    case float_abcd: return modbus_get_float_abcd(aTwoRegs);
    case float_badc: return modbus_get_float_badc(aTwoRegs);
    case float_cdab: return modbus_get_float_cdab(aTwoRegs);
    default:
    case float_dcba: return modbus_get_float_dcba(aTwoRegs);
  }
}


void ModbusConnection::setAsDouble(uint16_t *aTwoRegs, double aDouble)
{
  switch (mFloatMode) {
    case float_abcd: modbus_set_float_abcd((float)aDouble, aTwoRegs); break;
    case float_badc: modbus_set_float_badc((float)aDouble, aTwoRegs); break;
    case float_cdab: modbus_set_float_cdab((float)aDouble, aTwoRegs); break;
    default:
    case float_dcba: modbus_set_float_dcba((float)aDouble, aTwoRegs); break;
  }
}


void ModbusConnection::buildExceptionResponse(sft_t &aSft, int aExceptionCode, const char* aErrorText, ModBusPDU& aRsp, int& aRspLen)
{
  aRspLen = modbus_build_exception_response(
    mModbus,
    &aSft,
    aExceptionCode,
    (uint8_t *)aRsp,
    FALSE, // no flushing (and blocking!!)
    "Modbus exception %d - %s: %s\n", aExceptionCode, modbus_strerror(MODBUS_ENOBASE+aExceptionCode), nonNullCStr(aErrorText)
  );
}


void ModbusConnection::buildExceptionResponse(sft_t &aSft, ErrorPtr aError, ModBusPDU& aRsp, int& aRspLen)
{
  if (Error::notOK(aError) && aError->isDomain(ModBusError::domain())) {
    int ex = (int)aError->getErrorCode()-ModBusError::MBErr;
    if (ex<MODBUS_EXCEPTION_MAX) {
      buildExceptionResponse(aSft, ex, aError->text(), aRsp, aRspLen);
    }
  }
  else {
    buildExceptionResponse(aSft, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE, Error::text(aError), aRsp, aRspLen);
  }
}


void ModbusConnection::buildResponseBase(sft_t &aSft, ModBusPDU& aRsp, int& aRspLen)
{
  aRspLen = modbus_build_response_basis(mModbus, &aSft, aRsp);
}


bool ModbusConnection::appendToMessage(const uint8_t *aDataP, int aNumBytes, ModBusPDU& aMsg, int& aMsgLen)
{
  if (aMsgLen+aNumBytes>MODBUS_MAX_PDU_LENGTH) return false;
  if (aDataP)
    memcpy(&aMsg[aMsgLen], aDataP, aNumBytes);
  else
    memset(&aMsg[aMsgLen], 0, aNumBytes);
  aMsgLen += aNumBytes;
  return true;
}





// MARK: - ModbusMaster


ModbusMaster::ModbusMaster()
{
}


ModbusMaster::~ModbusMaster()
{
}


ErrorPtr ModbusMaster::connectAsMaster()
{
  ErrorPtr err = connect();
  if (Error::isOK(err) && !mIsTcp) {
    if (mSlaveAddress<0) {
      err = Error::err<ModBusError>(ModBusError::InvalidSlaveAddr, "no slave address set");
    }
  }
  return err;
}


ErrorPtr ModbusMaster::readSlaveInfo(string& aId, bool& aRunIndicator)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connectAsMaster();
  if (Error::isOK(err)) {
    ModBusPDU slaveid;
    int bytes = modbus_report_slave_id(mModbus, MODBUS_MAX_PDU_LENGTH, slaveid);
    if (bytes<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
    else {
      aId.assign((const char*)slaveid+2, bytes-2);
      aRunIndicator = true; // FIXME: actually get the value
    }
  }
  if (!wasConnected) close();
  return err;
}


ErrorPtr ModbusMaster::findSlaves(SlaveAddrList& aSlaveAddrList, string aMatchString, int aFirstAddr, int aLastAddr)
{
  ErrorPtr err;
  if (aFirstAddr<1 || aLastAddr>0xFF || aFirstAddr>aLastAddr) {
    err = Error::err<ModBusError>(ModBusError::InvalidSlaveAddr, "invalid slave address range");
  }
  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    int currentSlave = getSlaveAddress();
    aSlaveAddrList.clear();
    string id;
    bool runs;
    for (int sa=aFirstAddr; sa<=aLastAddr; ++sa) {
      setSlaveAddress(sa);
      err = readSlaveInfo(id, runs);
      if (Error::isOK(err)) {
        // check for id match
        if (aMatchString.empty() || id.find(aMatchString)!=string::npos) {
          aSlaveAddrList.push_back(sa);
        }
        else {
          OLOG(LOG_INFO, "address %d id '%s' does not match", sa, id.c_str());
        }
      }
      else {
        OLOG(LOG_INFO, "address %d returns error for slaveid query: %s", sa, err->text());
      }
    }
    setSlaveAddress(currentSlave);
  }
  if (!wasConnected) close();
  return err;
}





// MARK: - ModbusMaster register and bit access


ErrorPtr ModbusMaster::readRegister(int aRegAddr, uint16_t &aRegData, bool aInput)
{
  return readRegisters(aRegAddr, 1, &aRegData, aInput);
}


ErrorPtr ModbusMaster::readFloatRegister(int aRegAddr, double &aFloatData, bool aInput)
{
  uint16_t floatRegs[2];
  ErrorPtr err = readRegisters(aRegAddr, 2, floatRegs, aInput);
  if (Error::isOK(err)) {
    aFloatData = getAsDouble(floatRegs);
  }
  return err;
}



ErrorPtr ModbusMaster::readRegisters(int aRegAddr, int aNumRegs, uint16_t *aRegsP, bool aInput)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connectAsMaster();
  if (Error::isOK(err)) {
    int ret;
    if (aInput) {
      ret = modbus_read_input_registers(mModbus, aRegAddr, aNumRegs, aRegsP);
    }
    else {
      ret = modbus_read_registers(mModbus, aRegAddr, aNumRegs, aRegsP);
    }
    if (ret<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
  }
  if (!wasConnected) close();
  return err;
}




ErrorPtr ModbusMaster::writeRegister(int aRegAddr, const uint16_t aRegData)
{
  return writeRegisters(aRegAddr, 1, &aRegData);
}


ErrorPtr ModbusMaster::writeFloatRegister(int aRegAddr, double aFloatData)
{
  uint16_t floatRegs[2];
  setAsDouble(floatRegs, aFloatData);
  return writeRegisters(aRegAddr, 2, floatRegs);
}


ErrorPtr ModbusMaster::writeRegisters(int aRegAddr, int aNumRegs, const uint16_t *aRegsP)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connectAsMaster();
  if (Error::isOK(err)) {
    if (modbus_write_registers(mModbus, aRegAddr, aNumRegs, aRegsP)<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
  }
  if (!wasConnected) close();
  return err;
}


ErrorPtr ModbusMaster::readBit(int aBitAddr, bool &aBitData, bool aInput)
{
  uint8_t bit;
  ErrorPtr err = readBits(aBitAddr, 1, &bit, aInput);
  if (Error::isOK(err)) aBitData = bit!=0;
  return err;
}


ErrorPtr ModbusMaster::readBits(int aBitAddr, int aNumBits, uint8_t *aBitsP, bool aInput)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connectAsMaster();
  if (Error::isOK(err)) {
    int ret;
    if (aInput) {
      ret = modbus_read_input_bits(mModbus, aBitAddr, aNumBits, aBitsP);
    }
    else {
      ret = modbus_read_bits(mModbus, aBitAddr, aNumBits, aBitsP);
    }
    if (ret<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
  }
  if (!wasConnected) close();
  return err;
}


ErrorPtr ModbusMaster::writeBit(int aBitAddr, bool aBitData)
{
  uint8_t bit = aBitData;
  return writeBits(aBitAddr, 1, &bit);
}


ErrorPtr ModbusMaster::writeBits(int aBitAddr, int aNumBits, const uint8_t *aBitsP)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connectAsMaster();
  if (Error::isOK(err)) {
    if (modbus_write_bits(mModbus, aBitAddr, aNumBits, aBitsP)<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
  }
  if (!wasConnected) close();
  return err;
}





// MARK: - ModbusMaster file record access


ErrorPtr ModbusMaster::writeFileRecords(uint16_t aFileNo, uint16_t aFirstRecordNo, uint16_t aNumRecords, const uint8_t* aDataP)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    ModBusPDU req;
    int reqLen = modbus_build_request_basis(mModbus, MODBUS_FC_WRITE_FILE_RECORD, req);
    int lenIdx = reqLen++;
    req[reqLen++] = 0x06; // subrecord reference type
    req[reqLen++] = aFileNo>>8;
    req[reqLen++] = aFileNo & 0xFF;
    req[reqLen++] = aFirstRecordNo>>8;
    req[reqLen++] = aFirstRecordNo & 0xFF;
    req[reqLen++] = aNumRecords>>8; // aka "record length" in the specs
    req[reqLen++] = aNumRecords & 0xFF; // aka "record length" in the specs
    uint16_t bytes = aNumRecords*2;
    if (reqLen+bytes>MODBUS_MAX_PDU_LENGTH) {
      err = Error::err<ModBusError>(EMBBADEXC, "write file record PDU size exceeded");
    }
    else {
      // add actual data
      appendToMessage(aDataP, bytes, req, reqLen);
      req[lenIdx] = reqLen-lenIdx-1;
      // send it
      int rc;
      do {
        rc = modbus_send_msg(mModbus, req, reqLen);
        // might return EAGAIN when broadcasting w/o waiting very fast
      } while (isBroadCast() && rc<0 && errno==EAGAIN);
      if (rc<0) {
        err = Error::err<ModBusError>(errno)->withPrefix("sending write file record request: ");
      }
      else if (isBroadCast()) {
        // TODO: might need some pacing here
      }
      else {
        ModBusPDU rsp;
        int rspLen = modbus_receive_msg(mModbus, rsp, MSG_CONFIRMATION);
        if (rspLen<0) {
          rc = -1;
        }
        else if (rspLen>0) {
          rc = modbus_pre_check_confirmation(mModbus, req, rsp, rspLen);
          if (rc>0) {
            if (rsp[rc++]!=MODBUS_FC_WRITE_FILE_RECORD) { rc = -1; errno = EMBBADEXC; }
            else {
              if (rsp[rc]!=req[lenIdx] || rspLen<rsp[rc]+rc) { rc = -1; errno = EMBBADDATA; }
              else {
                // everything following, including length must be equal to request
                if (memcmp(req+lenIdx, rsp+rc, (size_t)req[lenIdx])!=0) { rc = -1; errno = EMBBADDATA; }
              }
            }
          }
        }
        if (rc<0) {
          err = Error::err<ModBusError>(errno)->withPrefix("receiving write file record response: ");
        }
      }
    }
  }
  if (!wasConnected) close();
  return err;
}


ErrorPtr ModbusMaster::readFileRecords(uint16_t aFileNo, uint16_t aFirstRecordNo, uint16_t aNumRecords, uint8_t* aDataP)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    ModBusPDU req;
    int reqLen = modbus_build_request_basis(mModbus, MODBUS_FC_READ_FILE_RECORD, req);
    int lenIdx = reqLen++;
    req[reqLen++] = 0x06; // subrecord reference type
    req[reqLen++] = aFileNo>>8;
    req[reqLen++] = aFileNo & 0xFF;
    req[reqLen++] = aFirstRecordNo>>8;
    req[reqLen++] = aFirstRecordNo & 0xFF;
    req[reqLen++] = aNumRecords>>8; // aka "record length" in the specs
    req[reqLen++] = aNumRecords & 0xFF; // aka "record length" in the specs
    uint16_t bytes = aNumRecords*2;
    if (reqLen+bytes>MODBUS_MAX_PDU_LENGTH) {
      err = Error::err<ModBusError>(EMBBADEXC, "read file record response would exceed PDU size");
    }
    else {
      req[lenIdx] = reqLen-lenIdx-1;
      // send the read request
      int rc = modbus_send_msg(mModbus, req, reqLen);
      if (rc<0) {
        err = Error::err<ModBusError>(errno)->withPrefix("sending read file record request: ");
      }
      else {
        ModBusPDU rsp;
        int rspLen = modbus_receive_msg(mModbus, rsp, MSG_CONFIRMATION);
        if (rspLen<0) {
          rc = -1;
        }
        else if (rspLen>0) {
          rc = modbus_pre_check_confirmation(mModbus, req, rsp, rspLen);
          if (rc>0) {
            if (
              (rsp[rc++]!=MODBUS_FC_READ_FILE_RECORD) ||
              (rsp[rc++]!=bytes+2) ||
              (rsp[rc++]!=bytes+1) ||
              (rsp[rc++]!=0x06)
            ) {
              rc = -1;
              errno = EMBBADDATA;
            }
            else {
              // get the data
              memcpy(aDataP, rsp+rc, bytes);
            }
          }
        }
        if (rc<0) {
          err = Error::err<ModBusError>(errno)->withPrefix("receiving read file record response: ");
        }
      }
    }
  }
  if (!wasConnected) close();
  return err;
}



// MARK: - ModbusMaster file transfers

#define WRITE_RECORD_RETRIES 3
#define READ_RECORD_RETRIES 3
#define WRITE_RETRY_DELAY (500*MilliSecond)
#define READ_RETRY_DELAY (500*MilliSecond)
#define READ_TIMEDOUT_RETRY_DELAY (10*Second)

ErrorPtr ModbusMaster::sendFile(const string& aLocalFilePath, int aFileNo, bool aUseP44Header)
{
  // create a file handler
  ModbusFileHandlerPtr handler = ModbusFileHandlerPtr(new ModbusFileHandler(aFileNo, 0, 1, aUseP44Header, aLocalFilePath));
  // send the file
  OLOG(LOG_NOTICE, "Sending file '%s' to fileNo %d in slave %d", aLocalFilePath.c_str(), aFileNo, getSlaveAddress());
  return sendFile(handler, aFileNo);
}



ErrorPtr ModbusMaster::sendFile(ModbusFileHandlerPtr aHandler, int aFileNo)
{
  ErrorPtr err;

  err = aHandler->openLocalFile(aFileNo, false); // for local read
  if (Error::isOK(err)) {
    bool wasConnected = isConnected();
    if (!wasConnected) err = connect();
    if (Error::isOK(err)) {
      uint8_t p44hdr[32];
      int hdrSz = aHandler->generateP44Header(p44hdr, 32);
      if (hdrSz<0) {
        err = Error::err<ModBusError>(EMBBADEXC, "cannot generate header");
      }
      else if (hdrSz>0) {
        int retries = WRITE_RECORD_RETRIES;
        while (true) {
          // we actually have a p44 header, send it
          err = writeFileRecords(aFileNo, 0, (hdrSz+1)/2, p44hdr);
          if (!isCommErr(err)) break;
          retries--;
          if (retries<=0) break;
          MainLoop::sleep(WRITE_RETRY_DELAY);
          modbus_flush(mModbus);
        };
      }
      if (Error::isOK(err)) {
        // header sent or none required, now send data
        ModBusPDU buf;
        uint32_t chunkIndex = 0;
        if (Error::isOK(err)) {
          while (true) {
            uint16_t fileNo, recordNo, recordLen;
            if (aHandler->isEOFforChunk(chunkIndex, false)) break; // local EOF reached
            aHandler->addressForMaxChunk(chunkIndex, fileNo, recordNo, recordLen);
            // get data from local file
            err = aHandler->readLocalFile(fileNo, recordNo, buf, recordLen*2);
            if (Error::notOK(err)) break;
            chunkIndex++;
            // write to the remote
            int retries = WRITE_RECORD_RETRIES;
            while (true) {
              err = writeFileRecords(fileNo, recordNo, recordLen, buf);
              if (!isCommErr(err)) break;
              retries--;
              if (retries<=0) break;
              MainLoop::sleep(WRITE_RETRY_DELAY);
              modbus_flush(mModbus);
            }
            if (Error::notOK(err)) break;
          }
        }
      }
      if (!wasConnected) close();
    }
  }
  return err;
}


ErrorPtr ModbusMaster::receiveFile(const string& aLocalFilePath, int aFileNo, bool aUseP44Header)
{
  ErrorPtr err;

  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    // create a file handler
    ModbusFileHandlerPtr handler = ModbusFileHandlerPtr(new ModbusFileHandler(aFileNo, 0, 1, aUseP44Header, aLocalFilePath));
    ModBusPDU buf;
    if (aUseP44Header) {
      // read p44header first
      int retries = READ_RECORD_RETRIES;
      while (retries--) {
        err = readFileRecords(aFileNo, 0, handler->numP44HeaderRecords(), buf);
        if (!isCommErr(err)) break;
        if (Error::isError(err, ModBusError::domain(), ETIMEDOUT)) {
          // extra wait, because this is most likely CRC calculation at the other end
          MainLoop::sleep(READ_TIMEDOUT_RETRY_DELAY);
        }
        MainLoop::sleep(READ_RETRY_DELAY);
        modbus_flush(mModbus);
      }
      if (Error::isOK(err)) {
        // "write" header (i.e. set up handler for receiving)
        err = handler->writeLocalFile(aFileNo, 0, buf, handler->numP44HeaderRecords()*2);
      }
    }
    if (Error::isOK(err)) {
      // header received or none required, now received data
      ModBusPDU buf;
      uint32_t chunkIndex = 0;
      if (Error::isOK(err)) {
        while (true) {
          uint16_t fileNo, recordNo, recordLen;
          if (handler->isEOFforChunk(chunkIndex, true)) break; // remote EOF reached (known via p44Header)
          handler->addressForMaxChunk(chunkIndex, fileNo, recordNo, recordLen);
          // read from the remote
          int retries = READ_RECORD_RETRIES;
          while (true) {
            err = readFileRecords(fileNo, recordNo, recordLen, buf);
            if (!isCommErr(err)) break;
            retries--;
            if (retries<=0) break;
            MainLoop::sleep(READ_RETRY_DELAY);
            modbus_flush(mModbus);
          }
          if (!aUseP44Header && Error::isError(err, ModBusError::domain(), ModBusError::MBErr+MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS)) {
            // EMBXILADD signals EOF
            err.reset();
            break;
          }
          if (Error::notOK(err)) break;
          // store data in the local file
          err = handler->writeLocalFile(fileNo, recordNo, buf, recordLen*2);
          if (Error::notOK(err)) return err;
          chunkIndex++;
        }
        ErrorPtr ferr = handler->finalize();
        if (Error::isOK(err)) err = ferr;
      }
    }
  }
  if (!wasConnected) close();
  return err;
}



ErrorPtr ModbusMaster::broadcastFile(const SlaveAddrList& aSlaveAddrList, const string& aLocalFilePath, int aFileNo, bool aUseP44Header)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    ModbusFileHandlerPtr handler = ModbusFileHandlerPtr(new ModbusFileHandler(aFileNo, 0, 1, aUseP44Header, aLocalFilePath));
    if (!aUseP44Header) {
      // simple one-by-one transfer, not real broadcast
      OLOG(LOG_NOTICE, "Sending file '%s' to fileNo %d in %lu slaves, no broadcast (no p44header)", aLocalFilePath.c_str(), aFileNo, aSlaveAddrList.size());
      for (SlaveAddrList::const_iterator pos = aSlaveAddrList.begin(); pos!=aSlaveAddrList.end(); ++pos) {
        setSlaveAddress(*pos);
        OLOG(LOG_NOTICE, "- sending file to slave %d", *pos);
        ErrorPtr fileErr = sendFile(handler, aFileNo);
        if (Error::notOK(fileErr)) {
          OLOG(LOG_ERR, "Error sending file '%s' to fileNo %d in slave %d: %s", aLocalFilePath.c_str(), aFileNo, *pos, fileErr->text());
          err = fileErr; // return most recent error
        }
      }
    }
    else {
      // with p44header, we can do real broadcast of the data
      OLOG(LOG_NOTICE, "Sending file '%s' to fileNo %d as broadcast", aLocalFilePath.c_str(), aFileNo);
      setSlaveAddress(MODBUS_BROADCAST_ADDRESS);
      err = sendFile(handler, aFileNo);
      if (Error::isOK(err)) {
        // query each slave for possibly missing records, send them
        OLOG(LOG_NOTICE, "Broadcast complete - now verifying successful transmission");
        for (SlaveAddrList::const_iterator pos = aSlaveAddrList.begin(); pos!=aSlaveAddrList.end(); ++pos) {
          ErrorPtr slerr;
          OLOG(LOG_NOTICE, "- Verifying with slave %d", *pos);
          setSlaveAddress(*pos);
          ModBusPDU buf;
          while (true) {
            // read p44header
            int retries = READ_RECORD_RETRIES;
            while (retries--) {
              slerr = readFileRecords(aFileNo, 0, handler->numP44HeaderRecords(), buf);
              if (!isCommErr(slerr)) break;
              if (Error::isError(slerr, ModBusError::domain(), ETIMEDOUT)) {
                // extra wait, because this is most likely CRC calculation at the other end
                MainLoop::sleep(READ_TIMEDOUT_RETRY_DELAY);
              }
              MainLoop::sleep(READ_RETRY_DELAY);
              modbus_flush(mModbus);
            }
            if (Error::notOK(slerr)) break; // failed, done with this slave
            // retransmit failed block, if any
            slerr = handler->parseP44Header(buf, 0, MODBUS_MAX_PDU_LENGTH, false);
            if (Error::notOK(slerr)) break; // failed, done with this slave
            uint16_t fileNo, recordNo, recordLen;
            if (handler->addrForNextRetransmit(fileNo, recordNo, recordLen)) {
              // retransmit that block
              handler->readLocalFile(fileNo, recordNo, buf, recordLen*2);
              retries = WRITE_RECORD_RETRIES;
              while (retries--) {
                slerr = writeFileRecords(fileNo, recordNo, recordLen, buf);
                if (!isCommErr(slerr)) break;
                MainLoop::sleep(WRITE_RETRY_DELAY);
                modbus_flush(mModbus);
              }
              if (Error::notOK(slerr)) break; // failed, done with this slave
            }
            else {
              // no more retransmits pending for this slave
              if (handler->fileIntegrityOK()) {
                OLOG(LOG_NOTICE, "- Sending file '%s' to fileNo %d in slave %d confirmed SUCCESSFUL!", aLocalFilePath.c_str(), aFileNo, *pos);
              }
              else {
                err = Error::err<ModBusError>(EMBBADCRC, "CRC or size mismatch after retransmitting all blocks");
              }
              break; // done with this slave
            }
          } // while bad blocks
          if (slerr) {
            OLOG(LOG_ERR, "Failed sending file No %d in slave %d: %s", aFileNo, *pos, slerr->text());
            err = slerr->withPrefix("Slave %d: ", *pos);
          }
        } // for all slaves
      } // if broadcast ok
    } // if p44header mode
  }
  if (!wasConnected) close();
  return err;
}





// MARK: - ModbusSlave


ModbusSlave::ModbusSlave() :
  mRegisterModel(NULL),
  mModbusRcv(NULL)
{
  // by default, server will accept TCP connection (rather than trying to connect)
  mDoAcceptConnections = true;
}

ModbusSlave::~ModbusSlave()
{
  close();
  freeRegisterModel();
}


void ModbusSlave::close()
{
  stopServing();
  inherited::close();
}


void ModbusSlave::freeRegisterModel()
{
  if (mRegisterModel) {
    modbus_mapping_free(mRegisterModel);
    mRegisterModel = NULL;
  }
}


void ModbusSlave::setSlaveId(const string aSlaveId)
{
  slaveId = aSlaveId;
  if (mModbus) {
    modbus_set_slave_id(mModbus, slaveId.c_str());
  }
}


void ModbusSlave::mbContextReady()
{
  if (!slaveId.empty()) {
    modbus_set_slave_id(mModbus, slaveId.c_str());
  }
  inherited::mbContextReady();
}


// MARK: - ModbusSlave Request processing

void ModbusSlave::startServing()
{
  if (!mModbus) return;
  cancelMsgReception();
  int fd = modbus_get_socket(mModbus);
  MainLoop::currentMainLoop().registerPollHandler(fd, POLLIN, boost::bind(&ModbusSlave::modbusFdPollHandler, this, _1, _2));
}


void ModbusSlave::stopServing()
{
  cancelMsgReception();
  if (mModbus) {
    int fd = modbus_get_socket(mModbus);
    MainLoop::currentMainLoop().unregisterPollHandler(fd);
  }
}


void ModbusSlave::cancelMsgReception()
{
  if (mModbusRcv) {
    mRcvTimeoutTicket.cancel();
    modbus_receive_free(mModbusRcv);
    mModbusRcv = NULL;
  }
}


void ModbusSlave::startTimeout()
{
  MLMicroSeconds timeout = MainLoop::timeValToMainLoopTime(modbus_get_select_timeout(mModbusRcv));
  if (timeout==Never)
    mRcvTimeoutTicket.cancel();
  else
    mRcvTimeoutTicket.executeOnce(boost::bind(&ModbusSlave::modbusTimeoutHandler, this), timeout);
}


void ModbusSlave::startMsgReception()
{
  cancelMsgReception(); // stop previous, if any
  mModbusRcv = modbus_receive_new(mModbus, mModbusReq);
  startTimeout();
}


bool ModbusSlave::modbusFdPollHandler(int aFD, int aPollFlags)
{
  if (aPollFlags & POLLIN) {
    // got some data
    if (!mModbusRcv) {
      // start new request
      startMsgReception();
      if (!mModbusRcv) {
        OLOG(LOG_CRIT, "cannot create new Modbus receive context");
        return false;
      }
    }
    int reqLen = modbus_receive_step(mModbusRcv);
    if (reqLen<0 && errno==EAGAIN) {
      // no complete message yet
      // - re-start timeout
      startTimeout();
      return true;
    }
    if (reqLen>0) {
      mRcvTimeoutTicket.cancel();
      // got request
      FOCUSOLOG("Modbus received request, %d bytes", reqLen);
      // - process it
      int rspLen = modbus_process_request(mModbus, mModbusReq, reqLen, mModbusRsp, modbus_slave_function_handler, this);
      /* Send response, if any */
      if (rspLen > 0) {
        int rc = modbus_send_msg(mModbus, mModbusRsp, rspLen);
        if (rc<0) {
          ErrorPtr err = Error::err<ModBusError>(errno)->withPrefix("sending response: ");
          OLOG(LOG_ERR, "Error sending Modbus response: %s", Error::text(err));
        }
      }
    }
    else if (reqLen<0) {
      if (errno==ECONNRESET) {
        // simulate HUP for check below, as we must always stop the connection when connection ends
        aPollFlags |= POLLHUP;
        FOCUSOLOG("ECONNRESET -> simulate POLLHUP");
      }
      else {
        ErrorPtr err = Error::err<ModBusError>(errno);
        OLOG(LOG_ERR, "Error receiving Modbus request: %s", Error::text(err));
      }
    }
    else {
      FOCUSOLOG("Modbus - message for other slave - ignored, reqLen = %d", reqLen);
    }
    // done with this message
    if (aPollFlags & POLLHUP) {
      // connection terminated
      FOCUSOLOG("POLLIN+POLLHUP - connection terminated");
      stopServing();
    }
    else {
      // connection still open, start reception of next message
      FOCUSOLOG("Connection still open (pollflags=0x%X, reqLen=%d) - wait for next msg", aPollFlags, reqLen);
      startMsgReception();
    }
    return true;
  }
  else if (aPollFlags & POLLHUP) {
    FOCUSOLOG("only POLLHUP - connection terminated");
    stopServing();
  }
  else if (aPollFlags & POLLERR) {
    // try to reconnect
    FOCUSOLOG("POLLERR - close and reopen connection");
    close(); // not just stop serving, really disconnect!
    startServing();
    return true;
  }
  return false;
}


void ModbusSlave::modbusTimeoutHandler()
{
  FOCUSLOG("modbus timeout - flushing received data");
  if (mModbus) {
    if (mModbus) modbus_flush(mModbus);
    startMsgReception();
  }
}


extern "C" {

  int modbus_slave_function_handler(modbus_t* ctx, sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp, void *user_ctx)
  {
    ModbusSlave *modbusSlave = (ModbusSlave*)user_ctx;
    if (!modbusSlave || !sft) return -1;
    return modbusSlave->handleRawRequest(*sft, offset, *(ModBusPDU*)req, req_length, *(ModBusPDU*)rsp);
  }

  const char* modbus_access_handler(modbus_t* ctx, modbus_mapping_t* mappings, modbus_data_access_t access, int addr, int cnt, modbus_data_t dataP, void *user_ctx)
  {
    ModbusSlave *modbusSlave = (ModbusSlave*)user_ctx;
    if (!modbusSlave) return "internal error";
    return modbusSlave->accessHandler(access, addr, cnt, dataP);
  }

}


int ModbusSlave::handleRawRequest(sft_t &aSft, int aOffset, const ModBusPDU& aReq, int aReqLen, ModBusPDU& aRsp)
{
  FOCUSLOG("Received request with FC=%d/0x%02x, for slaveid=%d, transactionId=%d", aSft.function, aSft.function, aSft.slave, aSft.t_id);
  int rspLen = 0;
  bool handled = false;
  // allow custom request handling to override anything
  if (mRawRequestHandler) {
    handled = mRawRequestHandler(aSft, aOffset, aReq, aReqLen, aRsp, rspLen);
  }
  if (!handled) {
    // handle files
    if (aSft.function==MODBUS_FC_READ_FILE_RECORD || aSft.function==MODBUS_FC_WRITE_FILE_RECORD) {
      handled = handleFileAccess(aSft, aOffset, aReq, aReqLen, aRsp, rspLen);
    }
  }
  if (!handled) {
    // handle registers and bits
    if (mRegisterModel) {
      modbus_mapping_ex_t map;
      map.mappings = mRegisterModel;
      if (
        mValueAccessHandler
        #if ENABLE_MODBUS_SCRIPT_FUNCS
        || mRepresentingObj
        #endif
      ) {
        map.access_handler = modbus_access_handler;
        map.access_handler_user_ctx = this;
      }
      else {
        map.access_handler = NULL;
        map.access_handler_user_ctx = NULL;
      }
      rspLen = modbus_reg_mapping_handler(mModbus, &aSft, aOffset, aReq, aReqLen, aRsp, &map);
      handled = true;
    }
  }
  if (handled) {
    FOCUSLOG("Handled request with FC=%d/0x%02x, for slaveid=%d, transactionId=%d: response length=%d", aSft.function, aSft.function, aSft.slave, aSft.t_id, rspLen);
    return rspLen;
  }
  LOG(LOG_CRIT, "no request handlers installed at all");
  return -1; // should not happen
}


const char* ModbusSlave::accessHandler(modbus_data_access_t access, int addr, int cnt, modbus_data_t dataP)
{
  ErrorPtr err;
  if (mRegisterModel) {
    for (int i=0; i<cnt; i++) {
      int reg;
      bool bit;
      bool input;
      bool write;
      switch (access) {
        case read_bit : reg = addr+mRegisterModel->start_bits+i; bit = true; input = false; write = false; break;
        case write_bit : reg = addr+mRegisterModel->start_bits+i; bit = true; input = false; write = true; break;
        case read_input_bit : reg = addr+mRegisterModel->start_input_bits+i; bit = true; input = true; write = false; break;
        case read_reg : reg = addr+mRegisterModel->start_registers+i; bit = false; input = false; write = false; break;
        case write_reg : reg = addr+mRegisterModel->start_registers+i; bit = false; input = false; write = true; break;
        case read_input_reg : reg = addr+mRegisterModel->start_input_registers+i; bit = false; input = true; write = false; break;
        default: err = TextError::err("unknown modbus access type"); break;
      }
      if (Error::isOK(err)) {
        #if ENABLE_MODBUS_SCRIPT_FUNCS
        if (mRepresentingObj) {
          mRepresentingObj->gotAccessed(reg, bit, input, write);
        }
        #endif
        if (mValueAccessHandler) {
          err = mValueAccessHandler(reg, bit, input, write);
        }
      }
    }
  }
  if (Error::notOK(err)) {
    mErrStr = err->description();
    return mErrStr.c_str(); // return error text to be returned with
  }
  return NULL; // no error
}



// MARK: - ModbusSlave Managing register model


ErrorPtr ModbusSlave::setRegisterModel(
  int aFirstCoil, int aNumCoils, // I/O bits = coils
  int aFirstBit, int aNumBits, // input bits
  int aFirstReg, int aNumRegs, // R/W registers
  int aFirstInp, int aNumInps // read only registers
)
{
  freeRegisterModel(); // forget old model
  modbus_mapping_t* map = modbus_mapping_new_start_address(aFirstCoil, aNumCoils, aFirstBit, aNumBits, aFirstReg, aNumRegs, aFirstInp, aNumInps);
  if (map==NULL) return Error::err<ModBusError>(errno);
  mRegisterModel = map;
  return ErrorPtr();
}


void ModbusSlave::setValueAccessHandler(ModbusValueAccessCB aValueAccessCB)
{
  mValueAccessHandler = aValueAccessCB;
}


uint8_t* ModbusSlave::getBitAddress(int aAddress, bool aInput, int aBits)
{
  if (!mRegisterModel) return NULL;
  aAddress -= (aInput ? mRegisterModel->start_input_bits : mRegisterModel->start_bits);
  if (aAddress<0 || aBits >= (aInput ? mRegisterModel->nb_input_bits : mRegisterModel->nb_bits)) return NULL;
  return aInput ? &(mRegisterModel->tab_input_bits[aAddress]) : &(mRegisterModel->tab_bits[aAddress]);
}


uint16_t* ModbusSlave::getRegisterAddress(int aAddress, bool aInput, int aRegs)
{
  if (!mRegisterModel) return NULL;
  aAddress -= (aInput ? mRegisterModel->start_input_registers : mRegisterModel->start_registers);
  if (aAddress<0 || aRegs >= (aInput ? mRegisterModel->nb_input_registers : mRegisterModel->nb_registers)) return NULL;
  return aInput ? &(mRegisterModel->tab_input_registers[aAddress]) : &(mRegisterModel->tab_registers[aAddress]);
}



uint16_t ModbusSlave::getValue(int aAddress, bool aBit, bool aInput)
{
  if (aBit) return (int)getBit(aAddress, aInput);
  else return getReg(aAddress, aInput);
}


uint16_t ModbusSlave::getReg(int aAddress, bool aInput)
{
  uint16_t *r = getRegisterAddress(aAddress, aInput, 1);
  if (r) return *r;
  return 0;
}


void ModbusSlave::setReg(int aAddress, bool aInput, uint16_t aRegValue)
{
  uint16_t *r = getRegisterAddress(aAddress, aInput, 1);
  if (r) *r = aRegValue;
}


double ModbusSlave::getFloatReg(int aAddress, bool aInput)
{
  uint16_t *r = getRegisterAddress(aAddress, aInput, 2);
  if (r) return getAsDouble(r);
  return 0;
}


void ModbusSlave::setFloatReg(int aAddress, bool aInput, double aFloatValue)
{
  uint16_t *r = getRegisterAddress(aAddress, aInput, 2);
  if (r) setAsDouble(r, aFloatValue);
}


bool ModbusSlave::getBit(int aAddress, bool aInput)
{
  uint8_t *r = getBitAddress(aAddress, aInput, 1);
  if (r) return *r!=0;
  return false;
}


void ModbusSlave::setBit(int aAddress, bool aInput, bool aBitValue)
{
  uint8_t *r = getBitAddress(aAddress, aInput, 1);
  if (r) *r = aBitValue ? 0x01 : 0x00; // this is important, no other values than these must be used in the tab_bits arrays!
}


// MARK: - File transfer handling


ModbusFileHandlerPtr ModbusSlave::addFileHandler(ModbusFileHandlerPtr aFileHandler)
{
  mFileHandlers.push_back(aFileHandler);
  return aFileHandler;
}


#if DEBUG
  // simulate file blocks
  #define SIMULATE_MISSING_RECORDS_FROM 210
  #define SIMULATE_MISSING_RECORDS_COUNT 100
#endif

bool ModbusSlave::handleFileAccess(sft_t &aSft, int aOffset, const ModBusPDU& aReq, int aReqLen, ModBusPDU& aRsp, int &aRspLen)
{
  ErrorPtr err;
  // req[aOffset] is the function code = first byte of actual request
  int e = aOffset+2+aReq[aOffset+1]; // first byte outside
  int i = aOffset+2; // first subrecord
  // - prepare response base (up to and including function code)
  buildResponseBase(aSft, aRsp, aRspLen);
  int lenIdx = aRspLen++; // actual length will be filled when all subrecords are processed
  // - process subrecords
  bool pendingFinalisations = false;
  while (i<e) {
    // read the subrecord
    int subRecordIdx = i;
    uint8_t reftype = aReq[i]; i += 1;
    if (reftype!=0x06) {
      err = Error::err<ModBusError>(EMBXILVAL, "Wrong subrequest reference type, expected 0x06, found 0x%02x", reftype);
      break; // incorrect reference type
    }
    uint16_t fileno = (aReq[i]<<8) + aReq[i+1]; i += 2;
    // check if we have a file handler
    ModbusFileHandlerPtr handler;
    for (FileHandlersList::iterator pos = mFileHandlers.begin(); pos!=mFileHandlers.end(); ++pos) {
      if ((*pos)->handlesFileNo(fileno)) {
        handler = *pos;
      }
    }
    if (!handler) {
      // file not found, abort
      err = Error::err<ModBusError>(EMBXILADD, "Unknown file number %d", fileno);
      break;
    }
    else {
      // process request with handler
      uint16_t recordno = (aReq[i]<<8) + aReq[i+1]; i += 2;
      uint16_t recordlen = (aReq[i]<<8) + aReq[i+1]; i += 2;
      uint16_t dataBytes = recordlen*2;
      if (aSft.function==MODBUS_FC_WRITE_FILE_RECORD) {
        // Write to file
        const uint8_t* writeDataP = aReq+i;
        i += dataBytes;
        #if SIMULATE_MISSING_RECORDS_FROM && SIMULATE_MISSING_RECORDS_COUNT
        // simulate missing blocks in brodcast mode
        if (
          aSft.slave==0 &&
          recordno>=SIMULATE_MISSING_RECORDS_FROM &&
          recordno<SIMULATE_MISSING_RECORDS_FROM+SIMULATE_MISSING_RECORDS_COUNT
        ) {
          LOG(LOG_WARNING, "**** SIMULATING MISSING recordNo %d", recordno);
          err = TextError::err("simulated missing recordNo %d", recordno);
          break;
        }
        #endif
        err = handler->writeLocalFile(fileno, recordno, writeDataP, dataBytes);
        if (Error::notOK(err)) break; // error
        // echo the subrequest
        appendToMessage(aReq+subRecordIdx, i-subRecordIdx, aRsp, aRspLen);
        if (handler->needFinalizing()) pendingFinalisations = true;
      }
      else if (aSft.function==MODBUS_FC_READ_FILE_RECORD) {
        // Read from file
        // - prepare special file read response
        aRsp[aRspLen++] = dataBytes+1; // number of bytes, including reference type that follows
        aRsp[aRspLen++] = 0x06; // reference type
        uint8_t* readDataP = aRsp+aRspLen; // data read from file goes here
        // - reserve space in PDU
        if (!appendToMessage(NULL, dataBytes, aRsp, aRspLen)) {
          // no room in PDU
          err = Error::err<ModBusError>(EMBXILVAL, "Read file response would exceed PDU size");
          break;
        }
        err = handler->readLocalFile(fileno, recordno, readDataP, dataBytes);
        if (Error::notOK(err)) break; // error
      }
      else {
        return false; // unknown function code
      }
    }
  } // all subrequests
  if (Error::isOK(err)) {
    // complete response
    aRsp[lenIdx] = aRspLen-lenIdx-1; // set length of data
    // return the answer BEFORE possibly doing finalisations
    if (aSft.slave!=MODBUS_BROADCAST_ADDRESS && aRspLen>0) {
      int rc = modbus_send_msg(mModbus, aRsp, aRspLen);
      if (rc>=0) {
        aRspLen = 0; // sent, caller must not send a result!
      }
      else {
        err = Error::err<ModBusError>(errno)->withPrefix("sending file record response: ");
        LOG(LOG_ERR, "Modbus error sending response for file record request: %s", Error::text(err));
      }
    }
    // do finalisations that might need more time than modbus request timeout now
    if (pendingFinalisations) {
      for (FileHandlersList::iterator pos = mFileHandlers.begin(); pos!=mFileHandlers.end(); ++pos) {
        ModbusFileHandlerPtr handler = *pos;
        if (handler->needFinalizing()) {
          err = handler->finalize();
          if (Error::notOK(err)) {
            LOG(LOG_ERR, "Error finalizing file: %s", err->text());
          }
        }
      }
    }
  }
  else {
    // failed
    LOG(LOG_INFO, "file access (FC=0x%02x) failed: %s", aSft.function, err->text());
    buildExceptionResponse(aSft, err, aRsp, aRspLen);
  }
  return true; // handled
}


// MARK: - ModbusFileHandler

ModbusFileHandler::ModbusFileHandler(int aFileNo, int aMaxSegments, int aNumFiles, bool aP44Header, const string aFilePath, bool aReadOnly, const string aFinalBasePath) :
  mFileNo(aFileNo),
  mMaxSegments(aMaxSegments),
  mNumFiles(aNumFiles),
  mUseP44Header(aP44Header),
  mFilePath(aFilePath),
  mFinalBasePath(aFinalBasePath),
  mReadOnly(aReadOnly),
  mCurrentBaseFileNo(0),
  mOpenFd(-1),
  mValidP44Header(false),
  mSingleRecordLength(0),
  mNeededSegments(1),
  mRecordsPerChunk(1),
  mFirstDataRecord(0),
  mRemoteMissingRecord(mNoneMissing),
  mRemoteCRC32(0),
  mRemoteFileSize(0),
  mLocalFileSize(0),
  mLocalCRC32(mInvalidCRC),
  mNextExpectedDataRecord(0), // expect start at very beginning
  mPendingFinalisation(false)
{
}


ModbusFileHandler::~ModbusFileHandler()
{

}


bool ModbusFileHandler::handlesFileNo(uint16_t aFileNo)
{
  return aFileNo>=mFileNo && aFileNo<mFileNo+mMaxSegments*mNumFiles;
}


ErrorPtr ModbusFileHandler::writeLocalFile(uint16_t aFileNo, uint16_t aRecordNo, const uint8_t *aDataP, size_t aDataLen)
{
  LOG(LOG_INFO, "writeFile: #%d, record=%d, bytes=%zu, starting with 0x%02X", aFileNo, aRecordNo, aDataLen, *aDataP);
  if (mReadOnly) {
    return Error::err<ModBusError>(EMBXILFUN, "read only file");
  }
  ErrorPtr err = openLocalFile(aFileNo, true);
  if (Error::notOK(err)) return err;
  uint32_t recordNo =
    ((aFileNo-mCurrentBaseFileNo)<<16) +
    aRecordNo;
  // check for writing header
  if (mUseP44Header) {
    if (!mValidP44Header || recordNo<mFirstDataRecord) {
      // accessing header
      if (recordNo>=numP44HeaderRecords()) {
        return Error::err<ModBusError>(EMBXILADD, "must write P44 header before writing data records");
      }
      if (recordNo!=0 && aDataLen<numP44HeaderRecords()*2) {
        return Error::err<ModBusError>(EMBXILADD, "p44 header must be written in one piece, records 0..%d", numP44HeaderRecords()-1);
      }
      // complete header present, parse it to init data receiving state
      err = parseP44Header(aDataP, 0, (int)aDataLen, true);
      if (Error::notOK(err)) return err;
      if (!mFinalBasePath.empty()) {
        // writing to temp file, remove previous version first
        FOCUSLOG("- writing to temp file, erasing it first");
        closeLocalFile();
        if (unlink(filePathFor(aFileNo, true).c_str())<0) {
          return Error::err<ModBusError>(errno)->withPrefix("erasing temp file before writing: ");
        }
        err = openLocalFile(aFileNo, true);
        if (Error::notOK(err)) return err->withPrefix("re-creating temp file after erasing: ");
      }
      // truncate file to size found in header if it is bigger
      readLocalFileInfo(false);
      if (mLocalFileSize>mRemoteFileSize) {
        FOCUSLOG("- local file is already bigger than p44header declares -> truncating from %u to %u", mLocalFileSize, mRemoteFileSize);
        if (ftruncate(mOpenFd, mRemoteFileSize)<0) {
          return Error::err<ModBusError>(errno)->withPrefix("truncating file");
        }
        mLocalFileSize = mRemoteFileSize;
      }
      return ErrorPtr();
    }
    // not accessing header data
    recordNo -= mFirstDataRecord;
  }
  // now recordno is relative to the file DATA beginning (i.e., excluding header, if any)
  if (mUseP44Header && mValidP44Header && mNextExpectedDataRecord==mNoneMissing && fileIntegrityOK()) {
    LOG(LOG_WARNING, "fileNo %d already completely written -> suppress writing", mCurrentBaseFileNo);
    closeLocalFile();
    return ErrorPtr();
  }
  // - seek to position
  uint32_t filePos = recordNo*mSingleRecordLength*2;
  if (lseek(mOpenFd, filePos , SEEK_SET)<0) {
    return Error::err<ModBusError>(errno)->withPrefix("seeking write position");
  }
  // - check for writing over actual file length
  if (mUseP44Header && filePos+aDataLen>mRemoteFileSize) {
    LOG(LOG_INFO, "last chunk of file: ignoring %lu excessive bytes in chunk", filePos+aDataLen-mRemoteFileSize);
    aDataLen = mRemoteFileSize-filePos; // only write as much as the actual file size allows, ignore rest of chunk
  }
  // - write date to file
  ssize_t by = write(mOpenFd, aDataP, aDataLen);
  if (by<0) {
    return Error::err<ModBusError>(errno)->withPrefix("writing to local file");
  }
  else if (by!=aDataLen) {
    return Error::err<ModBusError>(EMBBADEXC, "could only write %zd/%zu bytes", by, aDataLen);
  }
  // File writing is successful
  // - update file size
  filePos += aDataLen;
  if (filePos>mLocalFileSize) mLocalFileSize = filePos;
  if (mUseP44Header) {
    // - update missing record state
    if (recordNo>=mNextExpectedDataRecord) {
      // if there are some missing in between, track them
      while (mNextExpectedDataRecord<recordNo) {
        mMissingDataRecords.push_back(mNextExpectedDataRecord);
        LOG(LOG_INFO, "- missing DATA recordNo %u -> added to list (total missing=%zu)", mNextExpectedDataRecord, mMissingDataRecords.size());
        mNextExpectedDataRecord += recordAddrsPerChunk();
      }
      // update expected next record
      mNextExpectedDataRecord = recordNo+(((uint32_t)aDataLen+2*mSingleRecordLength-1)/2/mSingleRecordLength);
    }
    else if (recordNo<mNextExpectedDataRecord) {
      // is a re-write of an earlier block, remove it from our list if present
      for (RecordNoList::iterator pos = mMissingDataRecords.begin(); pos!=mMissingDataRecords.end(); ++pos) {
        if (*pos==recordNo) {
          mMissingDataRecords.erase(pos);
          LOG(LOG_INFO, "- successful retransmit of previously missing DATA recordNo %u -> removed from list (remaining missing=%zu)", recordNo, mMissingDataRecords.size());
          if (mMissingDataRecords.size()==0) {
            LOG(LOG_NOTICE, "- all missing blocks now retransmitted. File size=%u (expected=%u)", mLocalFileSize, mRemoteFileSize);
          }
          break;
        }
      }
    }
    if (mNextExpectedDataRecord*mSingleRecordLength*2>=mRemoteFileSize && mMissingDataRecords.empty()) {
      // file is complete
      mNextExpectedDataRecord = mNoneMissing;
      mPendingFinalisation = true;
      // - update info (CRC)
      err = readLocalFileInfo(false);
      if (Error::notOK(err)) return err;
      LOG(LOG_NOTICE, "Successful p44header-controlled file transfer - ready for finalizing");
    }
  }
  return ErrorPtr();
}


ErrorPtr ModbusFileHandler::readLocalFile(uint16_t aFileNo, uint16_t aRecordNo, uint8_t *aDataP, size_t aDataLen)
{
  ErrorPtr err;

  LOG(LOG_INFO, "readFile: #%d, record=%d, bytes=%zu", aFileNo, aRecordNo, aDataLen);
  uint16_t baseFileNo = baseFileNoFor(aFileNo);
  if (baseFileNo!=mCurrentBaseFileNo) {
    // new file, need to re-open early
    err = openLocalFile(aFileNo, false);
    if (Error::notOK(err)) return err;
  }
  uint32_t recordNo =
    ((aFileNo-mCurrentBaseFileNo)<<16) +
    aRecordNo;
  // check for reading header
  // Note: we want to avoid reading from opening the file if it still has valid P44header info,
  //   because reading the header after finalisation must return the finalized status
  //   of the written file (which might be a temp file)
  if (mUseP44Header) {
    if (!mValidP44Header) {
      openLocalFile(aFileNo, false);
      if (Error::notOK(err)) return err;
    }
    if (recordNo<mFirstDataRecord) {
      if (recordNo+aDataLen>numP44HeaderRecords()*2) {
        return Error::err<ModBusError>(EMBXILADD, "out of header record range 0..%d", numP44HeaderRecords()-1);
      }
      ModBusPDU buf;
      generateP44Header(buf, MODBUS_MAX_PDU_LENGTH);
      memcpy(aDataP, buf+recordNo*2, aDataLen);
      return ErrorPtr();
    }
    // not accessing header data
    recordNo -= mFirstDataRecord;
  }
  // now latest we need the file to be open
  openLocalFile(aFileNo, false);
  if (Error::notOK(err)) return err;
  // now recordno is relative to the file DATA beginning (i.e., excluding header, if any)
  // - seek to position
  uint32_t filePos = recordNo*mSingleRecordLength*2;
  if (filePos>=mLocalFileSize) {
    return Error::err<ModBusError>(EMBXILADD, "cannot read past file end");
  }
  if (lseek(mOpenFd, filePos , SEEK_SET)<0) {
    return Error::err<ModBusError>(errno)->withPrefix("seeking read position: ");
  }
  // - read
  ssize_t by = read(mOpenFd, aDataP, aDataLen);
  if (by<0) {
    return Error::err<ModBusError>(errno)->withPrefix("reading from local file: ");
  }
  if (by<aDataLen) {
    // fill rest of data with 0xFF
    memset(aDataP+by, 0xFF, aDataLen-by);
  }
  return ErrorPtr();
}



uint16_t ModbusFileHandler::baseFileNoFor(uint16_t aFileNo)
{
  if (mMaxSegments<2) return aFileNo; // no segmenting configured
  return mFileNo + (aFileNo-mFileNo)/mMaxSegments*mMaxSegments;
}


string ModbusFileHandler::filePathFor(uint16_t aFileNo, bool aTemp)
{
  string path;
  if (!mFinalBasePath.empty()) {
    if (aTemp) {
      path = Application::sharedApplication()->tempPath(mFilePath);
    }
    else {
      path = mFinalBasePath + mFilePath.c_str();
    }
  }
  else {
    path = mFilePath;
  }
  if (mNumFiles==1) return path;
  // path must contain a % specifier for rendering aFileNo
  return string_format(path.c_str(), aFileNo);
}


ErrorPtr ModbusFileHandler::openLocalFile(uint16_t aFileNo, bool aForLocalWrite)
{
  ErrorPtr err;
  uint16_t baseFileNo = baseFileNoFor(aFileNo);
  if (baseFileNo!=mCurrentBaseFileNo) {
    // Note: switching files invalidates the header, just re-opening must NOT invalidate it!
    if (mCurrentBaseFileNo!=0) {
      mValidP44Header = false;
    }
    closeLocalFile();
  }
  if (mOpenFd<0) {
    string path = filePathFor(baseFileNo, aForLocalWrite); // writing occurs on temp version of the file (if any is set)
    mOpenFd = open(path.c_str(), aForLocalWrite ? O_RDWR|O_CREAT : O_RDONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (mOpenFd<0) {
      err = SysError::errNo("cannot open local file: ");
    }
    else {
      mCurrentBaseFileNo = baseFileNo;
      if (!aForLocalWrite) {
        // reading local file info for sending to remote
        err = readLocalFileInfo(true);
      }
    }
  }
  return err;
}


void ModbusFileHandler::closeLocalFile()
{
  if (mOpenFd>=0) {
    close(mOpenFd);
    mOpenFd = -1;
  }
}


uint16_t ModbusFileHandler::maxRecordsPerRequest()
{
  // The PDU max size is MODBUS_MAX_PDU_LENGTH (253).
  // A nice payload size below that is 200 = 100 records
  return 100;
}


uint16_t ModbusFileHandler::recordAddrsPerChunk()
{
  return maxRecordsPerRequest()/mSingleRecordLength;
}



// P44 Header
// Rec  Offs  Size  Type      Field Description
// ---  ----  ----  --------  -------------------------------------------------------------------------------------------------
//   0     0     4  uint32_t  magic 32bit word to identify this version of the P44Fileheader
//   2     4     4  uint32_t  file size in bytes
//   4     8     4  uint32_t  CRC32 of total file
//   6    12     1  uint8_t   (MSByte) number of segments (consecutive file numbers for the same file)
//   6    13     1  uint8_t   (LSByte) number of uint16_t quantities addressed by a record number
//   7    14     2  uint16_t  record number of first actual file data record. Only from this record number onwards singleRecordLength>0 is active
//   8    16     4  uint32_t  number of first failed record (over all segments!) in multicast write. 0=none (because record 0 is always in header)
//  10    20                  HEADER SIZE
static const uint16_t p44HeaderSize = 20;
static const uint16_t p44HeaderRecords = (p44HeaderSize+1)/2;

static const uint8_t p44HeaderMagic[4] = { 0x42, 0x93, 0x25, 0x44 };


uint16_t ModbusFileHandler::numP44HeaderRecords()
{
  return p44HeaderRecords;
}


int ModbusFileHandler::generateP44Header(uint8_t* aDataP, int aMaxDataLen)
{
  if (!mUseP44Header) return 0;
  if (!mValidP44Header || aMaxDataLen<p44HeaderSize) return -1;
  int i = 0;
  // magic ID word
  aDataP[i++] = p44HeaderMagic[0];
  aDataP[i++] = p44HeaderMagic[1];
  aDataP[i++] = p44HeaderMagic[2];
  aDataP[i++] = p44HeaderMagic[3];
  // file size
  aDataP[i++] = (mLocalFileSize>>24) & 0xFF;
  aDataP[i++] = (mLocalFileSize>>16) & 0xFF;
  aDataP[i++] = (mLocalFileSize>>8) & 0xFF;
  aDataP[i++] = (mLocalFileSize) & 0xFF;
  // CRC
  aDataP[i++] = (mLocalCRC32>>24) & 0xFF;
  aDataP[i++] = (mLocalCRC32>>16) & 0xFF;
  aDataP[i++] = (mLocalCRC32>>8) & 0xFF;
  aDataP[i++] = (mLocalCRC32) & 0xFF;
  // number of segments needed (consecutive file numbers for the same file)
  aDataP[i++] = (uint8_t)mNeededSegments;
  // number of uint16_t quantities addressed by a record number
  aDataP[i++] = (uint8_t)mSingleRecordLength;
  // record number of first actual file data record
  aDataP[i++] = (mFirstDataRecord>>8) & 0xFF;
  aDataP[i++] = (mFirstDataRecord) & 0xFF;
  // number of first missing record in multicast write, or noneMissing if all complete
  uint32_t localMissingRecord = mNoneMissing;
  if (!mMissingDataRecords.empty()) {
    // report first missing record
    localMissingRecord = mMissingDataRecords.front();
  }
  aDataP[i++] = (localMissingRecord>>24) & 0xFF;
  aDataP[i++] = (localMissingRecord>>16) & 0xFF;
  aDataP[i++] = (localMissingRecord>>8) & 0xFF;
  aDataP[i++] = (localMissingRecord) & 0xFF;
  // done
  return i;
}


ErrorPtr ModbusFileHandler::parseP44Header(const uint8_t* aDataP, int aPos, int aDataLen, bool aInitialize)
{
  if (mUseP44Header) {
    // check size
    if (aPos+p44HeaderSize>aDataLen) {
      return Error::err<ModBusError>(ModBusError::P44HeaderError, "not enough bytes for a p44 header");
    }
    // check magic
    if (
      aDataP[aPos]!=p44HeaderMagic[0] ||
      aDataP[aPos+1]!=p44HeaderMagic[1] ||
      aDataP[aPos+2]!=p44HeaderMagic[2] ||
      aDataP[aPos+3]!=p44HeaderMagic[3]
    ) {
      return Error::err<ModBusError>(ModBusError::P44HeaderError, "bad p44 header magic");
    }
    aPos += 4;
    // expected file size
    mRemoteFileSize =
      (aDataP[aPos]<<24) +
      (aDataP[aPos+1]<<16) +
      (aDataP[aPos+2]<<8) +
      (aDataP[aPos+3]);
    aPos += 4;
    // expected CRC
    mRemoteCRC32 =
      (aDataP[aPos]<<24) +
      (aDataP[aPos+1]<<16) +
      (aDataP[aPos+2]<<8) +
      (aDataP[aPos+3]);
    aPos += 4;
    // file layout
    uint8_t nseg = aDataP[aPos++]; // number of segments that will/should be used for the transfer
    uint8_t srl = aDataP[aPos++]; // number of uint16_t quantities addressed by a record number
    uint16_t fdr = // record number of first actual file data record
      (aDataP[aPos]<<8) +
      (aDataP[aPos+1]);
    aPos += 2;
    if (aInitialize) {
      mNeededSegments = nseg;
      mSingleRecordLength = srl;
      mFirstDataRecord = fdr;
      // derive recordsPerChunk
      mRecordsPerChunk = recordAddrsPerChunk()*mSingleRecordLength; // how many records (2-byte data items) are in a chunk
      // reset missing records tracking
      mMissingDataRecords.clear();
      mNextExpectedDataRecord = 0; // no data received yet (header not counted in DATA record numbers
      mLocalCRC32 = mInvalidCRC;
      mPendingFinalisation = false;
      mValidP44Header = true;
    }
    else {
      if (nseg!=mNeededSegments || srl!=mSingleRecordLength || mFirstDataRecord!=fdr) {
        return Error::err<ModBusError>(ModBusError::P44HeaderError,
          "p44 header file layout mismatch: segments/recordlen/firstrecord expected=%d/%d/%d, found=%d/%d/%d",
          mNeededSegments, mSingleRecordLength, mFirstDataRecord,
          nseg, srl, fdr
        );
      }
    }
    // number of next remotely detected missing record in multicast write. 0=none
    mRemoteMissingRecord =
      (aDataP[aPos]<<24) +
      (aDataP[aPos+1]<<16) +
      (aDataP[aPos+2]<<8) +
      (aDataP[aPos+3]);
    aPos += 4;
    FOCUSLOG(
      "File no %d / '%s' successfully read p44 header:\n"
      "- remoteFileSize = %u, CRC=0x%08x\n"
      "- neededSegments=%u, maxSegments=%u\n"
      "- firstDataRecord=%u, singleRecordLength=%u, recordsPerChunk=%u, maxRecordsPerRequest=%hu\n"
      "- remoteMissingRecord=%u/0x%x",
      mFileNo, filePathFor(mFileNo, true).c_str(),
      mRemoteFileSize, mRemoteCRC32,
      mNeededSegments, mMaxSegments,
      mFirstDataRecord, mSingleRecordLength, mRecordsPerChunk, maxRecordsPerRequest(),
      mRemoteMissingRecord, mRemoteMissingRecord
    );
  }
  return ErrorPtr();
}


ErrorPtr ModbusFileHandler::updateLocalCRC()
{
  mLocalCRC32 = mInvalidCRC;
  if (mOpenFd<0) return TextError::err("finalize: file not open");
  // also obtain the CRC
  lseek(mOpenFd, 0, SEEK_SET);
  Crc32 crc;
  uint32_t bytes = mLocalFileSize;
  size_t crcBufSz = 8*1024;
  uint8_t crcbuf[crcBufSz];
  while (bytes>0) {
    int rc = (int)read(mOpenFd, crcbuf, bytes>crcBufSz ? crcBufSz : bytes);
    if (rc<0) {
      return SysError::errNo("cannot read file data for CRC: ");
    }
    crc.addBytes(rc, crcbuf);
    bytes -= rc;
  }
  mLocalCRC32 = crc.getCRC();
  return ErrorPtr();
}


ErrorPtr ModbusFileHandler::finalize()
{
  if (mPendingFinalisation && mUseP44Header) {
    updateLocalCRC();
    mPendingFinalisation = false;
    LOG(LOG_NOTICE,
        "Successful p44header-controlled file transfer finalisation:\n"
        "- path='%s'\n"
        "- finalpath='%s'\n"
        "- local: size=%u, CRC=0x%08x\n"
        "- remote: size=%u, CRC=0x%08x",
        filePathFor(mCurrentBaseFileNo, true).c_str(),
        filePathFor(mCurrentBaseFileNo, false).c_str(),
        mLocalFileSize, mLocalCRC32,
        mRemoteFileSize, mRemoteCRC32
    );
    // make sure file is properly closed before executing callback
    closeLocalFile();
    if (mFileWriteCompleteCB) {
      mFileWriteCompleteCB(
        mCurrentBaseFileNo, // the fileNo accessed
        filePathFor(mCurrentBaseFileNo, false), // the final path
        mFinalBasePath.empty() ? "" : filePathFor(mCurrentBaseFileNo, true) // the temp path, if any
      );
    }
  }
  else {
    closeLocalFile();
  }
  return (!mUseP44Header || fileIntegrityOK()) ? ErrorPtr() : Error::err<ModBusError>(EMBBADCRC, "File CRC mismatch in p44header");
}


ErrorPtr ModbusFileHandler::readLocalFileInfo(bool aInitialize)
{
  if (mOpenFd<0) return TextError::err("readLocalFileInfo: file not open");
  if (aInitialize) mValidP44Header = false; // forget current header info
  struct stat s;
  fstat(mOpenFd, &s);
  mLocalFileSize = (uint32_t)s.st_size;
  if (aInitialize) {
    updateLocalCRC();
    // most compatible mode, ok for small files
    mSingleRecordLength = 1;
    mNeededSegments = 1;
    mFirstDataRecord = mUseP44Header ? p44HeaderRecords : 0;
    // when starting from a local file, we just set our size
    mRecordsPerChunk = maxRecordsPerRequest();
    // calculate singleRecordLength
    if (mLocalFileSize>(0x10000-mFirstDataRecord)*2) {
      // file is too big for single register record.
      if (!mUseP44Header) {
        return Error::err<ModBusError>(EMBXILVAL, "file to big to send w/o p44hdr");
      }
      mSingleRecordLength = mRecordsPerChunk; // fits into a PDU along with overhead
      // now calculate the needed number of segments
      mNeededSegments = (mLocalFileSize/2/mSingleRecordLength+mFirstDataRecord)/0x10000+1;
      if (mMaxSegments!=0 && mNeededSegments>mMaxSegments) {
        return Error::err<ModBusError>(EMBXILVAL, "file exceeds max allowed segments");
      }
    }
    mValidP44Header = true; // is valid now
  }
  return ErrorPtr();
}


bool ModbusFileHandler::isEOFforChunk(uint32_t aChunkIndex, bool aRemotely)
{
  if (mCurrentBaseFileNo==0) return true; // no file is current -> EOF
  if (!mValidP44Header) return false; // we don't know where the EOF is -> assume NOT EOF
  return (aChunkIndex*mRecordsPerChunk*2) >= (aRemotely ? mRemoteFileSize : mLocalFileSize);
}


void ModbusFileHandler::addressForMaxChunk(uint32_t aChunkIndex, uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords)
{
  if (mCurrentBaseFileNo==0) return; // no file is current
  // now calculate record and file no out of chunk no
  int recordAddrsPerChunk = mRecordsPerChunk/mSingleRecordLength; // how many record *addresses* are in a chunk
  uint32_t rawRecordNo = mFirstDataRecord + aChunkIndex*recordAddrsPerChunk;
  uint16_t segmentOffset = rawRecordNo>>16; // recordno only has 16 bits
  // assign results
  aFileNo = mCurrentBaseFileNo+segmentOffset;
  aRecordNo = rawRecordNo & 0xFFFF;
  aNumRecords = mRecordsPerChunk;
}


bool ModbusFileHandler::addrForNextRetransmit(uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords)
{
  if (!mValidP44Header || mRemoteMissingRecord==mNoneMissing) return false;
  uint32_t rec = mRemoteMissingRecord+mFirstDataRecord;
  uint16_t seg = (rec>>16) & 0xFFFF;
  if (seg>=mNeededSegments) return false;
  aFileNo = mFileNo + seg;
  aRecordNo = rec & 0xFFFF;
  aNumRecords = mRecordsPerChunk;
  return true;
}


bool ModbusFileHandler::fileIntegrityOK()
{
  return
    mValidP44Header &&
    mLocalFileSize==mRemoteFileSize &&
    mLocalCRC32==mRemoteCRC32;
}

#if ENABLE_MODBUS_SCRIPT_FUNCS

// MARK: - modbus connection scripting

// bytetime(byte_time_in_seconds)
FUNC_ARG_DEFS(bytetime, { numeric } );
static void bytetime_func(ModbusConnectionPtr aModbusConnection, BuiltinFunctionContextPtr f)
{
  int bytetime = f->arg(0)->doubleValue()*1E9;
  f->finish(ErrorValue::trueOrError(aModbusConnection->setByteTimeNs(bytetime)));
}
static void m_bytetime_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  bytetime_func(o->modbus(), f);
}
static void s_bytetime_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  bytetime_func(o->modbus(), f);
}


// recoverymode(link, protocol)
FUNC_ARG_DEFS(recoverymode, { numeric|optionalarg }, { numeric|optionalarg } );
static void recoverymode_func(ModbusConnectionPtr aModbusConnection, BuiltinFunctionContextPtr f)
{
  int rmod = MODBUS_ERROR_RECOVERY_NONE;
  if (f->arg(0)->boolValue()) rmod |= MODBUS_ERROR_RECOVERY_LINK;
  if (f->arg(1)->boolValue()) rmod |= MODBUS_ERROR_RECOVERY_PROTOCOL;
  f->finish(ErrorValue::trueOrError(aModbusConnection->setRecoveryMode((modbus_error_recovery_mode)rmod)));
}
static void m_recoverymode_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  recoverymode_func(o->modbus(), f);
}
static void s_recoverymode_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  recoverymode_func(o->modbus(), f);
}


// debug(enable)
FUNC_ARG_DEFS(debug, { numeric } );
static void debug_func(ModbusConnectionPtr aModbusConnection, BuiltinFunctionContextPtr f)
{
  aModbusConnection->setDebug(f->arg(0)->boolValue());
  f->finish();
}
static void m_debug_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  debug_func(o->modbus(), f);
}
static void s_debug_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  debug_func(o->modbus(), f);
}



// connection(connectionspec [, txenablepin|RS232|RTS [, rxenablepin, [, txdisabledelay]])
FUNC_ARG_DEFS(connection, { text }, { text|objectvalue|optionalarg }, { text|objectvalue|optionalarg } );
static void connection_func(ModbusConnectionPtr aModbusConnection, BuiltinFunctionContextPtr f)
{
  const char* txspecP = NULL;
  if (dynamic_cast<DigitalIoObj*>(f->arg(1).get())==NULL) {
    // not a pin
    string txspec = f->arg(1)->stringValue();
    if (txspec=="RS232" || txspec=="RTS") {
      txspecP = txspec.c_str(); // pass through
    }
    else {
      txspecP = "*"; // placeholder for pin set later
    }
  }
  else {
    // pre-existing pin object, will be set later
    txspecP = "*"; // placeholder for pin set later
  }
  // create basic connection
  MLMicroSeconds txdisabledelay = Never;
  if (f->arg(3)->defined()) txdisabledelay = f->arg(3)->doubleValue()*Second;
  ErrorPtr err = aModbusConnection->setConnectionSpecification(
    f->arg(0)->stringValue().c_str(),
    MODBUS_TCP_DEFAULT_PORT,
    MODBUS_RTU_DEFAULT_PARAMS,
    txspecP,
    txdisabledelay,
    NULL
  );
  if (Error::isOK(err)) {
    // set the pins
    aModbusConnection->mModbusTxEnable = DigitalIoObj::digitalIoFromArg(f->arg(1), true, false);
    aModbusConnection->mModbusRxEnable = DigitalIoObj::digitalIoFromArg(f->arg(2), true, false);
  }
  f->finish(ErrorValue::trueOrError(err));
}
static void m_connection_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  connection_func(o->modbus(), f);
}
static void s_connection_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  connection_func(o->modbus(), f);
}


// connect([autoflush])
FUNC_ARG_DEFS(connect, { numeric|optionalarg } );
static void connect_func(ModbusConnectionPtr aModbusConnection, BuiltinFunctionContextPtr f)
{
  f->finish(ErrorValue::trueOrError(aModbusConnection->connect(f->arg(0)->boolValue())));
}
static void m_connect_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  connect_func(o->modbus(), f);
}
static void s_connect_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  connect_func(o->modbus(), f);
}


// close()
static void close_func(ModbusConnectionPtr aModbusConnection, BuiltinFunctionContextPtr f)
{
  aModbusConnection->close();
  f->finish();
}
static void m_close_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  close_func(o->modbus(), f);
}
static void s_close_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  close_func(o->modbus(), f);
}





// MARK: - modbus slave scripting

ModbusSlaveObjPtr ModbusSlave::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new ModbusSlaveObj(this);
  }
  return mRepresentingObj;
}


// setreg(regaddr, value [,input])
// setbit(bitaddr, value [,input])
FUNC_ARG_DEFS(set, { numeric }, { numeric }, { numeric|optionalarg } );
static void setreg_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  o->modbus()->setReg(f->arg(0)->intValue(), f->arg(2)->boolValue(), f->arg(1)->intValue());
  f->finish(o); // return myself for chaining calls
}
static void setbit_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  o->modbus()->setBit(f->arg(0)->intValue(), f->arg(2)->boolValue(), f->arg(1)->boolValue());
  f->finish(o); // return myself for chaining calls
}


// getreg(regaddr [,input])
// getsreg(regaddr [,input]) // signed interpretation of 16-bit value
// getbit(bitaddr [,input])
FUNC_ARG_DEFS(get, { numeric }, { numeric|optionalarg } );
static void getreg_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  f->finish(new IntegerValue((uint16_t)(o->modbus()->getReg(f->arg(0)->intValue(), f->arg(1)->boolValue()))));
}
static void getsreg_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  f->finish(new IntegerValue((int16_t)(o->modbus()->getReg(f->arg(0)->intValue(), f->arg(1)->boolValue()))));
}
static void getbit_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  f->finish(new BoolValue(o->modbus()->getBit(f->arg(0)->intValue(), f->arg(1)->boolValue())));
}


// access()
static void access_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  // return event source for access messages
  f->finish(new OneShotEventNullValue(o, "modbus slave access"));
}


// slaveaddress([slave_address])
FUNC_ARG_DEFS(slaveaddress, { numeric|optionalarg } );
static void slaveaddress_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  if (f->arg(0)->defined()) {
    o->modbus()->setSlaveAddress(f->arg(0)->intValue());
  }
  f->finish(new IntegerValue(o->modbus()->getSlaveAddress()));
}


// slaveid(slave_id_string)
FUNC_ARG_DEFS(slaveid, { text } );
static void slaveid_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  o->modbus()->setSlaveId(f->arg(0)->stringValue());
  f->finish();
}


// setmodel(registermodel_json)
//   { "coils" : { "first":100, "num":10 }, "registers":{ "first":100, "num":20 } }
FUNC_ARG_DEFS(setmodel, { objectvalue } );
static void setmodel_func(BuiltinFunctionContextPtr f)
{
  ModbusSlaveObj* o = dynamic_cast<ModbusSlaveObj*>(f->thisObj().get());
  assert(o);
  struct {
    int first;
    int num;
  } regdef[4];
  const char* types[4] = { "coils", "bits", "registers", "inputs" };
  JsonObjectPtr j = f->arg(0)->jsonValue();
  ErrorPtr err;
  if (j) {
    for (int k=0; k<4; k++) {
      regdef[k].first = 1;
      regdef[k].num = 0;
      JsonObjectPtr rd = j->get(types[k]);
      if (rd) {
        JsonObjectPtr o;
        if (rd->get("first", o)) {
          regdef[k].first = o->int32Value();
          regdef[k].num = 1; // default to one
        }
        if (rd->get("num", o)) regdef[k].num = o->int32Value();
      }
    }
  }
  err = o->modbus()->setRegisterModel(
    regdef[0].first, regdef[0].num,
    regdef[1].first, regdef[1].num,
    regdef[2].first, regdef[2].num,
    regdef[3].first, regdef[3].num
  );
  f->finish(ErrorValue::trueOrError(err));
}


// master()
static void s_ismaster_func(BuiltinFunctionContextPtr f)
{
  f->finish(new BoolValue(false));
}


static const BuiltinMemberDescriptor modbusSlaveMembers[] = {
  { "master", executable|numeric, 0, NULL, &s_ismaster_func },  // for applevel predefined master or slave mode, this is to check which type we have
  // common
  { "connection", executable|objectvalue, connection_numargs, connection_args, &s_connection_func },
  { "bytetime", executable|objectvalue, bytetime_numargs, bytetime_args, &s_bytetime_func },
  { "recoverymode", executable|objectvalue, recoverymode_numargs, recoverymode_args, &s_recoverymode_func },
  { "debug", executable|objectvalue, debug_numargs, debug_args, &s_debug_func },
  { "connect", executable|null, connect_numargs, connect_args, &s_connect_func },
  { "close", executable|null, 0, NULL, &s_close_func },
  // slave only
  FUNC_DEF_C_ARG(setreg, executable|null, set),
  FUNC_DEF_C_ARG(setbit, executable|null, set),
  FUNC_DEF_C_ARG(getreg, executable|null, get),
  FUNC_DEF_C_ARG(getsreg, executable|null, get),
  FUNC_DEF_C_ARG(getbit, executable|null, get),
  FUNC_DEF_NOARG(access, executable|text|null),
  FUNC_DEF_W_ARG(slaveaddress, executable|numeric),
  FUNC_DEF_W_ARG(slaveid, executable|null),
  FUNC_DEF_W_ARG(setmodel, executable|null),
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedModbusSlaveFunctionLookupP = NULL;

ModbusSlaveObj::ModbusSlaveObj(ModbusSlavePtr aModbus) :
  mModbus(aModbus)
{
  registerSharedLookup(sharedModbusSlaveFunctionLookupP, modbusSlaveMembers);
}


void ModbusSlaveObj::deactivate()
{
  mModbus->setValueAccessHandler(NoOP);
  mModbus->close();
}



ModbusSlaveObj::~ModbusSlaveObj()
{
  mModbus->setValueAccessHandler(NoOP);
}


ErrorPtr ModbusSlaveObj::gotAccessed(int aAddress, bool aBit, bool aInput, bool aWrite)
{
  ObjectValue* acc = new ObjectValue();
  acc->setMemberByName("reg", aBit ? new ScriptObj() : new IntegerValue(aAddress));
  acc->setMemberByName("bit", aBit ? new IntegerValue(aAddress) : new ScriptObj());
  acc->setMemberByName("addr", new IntegerValue(aAddress));
  acc->setMemberByName("input", new BoolValue(aInput));
  acc->setMemberByName("write", new BoolValue(aWrite));
  sendEvent(acc);
  return ErrorPtr();
}


// MARK: - modbus master scripting

ModbusMasterObjPtr ModbusMaster::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new ModbusMasterObj(this);
  }
  return mRepresentingObj;
}


// slave(slaveaddress)
FUNC_ARG_DEFS(slave, { numeric } );
static void slave_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  o->modbus()->setSlaveAddress(f->arg(0)->intValue());
  f->finish(o); // return myself for chaining calls
}


// writereg(regaddr, value)
// writebit(bitaddr, value)
FUNC_ARG_DEFS(write, { numeric }, { numeric } );
static void writereg_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  ErrorPtr err = o->modbus()->writeRegister(f->arg(0)->intValue(), f->arg(1)->intValue());
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err->withPrefix("writing register: ")));
    return;
  }
  f->finish(); // return myself for chaining calls
}
static void writebit_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  ErrorPtr err = o->modbus()->writeRegister(f->arg(0)->intValue(), f->arg(1)->boolValue());
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err->withPrefix("writing bit: ")));
    return;
  }
  f->finish(); // return myself for chaining calls
}


// readreg(regaddr [,input])
// readsreg(regaddr [,input]) // signed interpretation of 16-bit value
// readbit(bitaddr [,input])
FUNC_ARG_DEFS(read, { numeric }, { numeric|optionalarg } );
static void readreg_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  uint16_t v;
  ErrorPtr err = o->modbus()->readRegister(f->arg(0)->intValue(), v, f->arg(1)->boolValue());
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err->withPrefix("reading register: ")));
    return;
  }
  f->finish(new IntegerValue(v));
}
static void readsreg_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  uint16_t v;
  ErrorPtr err = o->modbus()->readRegister(f->arg(0)->intValue(), v, f->arg(1)->boolValue());
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err->withPrefix("reading register: ")));
    return;
  }
  f->finish(new IntegerValue((int16_t)v));
}
static void readbit_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  bool b;
  ErrorPtr err = o->modbus()->readBit(f->arg(0)->intValue(), b, f->arg(1)->boolValue());
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err->withPrefix("reading bit: ")));
    return;
  }
  f->finish(new BoolValue(b));
}

// readinfo()
static void readinfo_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  string id;
  bool runIndicator;
  ErrorPtr err = o->modbus()->readSlaveInfo(id, runIndicator);
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err->withPrefix("reading info: ")));
    return;
  }
  f->finish(new StringValue(id));
}


// findslaves(idmatch, from, to)
FUNC_ARG_DEFS(findslaves, { text }, { numeric }, { numeric } );
static void findslaves_func(BuiltinFunctionContextPtr f)
{
  ModbusMasterObj* o = dynamic_cast<ModbusMasterObj*>(f->thisObj().get());
  assert(o);
  p44::ModbusMaster::SlaveAddrList slaves;
  ErrorPtr err = o->modbus()->findSlaves(slaves, f->arg(0)->stringValue(), f->arg(1)->intValue(), f->arg(2)->intValue());
  ArrayValue* res = new ArrayValue();
  for(p44::ModbusMaster::SlaveAddrList::iterator pos = slaves.begin(); pos!=slaves.end(); ++pos) {
    res->appendMember(new IntegerValue(*pos));
  }
  f->finish(res);
}


// master()
static void m_ismaster_func(BuiltinFunctionContextPtr f)
{
  f->finish(new BoolValue(true));
}


static const BuiltinMemberDescriptor modbusMasterMembers[] = {
  { "master", executable|numeric, 0, NULL, &m_ismaster_func }, // for applevel predefined master or slave mode, this is to check which type we have
  // common
  { "connection", executable|objectvalue, connection_numargs, connection_args, &m_connection_func },
  { "bytetime", executable|objectvalue, bytetime_numargs, bytetime_args, &m_bytetime_func },
  { "recoverymode", executable|objectvalue, recoverymode_numargs, recoverymode_args, &m_recoverymode_func },
  { "debug", executable|objectvalue, debug_numargs, debug_args, &m_debug_func },
  { "connect", executable|null, connect_numargs, connect_args, &m_connect_func },
  { "close", executable|null, 0, NULL, &m_close_func },
  // master only
  FUNC_DEF_W_ARG(slave, executable|objectvalue),
  FUNC_DEF_W_ARG(findslaves, executable|objectvalue),
  FUNC_DEF_C_ARG(writereg, executable|error|null, write),
  FUNC_DEF_C_ARG(writebit, executable|error|null, write),
  FUNC_DEF_C_ARG(readreg, executable|error|numeric, read),
  FUNC_DEF_C_ARG(readsreg, executable|error|numeric, read),
  FUNC_DEF_C_ARG(readbit, executable|error|numeric, read),
  FUNC_DEF_NOARG(readinfo, executable|error|text),
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedModbusMasterFunctionLookupP = NULL;

ModbusMasterObj::ModbusMasterObj(ModbusMasterPtr aModbus) :
  mModbus(aModbus)
{
  registerSharedLookup(sharedModbusMasterFunctionLookupP, modbusMasterMembers);
}


void ModbusMasterObj::deactivate()
{
  mModbus->close();
}


ModbusMasterObj::~ModbusMasterObj()
{
}



// modbusmaster()
static void modbusmaster_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  ModbusMasterPtr mbm = new ModbusMaster();
  f->finish(mbm->representingScriptObj());
}


// modbusslave()
static void modbusslave_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  ModbusSlavePtr mbs = new ModbusSlave();
  f->finish(mbs->representingScriptObj());
}


static const BuiltinMemberDescriptor modbusGlobals[] = {
  FUNC_DEF_NOARG(modbusmaster, executable|null),
  FUNC_DEF_NOARG(modbusslave, executable|null),
  { NULL } // terminator
};

ModbusLookup::ModbusLookup() :
  inherited(modbusGlobals)
{
}

#endif // ENABLE_MODBUS_SCRIPT_FUNCS

#endif // ENABLE_MODBUS




