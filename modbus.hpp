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

#ifndef __p44utils__modbus__
#define __p44utils__modbus__

#ifndef ENABLE_MODBUS
  // We assume that including this file in a build usually means that modbus support is actually needed.
  // Still, ENABLE_MODBUS can be set to 0 to create build variants w/o removing the file from the project/makefile
  #define ENABLE_MODBUS 1
#endif

#if ENABLE_MODBUS

#include "p44utils_common.hpp"
#include "digitalio.hpp"

#include <stdio.h>

// - modbus
#if EMBEDDED_LIBMODBUS
  // use p44utils internal libmodbus (statically linked)
  #include "modbus.h"
#else
  // target with libmodbus installed
  #include <modbus/modbus.h>
#endif

using namespace std;

namespace p44 {


  class ModBusError : public Error
  {
  public:
    enum {
      // adding to internal libmodbus errors + 100
      InvalidConnParams = EMBXGTAR+100, ///< invalid connection parameters
      NoContext, ///< no valid modbus context
    };
    static const char *domain() { return "Modbus"; }
    virtual const char *getErrorDomain() const { return ModBusError::domain(); };
    ModBusError(ErrorCode aError) : Error(aError>0 ? aError-MODBUS_ENOBASE : aError, modbus_strerror((int)aError)) {
      prefixMessage("Modbus: ");
    };
  };


  extern "C" {
    void setRts(modbus_t* ctx, int on, void* cbctx);
  }


  class ModbusConnection : public P44Obj
  {
    friend void setRts(modbus_t *ctx, int on, void* cbctx);

  public:

    typedef enum {
      float_abcd,
      float_dcba,
      float_badc,
      float_cdab
    } FloatMode;

  protected:

    modbus_t *modbus; ///< the connection context (modbus RTU or TCP context)
    bool isTcp; ///< set if the backend is TCP
    bool doAcceptConnections; ///< for TCP, if set, connect() will start listening rather than connecting to a server
    int serverSocket; ///< socket where we are listening

    int slaveAddress; ///< current slave address
    bool connected; ///< set if connection is open
    FloatMode floatMode; ///< current mode for setting/getting float values

  public:

    ModbusConnection();
    virtual ~ModbusConnection();

    /// Specify the Modbus connection parameters as single string
    /// @param aConnectionSpec "/dev[:commParams]" or "hostname[:port]"
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    /// @param aDefaultCommParams default communication parameters (in case spec does not contain :commParams)
    /// @param aTransmitEnableSpec optional specification of a DigitalIo used to enable/disable the RS485 transmit driver.
    ///    If set to NULL or "RTS", the RTS line enables the RS485 drivers.
    ///    If set to "RS232", the connection is a plain two-point serial connection
    /// @param aTxDisableDelay if>0, time delay in uS before disabling Tx driver after sending
    /// @return error in case the connection context cannot be created from these parameters
    /// @note commParams syntax is: [baud rate][,[bits][,[parity][,[stopbits][,[H]]]]]
    ///   - parity can be O, E or N
    ///   - H means hardware handshake enabled
    ErrorPtr setConnectionSpecification(
      const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
      const char *aTransmitEnableSpec = NULL, MLMicroSeconds aTxDisableDelay = Never
    );

    /// set the slave address (when RTU endpoints are involved)
    /// @param aSlaveAddress the slave address
    /// @note in master mode, this specifies the slave to connect to (or all when set to MODBUS_BROADCAST_ADDRESS).
    ///   In slave mode, this is this instance's slave address
    void setSlaveAddress(int aSlaveAddress);

    /// @return the currently set slave address
    int getSlaveAddress() { return slaveAddress; };

    /// enable accepting connections (TCP only)
    /// @param aAccept true if TCP servere
    void acceptConnections(bool aAccept) { doAcceptConnections = aAccept; };

    /// open the connection
    /// @return error, if any
    ErrorPtr connect();

    /// close the modbus connection
    virtual void close();

    /// flush the connection (forget bytes already received from transport (but do not flush socket/filedesc itself)
    /// @return number of bytes flushed (or -1 in case of error)
    int flush();

    /// @return true if connection is open
    bool isConnected() { return connected; };

    /// enable debug messages to stderr
    /// @param aDebugEnabled set true to enable debug messages
    void setDebug(bool aDebugEnabled);


    /// set float mode
    /// @param aFloatMode the new float mode
    void setFloatMode(FloatMode aFloatMode) { floatMode = aFloatMode; };

    /// convert two register's contents to double
    /// @param aTwoRegs pointer to first of two consecutive registers containing a modbus float value
    /// @return the floating point value
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    double getAsDouble(const uint16_t *aTwoRegs);

