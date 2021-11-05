//
//  Copyright (c) 2013-2021 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


#include "i2c.hpp"

// locally disable actual functionality on unsupported platforms (but still provide console output dummies)
#if !defined(DISABLE_I2C) && (defined(__APPLE__) || P44_BUILD_DIGI) && !P44_BUILD_RPI && !P44_BUILD_RB && !P44_BUILD_OW
  #define DISABLE_I2C 1
#endif

#if !DISABLE_I2C
  #if P44_BUILD_OW
    #include <linux/lib-i2c-dev.h>
  #else
    #include <linux/i2c-dev.h>
    #ifndef LIB_I2CDEV_H
      #warning "Extended i2c-dev.h header not available - including local i2c-dev-extensions.h to augment existing i2c-dev.h"
      #include "i2c-dev-extensions.h"
    #endif
  #endif
#else
  #warning "No i2C supported on this platform - just showing calls in focus debug output"
#endif

using namespace p44;
#if ENABLE_I2C_SCRIPT_FUNCS
using namespace P44Script;
#endif

// MARK: - I2C Manager

static I2CManager *sharedI2CManager = NULL;


I2CManager::I2CManager()
{
}

I2CManager::~I2CManager()
{
}


I2CManager &I2CManager::sharedManager()
{
  if (!sharedI2CManager) {
    sharedI2CManager = new I2CManager();
  }
  return *sharedI2CManager;
}



I2CBusPtr I2CManager::getBus(int aBusNumber)
{
  // find or create bus
  I2CBusMap::iterator pos = busMap.find(aBusNumber);
  I2CBusPtr bus;
  if (pos!=busMap.end()) {
    bus = pos->second;
  }
  else {
    // bus does not exist yet, create it
    bus = I2CBusPtr(new I2CBus(aBusNumber));
    busMap[aBusNumber] = bus;
  }
  return bus;
}



