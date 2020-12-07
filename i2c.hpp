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

#ifndef __p44utils__i2c__
#define __p44utils__i2c__

#include "p44utils_common.hpp"

#include "iopin.hpp"

using namespace std;

namespace p44 {

  class I2CManager;
  class I2CBus;

  class I2CDevice : public P44Obj
  {
    friend class I2CBus;

  protected:

    I2CBus *i2cbus;
    uint8_t deviceAddress;

  public:

    /// @return fully qualified device identifier (deviceType@hexaddress)
    string deviceID();

    /// @return the bus object, allows directly communicating with a device
    I2CBus &getBus() { return *i2cbus; };

    /// @return device type identifier
    virtual const char *deviceType() { return "generic"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType);

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    I2CDevice(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

  };
  typedef boost::intrusive_ptr<I2CDevice> I2CDevicePtr;

  typedef std::map<string, I2CDevicePtr> I2CDeviceMap;



  class I2CBus : public P44Obj
  {
    friend class I2CManager;

    int busNumber;
    I2CDeviceMap deviceMap;

    int busFD;
    int lastDeviceAddress;

  protected:
    /// create i2c bus
    /// @param aBusNumber i2c bus number in the system
    I2CBus(int aBusNumber);

    /// register new I2CDevice
    /// @param aDevice the device to register
    void registerDevice(I2CDevicePtr aDevice);

    /// get device registered for address
    /// @param aDeviceID the device ID string in fully qualified "devicetype@2digithexaddr" form
    /// @return the device registered for this type/address or empty pointer if none registered
    I2CDevicePtr getDevice(const char *aDeviceID);

  public:

    virtual ~I2CBus();

    typedef uint8_t smbus_block_t[32];

    /// SMBus read byte
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aByte will receive result
    /// @return true if successful
    bool SMBusReadByte(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t &aByte);

    /// SMBus read word
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aWord will receive result
    /// @param aMSBFirst if set, byte order on the bus is expected to be MSByte first (otherwise, LSByte first)
    /// @return true if successful
    bool SMBusReadWord(I2CDevice *aDeviceP, uint8_t aRegister, uint16_t &aWord, bool aMSBFirst = false);

    /// SMBus read byte/word/block
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aCount number of bytes
    /// @param aData will receive result
    /// @return true if successful
    bool SMBusReadBlock(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t &aCount, smbus_block_t &aData);

    /// SMBus write byte
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aByte data to write
    /// @return true if successful
    bool SMBusWriteByte(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t aByte);

    /// SMBus write word
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aWord data to write
    /// @param aMSBFirst if set, byte order on the bus is expected to be MSByte first (otherwise, LSByte first)
    /// @return true if successful
    bool SMBusWriteWord(I2CDevice *aDeviceP, uint8_t aRegister, uint16_t aWord, bool aMSBFirst = false);

    /// SMBus write SMBus block
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aCount number of bytes
    /// @param aDataP data to write
    /// @return true if successful
    bool SMBusWriteBlock(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, const uint8_t *aDataP);

    /// SMBus write a number of bytes (but not using the SMBus block semantics)
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aCount number of bytes
    /// @param aDataP data to write
    /// @return true if successful
    bool SMBusWriteBytes(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, const uint8_t *aDataP);

    /// I2C direct read/write without SMBus protocol (old devices like PCF8574)
    bool I2CReadByte(I2CDevice *aDeviceP, uint8_t &aByte);
    bool I2CWriteByte(I2CDevice *aDeviceP, uint8_t aByte);
    bool I2CReadBytes(I2CDevice *aDeviceP, uint8_t aCount, uint8_t *aBufferP);


  private:
    bool accessDevice(I2CDevice *aDeviceP);
    bool accessBus();
    void closeBus();

  };
  typedef boost::intrusive_ptr<I2CBus> I2CBusPtr;



  typedef std::map<int, I2CBusPtr> I2CBusMap;

  class I2CManager : public P44Obj
  {
    I2CBusMap busMap;

    I2CManager();
  public:
    virtual ~I2CManager();

    /// get shared instance of manager
    static I2CManager &sharedManager();

    /// get device
    /// @param aBusNumber the i2c bus number in the system to use
    /// @param aDeviceID the device name identifying address and type of device
    ///   like "tca9555@25" meaning TCA9555 chip based IO at HEX!! bus address 25
    /// @return a device of proper type or empty pointer if none could be found
    I2CDevicePtr getDevice(int aBusNumber, const char *aDeviceID);

    /// get bus (for directly communicating with i2c device)
    /// @param aBusNumber the i2c bus number in the system to use
    /// @return the ready-to-use i2c bus object or empty pointer if specified bus number is not available
    I2CBusPtr getBus(int aBusNumber);

  };


  // MARK: - digital IO


  class I2CBitPortDevice : public I2CDevice
  {
    typedef I2CDevice inherited;

  protected:
    uint32_t outputEnableMask; ///< bit set = pin is output
    uint32_t pinStateMask; ///< state of pins 0..31(max)
    uint32_t outputStateMask; ///< state of outputs 0..31(max)
    uint32_t pullUpMask; ///< bit set = enable pullup for inputs