    /// convert double into two registers
    /// @param aTwoRegs pointer to first of two consecutive registers which will receive a modbus float value
    /// @param aDouble the new floating point value
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    void setAsDouble(uint16_t *aTwoRegs, double aDouble);


  protected:

    /// is called when the "modbus" member variable is ready with a newly set-up context
    virtual void mbContextReady();

    /// start receiving messages as server/slave if possible
    virtual void startServing() { /* NOP in base class */ };

  private:

    void clearModbusContext();
    bool connectionAcceptHandler(int aFd, int aPollFlags);


  public:
    // stuff that needs to be public because friend declaration does not work in gcc (does in clang)
    DigitalIoPtr modbusTxEnable; ///< if set, this I/O is used to enable sending

  };



  class ModbusMaster : public ModbusConnection
  {
    typedef ModbusConnection inherited;

  public:

    ModbusMaster();
    virtual ~ModbusMaster();

    /// read single register
    /// @param aRegAddr the register address
    /// @param aRegData will receive the register's data
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr readRegister(int aRegAddr, uint16_t &aRegData);

    /// read float register pair
    /// @param aRegAddr address of the first of two registers containing a float value
    /// @param aFloatData will receive the float data
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    ErrorPtr readFloatRegister(int aRegAddr, double &aFloatData);

    /// read multiple registers
    /// @param aRegAddr the register address of the first register
    /// @param aNumRegs how many consecutive registers to read
    /// @param aRegsP pointer to where to store the register data (must have room for aNumRegs registers)
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr readRegisters(int aRegAddr, int aNumRegs, uint16_t *aRegsP);

    /// write single register
    /// @param aRegAddr the register address
    /// @param aRegData data to write to the register
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr writeRegister(int aRegAddr, uint16_t aRegData);

    /// write float register pair
    /// @param aRegAddr address of the first of two registers to store a float value
    /// @param aFloatData the float data to write to the two registers
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    ErrorPtr writeFloatRegister(int aRegAddr, double aFloatData);

    /// write multiple registers
    /// @param aRegAddr the register address of the first register
    /// @param aNumRegs how many consecutive registers to write
    /// @param aRegsP pointer to data to write to the registers (must be data for aNumRegs registers)
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr writeRegisters(int aRegAddr, int aNumRegs, const uint16_t *aRegsP);

    /// request slave info (ID and run indicator)
    /// @param aId will be set to the id returned from the slave
    /// @param aRunIndicator will be set to the run indicator status from the slave
    /// @note the id is device specific - usually it is a string but it could be any sequence of bytes
    ErrorPtr readSlaveInfo(string& aId, bool& aRunIndicator);

  };
  typedef boost::intrusive_ptr<ModbusMaster> ModbusMasterPtr;


  /// callback for accessed registers in the register model
  /// @param aAddress the modbus register or bit address
  /// @param aBit if true, aAddress is a bit address; if false, aAddress is a register address
  /// @param aInput if true, a read-only bit or input register is accessed; if false, a read-write coil bit or holding register is accessed
  /// @param aWrite if true, it is a write access and aValue contains the newly written data;
  ///   if false, it is a read access and aValue might be updated to return actualized data (otherwise, current register/bit content is returned)
  /// @return return Error object in case the access has failed, ErrorPtr() otherwise
  /// @note use the register model accessor functions to update/read the registers
  typedef boost::function<ErrorPtr (int aAddress, bool aBit, bool aInput, bool aWrite)> ModbusValueAccessCB;


  /// Raw modbus request handler
  typedef boost::function<bool (sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp, int &rsp_length)> ModbusReqCB;


  extern "C" {
    int modbus_slave_function_handler(modbus_t* ctx, sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp, void *user_ctx);
    const char *modbus_access_handler(modbus_t* ctx, modbus_mapping_t* mappings, modbus_data_access_t access, int addr, int cnt, modbus_data_t dataP, void *user_ctx);
  }

  class ModbusSlave : public ModbusConnection
  {
    typedef ModbusConnection inherited;

    string slaveId;
    modbus_mapping_t* registerModel;
    ModbusValueAccessCB valueAccessHandler;
    ModbusReqCB rawRequestHandler;

    modbus_rcv_t *modbusRcv;
    uint8_t modbusReq[MODBUS_MAX_PDU_LENGTH];
    uint8_t modbusRsp[MODBUS_MAX_PDU_LENGTH];
    MLTicket rcvTimeoutTicket;

    string errStr; ///< holds error string for libmodbus to access a c_str after handler returns

