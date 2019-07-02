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





// MARK: - ModbusMaster


ModbusMaster::ModbusMaster()
{
}


ModbusMaster::~ModbusMaster()
{
}


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
          LOG(LOG_ERR, "Modbus response sending error: %s", Error::text(err).c_str());
        }
      }
    }
    else if (reqLen<0) {
      ErrorPtr err = Error::err<ModBusError>(errno);
      if (errno!=ECONNRESET) LOG(LOG_ERR, "Modbus request receiving error: %s", Error::text(err).c_str());
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
    if (!modbusSlave) return -1;
    return modbusSlave->handleRawRequest(sft, offset, req, req_length, rsp);
  }

  const char* modbus_access_handler(modbus_t* ctx, modbus_mapping_t* mappings, modbus_data_access_t access, int addr, int cnt, modbus_data_t dataP, void *user_ctx)
  {
    ModbusSlave *modbusSlave = (ModbusSlave*)user_ctx;
    if (!modbusSlave) return "internal error";
    return modbusSlave->accessHandler(access, addr, cnt, dataP);
  }

}


int ModbusSlave::handleRawRequest(sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp)
{
  if (sft) {
    LOG(LOG_NOTICE, "Received request with FC=%d, for slaveid=%d, transactionId=%d", sft->function, sft->slave, sft->t_id);
    int rspLen = 0;
    if (rawRequestHandler) {
      if (rawRequestHandler(sft, offset, req, req_length, rsp, rspLen)) return rspLen; // handled
    }
    // TODO: implement file handling
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
      return modbus_reg_mapping_handler(modbus, sft, offset, req, req_length, rsp, &map);
    }
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





#endif // ENABLE_MODBUS




