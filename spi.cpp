//
//  Copyright (c) 2016-2021 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


#include "spi.hpp"

// locally disable actual functionality on unsupported platforms (but still provide console output dummies)
#if !defined(DISABLE_SPI) && (defined(__APPLE__) || P44_BUILD_DIGI) && !P44_BUILD_RPI && !P44_BUILD_RB && !P44_BUILD_OW
  #define DISABLE_SPI 1
#endif

#if !DISABLE_SPI
extern "C" {
  #include <sys/ioctl.h>
  #include <linux/spi/spidev.h>
}
#else
  #warning "No SPI supported on this platform - just showing calls in focus debug output"
#endif

#if !defined(ESP_PLATFORM)
  #if ENABLE_APPLICATION_SUPPORT
    #include "application.hpp" // we need it for user level, syscmd is only allowed with userlevel>=2
  #endif
#endif

using namespace p44;
#if ENABLE_SPI_SCRIPT_FUNCS
using namespace P44Script;
#endif


#define SPI_MAX_SPEED_HZ 100000 // 1MHz seems reasonable, faster sometimes does not work ok e.g. on RPi

// MARK: - I2C Manager

static SPIManager *sharedSPIManager = NULL;


SPIManager::SPIManager()
{
}

SPIManager::~SPIManager()
{
}


SPIManager &SPIManager::sharedManager()
{
  if (!sharedSPIManager) {
    sharedSPIManager = new SPIManager();
  }
  return *sharedSPIManager;
}




