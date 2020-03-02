//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  Based on code by Miguel Balboa (circuitito.com), Jan, 2012,
//  "RFID.h - Library to use ARDUINO RFID MODULE KIT 13.56 MHZ WITH TAGS SPI W AND R BY COOQROBOT."
//  which was based on code by Dr.Leong (WWW.B2CQSHOP.COM)
//  and was modified by Paul Kourany to run on Spark Core with added support for Software SPI, Mar, 2014.
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


#ifndef rfid_hpp
#define rfid_hpp

#include <stdio.h>

#include "digitalio.hpp"
#include "spi.hpp"


using namespace std;

namespace p44 {


  class RFID522 : public P44Obj
  {

  public:
    /// @param aReaderIndex index of reader to select, RFID522::Deselect = none selected
    typedef boost::function<void (int aReaderIndex)> SelectCB;
    static const int Deselect = -1; ///< pseudo-index to deselect all readers

  private:

    SPIDevicePtr spidev;
    int readerIndex;
    SelectCB readerSelectFunc;

  public:

    /// create RFID522 reader instance
    /// @param aSPIGenericDev a generic SPI device for the bus this reader is connected to
    /// @param aReaderIndex the selection address of this reader
    /// @param aReaderSelectFunc will be called to select this particular reader by aSelectAddress
    RFID522(SPIDevicePtr aSPIGenericDev, int aReaderIndex, SelectCB aReaderSelectFunc);

    /// get this reader's index
    int getReaderIndex() { return readerIndex; };

    /// write single byte to a register
    void writeReg(uint8_t aReg, uint8_t aVal);
    /// write multiple bytes to FIFO data register
    void writeFIFO(const uint8_t* aData, size_t aNumBytes);
    /// read single byte from a register
    uint8_t readReg(uint8_t aReg);
    /// read multiple bytes from FIFO data register
    void readFIFO(uint8_t* aData, size_t aNumBytes);
    /// set some bits in a register (read-modify-write)
    void setRegBits(uint8_t reg, uint8_t mask);
    /// clear some bits in a register (read-modify-write)
    void clrRegBits(uint8_t reg, uint8_t mask);

    /// execute PICC command
    uint8_t execPICCCmd(uint8_t aCmd, uint8_t *aTxDataP, uint8_t aTxBytes, uint8_t *aRxDataP, uint16_t &aRxBits);

    bool isCard();
    bool readCardSerial();

    void init();
    void reset();
    void antennaOn(void);
    void calculateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData);
    uint8_t MFRC522Request(uint8_t reqMode, uint8_t *TagType);
    uint8_t anticoll(uint8_t *serNum);
    uint8_t auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum);
    uint8_t read(uint8_t blockAddr, uint8_t *recvData);
    uint8_t write(uint8_t blockAddr, uint8_t *writeData);
    void halt();

    uint8_t serNum[5];  // the serial number.
    uint8_t AserNum[5]; // the serial number of the current section.
  };
  typedef boost::intrusive_ptr<RFID522> RFID522Ptr;


} // namespace p44


#endif /* rfid_hpp */