I2CDevicePtr I2CManager::getDevice(int aBusNumber, const char *aDeviceID)
{
  // get the bus
  I2CBusPtr bus = getBus(aBusNumber);
  // dissect device ID into type and busAddress
  // - type string
  //   consists of Chip name plus optional options suffix. Like "PCA9685" or "PCA9685-TP" (TP=options)
  string typeString = "generic";
  string deviceOptions = ""; // no options
  string s = aDeviceID;
  size_t i = s.find("@");
  if (i!=string::npos) {
    typeString = s.substr(0,i);
    s.erase(0,i+1);
    // extract device options, if any (appended to device name after a dash)
    size_t j = typeString.find("-");
    if (j!=string::npos) {
      deviceOptions = typeString.substr(j+1);
      typeString.erase(j);
    }
  }
  // - device address (hex)
  int deviceAddress = 0;
  sscanf(s.c_str(), "%x", &deviceAddress);
  // reconstruct fully qualified device name for searching
  s = string_format("%s@%02X", typeString.c_str(), deviceAddress);
  // get possibly already existing device of correct type at that address
  I2CDevicePtr dev = bus->getDevice(s.c_str());
  if (!dev) {
    // create device from typestring
    if (typeString=="TCA9555")
      dev = I2CDevicePtr(new TCA9555(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="MCP23017")
      dev = I2CDevicePtr(new MCP23017(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="PCF8574")
      dev = I2CDevicePtr(new PCF8574(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="PCA9685")
      dev = I2CDevicePtr(new PCA9685(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="LM75")
      dev = I2CDevicePtr(new LM75(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="MCP3021")
      dev = I2CDevicePtr(new MCP3021(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="MAX1161x")
      dev = I2CDevicePtr(new MAX1161x(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="generic")
      dev = I2CDevicePtr(new I2CDevice(deviceAddress, bus.get(), deviceOptions.c_str()));
    // TODO: add more device types
    // Register new device
    if (dev) {
      bus->registerDevice(dev);
    }
  }
  return dev;
}


// MARK: - I2CBus


I2CBus::I2CBus(int aBusNumber) :
  busFD(-1),
  busNumber(aBusNumber),
  lastDeviceAddress(-1)
{
}


I2CBus::~I2CBus()
{
  closeBus();
}


void I2CBus::registerDevice(I2CDevicePtr aDevice)
{
  deviceMap[aDevice->deviceID()] = aDevice;
}


I2CDevicePtr I2CBus::getDevice(const char *aDeviceID)
{
  I2CDeviceMap::iterator pos = deviceMap.find(aDeviceID);
  if (pos!=deviceMap.end())
    return pos->second;
  return I2CDevicePtr();
}



bool I2CBus::I2CReadByte(I2CDevice *aDeviceP, uint8_t &aByte)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  #if !DISABLE_I2C
  int res = i2c_smbus_read_byte(busFD);
  #else
  int res = 0x42; // dummy
  #endif
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("i2c_smbus_read_byte() = %d / 0x%02X", res, res);
  if (res<0) return false;
  aByte = (uint8_t)res;
  return true;
}


bool I2CBus::I2CReadBytes(I2CDevice *aDeviceP, uint8_t aCount, uint8_t *aBufferP)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  #if !DISABLE_I2C
  ssize_t res = read(busFD, aBufferP, aCount);
  #else
  for (int n=0; n<aCount; n++) {
    aBufferP[n] = 0x42; // dummy
  }
  ssize_t res = aCount;
  #endif
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("i2c device read(): first byte = %d / 0x%02X, res=%zd", *aBufferP, *aBufferP, res);
  if (res<0 || res!=aCount) return false;
  return true;
}



bool I2CBus::I2CWriteByte(I2CDevice *aDeviceP, uint8_t aByte)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  #if !DISABLE_I2C
  int res = i2c_smbus_write_byte(busFD, aByte);
  #else
  int res = 1; // ok
  #endif
  FOCUSLOG("i2c_smbus_write_byte(byte=0x%02X) = %d", aByte, res);
  return (res>=0);
}



bool I2CBus::SMBusReadByte(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t &aByte)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  #if !DISABLE_I2C
  int res = i2c_smbus_read_byte_data(busFD, aRegister);
  #else
  int res = 0x42; // dummy
  #endif
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("i2c_smbus_read_byte_data(cmd=0x%02X) = %d / 0x%02X", aRegister, res, res);
  if (res<0) return false;
  aByte = (uint8_t)res;
  return true;
}


bool I2CBus::SMBusReadWord(I2CDevice *aDeviceP, uint8_t aRegister, uint16_t &aWord, bool aMSBFirst)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  #if !DISABLE_I2C
  int res = i2c_smbus_read_word_data(busFD, aRegister);
  if (res<0) return false;
  if (aMSBFirst) {
    // swap
    res = ((res&0xFF)<<8) + ((res>>8)&0xFF);
  }
  #else
  int res = 0x4242; // dummy
  #endif
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("i2c_smbus_read_word_data(cmd=0x%02X) = %d / 0x%04X", aRegister, res, res);
  if (res<0) return false;
  aWord = (uint16_t)res;
  return true;
}


bool I2CBus::SMBusReadBlock(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t &aCount, smbus_block_t &aData)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  #if !DISABLE_I2C
  int res = i2c_smbus_read_block_data(busFD, aRegister, aData);
  if (res<0) return false;
  #else
  int res = 0; // no data
  #endif
  if (FOCUSLOGENABLED) {
    string data;
    for (uint8_t i=0; i<res; i++) string_format_append(data, ", 0x%02X", aData[i]);
    FOCUSLOG("i2c_smbus_read_block_data(cmd=0x%02X) = %d / 0x%02X%s", aRegister, res, res, data.c_str());
  }
  if (res<0) return false;
  aCount = (uint16_t)res;
  return true;
}





bool I2CBus::SMBusWriteByte(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t aByte)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  #if !DISABLE_I2C
  int res = i2c_smbus_write_byte_data(busFD, aRegister, aByte);
  #else
  int res = 1; // ok
  #endif
  FOCUSLOG("i2c_smbus_write_byte_data(cmd=0x%02X, byte=0x%02X) = %d", aRegister, aByte, res);
  return (res>=0);
}


bool I2CBus::SMBusWriteWord(I2CDevice *aDeviceP, uint8_t aRegister, uint16_t aWord, bool aMSBFirst)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  #if !DISABLE_I2C
  if (aMSBFirst) {
    // swap
    aWord = ((aWord&0xFF)<<8) + ((aWord>>8)&0xFF);
  }
  int res = i2c_smbus_write_word_data(busFD, aRegister, aWord);
  #else
  int res = 1; // ok
  #endif
  FOCUSLOG("i2c_smbus_write_word_data(cmd=0x%02X, word=0x%04X) = %d", aRegister, aWord, res);
  return (res>=0);
}


bool I2CBus::SMBusWriteBlock(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, const uint8_t *aDataP)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  #if !DISABLE_I2C
  int res = i2c_smbus_write_block_data(busFD, aRegister, aCount, (uint8_t *)aDataP);
  #else
  int res = 1; // ok
  #endif
  if (FOCUSLOGENABLED) {
    string data;
    if (res>=0) {
      for (uint8_t i=0; i<aCount; i++) string_format_append(data, ", 0x%02X", aDataP[i]);
    }
    FOCUSLOG("i2c_smbus_write_block_data(cmd=0x%02X, count=0x%02X%s) = %d", aRegister, aCount, data.c_str(), res);
  }
  return (res>=0);
}