SPIDevicePtr SPIManager::getDevice(int aBusNumber, const char *aDeviceID)
{
  // find or create bus
  SPIBusMap::iterator pos = busMap.find(aBusNumber);
  SPIBusPtr bus;
  if (pos!=busMap.end()) {
    bus = pos->second;
  }
  else {
    // bus does not exist yet, create it
    bus = SPIBusPtr(new SPIBus(aBusNumber));
    busMap[aBusNumber] = bus;
  }
  // dissect device ID into type and busAddress
  // - type string
  //   consists of Chip name plus optional options suffix. Like "MCP23S17" or "MCP23S17-xy" (xy=options)
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
  SPIDevicePtr dev = bus->getDevice(s.c_str());
  if (!dev) {
    // create device from typestring
    if (typeString=="MCP23S17")
      dev = SPIDevicePtr(new MCP23S17(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="MCP3008")
      dev = SPIDevicePtr(new MCP3008(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="MCP3002")
      dev = SPIDevicePtr(new MCP3002(deviceAddress, bus.get(), deviceOptions.c_str()));
    else if (typeString=="generic")
      dev = SPIDevicePtr(new SPIDevice(deviceAddress, bus.get(), deviceOptions.c_str()));
    // TODO: add more device types
    // Register new device
    if (dev) {
      bus->registerDevice(dev);
    }
  }
  return dev;
}


// MARK: - SPIBus


SPIBus::SPIBus(int aBusNumber) :
  busFD(-1),
  busNumber(aBusNumber),
  lastSpiMode(0xFF) // invalid mode -> force setting it on first use
{
}


SPIBus::~SPIBus()
{
  closeBus();
}


void SPIBus::registerDevice(SPIDevicePtr aDevice)
{
  deviceMap[aDevice->deviceID()] = aDevice;
}


SPIDevicePtr SPIBus::getDevice(const char *aDeviceID)
{
  SPIDeviceMap::iterator pos = deviceMap.find(aDeviceID);
  if (pos!=deviceMap.end())
    return pos->second;
  return SPIDevicePtr();
}



// Note that "cs_change" functionality is described in a misleading way
// in other places, e.g. in spidev.h - suggesting cs_change must be SET
// to make CS go inactive at the end of the transfer.
// However, truth is that cs_change just INVERTS the normal way of operation:
// - between multiple transfers, CS is normally kept active -> cs_change
//   causes CS to go high quickly between transfers
// - after the last transfer, CS is normally made inactive -> cs_change
//   causes CS to REMAIN ACTIVE. This apparently had no effect with
//   spi-bcm2708 (old RPi SPI driver) but did completely mess up
//   SPI communication with spi-bcm2835 (one single never ending transaction).
//
// Bottom line: under normal circumstances (i.e. one SPI_IOC_MESSAGE ioctl call
// per transaction), cs_change must not be set to do the right thing.

// The following is from include/linux/spi/spi.h:
//
//  All SPI transfers start with the relevant chipselect active.  Normally
//  it stays selected until after the last transfer in a message.  Drivers
//  can affect the chipselect signal using cs_change.
//
//  (i) If the transfer isn't the last one in the message, this flag is
//  used to make the chipselect briefly go inactive in the middle of the
//  message.  Toggling chipselect in this way may be needed to terminate
//  a chip command, letting a single spi_message perform all of group of
//  chip transactions together.
//
//  (ii) When the transfer is the last one in the message, the chip may
//  stay selected until the next transfer.  On multi-device SPI busses
//  with nothing blocking messages going to other devices, this is just
//  a performance hint; starting a message to another device deselects
//  this one.  But in other cases, this can be used to ensure correctness.
//  Some devices need protocol transactions to be built from a series of
//  spi_message submissions, where the content of one message is determined
//  by the results of previous messages and where the whole transaction
//  ends when the chipselect goes inactive.



int SPIBus::spidev_write_read(
  SPIDevice *aDeviceP,
  unsigned int num_out_bytes,
  uint8_t *out_buffer,
  unsigned int num_in_bytes,
  uint8_t *in_buffer,
  bool writeWrite,
  bool fullDuplex
)
{
  #if !DISABLE_SPI
  struct spi_ioc_transfer mesg[2];
  uint8_t num_tr = 0;
  int ret;

  // init all fields of the struct, important for spi_bcm2835 driver (not relevant for spi_bcm2708)
  for (int i=0; i<2; ++i) {
    memset(&mesg[i], 0, sizeof (spi_ioc_transfer));
    mesg[i].bits_per_word = 0; // means 8 -> From SPI_IOC_WR_BITS_PER_WORD docs: "The value zero signifies eight bits"
    mesg[i].speed_hz = aDeviceP->speedHz; // current speed
  }
  // prepare output transfer, if any data provided
  if((out_buffer != NULL) && (num_out_bytes != 0)) {
    mesg[num_tr].tx_buf = (unsigned long)out_buffer;
    mesg[num_tr].len = num_out_bytes;
    if (fullDuplex) {
      mesg[num_tr].rx_buf = (unsigned long)in_buffer;
      if (num_in_bytes<num_out_bytes) return -1; // must be at least same number of input bytes as output
    }
    else {
      mesg[num_tr].rx_buf = (unsigned long)NULL;
    }
    num_tr++;
  }
  // prepare input (or second write) transfer, if buffer provided
  if(!fullDuplex && (in_buffer != NULL) && (num_in_bytes != 0)) {
    if (writeWrite) {
      mesg[num_tr].tx_buf = (unsigned long)in_buffer;
      mesg[num_tr].rx_buf = (unsigned long)NULL;
    }
    else {
      mesg[num_tr].tx_buf = (unsigned long)NULL;
      mesg[num_tr].rx_buf = (unsigned long)in_buffer;
    }
    mesg[num_tr].len = num_in_bytes;
    num_tr++;
  }
  // execute
  if(num_tr > 0) {
    ret = ioctl(busFD, SPI_IOC_MESSAGE(num_tr), mesg);
    if(ret == 1) {
      return 1;
    }
  }
  #else
  DBGFOCUSLOG("SPI_IOC_MESSAGE writing %d bytes and reading %d bytes", num_out_bytes, num_in_bytes);
  #endif
  return 0;
}


#define SPI_RD(dev) (((dev&0x7F)<<1) + 0x01)
#define SPI_WR(dev) ((dev&0x7F)<<1)

bool SPIBus::SPIRegReadByte(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t &aByte)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  uint8_t msg[2];
  msg[0] = SPI_RD(aDeviceP->deviceAddress);
  msg[1] = aRegister;
  uint8_t ans = 0;
  int res = spidev_write_read(aDeviceP, 2, msg, 1, &ans);
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("SPIRegReadByte(devaddr=0x%02X, reg=0x%02X) = %d / 0x%02X (res=%d)", aDeviceP->deviceAddress, aRegister, ans, ans, res);
  if (res<0) return false;
  aByte = (uint8_t)ans;
  return true;
}


bool SPIBus::SPIRegReadWord(SPIDevice *aDeviceP, uint8_t aRegister, uint16_t &aWord)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  uint8_t msg[2];
  msg[0] = SPI_RD(aDeviceP->deviceAddress);
  msg[1] = aRegister;
  uint16_t ans = 0;
  int res = spidev_write_read(aDeviceP, 2, msg, 2, (uint8_t *)&ans);
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("SPIRegReadWord(devaddr=0x%02X, reg=0x%02X) = %d / 0x%02X (res=%d)", aDeviceP->deviceAddress, aRegister, ans, ans, res);
  if (res<0) return false;
  aWord = ans;
  return true;
}


