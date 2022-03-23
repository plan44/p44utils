//
//  Copyright (c) 2016-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__spi__
#define __p44utils__spi__

#include "p44utils_common.hpp"

#include "iopin.hpp"

#if ENABLE_P44SCRIPT && !defined(ENABLE_SPI_SCRIPT_FUNCS)
  #define ENABLE_SPI_SCRIPT_FUNCS 1
#endif
#if ENABLE_SPI_SCRIPT_FUNCS && !ENABLE_P44SCRIPT
  #error "ENABLE_P44SCRIPT required when ENABLE_SPI_SCRIPT_FUNCS is set"
#endif

#ifndef SPI_SIMULATION
  #define SPI_SIMULATION 0
#endif

#if ENABLE_SPI_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif


using namespace std;

namespace p44 {

  class SPIManager;
  class SPIBus;

  #if ENABLE_SPI_SCRIPT_FUNCS
  namespace P44Script {
    class SPIDeviceObj;
    typedef boost::intrusive_ptr<SPIDeviceObj> SPIDeviceObjPtr;
  }
  #endif

  class SPIDevice : public P44Obj
  {
    friend class SPIBus;

    #if ENABLE_SPI_SCRIPT_FUNCS
    P44Script::SPIDeviceObjPtr mRepresentingObj; ///< the (singleton) ScriptObj representing this SPI device
    #endif

  protected:

    SPIBus *spibus;
    uint8_t deviceAddress;
    uint8_t spimode;
    uint32_t speedHz;

  public:

    /// @return fully qualified device identifier (deviceType@hexaddress)
    string deviceID();

    /// @return the bus object, allows directly communicating with a device
    SPIBus &getBus() { return *spibus; };

    /// @return device type identifier
    virtual const char *deviceType() { return "generic"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType);

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP SPIBus object
    /// @param aDeviceOptions device options (in base class: SPI mode)
    SPIDevice(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions);

    /// SPI raw transaction with read and write
    /// @param aDeviceP device to access
    /// @param aOutSz number of bytes to send
    /// @param aOutP pointer to bytes to send
    /// @param aInSz number of bytes to receive, can be 0
    /// @param aInP pointer to bytes to receive, can be NULL
    /// @param aFullDuplex send and receive simultaneously (otherwise: first send, then receive)
    /// @param aKeepCSActive if set, CS is kept active at the end of the transaction (usually meaning actual logical transaction continues)
    /// @return true if successful
    bool SPIRawWriteRead(unsigned int aOutSz, const uint8_t *aOutP, unsigned int aInSz, uint8_t *aInP, bool aFullDuplex = false, bool aKeepCSActive = false);

    #if ENABLE_SPI_SCRIPT_FUNCS
    /// @return a singleton script object, representing this SPI device
    P44Script::SPIDeviceObjPtr representingScriptObj();
    #endif

  };
  typedef boost::intrusive_ptr<SPIDevice> SPIDevicePtr;

  typedef std::map<string, SPIDevicePtr> SPIDeviceMap;



  class SPIBus : public P44Obj
  {
    friend class SPIManager;

    int busNumber;
    SPIDeviceMap deviceMap;

    int busFD;
    uint8_t lastSpiMode;

    #if SPI_SIMULATION
    string mSimReadData;
    size_t mSimDataIdx;
  public:
    void setDataToRead(const string aBinarySimData) { mSimReadData = aBinarySimData; mSimDataIdx = 0; }
    #endif

  protected:
    /// create spi bus
    /// @param aBusNumber spi bus number in the system
    SPIBus(int aBusNumber);

    /// register new SPIDevice
    /// @param aDevice device to register
    void registerDevice(SPIDevicePtr aDevice);

    /// get device registered for address
    /// @param aDeviceID the device ID string in fully qualified "devicetype@2digithexaddr" form
    /// @return the device registered for this type/address or empty pointer if none registered
    SPIDevicePtr getDevice(const char *aDeviceID);

    /// helper: prepare SPI transaction
    int spidev_write_read(
      SPIDevice *aDeviceP,
      unsigned int num_out_bytes,
      const uint8_t *out_buffer,
      unsigned int num_in_bytes,
      uint8_t *in_buffer,
      bool writeWrite = false,
      bool fullDuplex = false,
      bool keepCSActive = false
    );


  public:

    virtual ~SPIBus();

    /// SPI register read byte
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aByte will receive result
    /// @return true if successful
    bool SPIRegReadByte(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t &aByte);

    /// SPI register read word
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aWord will receive result
    /// @return true if successful
    bool SPIRegReadWord(SPIDevice *aDeviceP, uint8_t aRegister, uint16_t &aWord);

    /// SPI register read a number of bytes
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aCount number of bytes
    /// @param aDataP will receive result
    /// @return true if successful
    bool SPIRegReadBytes(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, uint8_t *aDataP);

    /// SPI register write byte
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aByte data to write
    /// @return true if successful
    bool SPIRegWriteByte(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t aByte);

    /// SPI register write word
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aWord data to write
    /// @return true if successful
    bool SPIRegWriteWord(SPIDevice *aDeviceP, uint8_t aRegister, uint16_t aWord);

    /// SPI register write a number of bytes
    /// @param aDeviceP device to access
    /// @param aRegister register/command to access
    /// @param aCount number of bytes
    /// @param aDataP data to write
    /// @return true if successful
    bool SPIRegWriteBytes(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, const uint8_t *aDataP);