// Not proper block, but just some bytes in a row, count byte not sent
bool I2CBus::SMBusWriteBytes(I2CDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, const uint8_t *aDataP)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  #if !DISABLE_I2C
  int res = i2c_smbus_write_i2c_block_data(busFD, aRegister, aCount, (uint8_t *)aDataP);
  #else
  int res = 1; // ok
  #endif
  if (FOCUSLOGENABLED) {
    string data;
    if (res>=0) {
      for (uint8_t i=0; i<aCount; i++) string_format_append(data, ", 0x%02X", aDataP[i]);
    }
    FOCUSLOG("i2c_smbus_write_i2c_block_data(cmd=0x%02X, count=0x%02X%s) = %d", aRegister, aCount, data.c_str(), res);
  }
  return (res>=0);
}




bool I2CBus::accessDevice(I2CDevice *aDeviceP)
{
  if (!accessBus())
    return false;
  if (aDeviceP->deviceAddress == lastDeviceAddress)
    return true; // already set to access that device
  // address the device
  #if !DISABLE_I2C
  if (ioctl(busFD, I2C_SLAVE, aDeviceP->deviceAddress) < 0) {
    LOG(LOG_ERR, "Error: Cannot access device '%s' on bus %d", aDeviceP->deviceID().c_str(), busNumber);
    lastDeviceAddress = -1; // invalidate
    return false;
  }
  #endif
  FOCUSLOG("ioctl(busFD, I2C_SLAVE, 0x%02X)", aDeviceP->deviceAddress);
  // remember
  lastDeviceAddress = aDeviceP->deviceAddress;
  return true; // ok
}


bool I2CBus::accessBus()
{
  if (busFD>=0)
    return true; // already open
  // need to open
  lastDeviceAddress = -1; // invalidate
  string busDevName = string_format("/dev/i2c-%d", busNumber);
  #if !DISABLE_I2C
  busFD = open(busDevName.c_str(), O_RDWR);
  if (busFD<0) {
    LOG(LOG_ERR, "Error: Cannot open i2c bus device '%s'",busDevName.c_str());
    return false;
  }
  #else
  busFD = 1; // dummy, signalling open
  #endif
  FOCUSLOG("open(\"%s\", O_RDWR) = %d", busDevName.c_str(), busFD);
  return true;
}



void I2CBus::closeBus()
{
  if (busFD>=0) {
    #ifndef __APPLE__
    close(busFD);
    #endif
    busFD = -1;
  }
}



// MARK: - I2CDevice


I2CDevice::I2CDevice(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions)
{
  i2cbus = aBusP;
  deviceAddress = aDeviceAddress;
}


string I2CDevice::deviceID()
{
  return string_format("%s@%02X", deviceType(), deviceAddress);
}




bool I2CDevice::isKindOf(const char *aDeviceType)
{
  return (strcmp(deviceType(),aDeviceType)==0);
}



// MARK: - I2CBitPortDevice


I2CBitPortDevice::I2CBitPortDevice(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions),
  outputEnableMask(0),
  pinStateMask(0),
  pullUpMask(0)
{
}



bool I2CBitPortDevice::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


bool I2CBitPortDevice::getBitState(int aBitNo)
{
  uint32_t bitMask = 1<<aBitNo;
  if (outputEnableMask & bitMask) {
    // is output, just return the last set state
    return (outputStateMask & bitMask)!=0;
  }
  else {
    // is input, get actual input state
    updateInputState(aBitNo); // update
    return (pinStateMask & bitMask)!=0;
  }
}


void I2CBitPortDevice::setBitState(int aBitNo, bool aState)
{
  uint32_t bitMask = 1<<aBitNo;
  if (outputEnableMask & bitMask) {
    // is output, set new state (always, even if seemingly already set)
    if (aState)
      outputStateMask |= bitMask;
    else
      outputStateMask &= ~bitMask;
    // update hardware
    updateOutputs(aBitNo);
  }
}


void I2CBitPortDevice::setAsOutput(int aBitNo, bool aOutput, bool aInitialState, bool aPullUp)
{
  uint32_t bitMask = 1<<aBitNo;
  // Input or output
  if (aOutput)
    outputEnableMask |= bitMask;
  else
    outputEnableMask &= ~bitMask;
  // Pullup or not
  if (aPullUp)
    pullUpMask |= bitMask;
  else {
    pullUpMask &= ~bitMask;
  }
  // before actually updating direction, set initial value
  setBitState(aBitNo, aInitialState);
  // now update direction
  updateDirection(aBitNo);
}