bool SPIBus::SPIRegReadBytes(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, uint8_t *aDataP)
{
  if (!accessDevice(aDeviceP)) return false; // cannot read
  uint8_t msg[2];
  msg[0] = SPI_RD(aDeviceP->deviceAddress);
  msg[1] = aRegister;
  int res = spidev_write_read(aDeviceP, 2, msg, aCount, aDataP);
  // read is shown only in real Debug log, because button polling creates lots of accesses
  DBGFOCUSLOG("SPIRegReadBytes(devaddr=0x%02X, reg=0x%02X), %d bytes read (res=%d)", aDeviceP->deviceAddress, aRegister, aCount, res);
  return (res>=0);
}


bool SPIBus::SPIRegWriteByte(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t aByte)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  uint8_t msg[3];
  msg[0] = SPI_WR(aDeviceP->deviceAddress);
  msg[1] = aRegister;
  msg[2] = aByte;
  int res = spidev_write_read(aDeviceP, 3, msg, 0, NULL);
  FOCUSLOG("SPIRegWriteByte(devaddr=0x%02X, reg=0x%02X, byte=0x%02X), res=%d", aDeviceP->deviceAddress, aRegister, aByte, res);
  return (res>=0);
}


bool SPIBus::SPIRegWriteWord(SPIDevice *aDeviceP, uint8_t aRegister, uint16_t aWord)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  uint8_t msg[4];
  msg[0] = SPI_WR(aDeviceP->deviceAddress);
  msg[1] = aRegister;
  *((uint16_t *)&(msg[2])) = aWord;
  int res = spidev_write_read(aDeviceP, 4, msg, 0, NULL);
  FOCUSLOG("SPIRegWriteWord(devaddr=0x%02X, reg=0x%02X, word=0x%04X), res=%d", aDeviceP->deviceAddress, aRegister, aWord, res);
  return (res>=0);
}


bool SPIBus::SPIRegWriteBytes(SPIDevice *aDeviceP, uint8_t aRegister, uint8_t aCount, const uint8_t *aDataP)
{
  if (!accessDevice(aDeviceP)) return false; // cannot write
  uint8_t msg[2];
  msg[0] = SPI_WR(aDeviceP->deviceAddress);
  msg[1] = aRegister;
  int res = spidev_write_read(aDeviceP, 2, msg, aCount, (uint8_t *)aDataP, true);
  FOCUSLOG("SPIRegWriteBytes(devaddr=0x%02X, reg=0x%02X), %d bytes written (res=%d)", aDeviceP->deviceAddress, aRegister, aCount, res);
  return (res>=0);
}


bool SPIBus::SPIRawWriteRead(SPIDevice *aDeviceP, unsigned int aOutSz, uint8_t *aOutP, unsigned int aInSz, uint8_t *aInP, bool aFullDuplex)
{
  if (!accessDevice(aDeviceP)) return false; // cannot access
  int res = spidev_write_read(aDeviceP, aOutSz, aOutP, aInSz, aInP, false, aFullDuplex);
  // shown only in real Debug log, because polling creates lots of accesses
  DBGFOCUSLOG("SPIRawWriteRead(devaddr=0x%02X), %d bytes written, %d bytes read (res=%d)", aDeviceP->deviceAddress, aOutSz, aFullDuplex ? aOutSz : aInSz, res);
  return (res>=0);
}



