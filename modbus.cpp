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

#include "serialcomm.hpp" // for parameter parsing
#include "crc32.hpp"

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
  const char *aTransmitEnableSpec, MLMicroSeconds aTxDisableDelay
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
        return Error::err<ModBusError>(errno, "cannot listen");
      }
      // - install connection watcher
      MainLoop::currentMainLoop().registerPollHandler(serverSocket, POLLIN, boost::bind(&ModbusConnection::connectionAcceptHandler, this, _1, _2));
      connected = true;
    }
    else {
      // act as TCP client or just serial connection
      if (modbus_connect(modbus)<0) {
        err = ModBusError::err<ModBusError>(errno);
      }
      else {
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
    "Modbus exception %d: %s\n", aExceptionCode-MODBUS_ENOBASE, nonNullCStr(aErrorText)
  );
}


void ModbusConnection::buildExceptionResponse(sft_t &aSft, ErrorPtr aError, ModBusPDU& aRsp, int& aRspLen)
{
  if (!Error::isOK(aError) && aError->isDomain(ModBusError::domain())) {
    buildExceptionResponse(aSft, (int)aError->getErrorCode(), aError->text(), aRsp, aRspLen);
  }
  else {
    buildExceptionResponse(aSft, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE, "undefined error", aRsp, aRspLen);
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
    uint8_t slaveid[MODBUS_MAX_PDU_LENGTH];
    int bytes = modbus_report_slave_id(modbus, MODBUS_MAX_PDU_LENGTH, slaveid);
    if (bytes<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
    else {
      aId.assign((const char*)slaveid+2, bytes-2);
      aRunIndicator = true; // FIXME: actually get the value
    }
  }
  if (wasConnected) close();
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
  }
  if (wasConnected) close();
  return err;
}





// MARK: - ModbusMaster register and bit access


ErrorPtr ModbusMaster::readRegister(int aRegAddr, uint16_t &aRegData)
{
  return readRegisters(aRegAddr, 1, &aRegData);
}


ErrorPtr ModbusMaster::readFloatRegister(int aRegAddr, double &aFloatData)
{
  uint16_t floatRegs[2];
  ErrorPtr err = readRegisters(aRegAddr, 2, floatRegs);
  if (Error::isOK(err)) {
    aFloatData = getAsDouble(floatRegs);
  }
  return err;
}



ErrorPtr ModbusMaster::readRegisters(int aRegAddr, int aNumRegs, uint16_t *aRegsP)
{
  ErrorPtr err;
  bool wasConnected = isConnected();
  if (!wasConnected) err = connect();
  if (Error::isOK(err)) {
    if (modbus_read_registers(modbus, aRegAddr, aNumRegs, aRegsP)<0) {
      err = ModBusError::err<ModBusError>(errno);
    }
  }
  if (wasConnected) close();
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
  if (wasConnected) close();
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
    int lenIdx = reqLen;
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
      req[lenIdx] = reqLen;
      // send it
      int rc = modbus_send_msg(modbus, req, reqLen);
      if (rc>0) {
        ModBusPDU rsp;
        int rspLen = modbus_receive_msg(modbus, rsp, MSG_CONFIRMATION);
        if (rspLen>0) {
          rc = modbus_pre_check_confirmation(modbus, req, rsp, rspLen);
          if (rc>0) {
            if (rsp[rc++]!=MODBUS_FC_WRITE_FILE_RECORD) rc = EMBBADEXC;
            else {
              if (rsp[rc]+rc != rspLen) rc = EMBBADDATA; // wrong length
              else {
                // everything following, including length must be equal to request
                if (memcmp(req+lenIdx, rsp+rc, (size_t)req[lenIdx])!=0) {
                  rc = EMBBADDATA;
                }
              }
            }
          }
        }
      }
      if (rc<0) {
        err = Error::err<ModBusError>(errno, "sending write file request");
      }
    }
  }
  if (wasConnected) close();
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
    int lenIdx = reqLen;
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
      req[lenIdx] = reqLen;
      // send the read request
      int rc = modbus_send_msg(modbus, req, reqLen);
      if (rc>0) {
        ModBusPDU rsp;
        int rspLen = modbus_receive_msg(modbus, rsp, MSG_CONFIRMATION);
        if (rspLen>0) {
          rc = modbus_pre_check_confirmation(modbus, req, rsp, rspLen);
          if (rc>0) {
            if (rsp[rc++]!=MODBUS_FC_READ_FILE_RECORD) rc = EMBBADEXC;
            else {
              // check length
              if (bytes!=rsp[rc++]) rc = EMBBADDATA; // wrong length
              else {
                // get the data
                memcpy(aDataP, rsp+rc, bytes);
              }
            }
          }
        }
      }
      if (rc<0) {
        err = Error::err<ModBusError>(errno, "sending write file request");
      }
    }
  }
  if (wasConnected) close();
  return err;
}