    virtual void updateInputState(int aForBitNo) = 0;
    virtual void updateOutputs(int aForBitNo) = 0;
    virtual void updateDirection(int aForBitNo) = 0;

  public:
    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    I2CBitPortDevice(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "BitPort"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    bool getBitState(int aBitNo);
    void setBitState(int aBitNo, bool aState);
    void setAsOutput(int aBitNo, bool aOutput, bool aInitialState, bool aPullUp);

  };
  typedef boost::intrusive_ptr<I2CBitPortDevice> I2CBitPortDevicePtr;



  class TCA9555 : public I2CBitPortDevice
  {
    typedef I2CBitPortDevice inherited;

  protected:

    virtual void updateInputState(int aForBitNo) P44_OVERRIDE;
    virtual void updateOutputs(int aForBitNo) P44_OVERRIDE;
    virtual void updateDirection(int aForBitNo) P44_OVERRIDE;


  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    TCA9555(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "TCA9555"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

  };



  class PCF8574 : public I2CBitPortDevice
  {
    typedef I2CBitPortDevice inherited;

  protected:

    virtual void updateInputState(int aForBitNo) P44_OVERRIDE;
    virtual void updateOutputs(int aForBitNo) P44_OVERRIDE;
    virtual void updateDirection(int aForBitNo) P44_OVERRIDE;


  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    PCF8574(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "PCF8574"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;
    
  };
  

  class MCP23017 : public I2CBitPortDevice
  {
    typedef I2CBitPortDevice inherited;

  protected:

    virtual void updateInputState(int aForBitNo) P44_OVERRIDE;
    virtual void updateOutputs(int aForBitNo) P44_OVERRIDE;
    virtual void updateDirection(int aForBitNo) P44_OVERRIDE;


  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP SPIBus object
    /// @param aDeviceOptions optional device-level options
    MCP23017(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "MCP23017"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;
    
  };



  // MARK: - analog IO


  class I2CAnalogPortDevice : public I2CDevice
  {
    typedef I2CDevice inherited;

  public:
    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    I2CAnalogPortDevice(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "AnalogPort"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) = 0;
    virtual void setPinValue(int aPinNo, double aValue) = 0;
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) { return false; }

  };
  typedef boost::intrusive_ptr<I2CAnalogPortDevice> I2CAnalogPortDevicePtr;



  class PCA9685 : public I2CAnalogPortDevice
  {
    typedef I2CAnalogPortDevice inherited;

  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    PCA9685(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "PCA9685"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) P44_OVERRIDE;
    virtual void setPinValue(int aPinNo, double aValue) P44_OVERRIDE;
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;
    
  };


  class LM75 : public I2CAnalogPortDevice
  {
    typedef I2CAnalogPortDevice inherited;

    int bits;

  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    LM75(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "LM75"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) P44_OVERRIDE;
    virtual void setPinValue(int aPinNo, double aValue) P44_OVERRIDE { /* dummy */ };
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };


  class MCP3021 : public I2CAnalogPortDevice
  {
    typedef I2CAnalogPortDevice inherited;

  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    MCP3021(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "MCP3021"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) P44_OVERRIDE;
    virtual void setPinValue(int aPinNo, double aValue) P44_OVERRIDE { /* dummy */ };
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };


  class MAX1161x : public I2CAnalogPortDevice
  {
    typedef I2CAnalogPortDevice inherited;

  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    MAX1161x(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "MAX1161x"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) P44_OVERRIDE;
    virtual void setPinValue(int aPinNo, double aValue) P44_OVERRIDE { /* dummy */ };
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };




  // MARK: - Wrapper classes


  /// wrapper class for a pin that is used as digital I/O (can also make use of analog I/O pins for that)
  class I2CPin : public IOPin
  {
    typedef IOPin inherited;

    I2CBitPortDevicePtr bitPortDevice;
    I2CAnalogPortDevicePtr analogPortDevice;
    int pinNumber;
    bool output;
    bool lastSetState;

  public:

    /// create i2c based digital input or output pin
    I2CPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, bool aInitialState, Tristate aPull);

    /// get state of pin
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    virtual bool getState() P44_OVERRIDE;

    /// set state of pin (NOP for inputs)
    /// @param aState new state to set output to
    virtual void setState(bool aState) P44_OVERRIDE;
  };



  /// wrapper class for analog I/O pin actually used as analog I/O
  class AnalogI2CPin : public AnalogIOPin
  {
    typedef AnalogIOPin inherited;

    I2CAnalogPortDevicePtr analogPortDevice;
    int pinNumber;
    bool output;

  public:

    /// create i2c based digital input or output pin
    AnalogI2CPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, double aInitialValue);

    /// get value of pin
    /// @return current value (from actual pin for inputs, from last set state for outputs)
    virtual double getValue() P44_OVERRIDE;

    /// set value of pin (NOP for inputs)
    /// @param aValue new value to set output to
    virtual void setValue(double aValue) P44_OVERRIDE;

    /// get range and resolution of this input
    /// @param aMin minimum value
    /// @param aMax maximum value
    /// @param aResolution resolution (LSBit value)
    /// @return false if no range information is available (arguments are not touched then)
    virtual bool getRange(double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };


} // namespace

#endif /* defined(__p44utils__i2c__) */