// MARK: - TCA9555


TCA9555::TCA9555(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // make sure we have all inputs
  updateDirection(0); // port 0
  updateDirection(8); // port 1
  // reset polarity inverter
  i2cbus->SMBusWriteByte(this, 4, 0); // reset polarity inversion port 0
  i2cbus->SMBusWriteByte(this, 5, 0); // reset polarity inversion port 1
}


bool TCA9555::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


void TCA9555::updateInputState(int aForBitNo)
{
  if (aForBitNo>15) return;
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  uint8_t data;
  i2cbus->SMBusReadByte(this, port, data); // get input byte
  pinStateMask = (pinStateMask & ~(((uint32_t)0xFF) << shift)) | ((uint32_t)data << shift);
}


void TCA9555::updateOutputs(int aForBitNo)
{
  if (aForBitNo>15) return;
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  i2cbus->SMBusWriteByte(this, port+2, (outputStateMask >> shift) & 0xFF); // write output byte
}



void TCA9555::updateDirection(int aForBitNo)
{
  if (aForBitNo>15) return;
  updateOutputs(aForBitNo); // make sure output register has the correct value
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  uint8_t data = ~((outputEnableMask >> shift) & 0xFF); // TCA9555 config register has 1 for inputs, 0 for outputs
  i2cbus->SMBusWriteByte(this, port+6, data); // set input enable flags in reg 6 or 7
}


// MARK: - PCF8574


PCF8574::PCF8574(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // make sure we have all inputs
  updateDirection(0); // port 0
}


bool PCF8574::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


void PCF8574::updateInputState(int aForBitNo)
{
  if (aForBitNo>7) return;
  uint8_t data;
  if (i2cbus->I2CReadByte(this, data)) {
    pinStateMask = data;
  }
}


void PCF8574::updateOutputs(int aForBitNo)
{
  if (aForBitNo>7) return;
  // PCF8574 does not have a direction register, but reading just senses the pin level.
  // With output set to H, the pin is OC and can be set to Low
  // -> pins to be used as inputs must always be high
  uint8_t b =
    ((~outputEnableMask) & 0xFF) | // pins used as input must have output state High
    (outputStateMask & 0xFF); // pins used as output will have the correct state from beginning
  i2cbus->I2CWriteByte(this, b);
}



void PCF8574::updateDirection(int aForBitNo)
{
  // There is no difference in updating outputs or updating direction for the primitive PCF8574
  updateOutputs(aForBitNo);
}



// MARK: - MCP23017


MCP23017::MCP23017(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // initially, IOCON==0 -> IOCON.BANK==0 -> A/B interleaved register access
  // enable hardware addressing if selected
  if (strchr(aDeviceOptions, 'A')) {
    i2cbus->SMBusWriteByte(this, 0x0A, 0x08); // set HAEN (hardware address enable) in IOCON
  }
  // make sure we have all inputs
  updateDirection(0); // port 0
  updateDirection(8); // port 1
  // reset polarity inverter
  i2cbus->SMBusWriteByte(this, 0x02, 0); // reset polarity inversion A
  i2cbus->SMBusWriteByte(this, 0x03, 0); // reset polarity inversion B
}


bool MCP23017::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


void MCP23017::updateInputState(int aForBitNo)
{
  if (aForBitNo>15) return;
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  uint8_t data;
  i2cbus->SMBusReadByte(this, port+0x12, data); // get current port state from GPIO reg 12/13
  pinStateMask = (pinStateMask & ~(((uint32_t)0xFF) << shift)) | ((uint32_t)data << shift);
}


void MCP23017::updateOutputs(int aForBitNo)
{
  if (aForBitNo>15) return;
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  i2cbus->SMBusWriteByte(this, port+0x14, (outputStateMask >> shift) & 0xFF); // write to output latch (OLAT) A/B reg 14/15
}



void MCP23017::updateDirection(int aForBitNo)
{
  if (aForBitNo>15) return;
  updateOutputs(aForBitNo); // make sure output register has the correct value
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  uint8_t data;
  // configure pullups
  data = (pullUpMask >> shift) & 0xFF; // MCP23S17 GPPU register has 1 for pullup enabled
  i2cbus->SMBusWriteByte(this, port+0x0C, data); // set pullup enable flags in GPPU reg C or D
  // configure direction
  data = ~((outputEnableMask >> shift) & 0xFF); // MCP23S17 IODIR register has 1 for inputs, 0 for outputs
  i2cbus->SMBusWriteByte(this, port+0x00, data); // set input enable flags in IODIR reg 0 or 1
}



// MARK: - I2Cpin