// MARK: - ModbusMaster file transfers

#define WRITE_RECORD_RETRIES 3

ErrorPtr ModbusMaster::sendFile(const string& aLocalFilePath, int aFileNo, bool aUseP44Header)
{
  ErrorPtr err;

  // create a file handler
  ModbusFileHandlerPtr handler = ModbusFileHandlerPtr(new ModbusFileHandler(aFileNo, 0, 1, aUseP44Header, aLocalFilePath));
  err = handler->openLocalFile(aFileNo, true);
  if (Error::isOK(err)) {
    err = handler->readLocalFileInfo();
    if (Error::isOK(err)) {
      uint8_t p44hdr[32];
      int hdrSz = handler->generateP44Header(p44hdr, 32);
      if (hdrSz<0) {
        err = Error::err<ModBusError>(EMBBADEXC, "cannot generate header");
      }
      else if (hdrSz>0) {
        int retries = WRITE_RECORD_RETRIES;
        while (true) {
          // we actually have a p44 header, send it
          err = writeFileRecords(aFileNo, 0, (hdrSz+1)/2, p44hdr);
          if (Error::isOK(err)) break;
          retries--;
          if (retries<=0) return err;
        };
      }
      // header sent or none required, now send data
      ModBusPDU buf;
      uint32_t chunkIndex = 0;
      while (true) {
        uint16_t fileNo, recordNo, recordLen;
        if (!handler->addressForMaxChunk(chunkIndex, fileNo, recordNo, recordLen)) break; // done
        // get data from local file
        err = handler->readFile(fileNo, recordNo, buf, recordLen*2);
        if (Error::isOK(err)) return err;
        chunkIndex++;
        // write to the remote
        int retries = WRITE_RECORD_RETRIES;
        while (true) {
          err = writeFileRecords(fileNo, recordNo, recordLen, buf);
          if (Error::isOK(err)) break;
          retries--;
          if (retries<=0) return err;
        }
      }
    }
  }
  return err;
}


ErrorPtr ModbusMaster::receiveFile(const string& aLocalFilePath, int aFileNo, bool aUseP44Header)
{
  // TODO: implement
  return Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_FUNCTION, "%%% not yet implemented");
}



ErrorPtr ModbusMaster::broadcastFile(const SlaveAddrList& aSlaveAddrList, const string& aLocalFilePath, int aFileNo, bool aUseP44Header)
{
  ErrorPtr err;
  if (!aUseP44Header) {
    // simple one-by-one transfer, not real broadcast
    for (SlaveAddrList::const_iterator pos = aSlaveAddrList.begin(); pos!=aSlaveAddrList.end(); ++pos) {
      setSlaveAddress(*pos);
      ErrorPtr fileErr = sendFile(aLocalFilePath, aFileNo, false);
      if (!Error::isOK(fileErr)) {
        LOG(LOG_ERR, "Error sending file '%s' to fileNo %d in slave %d: %s", aLocalFilePath.c_str(), aFileNo, *pos, fileErr->text());
        err = fileErr; // return most recent error
      }
    }
  }
  else {
    // with p44header, we can do real broadcast of the data
  }

  // TODO: remove
  return Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_FUNCTION, "%%% not yet implemented");
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


