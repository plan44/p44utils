//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "p44utils_main.hpp"

#ifndef ENABLE_RFID
  #define ENABLE_RFID 1
#endif

#if ENABLE_RFID

#include <stdio.h>

#include "digitalio.hpp"
#include "spi.hpp"

#define IRQ_WATCHDOG 0

using namespace std;

namespace p44 {

  class RFIDError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Timeout, ///< timeout
      ChipErr, ///< chip error
      UnknownCmd, ///< unknown command
      BadAnswer, ///< bad answer (e.g. wrong number of bits, checksum error)
      IRQTimeout, ///< IRQ timeout (irqHandler() not called soon enough)
    } ErrorCodes;

    static const char *domain() { return "RFID"; }
    virtual const char *getErrorDomain() const { return RFIDError::domain(); };
    RFIDError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };


  class RFID522 : public P44Obj
  {

  public:
    /// @param aReaderIndex index of reader to select, RFID522::Deselect = none selected
    typedef boost::function<void (int aReaderIndex)> SelectCB;
    static const int Deselect = -1; ///< pseudo-index to deselect all readers

    /// execPICCCmd result callback
    typedef boost::function<void (ErrorPtr aErr, uint16_t aResultBits, const string aResult)> ExecResultCB;

  private:

    SPIDevicePtr mSpiDev;
    int mReaderIndex;
    SelectCB mReaderSelectFunc;

    // execPICCCmd state
    ExecResultCB mExecResultCB;
    uint8_t mCmd; ///< the command being executed
    uint8_t mIrqEn; ///< enabled IRQs
    uint8_t mWaitIrq; ///< IRQs we are waiting for to terminate execPICCCmd
    #if IRQ_WATCHDOG
    MLTicket mIrqWatchdog;
    #else
    MLMicroSeconds mCmdStart;
    #endif

  public:

    /// create RFID522 reader instance
    /// @param aSPIGenericDev a generic SPI device for the bus this reader is connected to
    /// @param aReaderIndex the selection address of this reader
    /// @param aReaderSelectFunc will be called to select this particular reader by aSelectAddress
    RFID522(SPIDevicePtr aSPIGenericDev, int aReaderIndex, SelectCB aReaderSelectFunc);
    virtual ~RFID522();

    /// get this reader's index
    int getReaderIndex() { return mReaderIndex; };

    void init();

    void reset();

    /// must be called when external IRQ line (possibly common to multiple readers) gets active
    /// @return true if reader still waits for an interrupt
    bool irqHandler();

    /// check for Type A (MiFare) card
    /// @param aStatusCB returns ok when there is a card, Error otherwise
    /// @param aWait - repeat command until there is an answer from a card (ok or error), do not timeout
    void probeTypeA(StatusCB aStatusCB, bool aWait = false);

    /// run anticollision procedure and return card nUID
    /// @param aResultCB returns card nUID as response or error
    /// @param aStoreNUID if set, the winning card's nUID is stored for selecting later
    void antiCollision(ExecResultCB aResultCB, bool aStoreNUID = false);

  private:

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

    /// turn on the energy field
    void energyFieldOn();

    /// set the timeout timer
    void setTimer(uint16_t aTimerReload);

    /// execute PICC command
    //void execPICCCmd(uint8_t aCmd, uint8_t *aTxDataP, uint8_t aTxBytes, uint8_t *aRxDataP, uint16_t &aRxBits);
    void execPICCCmd(uint8_t aCmd, const string aTxData, ExecResultCB aResultCB);

    // execPICCCmd helpers
    void execResult(ErrorPtr aErr, uint16_t aResultBits = 0, const string aResult = "");
    void commandTimeout();
    #if IRQ_WATCHDOG
    void irqTimeout(MLTimer &aTimer);
    #endif

    /// Search for cards in field
    /// @param aReqCmd - REQA, REQB, WUPA, WUPB
    /// @param aWait - repeat command until there is an answer from a card (ok or error), do not timeout
    /// @param aStatusCB called to report card (ATQx received: ok, error otherwise)
    void requestPICC(uint8_t aReqCmd, bool aWait, StatusCB aStatusCB);

    // requestPICC helper
    void requestResponse(uint8_t aReqCmd, StatusCB aStatusCB, bool aWait, ErrorPtr aErr, uint16_t aResultBits, const string aResult);

    // anticoll and readCardSerial helper
    void anticollResponse(ExecResultCB aResultCB, bool aStoreNUID, ErrorPtr aErr, uint16_t aResultBits, const string aResult);

    void calculateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData);

    /*
    uint8_t auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum);
    uint8_t read(uint8_t blockAddr, uint8_t *recvData);
    uint8_t write(uint8_t blockAddr, uint8_t *writeData);
    void halt();
    */

    uint8_t serNum[5];  // the serial number.
    uint8_t AserNum[5]; // the serial number of the current section.
  };
  typedef boost::intrusive_ptr<RFID522> RFID522Ptr;


} // namespace p44

#endif // ENABLE_RFID

#endif /* rfid_hpp */