/// create i2c based digital input or output pin (or use an analog pin as digital I/O)
I2CPin::I2CPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, bool aInitialState, Tristate aPull) :
  output(false),
  lastSetState(false)
{
  pinNumber = aPinNumber;
  output = aOutput;
  I2CDevicePtr dev = I2CManager::sharedManager().getDevice(aBusNumber, aDeviceId);
  bitPortDevice = boost::dynamic_pointer_cast<I2CBitPortDevice>(dev);
  analogPortDevice = boost::dynamic_pointer_cast<I2CAnalogPortDevice>(dev);
  if (bitPortDevice) {
    // bitport device, which is configurable for I/O and pullup
    bitPortDevice->setAsOutput(pinNumber, output, aInitialState, aPull==yes);
  }
  else if (analogPortDevice) {
    // analog device used as digital signal
    setState(aInitialState); // just set the state
  }
  lastSetState = aInitialState;
}


/// get state of pin
/// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
bool I2CPin::getState()
{
  if (bitPortDevice) {
    if (output)
      return lastSetState;
    else
      return bitPortDevice->getBitState(pinNumber);
  }
  else if (analogPortDevice) {
    // use analog pin as digital input
    double min=0, max=100, res=1;
    analogPortDevice->getPinRange(pinNumber, min, max, res);
    return analogPortDevice->getPinValue(pinNumber)>min+(max-min)/2; // above the middle
  }
  return false;
}


/// set state of pin (NOP for inputs)
/// @param aState new state to set output to
void I2CPin::setState(bool aState)
{
  if (output) {
    if (bitPortDevice) {
      bitPortDevice->setBitState(pinNumber, aState);
    }
    else if (analogPortDevice) {
      // use analog pin as digital output
      double min=0, max=100, res=1;
      analogPortDevice->getPinRange(pinNumber, min, max, res);
      analogPortDevice->setPinValue(pinNumber, aState ? max : min);
    }
  }
  lastSetState = aState;
}



// MARK: - I2CAnalogPortDevice


I2CAnalogPortDevice::I2CAnalogPortDevice(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
}



bool I2CAnalogPortDevice::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}



// MARK: - PCA9685


PCA9685::PCA9685(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // Initalize
  // device options:
  // - 'I' : output invert (Low when active)
  // - 'O' : open drain (pull to low only, vs. totem pole)
  // - 'Sxxxx' : PWM speed in Hz (max is 2kHz, min is 24Hz)
  bool inverted = strchr(aDeviceOptions, 'I');
  bool opendrain = strchr(aDeviceOptions, 'O');
  // Internal OSC is 25Mhz, pre_scale = (25MHz/4096/PWMfreq)-1
  uint8_t pre_scale = 30; // default reset value of PCA9685 PRE_SCALE register = 200Hz
  const char *p = strchr(aDeviceOptions, 'S');
  if (p) {
    int speed = atoi(p+1);
    if (speed<24) speed=24; // limit to >=24Hz
    pre_scale = (int)6103/speed; // approx, no rounding but omitting -1 instead
    if (pre_scale<3) pre_scale=3; // PRE_SCALE does not accept < 3
  }
  // - prepare for setting PRE_SCALE: control register 0 = MODE1: SLEEP=1
  i2cbus->SMBusWriteByte(this, 0, 0x10);
  // - set speed (can only be set when SLEEP=1)
  i2cbus->SMBusWriteByte(this, 0xFE, pre_scale); // set PRE_SCALE register
  // - control register 0 = MODE1: normal operation, SLEEP=0, auto-increment register address, no subadresses
  i2cbus->SMBusWriteByte(this, 0, 0x20);
  // - control register 1 = MODE2: when OE is 1, outputs are high impedance, plus invert and opendrain/totempole according to options
  i2cbus->SMBusWriteByte(this, 1, 0x03 + (inverted ? 0x10 : 0) + (opendrain ? 0 : 0x04));
  // - turn off all LEDs
  i2cbus->SMBusWriteByte(this, 0xFB, 0x00); // none full on
  i2cbus->SMBusWriteByte(this, 0xFD, 0x10); // all full off
}


bool PCA9685::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


double PCA9685::getPinValue(int aPinNo)
{
  // get current ratio
  uint8_t h,l;
  uint16_t onTime, offTime;
  // get off time
  i2cbus->SMBusReadByte(this, 9+aPinNo*4, h);
  if (h & 0x10) {
    // full off
    return 0;
  }
  i2cbus->SMBusReadByte(this, 8+aPinNo*4, l);
  offTime = (h & 0xF)<<8 | l;
  // get on time
  i2cbus->SMBusReadByte(this, 7+aPinNo*4, h);
  if (h & 0x10) {
    // full on
    return 100;
  }
  i2cbus->SMBusReadByte(this, 6+aPinNo*4, l);
  onTime = (h & 0xF)<<8 | l;
  // calculate on ratio in percent
  onTime = (offTime-onTime) & 0xFFF;
  return (double)(onTime)/40.96; // %
}