void ModbusSlave::startMsgReception()
{
  cancelMsgReception(); // stop previous, if any
  modbusRcv = modbus_receive_new(modbus, modbusReq);
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
      MLMicroSeconds timeout = MainLoop::timeValToMainLoopTime(modbus_get_select_timeout(modbusRcv));
      if (timeout!=Never) rcvTimeoutTicket.executeOnce(boost::bind(&ModbusSlave::modbusTimeoutHandler, this), timeout);
      return true;
    }
    if (reqLen>0) {
      // got request
      LOG(LOG_NOTICE, "Modbus received request, %d bytes", reqLen);
      // - process it
      int rspLen = modbus_process_request(modbus, modbusReq, reqLen, modbusRsp, modbus_slave_function_handler, this);
      /* Send response, if any */
      if (rspLen > 0) {
        int rc = modbus_send_msg(modbus, modbusRsp, rspLen);
        if (rc<0) {
          ErrorPtr err = Error::err<ModBusError>(errno);
          LOG(LOG_ERR, "Modbus response sending error: %s", Error::text(err));
        }
      }
    }
    else if (reqLen<0) {
      ErrorPtr err = Error::err<ModBusError>(errno);
      if (errno!=ECONNRESET) LOG(LOG_ERR, "Modbus request receiving error: %s", Error::text(err));
    }
    else {
      LOG(LOG_DEBUG, "Modbus - message for other slave - ignored, reqLen = %d", reqLen);
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
  LOG(LOG_DEBUG, "timeout");
  if (modbus) {
    MLMicroSeconds timeout = MainLoop::timeValToMainLoopTime(modbus_get_select_timeout(modbusRcv));
    if (timeout!=Never) rcvTimeoutTicket.executeOnce(boost::bind(&ModbusSlave::modbusRecoveryHandler, this), timeout);
  }
}


void ModbusSlave::modbusRecoveryHandler()
{
  LOG(LOG_DEBUG, "recovery timeout");
  if (modbus) modbus_flush(modbus);
  startMsgReception();
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
  LOG(LOG_NOTICE, "Received request with FC=%d, for slaveid=%d, transactionId=%d", aSft.function, aSft.slave, aSft.t_id);
  int rspLen = 0;
  // allow custom request handling to override anything
  if (rawRequestHandler) {
    if (rawRequestHandler(aSft, aOffset, aReq, aReqLen, aRsp, rspLen)) return rspLen; // handled
  }
  // handle files
  if (aSft.function==MODBUS_FC_READ_FILE_RECORD || aSft.function==MODBUS_FC_WRITE_FILE_RECORD) {
    if (handleFileAccess(aSft, aOffset, aReq, aReqLen, aRsp, rspLen)) return rspLen; // handled
  }
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
    return modbus_reg_mapping_handler(modbus, &aSft, aOffset, aReq, aReqLen, aRsp, &map);
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
  if (!Error::isOK(err)) {
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


bool ModbusSlave::handleFileAccess(sft_t &aSft, int aOffset, const ModBusPDU& aReq, int aReqLen, ModBusPDU& aRsp, int &aRspLen)
{
  ErrorPtr err;
  // req[aOffset] is the function code = first byte of actual request
  int e = aOffset+2+aReq[aOffset+1]; // first byte outside
  int i = aOffset+2; // first subrecord
  // - prepare response base
  buildResponseBase(aSft, aRsp, aRspLen);
  aRsp[aRspLen++] = aSft.function;
  int lenIdx = aRspLen++; // actual length will be filled when all subrecords are processed
  // - prepare response header (before subrecords)
  while (i<e) {
    // read the subrecord
    int subRecordIdx = i;
    uint8_t reftype = aReq[i]; i += 1;
    if (reftype!=0x06) {
      err = Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, "Wrong subrequest reference type, expected 0x06, found 0x%02x", reftype);
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
      err = Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "Unknown file number %d", fileno);
      break;
    }
    else {
      // process request with handler
      ErrorPtr err;
      uint16_t recordno = (aReq[i]<<8) + aReq[i+1]; i += 2;
      uint16_t recordlen = (aReq[i]<<8) + aReq[i+1]; i += 2;
      uint16_t dataBytes = recordlen*2;
      if (aSft.function==MODBUS_FC_WRITE_FILE_RECORD) {
        // Write to file
        const uint8_t* writeDataP = aReq+i;
        i += dataBytes;
        err = handler->writeFile(fileno, recordno, writeDataP, dataBytes);
        if (!Error::isOK(err)) break; // error
        // echoe the subrequest
        appendToMessage(aRsp+subRecordIdx, i-subRecordIdx, aRsp, aRspLen);
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
          err = Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, "Read file response would exceed PDU size");
          break;
        }
        err = handler->readFile(fileno, recordno, readDataP, dataBytes);
        if (!Error::isOK(err)) break; // error
      }
      else {
        return false; // unknown function code
      }
    }
  } // all subrequests
  if (Error::isOK(err)) {
    // complete response
    aRsp[lenIdx] = aRspLen-lenIdx; // set length of data
  }
  else {
    // failed
    buildExceptionResponse(aSft, err, aRsp, aRspLen);
  }
  return true; // handled
}