bool SPIBus::accessDevice(SPIDevice *aDeviceP)
{
  if (!accessBus())
    return false;
  if (aDeviceP->spimode == lastSpiMode)
    return true; // already prepared to access that device
  // set the SPI mode
  #if !DISABLE_SPI
  if (ioctl(busFD, SPI_IOC_WR_MODE, &aDeviceP->spimode) < 0) {
    LOG(LOG_ERR, "Error: Cannot SPI_IOC_WR_MODE for device '%s' on bus %d", aDeviceP->deviceID().c_str(), busNumber);
    lastSpiMode = 0; // assume default mode
    return false;
  }
  #endif
  FOCUSLOG("ioctl(busFD, SPI_IOC_WR_MODE, 0x%02X)", aDeviceP->spimode);
  // remember
  lastSpiMode = aDeviceP->spimode;
  return true; // ok
}


bool SPIBus::accessBus()
{
  if (busFD>=0)
    return true; // already open
  // need to open
  lastSpiMode = 0; // assume default mode
  string busDevName = string_format("/dev/spidev%d.%d", busNumber/10, busNumber%10);
  #if !DISABLE_SPI
  busFD = open(busDevName.c_str(), O_RDWR);
  if (busFD<0) {
    LOG(LOG_ERR, "Error: Cannot open SPI device '%s'",busDevName.c_str());
    return false;
  }
  // - limit max speed
  uint32_t speed = SPI_MAX_SPEED_HZ;
  if (ioctl(busFD, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
    LOG(LOG_ERR, "Error: Cannot SPI_IOC_WR_MAX_SPEED_HZ for bus %d", busNumber);
    return false;
  }
  // - at this time, we only support 8-bit words
  uint8_t bpw = 0; // means 8 -> From SPI_IOC_WR_BITS_PER_WORD docs: "The value zero signifies eight bits"
  if (ioctl(busFD, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0) {
    LOG(LOG_ERR, "Error: Cannot SPI_IOC_WR_BITS_PER_WORD for bus %d", busNumber);
    return false;
  }
  #else
  busFD = 1; // dummy, signalling open
  #endif
  FOCUSLOG("open(\"%s\", O_RDWR) = %d", busDevName.c_str(), busFD);
  return true;
}



void SPIBus::closeBus()
{
  if (busFD>=0) {
    #ifndef __APPLE__
    close(busFD);
    #endif
    busFD = -1;
  }
}



// MARK: - SPIDevice


//  #define SPI_CPHA		0x01
//  #define SPI_CPOL		0x02
//
//  #define SPI_MODE_0		(0|0)
//  #define SPI_MODE_1		(0|SPI_CPHA)
//  #define SPI_MODE_2		(SPI_CPOL|0)
//  #define SPI_MODE_3		(SPI_CPOL|SPI_CPHA)
//
//  #define SPI_CS_HIGH		0x04
//  #define SPI_LSB_FIRST		0x08
//  #define SPI_3WIRE		0x10
//  #define SPI_LOOP		0x20
//  #define SPI_NO_CS		0x40
//  #define SPI_READY		0x80


SPIDevice::SPIDevice(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions)
{
  spibus = aBusP;
  deviceAddress = aDeviceAddress;
  // evaluate SPI mode options
  spimode = 0;
  speedHz = SPI_MAX_SPEED_HZ; // use bus' default max speed
  #if !DISABLE_SPI
  if (strchr(aDeviceOptions, 'H')) spimode |= SPI_CPHA; // inverted phase (compared to original microwire SPI)
  if (strchr(aDeviceOptions, 'P')) spimode |= SPI_CPOL; // inverted polarity (compared to original microwire SPI)
  if (strchr(aDeviceOptions, 'C')) spimode |= SPI_CS_HIGH; // chip select high
  if (strchr(aDeviceOptions, 'N')) spimode |= SPI_NO_CS; // no chip select
  if (strchr(aDeviceOptions, '3')) spimode |= SPI_3WIRE; // 3 wire
  if (strchr(aDeviceOptions, 'R')) spimode |= SPI_READY; // slave pulls low to pause
  // reduced speeds
  if (strchr(aDeviceOptions, 'S')) speedHz /= 10; // slow speed - 1/10 of normal
  if (strchr(aDeviceOptions, 's')) speedHz /= 100; // very slow speed - 1/100 of normal
  #endif
}


string SPIDevice::deviceID()
{
  return string_format("%s@%02X", deviceType(), deviceAddress);
}




bool SPIDevice::isKindOf(const char *aDeviceType)
{
  return (strcmp(deviceType(),aDeviceType)==0);
}


bool SPIDevice::SPIRawWriteRead(unsigned int aOutSz, uint8_t *aOutP, unsigned int aInSz, uint8_t *aInP, bool aFullDuplex)
{
  return spibus->SPIRawWriteRead(this, aOutSz, aOutP, aInSz, aInP, aFullDuplex);
}




// MARK: - SPIBitPortDevice


SPIBitPortDevice::SPIBitPortDevice(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions),
  outputEnableMask(0),
  pinStateMask(0),
  pullUpMask(0)
{
}



bool SPIBitPortDevice::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


bool SPIBitPortDevice::getBitState(int aBitNo)
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


void SPIBitPortDevice::setBitState(int aBitNo, bool aState)
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


void SPIBitPortDevice::setAsOutput(int aBitNo, bool aOutput, bool aInitialState, bool aPullUp)
{
  uint32_t bitMask = 1<<aBitNo;
  // Input or output
  if (aOutput)
    outputEnableMask |= bitMask;
  else {
    outputEnableMask &= ~bitMask;
  }
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



// MARK: - MCP23S17


MCP23S17::MCP23S17(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // initially, IOCON==0 -> IOCON.BANK==0 -> A/B interleaved register access
  // enable hardware addressing if selected
  if (strchr(aDeviceOptions, 'A')) {
    spibus->SPIRegWriteByte(this, 0x0A, 0x08); // set HAEN (hardware address enable) in IOCON
  }
  // make sure we have all inputs
  updateDirection(0); // port 0
  updateDirection(8); // port 1
  // reset polarity inverter
  spibus->SPIRegWriteByte(this, 0x02, 0); // reset polarity inversion A
  spibus->SPIRegWriteByte(this, 0x03, 0); // reset polarity inversion B
}


bool MCP23S17::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


void MCP23S17::updateInputState(int aForBitNo)
{
  if (aForBitNo>15) return;
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  uint8_t data;
  spibus->SPIRegReadByte(this, port+0x12, data); // get current port state from GPIO reg 0x12/0x13
  pinStateMask = (pinStateMask & ~(((uint32_t)0xFF) << shift)) | ((uint32_t)data << shift);
}


void MCP23S17::updateOutputs(int aForBitNo)
{
  if (aForBitNo>15) return;
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  spibus->SPIRegWriteByte(this, port+0x14, (outputStateMask >> shift) & 0xFF); // write to output latch (OLAT) A/B reg 0x14/0x15
}



void MCP23S17::updateDirection(int aForBitNo)
{
  if (aForBitNo>15) return;
  updateOutputs(aForBitNo); // make sure output register has the correct value
  uint8_t port = aForBitNo >> 3; // calculate port No
  uint8_t shift = 8*port;
  uint8_t data;
  // configure pullups
  data = (pullUpMask >> shift) & 0xFF; // MCP23S17 GPPU register has 1 for pullup enabled
  spibus->SPIRegWriteByte(this, port+0x0C, data); // set pullup enable flags in GPPU reg C or D
  // configure direction
  data = ~((outputEnableMask >> shift) & 0xFF); // MCP23S17 IODIR register has 1 for inputs, 0 for outputs
  spibus->SPIRegWriteByte(this, port+0x00, data); // set input enable flags in IODIR reg 0 or 1
}



// MARK: - SPIpin


/// create SPI based digital input or output pin (or use an analog pin as digital I/O)
SPIPin::SPIPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, bool aInitialState, Tristate aPull) :
  output(false),
  lastSetState(false)
{
  pinNumber = aPinNumber;
  output = aOutput;
  SPIDevicePtr dev = SPIManager::sharedManager().getDevice(aBusNumber, aDeviceId);
  bitPortDevice = boost::dynamic_pointer_cast<SPIBitPortDevice>(dev);
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
bool SPIPin::getState()
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
void SPIPin::setState(bool aState)
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



// MARK: - SPIAnalogPortDevice


SPIAnalogPortDevice::SPIAnalogPortDevice(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
}



bool SPIAnalogPortDevice::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}



// MARK: - MCP3008

MCP3008::MCP3008(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // currently no device options
//  int b = atoi(aDeviceOptions);
}


bool MCP3008::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


double MCP3008::getPinValue(int aPinNo)
{
  // MCP3008 needs to transfer 3 bytes in and out for one conversion
  uint8_t out[3];
  uint8_t in[3];
  uint16_t raw = 0;
  // - first byte is 7 zero dummy bits plus LSB==1==start bit
  out[0] = 0x01;
  // - second byte is 4 bit channel selection/differential vs single, plus 4 bit dummy
  //   Bit 7     Bit 6    Bit 5    Bit 4
  //   D/S       CHSEL3   CHSEL2   CHSEL1
  //   0=Diff
  //   1=Single
  // - we invert the D/S bit to have 1:1 PinNo->Single ended channel assignments (0..7).
  //   PinNo 8..15 then represent the differential modes, see data sheet.
  out[1] = (aPinNo^0x08)<<4;
  // - third byte is dummy
  out[2] = 0;
  if (spibus->SPIRawWriteRead(this, 3, out, 3, in, true)) {
    // A/D output data are 10 LSB of data read back
    raw = ((uint16_t)(in[1] & 0x03)<<8) + in[2];
  }
  // return raw value (no physical unit at this level known, and no scaling or offset either)
  return raw;
}


bool MCP3008::getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution)
{
  // as we don't know what will be connected to the inputs, we return raw A/D value.
  aMin = 0;
  aMax = 1024;
  aResolution = 1;
  return true;
}


// MARK: - MCP3002

MCP3002::MCP3002(uint8_t aDeviceAddress, SPIBus *aBusP, const char *aDeviceOptions) :
  inherited(aDeviceAddress, aBusP, aDeviceOptions)
{
  // currently no device options
  //  int b = atoi(aDeviceOptions);
}


bool MCP3002::isKindOf(const char *aDeviceType)
{
  if (strcmp(deviceType(),aDeviceType)==0)
    return true;
  else
    return inherited::isKindOf(aDeviceType);
}


double MCP3002::getPinValue(int aPinNo)
{
  // MCP3002 needs to transfer 3 bytes in and out for one conversion
  // Note: with a correctly working SPI (not the case in MT7688),
  //   2 bytes would be sufficient. But as the first returned byte
  //   is flawed in MT7688 (see @wdu's comment in Onion forum:
  //   "In full duplex, I have observed errors in the second bit (always
  //   1 or 0, don't remember exactly), depending on the state of the
  //   first bit. This makes the first transmitted byte unreliable."),
  //   this implementation shifts the bit such that first returned byte
  //   can be discarded entirely.
  uint8_t out[3];
  uint8_t in[3];
  uint16_t raw = 0;
  // - first byte is 4 zero dummy bits, then 1==start bit, then:
  //   Bit 2     Bit 1    Bit 0
  //   D/S       CHSEL    MSBFirst
  // - we invert the D/S bit to have 1:1 PinNo->Single ended channel assignments (0,1).
  //   PinNo 2,3 then represent the differential modes, see data sheet.
  out[0] =
    0x08 | // start bit
    (((aPinNo&0x03)^0x02)<<1) | // channel and mode selection
    0x01; // MSB first
  // - second and third byte is dummy
  out[1] = 0;
  out[2] = 0;
  DBGFOCUSLOG("MCP3002 write: 0x%02X, 0x%02X, 0x%02X", out[0], out[1], out[2]);
  if (spibus->SPIRawWriteRead(this, 3, out, 3, in, true)) {
    // first byte returned is broken anyway on MT7688, no data there
    // second byte contains a 0 in Bit7, Bit6..0 = Bit9..3 of result
    // third byte contains Bit7..5 = Bit2..0 of result, rest is dummy
    DBGFOCUSLOG("MCP3002 read: 0x%02X, 0x%02X, 0x%02X", in[0], in[1], in[2]);
    raw = ((uint16_t)(in[1] & 0x7F)<<3) + (in[2]>>5);
  }
  // return raw value (no physical unit at this level known, and no scaling or offset either)
  return raw;
}


bool MCP3002::getPinRange(int aPinNo, double &aMin, double &aMax, double &aResolution)
{
  // as we don't know what will be connected to the inputs, we return raw A/D value.
  aMin = 0;
  aMax = 1024;
  aResolution = 1;
  return true;
}





// MARK: - AnalogSPIpin


/// create spi based digital input or output pin
AnalogSPIPin::AnalogSPIPin(int aBusNumber, const char *aDeviceId, int aPinNumber, bool aOutput, double aInitialValue) :
  output(false)
{
  pinNumber = aPinNumber;
  output = aOutput;
  SPIDevicePtr dev = SPIManager::sharedManager().getDevice(aBusNumber, aDeviceId);
  analogPortDevice = boost::dynamic_pointer_cast<SPIAnalogPortDevice>(dev);
  if (analogPortDevice && output) {
    analogPortDevice->setPinValue(pinNumber, aInitialValue);
  }
}


/// get state of pin
/// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
double AnalogSPIPin::getValue()
{
  if (analogPortDevice) {
    return analogPortDevice->getPinValue(pinNumber);
  }
  return 0;
}


/// set state of pin (NOP for inputs)
/// @param aState new state to set output to
void AnalogSPIPin::setValue(double aValue)
{
  if (analogPortDevice && output) {
    analogPortDevice->setPinValue(pinNumber, aValue);
  }
}


bool AnalogSPIPin::getRange(double &aMin, double &aMax, double &aResolution)
{
  if (analogPortDevice) {
    return analogPortDevice->getPinRange(pinNumber, aMin, aMax, aResolution);
  }
  return false;
}


#if ENABLE_SPI_SCRIPT_FUNCS

// MARK: - SPI scripting

SPIDeviceObjPtr SPIDevice::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new SPIDeviceObj(this);
  }
  return mRepresentingObj;
}