void PCA9685::setPinValue(int aPinNo, double aValue)
{
  uint8_t pwmdata[4];
  // check special full on and full off cases first
  // TODO: when we use shift, for some unknown reason, the PWM output flickers when the off time wraps around.
  //   So for now, we do NOT shift the on time.
  //int shift = aPinNo; // minimize current by distributing switch on time of the pins
  int shift = 0; // no on-time shifting, all starting at 0
  uint16_t v = (uint16_t)(aValue*40.96+0.5);
  if (v==0) {
    pwmdata[0] = 0;
    pwmdata[1] = 0x00; // not full ON
    pwmdata[2] = 0;
    pwmdata[3] = 0x10; // but full OFF
  }
  else if (v>=0x0FFF) {
    pwmdata[0] = 0; // LSB of start time is 0
    pwmdata[1] = 0x10+shift; // full ON, starting at pin's regular ON time
    pwmdata[2] = 0;
    pwmdata[3] = 0x00; // but not full OFF
  }
  else {
    // set on time, possibly shifted
    pwmdata[0] = 0x00; // LSB of start time is 0
    pwmdata[1] = shift; // each pin offsets onTime by 1/16 of 12-bit range: upper 4 bits = pinNo
    uint16_t t = shift<<8; // on time
    t = (t+v) & 0xFFF; // off time with wrap around
    // set off time
    pwmdata[2] = t & 0xFF; // LSB of end time
    pwmdata[3] = (t>>8) & 0xF; // 4 MSB of end time
  }
  // send as one transaction
  i2cbus->SMBusWriteBytes(this, 6+aPinNo*4, 4, pwmdata);
}


bool PCA9685::getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution)
{
  aMin = 0;
  aMax = 100;
  aResolution = 1.0/4096;
  return true;
}


// MARK: - LM75 (A,B,C...)

LM75::LM75(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions),
  bits(9) // default to 9 valid bits, most LM75 variants seem to have 9 bits, but some have 10 or 11
{
  // device options are number of bits
  int b = atoi(aDeviceOptions);
  if (b!=0) bits = b;
}


bool LM75::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


double LM75::getPinValue(int aPinNo)
{
  uint16_t raw;
  i2cbus->SMBusReadWord(this, 0x00, raw, true); // LM75A delivers MSB first
  // result is a signed 16 bit value covering a range of -128..127 degree celsius, in 1/256 degree steps
  // However, of these 16 bits, 5..7 LSBits (depending on LM75A versions with different ADC precision) are invalid
  // For example LM75A from NXP has 11 bit precision, while most other LM75 variants (TI, Maxim...) have 9 bits
  // So we mask out the invalid bits here
  uint16_t mask = ~((1<<(16-bits))-1);
  // see as 16 bit signed value of 1/256 degree celsius
  int16_t temp256th = (int16_t)(raw & mask);
  // return as celsius
  return ((double)temp256th)/256;
}


bool LM75::getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution)
{
  aMin = -127;
  aMax = 127;
  aResolution = 256.0/(1<<bits);
  return true;
}


// MARK: - MCP3021 (5 pin, 10bit ADC, Microchip)

MCP3021::MCP3021(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
}


bool MCP3021::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


double MCP3021::getPinValue(int aPinNo)
{
  uint8_t buf[2];
  uint16_t raw;
  i2cbus->I2CReadBytes(this, 2, buf); // MCP3021 delivers MSB first
  // discard two LSBs, limit to actual 10 bit result
  raw = (((uint16_t)buf[0]<<6)) + (buf[1]>>2);
  return raw;
}


bool MCP3021::getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution)
{
  // as we don't know what will be connected to the inputs, we return raw A/D value.
  aMin = 0;
  aMax = 1024;
  aResolution = 1;
  return true;
}



// MARK: - MAX11612-617 (12bit ADCs, Maxim)

MAX1161x::MAX1161x(uint8_t aDeviceAddress, I2CBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  i2cbus->I2CWriteByte(this,
    (1 << 7) | // B7 = 1 -> setup byte
    (5 << 4) | // SEL: use internal reference, REF = n/c, AIN_/REF = analog input
    (0 << 3) | // internal clock
    (0 << 2) | // unipolar mode
    (0 << 1) // reset configuration register to default
  );
}


bool MAX1161x::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


