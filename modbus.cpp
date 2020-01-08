//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


// MARK: - ModbusConnection

ModbusConnection::ModbusConnection() :
  modbus(NULL),
  isTcp(false),
  doAcceptConnections(false),
  serverSocket(-1),
  slaveAddress(-1), // none
  connected(false),
  floatMode(float_dcba) // this was the standard mode in older libmodbus
{
}


ModbusConnection::~ModbusConnection()
{
  clearModbusContext();
}


void ModbusConnection::clearModbusContext()
{
  close();
  if (modbus) {
    modbus_free(modbus);
    modbus = NULL;
  }
}


void ModbusConnection::setDebug(bool aDebugEnabled)
{
  if (modbus) {
    modbus_set_debug(modbus, aDebugEnabled);
  }
}


extern "C" {

  void setRts(modbus_t* ctx, int on, void* cbctx)
  {
    ModbusConnection* modbusConnection = (ModbusConnection*)cbctx;
    if (modbusConnection && modbusConnection->modbusTxEnable) {
      modbusConnection->modbusTxEnable->set(on);
    }
  }

}



ErrorPtr ModbusConnection::setConnectionSpecification(
  const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
  const char *aTransmitEnableSpec, MLMicroSeconds aTxDisableDelay, int aByteTimeNs
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
  isTcp = !SerialComm::parseConnectionSpecification(
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
  if (!isTcp) {
    bool rs232 = aTransmitEnableSpec && strcasecmp("RS232", aTransmitEnableSpec)==0;
    if (!rs232) {
      if (aTransmitEnableSpec!=NULL && *aTransmitEnableSpec!=0 && strcasecmp("RTS", aTransmitEnableSpec)!=0) {
        // not using native RTS, but digital IO specification
        modbusTxEnable = DigitalIoPtr(new DigitalIo(aTransmitEnableSpec, true, false));
      }
    }
    if (baudRate==0 || connectionPath.empty()) {
      err = Error::err<ModBusError>(ModBusError::InvalidConnParams, "invalid RTU connection params");
    }
    else {
      modbus = modbus_new_rtu(
        connectionPath.c_str(),
        baudRate,
        parityEnable ? (evenParity ? 'E' : 'O') : 'N',
        charSize,
        twoStopBits ? 2 : 1
      );
      if (modbus==0) {
        mberr = errno;
      }
      else {
        if (aByteTimeNs>0) {
          LOG(LOG_DEBUG, "Setting explicit byte time: %d nS, calculated value is %d nS", aByteTimeNs, modbus_rtu_get_byte_time(modbus));
          modbus_rtu_set_byte_time(modbus, (int)aByteTimeNs);
        }
        if (rs232) {
          if (modbus_rtu_set_serial_mode(modbus, MODBUS_RTU_RS232)<0) mberr = errno;
        }
        else {
          // set custom RTS if needed (FIRST, otherwise modbus_rtu_set_serial_mode() might fail when TIOCSRS485 does not work)
          if (mberr==0 && modbusTxEnable) {
            if (modbus_rtu_set_custom_rts_ex(modbus, setRts, this)<0) mberr = errno;
          }
          if (mberr==0) {
            if (modbus_rtu_set_serial_mode(modbus, MODBUS_RTU_RS485)<0) mberr = errno;
          }
          if (mberr==0) {
            if (modbus_rtu_set_rts(modbus, MODBUS_RTU_RTS_UP)<0) mberr = errno;
          }
        }
        if (mberr==0) {
          if (aTxDisableDelay!=Never) {
            if (modbus_rtu_set_rts_delay(modbus, (int)aTxDisableDelay)<0) mberr = errno;
          }
        }
      }
    }
  }
  else {
    if (!aConnectionSpec || *aConnectionSpec==0) {
      err = Error::err<ModBusError>(ModBusError::InvalidConnParams, "invalid TCP connection params");
    }
    modbus = modbus_new_tcp(connectionPath.c_str(), connectionPort);
    if (modbus==0) mberr = errno;
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


void ModbusConnection::mbContextReady()
{
  if (slaveAddress>=0) {
    modbus_set_slave(modbus, slaveAddress);
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
  if (aSlaveAddress!=slaveAddress) {
    slaveAddress = aSlaveAddress;
    if (modbus && slaveAddress>=0) {
      modbus_set_slave(modbus, aSlaveAddress);
    }
  }
}


ErrorPtr ModbusConnection::connect()
{
  ErrorPtr err;
  if (!modbus) {
    err = Error::err<ModBusError>(ModBusError::InvalidConnParams, "no valid connection parameters - cannot open connection");
  }
  if (!connected) {
    if (isTcp && doAcceptConnections) {
      // act as TCP server, waiting for connections
      serverSocket = modbus_tcp_listen(modbus, 1);
      if (serverSocket<0) {
        return Error::err<ModBusError>(errno)->withPrefix("cannot listen: ");
      }
      // - install connection watcher
      MainLoop::currentMainLoop().registerPollHandler(serverSocket, POLLIN, boost::bind(&ModbusConnection::connectionAcceptHandler, this, _1, _2));
      connected = true;
    }
    else {
      // act as TCP client or just serial connection
      if (modbus_connect(modbus)<0) {
        if (errno!=EINPROGRESS) {
          err = ModBusError::err<ModBusError>(errno)->withPrefix("connecting: ");
        }
      }
      if (Error::isOK(err)) {
        startServing(); // start serving in case this is a Modbus server
        connected = true;
      }
    }
  }
  return err;
}


bool ModbusConnection::connectionAcceptHandler(int aFd, int aPollFlags)
{
  if (aPollFlags & POLLIN) {
    // server socket has data, means connection waiting to get accepted
    modbus_tcp_accept(modbus, &serverSocket);
    startServing();
  }
  // handled
  return true;

}


void ModbusConnection::close()
{
  if (modbus && connected) {
    if (serverSocket>=0) {
      MainLoop::currentMainLoop().unregisterPollHandler(serverSocket);
      ::close(serverSocket);
    }
    modbus_close(modbus);
  }
  connected = false;
}


int ModbusConnection::flush()
{
  int flushed = 0;
  if (modbus) {
    flushed = modbus_flush(modbus);
  }
  return flushed;
}



double ModbusConnection::getAsDouble(const uint16_t *aTwoRegs)
{
  switch (floatMode) {
    case float_abcd: return modbus_get_float_abcd(aTwoRegs);
    case float_badc: return modbus_get_float_badc(aTwoRegs);
    case float_cdab: return modbus_get_float_cdab(aTwoRegs);
    default:
    case float_dcba: return modbus_get_float_dcba(aTwoRegs);
  }
}


void ModbusConnection::setAsDouble(uint16_t *aTwoRegs, double aDouble)
{
  switch (floatMode) {
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
    modbus,
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
  aRspLen = modbus_build_response_basis(modbus, &aSft, aRsp);
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


ErrorPtr ModbusMaster::readSlaveInfo(string& aId, bool& aRunIndicator)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    ModBusPDU slaveid;
    int bytes = modbus_report_slave_id(modbus, MODBUS_MAX_PDU_LENGTH, slaveid);
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
          LOG(LOG_INFO, "Slave %d id '%s' does not match", sa, id.c_str());
        }
      }
      else {
        LOG(LOG_INFO, "Slave %d returns error for slaveid query: %s", sa, err->text());
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
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    int ret;
    if (aInput) {
      ret = modbus_read_input_registers(modbus, aRegAddr, aNumRegs, aRegsP);
    }
    else {
      ret = modbus_read_registers(modbus, aRegAddr, aNumRegs, aRegsP);
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
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    if (modbus_write_registers(modbus, aRegAddr, aNumRegs, aRegsP)<0) {
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
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    int ret;
    if (aInput) {
      ret = modbus_read_input_bits(modbus, aBitAddr, aNumBits, aBitsP);
    }
    else {
      ret = modbus_read_bits(modbus, aBitAddr, aNumBits, aBitsP);
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
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    if (modbus_write_bits(modbus, aBitAddr, aNumBits, aBitsP)<0) {
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
    int reqLen = modbus_build_request_basis(modbus, MODBUS_FC_WRITE_FILE_RECORD, req);
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
        rc = modbus_send_msg(modbus, req, reqLen);
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
        int rspLen = modbus_receive_msg(modbus, rsp, MSG_CONFIRMATION);
        if (rspLen<0) {
          rc = -1;
        }
        else if (rspLen>0) {
          rc = modbus_pre_check_confirmation(modbus, req, rsp, rspLen);
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
    int reqLen = modbus_build_request_basis(modbus, MODBUS_FC_READ_FILE_RECORD, req);
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
      int rc = modbus_send_msg(modbus, req, reqLen);
      if (rc<0) {
        err = Error::err<ModBusError>(errno)->withPrefix("sending read file record request: ");
      }
      else {
        ModBusPDU rsp;
        int rspLen = modbus_receive_msg(modbus, rsp, MSG_CONFIRMATION);
        if (rspLen<0) {
          rc = -1;
        }
        else if (rspLen>0) {
          rc = modbus_pre_check_confirmation(modbus, req, rsp, rspLen);
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
  LOG(LOG_NOTICE, "Sending file '%s' to fileNo %d in slave %d", aLocalFilePath.c_str(), aFileNo, getSlaveAddress());
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
          modbus_flush(modbus);
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
              modbus_flush(modbus);
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
        modbus_flush(modbus);
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
            modbus_flush(modbus);
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
      LOG(LOG_NOTICE, "Sending file '%s' to fileNo %d in %lu slaves, no broadcast (no p44header)", aLocalFilePath.c_str(), aFileNo, aSlaveAddrList.size());
      for (SlaveAddrList::const_iterator pos = aSlaveAddrList.begin(); pos!=aSlaveAddrList.end(); ++pos) {
        setSlaveAddress(*pos);
        LOG(LOG_NOTICE, "- sending file to slave %d", *pos);
        ErrorPtr fileErr = sendFile(handler, aFileNo);
        if (Error::notOK(fileErr)) {
          LOG(LOG_ERR, "Error sending file '%s' to fileNo %d in slave %d: %s", aLocalFilePath.c_str(), aFileNo, *pos, fileErr->text());
          err = fileErr; // return most recent error
        }
      }
    }
    else {
      // with p44header, we can do real broadcast of the data
      LOG(LOG_NOTICE, "Sending file '%s' to fileNo %d as broadcast", aLocalFilePath.c_str(), aFileNo);
      setSlaveAddress(MODBUS_BROADCAST_ADDRESS);
      err = sendFile(handler, aFileNo);
      if (Error::isOK(err)) {
        // query each slave for possibly missing records, send them
        LOG(LOG_NOTICE, "Broadcast complete - now verifying successful transmission");
        for (SlaveAddrList::const_iterator pos = aSlaveAddrList.begin(); pos!=aSlaveAddrList.end(); ++pos) {
          ErrorPtr slerr;
          LOG(LOG_NOTICE, "- Verifying with slave %d", *pos);
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
              modbus_flush(modbus);
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
                modbus_flush(modbus);
              }
              if (Error::notOK(slerr)) break; // failed, done with this slave
            }
            else {
              // no more retransmits pending for this slave
              if (handler->fileIntegrityOK()) {
                LOG(LOG_NOTICE, "- Sending file '%s' to fileNo %d in slave %d confirmed SUCCESSFUL!", aLocalFilePath.c_str(), aFileNo, *pos);
              }
              else {
                err = Error::err<ModBusError>(EMBBADCRC, "CRC or size mismatch after retransmitting all blocks");
              }
              break; // done with this slave
            }
          } // while bad blocks
          if (slerr) {
            LOG(LOG_ERR, "Failed sending file No %d in slave %d: %s", aFileNo, *pos, slerr->text());
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
  registerModel(NULL),
  modbusRcv(NULL)
{
  // by default, server will accept TCP connection (rather than trying to connect)
  doAcceptConnections = true;
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
  if (registerModel) {
    modbus_mapping_free(registerModel);
    registerModel = NULL;
  }
}


void ModbusSlave::setSlaveId(const string aSlaveId)
{
  slaveId = aSlaveId;
  if (modbus) {
    modbus_set_slave_id(modbus, slaveId.c_str());
  }
}


void ModbusSlave::mbContextReady()
{
  if (!slaveId.empty()) {
    modbus_set_slave_id(modbus, slaveId.c_str());
  }
  inherited::mbContextReady();
}


// MARK: - ModbusSlave Request processing

void ModbusSlave::startServing()
{
  if (!modbus) return;
  cancelMsgReception();
  int fd = modbus_get_socket(modbus);
  MainLoop::currentMainLoop().registerPollHandler(fd, POLLIN, boost::bind(&ModbusSlave::modbusFdPollHandler, this, _1, _2));
}


void ModbusSlave::stopServing()
{
  cancelMsgReception();
  if (modbus) {
    int fd = modbus_get_socket(modbus);
    MainLoop::currentMainLoop().unregisterPollHandler(fd);
  }
}


void ModbusSlave::cancelMsgReception()
{
  if (modbusRcv) {
    rcvTimeoutTicket.cancel();
    modbus_receive_free(modbusRcv);
    modbusRcv = NULL;
  }
}


void ModbusSlave::startTimeout()
{
  MLMicroSeconds timeout = MainLoop::timeValToMainLoopTime(modbus_get_select_timeout(modbusRcv));
  if (timeout==Never)
    rcvTimeoutTicket.cancel();
  else
    rcvTimeoutTicket.executeOnce(boost::bind(&ModbusSlave::modbusTimeoutHandler, this), timeout);
}


void ModbusSlave::startMsgReception()
{
  cancelMsgReception(); // stop previous, if any
  modbusRcv = modbus_receive_new(modbus, modbusReq);
  startTimeout();
}


bool ModbusSlave::modbusFdPollHandler(int aFD, int aPollFlags)
{
  if (aPollFlags & POLLIN) {
    // got some data
    if (!modbusRcv) {
      // start new request
      startMsgReception();
      if (!modbusRcv) {
        LOG(LOG_CRIT, "cannot create new Modbus receive context");
        return false;
      }
    }
    int reqLen = modbus_receive_step(modbusRcv);
    if (reqLen<0 && errno==EAGAIN) {
      // no complete message yet
      // - re-start timeout
      startTimeout();
      return true;
    }
    if (reqLen>0) {
      rcvTimeoutTicket.cancel();
      // got request
      FOCUSLOG("Modbus received request, %d bytes", reqLen);
      // - process it
      int rspLen = modbus_process_request(modbus, modbusReq, reqLen, modbusRsp, modbus_slave_function_handler, this);
      /* Send response, if any */
      if (rspLen > 0) {
        int rc = modbus_send_msg(modbus, modbusRsp, rspLen);
        if (rc<0) {
          ErrorPtr err = Error::err<ModBusError>(errno)->withPrefix("sending response: ");
          LOG(LOG_ERR, "Error sending Modbus response: %s", Error::text(err));
        }
      }
    }
    else if (reqLen<0) {
      ErrorPtr err = Error::err<ModBusError>(errno);
      if (errno!=ECONNRESET) LOG(LOG_ERR, "Error receiving Modbus request: %s", Error::text(err));
    }
    else {
      FOCUSLOG("Modbus - message for other slave - ignored, reqLen = %d", reqLen);
    }
    // done with this message
    if (aPollFlags & POLLHUP) {
      // connection terminated
      stopServing();
    }
    else {
      // connection still open, start reception of next message
      startMsgReception();
    }
    return true;
  }
  else if (aPollFlags & POLLHUP) {
    stopServing();
  }
  else if (aPollFlags & POLLERR) {
    // try to reconnect
    close(); // not just stop serving, really disconnect!
    startServing();
    return true;
  }
  return false;
}


void ModbusSlave::modbusTimeoutHandler()
{
  FOCUSLOG("modbus timeout - flushing received data");
  if (modbus) {
    if (modbus) modbus_flush(modbus);
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
  if (rawRequestHandler) {
    handled = rawRequestHandler(aSft, aOffset, aReq, aReqLen, aRsp, rspLen);
  }
  if (!handled) {
    // handle files
    if (aSft.function==MODBUS_FC_READ_FILE_RECORD || aSft.function==MODBUS_FC_WRITE_FILE_RECORD) {
      handled = handleFileAccess(aSft, aOffset, aReq, aReqLen, aRsp, rspLen);
    }
  }
  if (!handled) {
    // handle registers and bits
    if (registerModel) {
      modbus_mapping_ex_t map;
      map.mappings = registerModel;
      if (valueAccessHandler) {
        map.access_handler = modbus_access_handler;
        map.access_handler_user_ctx = this;
      }
      else {
        map.access_handler = NULL;
        map.access_handler_user_ctx = NULL;
      }
      rspLen = modbus_reg_mapping_handler(modbus, &aSft, aOffset, aReq, aReqLen, aRsp, &map);
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
  if (valueAccessHandler && registerModel) {
    for (int i=0; i<cnt; i++) {
      switch (access) {
        case read_bit :
          err = valueAccessHandler(addr+registerModel->start_bits+i, true, false, false);
          break;
        case write_bit :
          err = valueAccessHandler(addr+registerModel->start_bits+i, true, false, true);
          break;
        case read_input_bit :
          err = valueAccessHandler(addr+registerModel->start_input_bits+i, true, true, false);
          break;
        case read_reg :
          err = valueAccessHandler(addr+registerModel->start_registers+i, false, false, false);
          break;
        case write_reg :
          err = valueAccessHandler(addr+registerModel->start_registers+i, false, false, true);
          break;
        case read_input_reg :
          err = valueAccessHandler(addr+registerModel->start_input_registers+i, false, true, false);
          break;
      }
    }
  }
  if (Error::notOK(err)) {
    errStr = err->description();
    return errStr.c_str(); // return error text to be returned with
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
  registerModel = map;
  return ErrorPtr();
}


void ModbusSlave::setValueAccessHandler(ModbusValueAccessCB aValueAccessCB)
{
  valueAccessHandler = aValueAccessCB;
}


uint8_t* ModbusSlave::getBitAddress(int aAddress, bool aInput, int aBits)
{
  if (!registerModel) return NULL;
  aAddress -= (aInput ? registerModel->start_input_bits : registerModel->start_bits);
  if (aAddress<0 || aBits >= (aInput ? registerModel->nb_input_bits : registerModel->nb_bits)) return NULL;
  return aInput ? &(registerModel->tab_input_bits[aAddress]) : &(registerModel->tab_bits[aAddress]);
}


uint16_t* ModbusSlave::getRegisterAddress(int aAddress, bool aInput, int aRegs)
{
  if (!registerModel) return NULL;
  aAddress -= (aInput ? registerModel->start_input_registers : registerModel->start_registers);
  if (aAddress<0 || aRegs >= (aInput ? registerModel->nb_input_registers : registerModel->nb_registers)) return NULL;
  return aInput ? &(registerModel->tab_input_registers[aAddress]) : &(registerModel->tab_registers[aAddress]);
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
  fileHandlers.push_back(aFileHandler);
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
    for (FileHandlersList::iterator pos = fileHandlers.begin(); pos!=fileHandlers.end(); ++pos) {
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
      int rc = modbus_send_msg(modbus, aRsp, aRspLen);
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
      for (FileHandlersList::iterator pos = fileHandlers.begin(); pos!=fileHandlers.end(); ++pos) {
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
  fileNo(aFileNo),
  maxSegments(aMaxSegments),
  numFiles(aNumFiles),
  useP44Header(aP44Header),
  filePath(aFilePath),
  finalBasePath(aFinalBasePath),
  readOnly(aReadOnly),
  currentBaseFileNo(0),
  openFd(-1),
  validP44Header(false),
  singleRecordLength(0),
  neededSegments(1),
  recordsPerChunk(1),
  firstDataRecord(0),
  remoteMissingRecord(noneMissing),
  remoteCRC32(0),
  remoteFileSize(0),
  localFileSize(0),
  localCRC32(invalidCRC),
  nextExpectedDataRecord(0), // expect start at very beginning
  pendingFinalisation(false)
{
}


ModbusFileHandler::~ModbusFileHandler()
{

}


bool ModbusFileHandler::handlesFileNo(uint16_t aFileNo)
{
  return aFileNo>=fileNo && aFileNo<fileNo+maxSegments*numFiles;
}


ErrorPtr ModbusFileHandler::writeLocalFile(uint16_t aFileNo, uint16_t aRecordNo, const uint8_t *aDataP, size_t aDataLen)
{
  LOG(LOG_INFO, "writeFile: #%d, record=%d, bytes=%zu, starting with 0x%02X", aFileNo, aRecordNo, aDataLen, *aDataP);
  if (readOnly) {
    return Error::err<ModBusError>(EMBXILFUN, "read only file");
  }
  ErrorPtr err = openLocalFile(aFileNo, true);
  if (Error::notOK(err)) return err;
  uint32_t recordNo =
    ((aFileNo-currentBaseFileNo)<<16) +
    aRecordNo;
  // check for writing header
  if (useP44Header) {
    if (!validP44Header || recordNo<firstDataRecord) {
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
      if (!finalBasePath.empty()) {
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
      if (localFileSize>remoteFileSize) {
        FOCUSLOG("- local file is already bigger than p44header declares -> truncating from %u to %u", localFileSize, remoteFileSize);
        if (ftruncate(openFd, remoteFileSize)<0) {
          return Error::err<ModBusError>(errno)->withPrefix("truncating file");
        }
        localFileSize = remoteFileSize;
      }
      return ErrorPtr();
    }
    // not accessing header data
    recordNo -= firstDataRecord;
  }
  // now recordno is relative to the file DATA beginning (i.e., excluding header, if any)
  if (useP44Header && validP44Header && nextExpectedDataRecord==noneMissing && fileIntegrityOK()) {
    LOG(LOG_WARNING, "fileNo %d already completely written -> suppress writing", currentBaseFileNo);
    closeLocalFile();
    return ErrorPtr();
  }
  // - seek to position
  uint32_t filePos = recordNo*singleRecordLength*2;
  if (lseek(openFd, filePos , SEEK_SET)<0) {
    return Error::err<ModBusError>(errno)->withPrefix("seeking write position");
  }
  // - check for writing over actual file length
  if (useP44Header && filePos+aDataLen>remoteFileSize) {
    LOG(LOG_INFO, "last chunk of file: ignoring %lu excessive bytes in chunk", filePos+aDataLen-remoteFileSize);
    aDataLen = remoteFileSize-filePos; // only write as much as the actual file size allows, ignore rest of chunk
  }
  // - write date to file
  ssize_t by = write(openFd, aDataP, aDataLen);
  if (by<0) {
    return Error::err<ModBusError>(errno)->withPrefix("writing to local file");
  }
  else if (by!=aDataLen) {
    return Error::err<ModBusError>(EMBBADEXC, "could only write %zd/%zu bytes", by, aDataLen);
  }
  // File writing is successful
  // - update file size
  filePos += aDataLen;
  if (filePos>localFileSize) localFileSize = filePos;
  if (useP44Header) {
    // - update missing record state
    if (recordNo>=nextExpectedDataRecord) {
      // if there are some missing in between, track them
      while (nextExpectedDataRecord<recordNo) {
        missingDataRecords.push_back(nextExpectedDataRecord);
        LOG(LOG_INFO, "- missing DATA recordNo %u -> added to list (total missing=%zu)", nextExpectedDataRecord, missingDataRecords.size());
        nextExpectedDataRecord += recordAddrsPerChunk();
      }
      // update expected next record
      nextExpectedDataRecord = recordNo+(((uint32_t)aDataLen+2*singleRecordLength-1)/2/singleRecordLength);
    }
    else if (recordNo<nextExpectedDataRecord) {
      // is a re-write of an earlier block, remove it from our list if present
      for (RecordNoList::iterator pos = missingDataRecords.begin(); pos!=missingDataRecords.end(); ++pos) {
        if (*pos==recordNo) {
          missingDataRecords.erase(pos);
          LOG(LOG_INFO, "- successful retransmit of previously missing DATA recordNo %u -> removed from list (remaining missing=%zu)", recordNo, missingDataRecords.size());
          if (missingDataRecords.size()==0) {
            LOG(LOG_NOTICE, "- all missing blocks now retransmitted. File size=%u (expected=%u)", localFileSize, remoteFileSize);
          }
          break;
        }
      }
    }
    if (nextExpectedDataRecord*singleRecordLength*2>=remoteFileSize && missingDataRecords.empty()) {
      // file is complete
      nextExpectedDataRecord = noneMissing;
      pendingFinalisation = true;
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
  if (baseFileNo!=currentBaseFileNo) {
    // new file, need to re-open early
    err = openLocalFile(aFileNo, false);
    if (Error::notOK(err)) return err;
  }
  uint32_t recordNo =
    ((aFileNo-currentBaseFileNo)<<16) +
    aRecordNo;
  // check for reading header
  // Note: we want to avoid reading from opening the file if it still has valid P44header info,
  //   because reading the header after finalisation must return the finalized status
  //   of the written file (which might be a temp file)
  if (useP44Header) {
    if (!validP44Header) {
      openLocalFile(aFileNo, false);
      if (Error::notOK(err)) return err;
    }
    if (recordNo<firstDataRecord) {
      if (recordNo+aDataLen>numP44HeaderRecords()*2) {
        return Error::err<ModBusError>(EMBXILADD, "out of header record range 0..%d", numP44HeaderRecords()-1);
      }
      ModBusPDU buf;
      generateP44Header(buf, MODBUS_MAX_PDU_LENGTH);
      memcpy(aDataP, buf+recordNo*2, aDataLen);
      return ErrorPtr();
    }
    // not accessing header data
    recordNo -= firstDataRecord;
  }
  // now latest we need the file to be open
  openLocalFile(aFileNo, false);
  if (Error::notOK(err)) return err;
  // now recordno is relative to the file DATA beginning (i.e., excluding header, if any)
  // - seek to position
  uint32_t filePos = recordNo*singleRecordLength*2;
  if (filePos>=localFileSize) {
    return Error::err<ModBusError>(EMBXILADD, "cannot read past file end");
  }
  if (lseek(openFd, filePos , SEEK_SET)<0) {
    return Error::err<ModBusError>(errno)->withPrefix("seeking read position: ");
  }
  // - read
  ssize_t by = read(openFd, aDataP, aDataLen);
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
  if (maxSegments<2) return aFileNo; // no segmenting configured
  return fileNo + (aFileNo-fileNo)/maxSegments*maxSegments;
}


string ModbusFileHandler::filePathFor(uint16_t aFileNo, bool aTemp)
{
  string path;
  if (!finalBasePath.empty()) {
    if (aTemp) {
      path = Application::sharedApplication()->tempPath(filePath);
    }
    else {
      path = finalBasePath + filePath.c_str();
    }
  }
  else {
    path = filePath;
  }
  if (numFiles==1) return path;
  // path must contain a % specifier for rendering aFileNo
  return string_format(path.c_str(), aFileNo);
}


ErrorPtr ModbusFileHandler::openLocalFile(uint16_t aFileNo, bool aForLocalWrite)
{
  ErrorPtr err;
  uint16_t baseFileNo = baseFileNoFor(aFileNo);
  if (baseFileNo!=currentBaseFileNo) {
    // Note: switching files invalidates the header, just re-opening must NOT invalidate it!
    if (currentBaseFileNo!=0) {
      validP44Header = false;
    }
    closeLocalFile();
  }
  if (openFd<0) {
    string path = filePathFor(baseFileNo, aForLocalWrite); // writing occurs on temp version of the file (if any is set)
    openFd = open(path.c_str(), aForLocalWrite ? O_RDWR|O_CREAT : O_RDONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (openFd<0) {
      err = SysError::errNo("cannot open local file: ");
    }
    else {
      currentBaseFileNo = baseFileNo;
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
  if (openFd>=0) {
    close(openFd);
    openFd = -1;
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
  return maxRecordsPerRequest()/singleRecordLength;
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
  if (!useP44Header) return 0;
  if (!validP44Header || aMaxDataLen<p44HeaderSize) return -1;
  int i = 0;
  // magic ID word
  aDataP[i++] = p44HeaderMagic[0];
  aDataP[i++] = p44HeaderMagic[1];
  aDataP[i++] = p44HeaderMagic[2];
  aDataP[i++] = p44HeaderMagic[3];
  // file size
  aDataP[i++] = (localFileSize>>24) & 0xFF;
  aDataP[i++] = (localFileSize>>16) & 0xFF;
  aDataP[i++] = (localFileSize>>8) & 0xFF;
  aDataP[i++] = (localFileSize) & 0xFF;
  // CRC
  aDataP[i++] = (localCRC32>>24) & 0xFF;
  aDataP[i++] = (localCRC32>>16) & 0xFF;
  aDataP[i++] = (localCRC32>>8) & 0xFF;
  aDataP[i++] = (localCRC32) & 0xFF;
  // number of segments needed (consecutive file numbers for the same file)
  aDataP[i++] = (uint8_t)neededSegments;
  // number of uint16_t quantities addressed by a record number
  aDataP[i++] = (uint8_t)singleRecordLength;
  // record number of first actual file data record
  aDataP[i++] = (firstDataRecord>>8) & 0xFF;
  aDataP[i++] = (firstDataRecord) & 0xFF;
  // number of first missing record in multicast write, or noneMissing if all complete
  uint32_t localMissingRecord = noneMissing;
  if (!missingDataRecords.empty()) {
    // report first missing record
    localMissingRecord = missingDataRecords.front();
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
  if (useP44Header) {
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
    remoteFileSize =
      (aDataP[aPos]<<24) +
      (aDataP[aPos+1]<<16) +
      (aDataP[aPos+2]<<8) +
      (aDataP[aPos+3]);
    aPos += 4;
    // expected CRC
    remoteCRC32 =
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
      neededSegments = nseg;
      singleRecordLength = srl;
      firstDataRecord = fdr;
      // derive recordsPerChunk
      recordsPerChunk = recordAddrsPerChunk()*singleRecordLength; // how many records (2-byte data items) are in a chunk
      // reset missing records tracking
      missingDataRecords.clear();
      nextExpectedDataRecord = 0; // no data received yet (header not counted in DATA record numbers
      localCRC32 = invalidCRC;
      pendingFinalisation = false;
      validP44Header = true;
    }
    else {
      if (nseg!=neededSegments || srl!=singleRecordLength || firstDataRecord!=fdr) {
        return Error::err<ModBusError>(ModBusError::P44HeaderError,
          "p44 header file layout mismatch: segments/recordlen/firstrecord expected=%d/%d/%d, found=%d/%d/%d",
          neededSegments, singleRecordLength, firstDataRecord,
          nseg, srl, fdr
        );
      }
    }
    // number of next remotely detected missing record in multicast write. 0=none
    remoteMissingRecord =
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
      fileNo, filePathFor(fileNo, true).c_str(),
      remoteFileSize, remoteCRC32,
      neededSegments, maxSegments,
      firstDataRecord, singleRecordLength, recordsPerChunk, maxRecordsPerRequest(),
      remoteMissingRecord, remoteMissingRecord
    );
  }
  return ErrorPtr();
}


ErrorPtr ModbusFileHandler::updateLocalCRC()
{
  localCRC32 = invalidCRC;
  if (openFd<0) return TextError::err("finalize: file not open");
  // also obtain the CRC
  lseek(openFd, 0, SEEK_SET);
  Crc32 crc;
  uint32_t bytes = localFileSize;
  size_t crcBufSz = 8*1024;
  uint8_t crcbuf[crcBufSz];
  while (bytes>0) {
    int rc = (int)read(openFd, crcbuf, bytes>crcBufSz ? crcBufSz : bytes);
    if (rc<0) {
      return SysError::errNo("cannot read file data for CRC: ");
    }
    crc.addBytes(rc, crcbuf);
    bytes -= rc;
  }
  localCRC32 = crc.getCRC();
  return ErrorPtr();
}


ErrorPtr ModbusFileHandler::finalize()
{
  if (pendingFinalisation && useP44Header) {
    updateLocalCRC();
    pendingFinalisation = false;
    LOG(LOG_NOTICE,
        "Successful p44header-controlled file transfer finalisation:\n"
        "- path='%s'\n"
        "- finalpath='%s'\n"
        "- local: size=%u, CRC=0x%08x\n"
        "- remote: size=%u, CRC=0x%08x",
        filePathFor(currentBaseFileNo, true).c_str(),
        filePathFor(currentBaseFileNo, false).c_str(),
        localFileSize, localCRC32,
        remoteFileSize, remoteCRC32
    );
    // make sure file is properly closed before executing callback
    closeLocalFile();
    if (fileWriteCompleteCB) {
      fileWriteCompleteCB(
        currentBaseFileNo, // the fileNo accessed
        filePathFor(currentBaseFileNo, false), // the final path
        finalBasePath.empty() ? "" : filePathFor(currentBaseFileNo, true) // the temp path, if any
      );
    }
  }
  else {
    closeLocalFile();
  }
  return (!useP44Header || fileIntegrityOK()) ? ErrorPtr() : Error::err<ModBusError>(EMBBADCRC, "File CRC mismatch in p44header");
}


ErrorPtr ModbusFileHandler::readLocalFileInfo(bool aInitialize)
{
  if (openFd<0) return TextError::err("readLocalFileInfo: file not open");
  if (aInitialize) validP44Header = false; // forget current header info
  struct stat s;
  fstat(openFd, &s);
  localFileSize = (uint32_t)s.st_size;
  if (aInitialize) {
    updateLocalCRC();
    // most compatible mode, ok for small files
    singleRecordLength = 1;
    neededSegments = 1;
    firstDataRecord = useP44Header ? p44HeaderRecords : 0;
    // when starting from a local file, we just set our size
    recordsPerChunk = maxRecordsPerRequest();
    // calculate singleRecordLength
    if (localFileSize>(0x10000-firstDataRecord)*2) {
      // file is too big for single register record.
      if (!useP44Header) {
        return Error::err<ModBusError>(EMBXILVAL, "file to big to send w/o p44hdr");
      }
      singleRecordLength = recordsPerChunk; // fits into a PDU along with overhead
      // now calculate the needed number of segments
      neededSegments = (localFileSize/2/singleRecordLength+firstDataRecord)/0x10000+1;
      if (maxSegments!=0 && neededSegments>maxSegments) {
        return Error::err<ModBusError>(EMBXILVAL, "file exceeds max allowed segments");
      }
    }
    validP44Header = true; // is valid now
  }
  return ErrorPtr();
}


bool ModbusFileHandler::isEOFforChunk(uint32_t aChunkIndex, bool aRemotely)
{
  if (currentBaseFileNo==0) return true; // no file is current -> EOF
  if (!validP44Header) return false; // we don't know where the EOF is -> assume NOT EOF
  return (aChunkIndex*recordsPerChunk*2) >= (aRemotely ? remoteFileSize : localFileSize);
}


void ModbusFileHandler::addressForMaxChunk(uint32_t aChunkIndex, uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords)
{
  if (currentBaseFileNo==0) return; // no file is current
  // now calculate record and file no out of chunk no
  int recordAddrsPerChunk = recordsPerChunk/singleRecordLength; // how many record *addresses* are in a chunk
  uint32_t rawRecordNo = firstDataRecord + aChunkIndex*recordAddrsPerChunk;
  uint16_t segmentOffset = rawRecordNo>>16; // recordno only has 16 bits
  // assign results
  aFileNo = currentBaseFileNo+segmentOffset;
  aRecordNo = rawRecordNo & 0xFFFF;
  aNumRecords = recordsPerChunk;
}


bool ModbusFileHandler::addrForNextRetransmit(uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords)
{
  if (!validP44Header || remoteMissingRecord==noneMissing) return false;
  uint32_t rec = remoteMissingRecord+firstDataRecord;
  uint16_t seg = (rec>>16) & 0xFFFF;
  if (seg>=neededSegments) return false;
  aFileNo = fileNo + seg;
  aRecordNo = rec & 0xFFFF;
  aNumRecords = recordsPerChunk;
  return true;
}


bool ModbusFileHandler::fileIntegrityOK()
{
  return
    validP44Header &&
    localFileSize==remoteFileSize &&
    localCRC32==remoteCRC32;
}



#endif // ENABLE_MODBUS