// regread(reg [,type, [, count])
static const BuiltInArgDesc regread_args[] = { { numeric }, { text|optionalarg } };
static const size_t regread_numargs = sizeof(regread_args)/sizeof(BuiltInArgDesc);
static void regread_func(BuiltinFunctionContextPtr f)
{
  SPIDeviceObj* o = dynamic_cast<SPIDeviceObj*>(f->thisObj().get());
  assert(o);
  SPIDevice* dev = o->spidevice().get();
  SPIBus& bus = dev->getBus();
  uint8_t reg = f->arg(0)->intValue();
  string ty;
  if (f->arg(1)->defined()) ty = f->arg(1)->stringValue();
  if (ty=="word") {
    // 16 bit word
    uint16_t w;
    if (bus.SPIRegReadWord(dev, reg, w)) {
      f->finish(new NumericValue(w));
      return;
    }
  }
  else if (ty=="bytes") {
    // a number of bytes
    uint8_t count = 1;
    if (f->arg(2)->defined()) count = f->arg(2)->intValue();
    uint8_t buf[256];
    if (bus.SPIRegReadBytes(dev, reg, count, buf)) {
      string data((char *)buf, (size_t)count);
      f->finish(new StringValue(data));
      return;
    }
  }
  else {
    // byte
    uint8_t b;
    if (bus.SPIRegReadByte(dev, reg, b)) {
      f->finish(new NumericValue(b));
      return;
    }
  }
  // no success
  f->finish(new ErrorValue(TextError::err("i2c smbus read error")));
}