double MAX1161x::getPinValue(int aPinNo)
{
  uint8_t buf[2];
  uint16_t raw;
  // - configure scan
  i2cbus->I2CWriteByte(this,
    (0 << 7) | // B7 = 0 -> config byte
    (3 << 5) | // Scan mode: 3 = just convert single channel
    ((aPinNo & 0x0F) << 1) | // Channel Select = pin number bits 0..3
    ((aPinNo & 0x10 ? 0 : 1) << 0) // differential when pin number bit 4 is set, single ended otherwise
  );
  // - read result
  i2cbus->I2CReadBytes(this, 2, buf); // MAX1161x deliver MSB first
  // actual result is in lower 4 bits of MSByte + 8 bits LSByte
  raw = ((buf[0]&0xF)<<8) + buf[1];
  return raw;
}


bool MAX1161x::getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution)
{
  // as we don't know what will be connected to the inputs, we return raw A/D value.
  aMin = 0;
  aMax = 4096;
  aResolution = 1;
  return true;
}





// MARK: - AnalogI2Cpin


AnalogI2CPin::AnalogI2CPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, double aInitialValue) :
  output(false)
{
  pinNumber = aPinNumber;
  output = aOutput;
  I2CDevicePtr dev = I2CManager::sharedManager().getDevice(aBusNumber, aDeviceId);
  analogPortDevice = boost::dynamic_pointer_cast<I2CAnalogPortDevice>(dev);
  if (analogPortDevice && output) {
    analogPortDevice->setPinValue(pinNumber, aInitialValue);
  }
}


double AnalogI2CPin::getValue()
{
  if (analogPortDevice) {
    return analogPortDevice->getPinValue(pinNumber);
  }
  return 0;
}


void AnalogI2CPin::setValue(double aValue)
{
  if (analogPortDevice && output) {
    analogPortDevice->setPinValue(pinNumber, aValue);
  }
}


bool AnalogI2CPin::getRange(double &aMin, double &aMax, double &aResolution)
{
  if (analogPortDevice) {
    return analogPortDevice->getPinRange(pinNumber, aMin, aMax, aResolution);
  }
  return false;
}


#if ENABLE_I2C_SCRIPT_FUNCS

// MARK: - i2c scripting

I2CDeviceObjPtr I2CDevice::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new I2CDeviceObj(this);
  }
  return mRepresentingObj;
}

// rawread([count])
static const BuiltInArgDesc rawread_args[] = { { numeric|optionalarg } };
static const size_t rawread_numargs = sizeof(rawread_args)/sizeof(BuiltInArgDesc);
static void rawread_func(BuiltinFunctionContextPtr f)
{
  I2CDeviceObj* o = dynamic_cast<I2CDeviceObj*>(f->thisObj().get());
  assert(o);
  I2CDevice* dev = o->i2cdevice().get();
  I2CBus& bus = dev->getBus();
  if (f->arg(1)->defined()) {
    // read a string of bytes
    uint8_t count = f->arg(1)->intValue();
    uint8_t buf[256];
    if (bus.I2CReadBytes(dev, count, buf)) {
      string data((char *)buf, (size_t)count);
      f->finish(new StringValue(data));
      return;
    }
  }
  else {
    // read a single byte
    uint8_t b;
    if (bus.I2CReadByte(dev, b)) {
      f->finish(new NumericValue(b));
      return;
    }
  }
  // no success
  f->finish(new ErrorValue(TextError::err("i2c raw read error")));
}


// rawwrite(byte)
static const BuiltInArgDesc rawwrite_args[] = { { numeric } };
static const size_t rawwrite_numargs = sizeof(rawwrite_args)/sizeof(BuiltInArgDesc);
static void rawwrite_func(BuiltinFunctionContextPtr f)
{
  I2CDeviceObj* o = dynamic_cast<I2CDeviceObj*>(f->thisObj().get());
  assert(o);
  I2CDevice* dev = o->i2cdevice().get();
  I2CBus& bus = dev->getBus();
  uint8_t b = f->arg(0)->intValue();
  if (bus.I2CWriteByte(dev, b)) {
    f->finish();
    return;
  }
  // no success
  f->finish(new ErrorValue(TextError::err("i2c raw write error")));
}