    /// SPI raw transaction with read and write
    /// @param aDeviceP device to access
    /// @param aOutSz number of bytes to send
    /// @param aOutP pointer to bytes to send
    /// @param aInSz number of bytes to receive
    /// @param aInP pointer to bytes to receive
    /// @param aFullDuplex send and receive simultaneously (otherwise: first send, then receive)
    /// @param aKeepCSActive if set, CS is kept active at the end of the transaction (usually meaning actual logical transaction continues)
    /// @return true if successful
    bool SPIRawWriteRead(SPIDevice *aDeviceP, unsigned int aOutSz, const uint8_t *aOutP, unsigned int aInSz, uint8_t *aInP, bool aFullDuplex = false, bool aKeepCSActive = false);

  private:
    bool accessDevice(SPIDevice *aDeviceP);
    bool accessBus();
    void closeBus();

  };
  typedef boost::intrusive_ptr<SPIBus> SPIBusPtr;



  typedef std::map<int, SPIBusPtr> SPIBusMap;

  class SPIManager : public P44Obj
  {
    SPIBusMap busMap;

    SPIManager();
  public:
    virtual ~SPIManager();

    /// get shared instance of manager
    static SPIManager &sharedManager();

    /// get device
    /// @param aBusNumber the spi bus number in the system to use
    /// @param aDeviceID the device name identifying address and type of device
    ///   like "tca9555@25" meaning TCA9555 chip based IO at HEX!! bus address 25
    /// @return a device of proper type or empty pointer if none could be found
    SPIDevicePtr getDevice(int aBusNumber, const char *aDeviceID);

  };


  // MARK: - digital IO


  class SPIBitPortDevice : public SPIDevice
  {
    typedef SPIDevice inherited;

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
    /// @param aBusP SPIBus object
    /// @param aDeviceOptions device options (in base class: SPI mode)
    SPIBitPortDevice(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "BitPort"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    bool getBitState(int aBitNo);
    void setBitState(int aBitNo, bool aState);
    void setAsOutput(int aBitNo, bool aOutput, bool aInitialState, bool aPullUp);

  };
  typedef boost::intrusive_ptr<SPIBitPortDevice> SPIBitPortDevicePtr;



  class MCP23S17 : public SPIBitPortDevice
  {
    typedef SPIBitPortDevice inherited;

  protected:

    virtual void updateInputState(int aForBitNo) P44_OVERRIDE;
    virtual void updateOutputs(int aForBitNo) P44_OVERRIDE;
    virtual void updateDirection(int aForBitNo) P44_OVERRIDE;


  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP SPIBus object
    /// @param aDeviceOptions optional device-level options
    MCP23S17(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "MCP23S17"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

  };




  // MARK: - analog IO


  class SPIAnalogPortDevice : public SPIDevice
  {
    typedef SPIDevice inherited;

  public:
    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP SPIBus object
    /// @param aDeviceOptions optional device-level options
    SPIAnalogPortDevice(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "AnalogPort"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) = 0;
    virtual void setPinValue(int aPinNo, double aValue) = 0;
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) { return false; }

  };
  typedef boost::intrusive_ptr<SPIAnalogPortDevice> SPIAnalogPortDevicePtr;



  class MCP3008 : public SPIAnalogPortDevice
  {
    typedef SPIAnalogPortDevice inherited;

  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    MCP3008(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "MCP3008"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) P44_OVERRIDE;
    virtual void setPinValue(int aPinNo, double aValue) P44_OVERRIDE { /* dummy */ };
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };



  class MCP3002 : public SPIAnalogPortDevice
  {
    typedef SPIAnalogPortDevice inherited;

  public:

    /// create device
    /// @param aDeviceAddress slave address of the device
    /// @param aBusP I2CBus object
    /// @param aDeviceOptions optional device-level options
    MCP3002(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions);

    /// @return device type identifier
    virtual const char *deviceType() P44_OVERRIDE { return "MCP3002"; };

    /// @return true if this device or one of it's ancestors is of the given type
    virtual bool isKindOf(const char *aDeviceType) P44_OVERRIDE;

    virtual double getPinValue(int aPinNo) P44_OVERRIDE;
    virtual void setPinValue(int aPinNo, double aValue) P44_OVERRIDE { /* dummy */ };
    virtual bool getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };



  // MARK: - Wrapper classes

  /// wrapper class for a pin that is used as digital I/O (can also make use of analog I/O pins for that)
  class SPIPin : public IOPin
  {
    typedef IOPin inherited;

    SPIBitPortDevicePtr bitPortDevice;
    SPIAnalogPortDevicePtr analogPortDevice;
    int pinNumber;
    bool output;
    bool lastSetState;

  public:

    /// create spi based digital input or output pin
    SPIPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, bool aInitialState, Tristate aPull);

    /// get state of pin
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    virtual bool getState() P44_OVERRIDE;

    /// set state of pin (NOP for inputs)
    /// @param aState new state to set output to
    virtual void setState(bool aState) P44_OVERRIDE;
  };



  /// wrapper class for analog I/O pin actually used as analog I/O
  class AnalogSPIPin : public AnalogIOPin
  {
    typedef AnalogIOPin inherited;

    SPIAnalogPortDevicePtr analogPortDevice;
    int pinNumber;
    bool output;

  public:

    /// create spi based digital input or output pin
    AnalogSPIPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, double aInitialValue);

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



  #if ENABLE_SPI_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a i2c device
    class SPIDeviceObj : public StructuredLookupObject
    {
      typedef StructuredLookupObject inherited;
      friend class p44::SPIDevice;

      SPIDevicePtr mSPIDevice;
    public:
      SPIDeviceObj(SPIDevicePtr aSPIDevice);
      virtual string getAnnotation() const P44_OVERRIDE { return "SPI device"; };
      SPIDevicePtr spidevice() { return mSPIDevice; }
    };

    /// represents the global objects related to i2c
    class SPILookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      SPILookup();
    };

  } // namespace
  #endif // ENABLE_SPI_SCRIPT_FUNCS

} // namespace

#endif /* defined(__p44utils__spi__) */