// regwrite(reg, value [,type])
static const BuiltInArgDesc regwrite_args[] = { { numeric }, { text|numeric }, { text|optionalarg } };
static const size_t regwrite_numargs = sizeof(regwrite_args)/sizeof(BuiltInArgDesc);
static void regwrite_func(BuiltinFunctionContextPtr f)
{
  SPIDeviceObj* o = dynamic_cast<SPIDeviceObj*>(f->thisObj().get());
  assert(o);
  SPIDevice* dev = o->spidevice().get();
  SPIBus& bus = dev->getBus();
  uint8_t reg = f->arg(0)->intValue();
  string ty;
  if (f->arg(2)->defined()) ty = f->arg(2)->stringValue();
  if (ty=="word") {
    // 16 bit word
    uint16_t w = f->arg(1)->intValue();
    if (bus.SPIRegWriteWord(dev, reg, w)) {
      f->finish();
      return;
    }
  }
  else if (ty=="bytes") {
    // number of bytes is determined by string length
    string d = f->arg(1)->stringValue();
    uint8_t c = d.size();
    if (bus.SPIRegWriteBytes(dev, reg, c, (uint8_t*)d.c_str())) {
      f->finish();
      return;
    }
  }
  else {
    // byte
    uint8_t b = f->arg(1)->intValue();
    if (bus.SPIRegWriteByte(dev, reg, b)) {
      f->finish();
      return;
    }
  }
  // no success
  f->finish(new ErrorValue(TextError::err("i2c smbus write error")));
}