// smbusread(reg [,type])
static const BuiltInArgDesc smbusread_args[] = { { numeric }, { text|optionalarg } };
static const size_t smbusread_numargs = sizeof(smbusread_args)/sizeof(BuiltInArgDesc);
static void smbusread_func(BuiltinFunctionContextPtr f)
{
  I2CDeviceObj* o = dynamic_cast<I2CDeviceObj*>(f->thisObj().get());
  assert(o);
  I2CDevice* dev = o->i2cdevice().get();
  I2CBus& bus = dev->getBus();
  uint8_t reg = f->arg(0)->intValue();
  string ty;
  if (f->arg(1)->defined()) ty = f->arg(1)->stringValue();
  if (ty=="word") {
    // 16 bit word
    uint16_t w;
    if (bus.SMBusReadWord(dev, reg, w)) {
      f->finish(new NumericValue(w));
      return;
    }
  }
  else if (ty=="block") {
    I2CBus::smbus_block_t d;
    uint8_t c;
    if (bus.SMBusReadBlock(dev, reg, c, d)) {
      string data((char *)&d, (size_t)c);
      f->finish(new StringValue(data));
      return;
    }
  }
  else {
    // byte
    uint8_t b;
    if (bus.SMBusReadByte(dev, reg, b)) {
      f->finish(new NumericValue(b));
      return;
    }
  }
  // no success
  f->finish(new ErrorValue(TextError::err("i2c smbus read error")));
}


// smbuswrite(reg, value [,type])
static const BuiltInArgDesc smbuswrite_args[] = { { numeric }, { text|numeric }, { text|optionalarg } };
static const size_t smbuswrite_numargs = sizeof(smbuswrite_args)/sizeof(BuiltInArgDesc);
static void smbuswrite_func(BuiltinFunctionContextPtr f)
{
  I2CDeviceObj* o = dynamic_cast<I2CDeviceObj*>(f->thisObj().get());
  assert(o);
  I2CDevice* dev = o->i2cdevice().get();
  I2CBus& bus = dev->getBus();
  uint8_t reg = f->arg(0)->intValue();
  string ty;
  if (f->arg(2)->defined()) ty = f->arg(2)->stringValue();
  if (ty=="word") {
    // 16 bit word
    uint16_t w = f->arg(1)->intValue();
    if (bus.SMBusWriteWord(dev, reg, w)) {
      f->finish();
      return;
    }
  }
  else if (ty=="block") {
    // SMBus block semantics
    string d = f->arg(1)->stringValue();
    uint8_t c = d.size();
    if (bus.SMBusWriteBlock(dev, reg, c, (uint8_t*)d.c_str())) {
      f->finish();
      return;
    }
  }
  else if (ty=="bytes") {
    // bytes w/o SMBus semantics, count byte not sent
    string d = f->arg(1)->stringValue();
    uint8_t c = d.size();
    if (bus.SMBusWriteBytes(dev, reg, c, (uint8_t*)d.c_str())) {
      f->finish();
      return;
    }
  }
  else {
    // byte
    uint8_t b = f->arg(1)->intValue();
    if (bus.SMBusWriteByte(dev, reg, b)) {
      f->finish();
      return;
    }
  }
  // no success
  f->finish(new ErrorValue(TextError::err("i2c smbus write error")));
}


static const BuiltinMemberDescriptor i2cDeviceMembers[] = {
  { "rawread", executable|error|text|numeric, rawread_numargs, rawread_args, &rawread_func },
  { "smbusread", executable|error|text|numeric, smbusread_numargs, smbusread_args, &smbusread_func },
  { "rawwrite", executable|error|text|numeric, rawwrite_numargs, rawwrite_args, &rawwrite_func },
  { "smbuswrite", executable|error|text|numeric, smbuswrite_numargs, smbuswrite_args, &smbuswrite_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedi2cDeviceFunctionLookupP = NULL;

I2CDeviceObj::I2CDeviceObj(I2CDevicePtr aI2CDevice) :
  mI2CDevice(aI2CDevice)
{
  registerSharedLookup(sharedi2cDeviceFunctionLookupP, i2cDeviceMembers);
}


// i2cdevice(busnumber, devicespec)
static const BuiltInArgDesc i2cdevice_args[] = { { numeric }, { text } };
static const size_t i2cdevice_numargs = sizeof(i2cdevice_args)/sizeof(BuiltInArgDesc);
static void i2cdevice_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<2) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  I2CDevicePtr dev = I2CManager::sharedManager().getDevice(f->arg(0)->intValue(), f->arg(1)->stringValue().c_str());
  if (dev) {
    f->finish(dev->representingScriptObj());
  }
  else {
    f->finish(new ErrorValue(ScriptError::NotFound, "unknown i2c device"));
  }
}


static const BuiltinMemberDescriptor i2cGlobals[] = {
  { "i2cdevice", executable|null, i2cdevice_numargs, i2cdevice_args, &i2cdevice_func },
  { NULL } // terminator
};

I2CLookup::I2CLookup() :
  inherited(i2cGlobals)
{
}

#endif // ENABLE_I2C_SCRIPT_FUNCS