// MARK: - ModbusFileHandler

ModbusFileHandler::ModbusFileHandler(int aFileNo, int aMaxSegments, int aNumFiles, bool aP44Header, const string aFilePath) :
  fileNo(aFileNo),
  maxSegments(aMaxSegments),
  numFiles(aNumFiles),
  useP44Header(aP44Header),
  filePath(aFilePath),
  openBaseFileNo(0),
  openFd(-1),
  validP44Header(false),
  nextFailedRecord(false)
{
}


ModbusFileHandler::~ModbusFileHandler()
{

}


bool ModbusFileHandler::handlesFileNo(uint16_t aFileNo)
{
  return aFileNo>=fileNo && aFileNo<fileNo+maxSegments*numFiles;
}



ErrorPtr ModbusFileHandler::writeFile(uint16_t aFileNo, uint16_t aRecordNo, const uint8_t *aDataP, size_t aDataLen)
{
  // TODO: actually implememnt - dummy only for now
  LOG(LOG_DEBUG, "writeFile: #%d, record=%d, bytes=%zu, starting with 0x%02X", aFileNo, aRecordNo, aDataLen, *aDataP);
  return ErrorPtr();
}


ErrorPtr ModbusFileHandler::readFile(uint16_t aFileNo, uint16_t aRecordNo, uint8_t *aDataP, size_t aDataLen)
{
  // TODO: actually implememnt - dummy only for now
  LOG(LOG_DEBUG, "readFile: #%d, record=%d, bytes=%zu, returning all 0x42", aFileNo, aRecordNo, aDataLen);
  memset(aDataP, 0x42, aDataLen);
  return ErrorPtr();
}


uint16_t ModbusFileHandler::baseFileNoFor(uint16_t aFileNo)
{
  return fileNo + (aFileNo-fileNo)/maxSegments*maxSegments;
}


string ModbusFileHandler::filePathFor(uint16_t aFileNo)
{
  if (numFiles==1) return filePath;
  return string_format("%s%05u", filePath.c_str(), aFileNo);
}


ErrorPtr ModbusFileHandler::openLocalFile(uint16_t aFileNo, bool aForWrite)
{
  ErrorPtr err;
  uint16_t baseFileNo = baseFileNoFor(aFileNo);
  string path = filePathFor(baseFileNo);
  if (baseFileNo!=openBaseFileNo) {
    closeLocalFile();
    openFd = open(path.c_str(), aForWrite ? O_RDWR : O_RDONLY);
    if (openFd<0) {
      err = SysError::errNo("cannot open local file: ");
    }
    else {
      openBaseFileNo = baseFileNo;
      err = readLocalFileInfo();
    }
  }
  return err;
}


void ModbusFileHandler::closeLocalFile()
{
  if (openFd>=0) {
    close(openFd);
    openFd = -1;
    validP44Header = false;
  }
}


uint16_t ModbusFileHandler::maxRecordsPerRequest()
{
  // The PDU max size is MODBUS_MAX_PDU_LENGTH (253).
  // A nice payload size below that is 200 = 100 records
  return 100;
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
//   8    16     2  uint16_t  number of first failed record in multicast write. 0=none (because record 0 is always in header)
//   9    18                  HEADER SIZE
static const uint8_t p44HeaderMagic[4] = { 0x42, 0x93, 0x25, 0x44 };
static const uint16_t p44HeaderSize = 18;
static const uint16_t p44HeaderRecords = (p44HeaderSize+1)/2;



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
  aDataP[i++] = (localSize>>24) & 0xFF;
  aDataP[i++] = (localSize>>16) & 0xFF;
  aDataP[i++] = (localSize>>8) & 0xFF;
  aDataP[i++] = (localSize) & 0xFF;
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
  // number of next failed record in multicast write. 0=none
  aDataP[i++] = (nextFailedRecord>>8) & 0xFF;
  aDataP[i++] = (nextFailedRecord) & 0xFF;
  // done
  return i;
}