// writeread(bytes_to_write [, num_bytes_to_read [, fullduplex]])
static const BuiltInArgDesc writeread_args[] = { { text }, { numeric }, { numeric|optionalarg } };
static const size_t writeread_numargs = sizeof(writeread_args)/sizeof(BuiltInArgDesc);
static void writeread_func(BuiltinFunctionContextPtr f)
{
  SPIDeviceObj* o = dynamic_cast<SPIDeviceObj*>(f->thisObj().get());
  assert(o);
  SPIDevice* dev = o->spidevice().get();
  SPIBus& bus = dev->getBus();
  string data = f->arg(0)->stringValue();
  unsigned int insz = 0; // no reading
  uint8_t *inP = NULL;
  if (f->arg(1)->defined()) {
    insz = f->arg(1)->intValue();
    if (insz>0) inP = new uint8_t[insz];
  }
  if (bus.SPIRawWriteRead(dev, (unsigned int)data.size(), (uint8_t *)data.c_str(), insz, inP, f->arg(2)->boolValue())) {
    if (inP) {
      f->finish(new StringValue(string((char *)inP, (size_t)insz)));
      delete inP;
    }
    else {
      // nothing read
      f->finish();
    }
    return;
  }
  if (inP) delete inP;
  // no success
  f->finish(new ErrorValue(TextError::err("i2c raw write error")));
}