  public:

    ModbusSlave();
    virtual ~ModbusSlave();

    /// set the text to be returned by "Report Server/Slave ID"
    void setSlaveId(const string aSlaveId);

    /// define the register model for this slave
    ErrorPtr setRegisterModel(
      int aFirstCoil, int aNumCoils, // I/O bits = coils
      int aFirstBit, int aNumBits, // input bits
      int aFirstReg, int aNumRegs, // R/W registers
      int aFirstInp, int aNumInps // read only registers
    );

    /// free the register model
    void freeRegisterModel();

    /// close the modbus connection
    virtual void close() P44_OVERRIDE;

    /// stop receiving messages
    void stopServing();

    /// set a register model access handler
    /// @param aValueAccessCB is called whenever a register or bit is accessed
    /// @note this is called for every single bit or register access once, even if multiple registers are read/written in the same transaction
    void setValueAccessHandler(ModbusValueAccessCB aValueAccessCB);

    /// @name register model accessors
    /// @{

    /// get value from internal bits/register model
    /// @param aAddress the modbus register address
    /// @param aBit if true, aAddress specifies a coil or input bit, if false, a holding or input register
    /// @param aInput if true, aAddress specifies a read-only register or bit; if false, aAddress specifies a read-write register or bit
    /// @return register or bit value
    /// @note if invalid address is specified, result is 0
    uint16_t getValue(int aAddress, bool aBit, bool aInput);

    /// get register value from internal register model
    /// @param aAddress the modbus register address
    /// @param aInput if true, aAddress specifies a read-only input register; if false, aAddress specifies a read-write holding register
    /// @return register value
    /// @note if invalid address is specified, result is 0
    uint16_t getReg(int aAddress, bool aInput);

    /// set register value in internal register model
    /// @param aAddress the modbus register address
    /// @param aInput if true, aAddress specifies a read-only input register; if false, aAddress specifies a read-write holding register
    /// @param aRegValue the new register value to write
    /// @note if invalid address is specified, nothing happens
    void setReg(int aAddress, bool aInput, uint16_t aRegValue);

    /// get floating point value from a pair of registers in the internal register model
    /// @param aAddress first of two modbus register addresses representing a float value
    /// @param aInput if true, aAddress specifies s read-only input register pair; if false, aAddress specifies a read-write holding register pair
    /// @return floating point value represented by register pair
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    /// @note if invalid address is specified, result is 0
    double getFloatReg(int aAddress, bool aInput);

    /// set floating point value into a pair of registers in the internal register model
    /// @param aAddress first of two modbus (holding) register addresses representing a float value
    /// @param aInput if true, aAddress specifies s read-only input register pair; if false, aAddress specifies a read-write holding register pair
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    /// @note if invalid address is specified, nothing happens
    void setFloatReg(int aAddress, bool aInput, double aFloatValue);

    /// get bit value from internal register model
    /// @param aAddress the modbus bit address
    /// @param aInput if true, aAddress specifies a read-only bit; if false, aAddress specifies a read-write coil bit
    /// @return bit state
    /// @note if invalid address is specified, result is false
    bool getBit(int aAddress, bool aInput);

    /// set bit value in internal register model
    /// @param aAddress the modbus bit address
    /// @param aInput if true, aAddress specifies a read-only bit; if false, aAddress specifies a read-write coil bit
    /// @param aBitValue the new bit state to write
    /// @note if invalid address is specified, nothing happens
    void setBit(int aAddress, bool aInput, bool aBitValue);

    /// @}


  protected:

    /// is called when the "modbus" member variable is ready with a newly set-up context
    virtual void mbContextReady() P44_OVERRIDE;

    /// start receiving messages as server/slave if possible
    virtual void startServing() P44_OVERRIDE;

  private:

    uint8_t* getBitAddress(int aAddress, bool aInput, int aBits);
    uint16_t* getRegisterAddress(int aAddress, bool aInput, int aRegs);

    void cancelMsgReception();
    void startMsgReception();
    bool modbusFdPollHandler(int aFD, int aPollFlags);
    void modbusTimeoutHandler();
    void modbusRecoveryHandler();

  public:

    // stuff that needs to be public because friend declaration does not work in gcc (does in clang)
    int handleRawRequest(sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp);
    const char* accessHandler(modbus_data_access_t access, int addr, int cnt, modbus_data_t dataP);

  };
  typedef boost::intrusive_ptr<ModbusSlave> ModbusSlavePtr;



} // namespace p44

#endif // ENABLE_MODBUS
#endif // __p44utils__modbus__