ErrorPtr ModbusFileHandler::parseP44Header(const uint8_t* aDataP, int& aPos, int aDataLen)
{
  if (useP44Header) {
    // check magic
    if (
      aPos+p44HeaderSize>aDataLen ||
      aDataP[aPos]!=p44HeaderMagic[0] ||
      aDataP[aPos+1]!=p44HeaderMagic[1] ||
      aDataP[aPos+2]!=p44HeaderMagic[2] ||
      aDataP[aPos+3]!=p44HeaderMagic[3]
    ) {
      return Error::err<ModBusError>(EMBBADDATA, "bad p44 header");
    }
    aPos += 4;
    // expected file size
    expectedSize =
      (aDataP[aPos]<<24) +
      (aDataP[aPos+1]<<16) +
      (aDataP[aPos+2]<<8) +
      (aDataP[aPos+3]);
    aPos += 4;
    // expected CRC
    expectedCRC32 =
      (aDataP[aPos]<<24) +
      (aDataP[aPos+1]<<16) +
      (aDataP[aPos+2]<<8) +
      (aDataP[aPos+3]);
    aPos += 4;
    // number of segments that will/should be used for the transfer
    neededSegments = aDataP[aPos++];
    // number of uint16_t quantities addressed by a record number
    singleRecordLength = aDataP[aPos++];
    // record number of first actual file data record
    firstDataRecord =
      (aDataP[aPos]<<8) +
      (aDataP[aPos+1]);
    aPos += 2;
    // number of next failed record in multicast write. 0=none
    nextFailedRecord =
      (aDataP[aPos]<<8) +
      (aDataP[aPos+1]);
    aPos += 2;
  }
  return ErrorPtr();
}




ErrorPtr ModbusFileHandler::readLocalFileInfo()
{
  if (openFd<0) return TextError::err("readLocalFileInfo: file not open");
  validP44Header = false; // forget current header info
  struct stat s;
  fstat(openFd, &s);
  localSize = (uint32_t)s.st_size;
  expectedSize = localSize; // no separate expectation, the file is local
  localCRC32 = 0;
  if (useP44Header) {
    // also obtain the CRC
    lseek(openFd, 0, SEEK_SET);
    Crc32 crc;
    uint32_t bytes = localSize;
    size_t crcBufSz = 8*1024;
    uint8_t crcbuf[crcBufSz];
    while (bytes>0) {
      int rc = (int)read(openFd, crcbuf, bytes>crcBufSz ? crcBufSz : bytes);
      if (rc<0) {
        return SysError::errNo("cannot read file data for CRC: ");
        break;
      }
      crc.addBytes(rc, crcbuf);
      bytes -= rc;
    }
    localCRC32 = crc.getCRC();
  }
  // most compatible mode, ok for small files
  singleRecordLength = 1;
  neededSegments = 1;
  firstDataRecord = useP44Header ? p44HeaderRecords : 0;
  // calculate singleRecordLength
  if (localSize>(0x10000-firstDataRecord)*2) {
    // file is too big for single register record.
    if (!useP44Header) {
      return Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "file to big to send w/o p44hdr");
    }
    singleRecordLength = maxRecordsPerRequest(); // fits into a PDU along with overhead
    // now calculate the needed number of segments
    neededSegments = (localSize/2/singleRecordLength+firstDataRecord)/0x10000+1;
    if (maxSegments!=0 && neededSegments>maxSegments) {
      return Error::err<ModBusError>(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, "file exceeds max allowed segments");
    }
  }
  validP44Header = true; // is valid now
  return ErrorPtr();
}


bool ModbusFileHandler::addressForMaxChunk(uint32_t aChunkIndex, uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords)
{
  if (openBaseFileNo==0) return false; // no file is open
  int recordAddrsPerChunk = maxRecordsPerRequest()/singleRecordLength; // how many record *addresses* are in a chunk
  int recordsPerChunk = recordAddrsPerChunk*singleRecordLength; // how many records (2-byte data items) are in a
  // check for EOF
  if (expectedSize>0 && aChunkIndex*recordsPerChunk>=expectedSize) return false;
  // now calculate record and file no out of chunk no
  uint32_t rawRecordNo = firstDataRecord + aChunkIndex*recordAddrsPerChunk;
  uint16_t segmentOffset = rawRecordNo>>16; // recordno only has 16 bits
  // assign results
  aFileNo = openBaseFileNo+segmentOffset;
  aRecordNo = rawRecordNo & 0xFFFF;
  aNumRecords = recordsPerChunk;
  return true;
}



#endif // ENABLE_MODBUS