static const BuiltinMemberDescriptor spiDeviceMembers[] = {
  { "regread", executable|error|text|numeric, regread_numargs, regread_args, &regread_func },
  { "regwrite", executable|error|text|numeric, regwrite_numargs, regwrite_args, &regwrite_func },
  { "writeread", executable|error|text, writeread_numargs, writeread_args, &writeread_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedspiDeviceFunctionLookupP = NULL;

SPIDeviceObj::SPIDeviceObj(SPIDevicePtr aSPIDevice) :
  mSPIDevice(aSPIDevice)
{
  registerSharedLookup(sharedspiDeviceFunctionLookupP, spiDeviceMembers);
}


// spidevice(busnumber, devicespec)
static const BuiltInArgDesc spidevice_args[] = { { numeric }, { text } };
static const size_t spidevice_numargs = sizeof(spidevice_args)/sizeof(BuiltInArgDesc);
static void spidevice_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<2) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  SPIDevicePtr dev = SPIManager::sharedManager().getDevice(f->arg(0)->intValue(), f->arg(1)->stringValue().c_str());
  if (dev) {
    f->finish(dev->representingScriptObj());
  }
  else {
    f->finish(new ErrorValue(ScriptError::NotFound, "unknown spi device"));
  }
}


static const BuiltinMemberDescriptor spiGlobals[] = {
  { "spidevice", executable|null, spidevice_numargs, spidevice_args, &spidevice_func },
  { NULL } // terminator
};

SPILookup::SPILookup() :
  inherited(spiGlobals)
{
}

#endif // ENABLE_SPI_SCRIPT_FUNCS
