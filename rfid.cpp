//  SPDX-License-Identifier: GPL-3.0-or-later
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "rfid.hpp"

#if ENABLE_RFID

using namespace p44;


// MARK: ==== MFRC522 chip definitions

#define MAX_LEN 16

// MF522 commands
#define PCD_IDLE              0x00               // no action, cancels current command execution
#define PCD_MFAUTHENT         0x0E               // performs the MIFARE standard authentication as a reader
#define PCD_RECEIVE           0x08               // activates receiver
#define PCD_TRANSMIT          0x04               // transmit data from the FIFO buffer
#define PCD_TRANSCEIVE        0x0C               // transmit data and automatically activate receiver after transmission
#define PCD_SOFTRESET         0x0F               // resets the MFRC522
#define PCD_CALCCRC           0x03               // activate CRC coprocessor (or self test)

// Mifare_One commands
#define PICC_REQA             0x26               // probe field for PICC of Type A -> Ready state
#define PICC_WUPA             0x52               // wake PICCs of Type A in HALT state -> Ready* state
#define PICC_ANTICOLL         0x93               // anti-colisiÃ³n
#define PICC_SELECTTAG        0x93               // ISO-14443 SEL command, start anticollision loop
#define PICC_AUTHENT1A        0x60               // verificaciÃ³n key A
#define PICC_AUTHENT1B        0x61               // verificaciÃ³n Key B
#define PICC_READ             0x30               // leer bloque
#define PICC_WRITE            0xA0               // Escribir en el bloque
#define PICC_DECREMENT        0xC0               // cargo
#define PICC_INCREMENT        0xC1               // recargar
#define PICC_RESTORE          0xC2               // Transferencia de datos de bloque de buffer
#define PICC_TRANSFER         0xB0               // Guardar los datos en el bÃºfer
#define PICC_HALT             0x50               // inactividad

// ------------------ MFRC522 registers ---------------
// Page 0:Command and Status
#define     Reserved00            0x00
#define     CommandReg            0x01
#define     CommIEnReg            0x02
#define     DivlEnReg             0x03
#define     CommIrqReg            0x04
#define     DivIrqReg             0x05
#define     ErrorReg              0x06
#define     Status1Reg            0x07
#define     Status2Reg            0x08
#define     FIFODataReg           0x09
#define     FIFOLevelReg          0x0A
#define     WaterLevelReg         0x0B
#define     ControlReg            0x0C
#define     BitFramingReg         0x0D
#define     CollReg               0x0E
#define     Reserved01            0x0F
// Page 1:Command
#define     Reserved10            0x10
#define     ModeReg               0x11
#define     TxModeReg             0x12
#define     RxModeReg             0x13
#define     TxControlReg          0x14
#define     TxASKReg              0x15
#define     TxSelReg              0x16
#define     RxSelReg              0x17
#define     RxThresholdReg        0x18
#define     DemodReg              0x19
#define     Reserved11            0x1A
#define     Reserved12            0x1B
#define     MifareReg             0x1C
#define     Reserved13            0x1D
#define     Reserved14            0x1E
#define     SerialSpeedReg        0x1F
// Page 2:CFG
#define     Reserved20            0x20
#define     CRCResultRegM         0x21
#define     CRCResultRegL         0x22
#define     Reserved21            0x23
#define     ModWidthReg           0x24
#define     Reserved22            0x25
#define     RFCfgReg              0x26
#define     GsNReg                0x27
#define     CWGsPReg              0x28
#define     ModGsPReg             0x29
#define     TModeReg              0x2A
#define     TPrescalerReg         0x2B
#define     TReloadRegH           0x2C
#define     TReloadRegL           0x2D
#define     TCounterValueRegH     0x2E
#define     TCounterValueRegL     0x2F
// Page 3:TestRegister
#define     Reserved30            0x30
#define     TestSel1Reg           0x31
#define     TestSel2Reg           0x32
#define     TestPinEnReg          0x33
#define     TestPinValueReg       0x34
#define     TestBusReg            0x35
#define     AutoTestReg           0x36
#define     VersionReg            0x37
#define     AnalogTestReg         0x38
#define     TestDAC1Reg           0x39
#define     TestDAC2Reg           0x3A
#define     TestADCReg            0x3B
#define     Reserved31            0x3C
#define     Reserved32            0x3D
#define     Reserved33            0x3E
#define     Reserved34            0x3F

// CommIrqReg |  xxxx  | TxIRq | RxIRq | IdleIRq || HiAlertIRq | LoAlertIRq | ErrIRq | TimerIRq |
#define     IrqLineInv_Mask       0x80
#define     TxIRq_Mask            0x40
#define     RxIRq_Mask            0x20
#define     IdleIRq_Mask          0x10
#define     HiAlertIRq_Mask       0x08
#define     LoAlertIRq_Mask       0x04
#define     ErrIRq_Mask           0x02
#define     TimerIRq_Mask         0x01


RFID522::RFID522(SPIDevicePtr aSPIGenericDev, int aReaderIndex, SelectCB aReaderSelectFunc, uint16_t aChipTimer, bool aUseIrqWatchdog, MLMicroSeconds aCmdTimeout) :
  mCmd(PCD_IDLE),
  mIrqEn(0),
  mWaitIrq(0),
  mChipTimer(aChipTimer),
  mUseIrqWatchdog(aUseIrqWatchdog),
  mCmdTimeout(aCmdTimeout),
  mCmdStart(Never)
{
  mSpiDev = aSPIGenericDev;
  mReaderIndex = aReaderIndex;
  mReaderSelectFunc = aReaderSelectFunc;
}


RFID522::~RFID522()
{
  reset();
}

string RFID522::logContextPrefix()
{
  return string_format("RFID522 #%d", mReaderIndex);
}


// MARK: ==== Basic register access


void RFID522::writeReg(uint8_t aReg, uint8_t aVal)
{
  uint8_t out[2];
  out[0] = (aReg<<1)&0x7E;
  out[1] = aVal;
  if (mReaderSelectFunc) mReaderSelectFunc(mReaderIndex);
  mSpiDev->SPIRawWriteRead(2, out, 0, NULL);
  if (mReaderSelectFunc) mReaderSelectFunc(Deselect);
  FOCUSOLOG("writeReg(0x%02x, 0x%02x)", aReg, aVal);
}

void RFID522::writeFIFO(const uint8_t* aData, size_t aNumBytes)
{
  const size_t maxBytes = 64; // FIFO size
  uint8_t buf[maxBytes+1]; // one for command
  buf[0] = (FIFODataReg<<1)&0x7E;
  if (aNumBytes>maxBytes) aNumBytes = maxBytes;
  memcpy(buf+1, aData, aNumBytes);
  if (mReaderSelectFunc) mReaderSelectFunc(mReaderIndex);
  mSpiDev->SPIRawWriteRead((unsigned int )(aNumBytes+1), buf, 0, NULL);
  if (mReaderSelectFunc) mReaderSelectFunc(Deselect);
  FOCUSOLOG("writeFIFO([%s], %zu)", dataToHexString(aData, aNumBytes, ',').c_str(), aNumBytes);
}


uint8_t RFID522::readReg(uint8_t aReg)
{
  uint8_t val, ad;
  ad = ((aReg<<1)&0x7E) | 0x80; // WR=Bit7, addr=Bit6..1, bit0=0
  if (mReaderSelectFunc) mReaderSelectFunc(mReaderIndex);
  mSpiDev->SPIRawWriteRead(1, &ad, 1, &val);
  if (mReaderSelectFunc) mReaderSelectFunc(Deselect);
  FOCUSOLOG("readReg(0x%02x) = 0x%02x)", aReg, val);
  return val;
}

void RFID522::readFIFO(uint8_t* aData, size_t aNumBytes)
{
  const size_t maxBytes = 64; // FIFO size
  uint8_t obuf[maxBytes+1]; // one for command
  uint8_t ibuf[maxBytes+1]; // one for command
  uint8_t reg = ((FIFODataReg<<1)&0x7E) | 0x80;
  memset(obuf, reg, maxBytes+1); // always send the register address
  if (aNumBytes>maxBytes) aNumBytes = maxBytes;
  if (mReaderSelectFunc) mReaderSelectFunc(mReaderIndex);
  mSpiDev->SPIRawWriteRead((unsigned int )(aNumBytes+1), obuf, (unsigned int )(aNumBytes+1), ibuf, true);
  if (mReaderSelectFunc) mReaderSelectFunc(Deselect);
  memcpy(aData, ibuf+1, aNumBytes);
  FOCUSOLOG("readFIFO(buf, %zu) = [%s]", aNumBytes, dataToHexString(aData, aNumBytes, ',').c_str());
}


void RFID522::setRegBits(uint8_t aReg, uint8_t aBitMask)
{
  uint8_t tmp;
  tmp = readReg(aReg);
  writeReg(aReg, tmp | aBitMask);  // set bit mask
}

void RFID522::clrRegBits(uint8_t aReg, uint8_t aBitMask)
{
  uint8_t tmp;
  tmp = readReg(aReg);
  writeReg(aReg, tmp & (~aBitMask));  // clear bit mask
}



// MARK: ==== Initialisation & Reset

void RFID522::reset()
{
  if (mUseIrqWatchdog) {
    mIrqWatchdog.cancel();
  }
  // Soft reset, all registers set to reset values, buffer unchanged
  writeReg(CommandReg, PCD_SOFTRESET);
  mCmd = 0;
  mIrqEn = 0;
  mWaitIrq = 0;
}


void RFID522::init()
{
  reset();

  // ??Timer: TPrescaler*TreloadVal/6.78MHz = 24ms

  // Timer Frequency
  // - fTimer = 13.56Mhz/ (2 * TPreScaler+1)  - when TPrescalEven=0
  // - fTimer = 13.56Mhz/ (2 * TPreScaler+2)  - when TPrescalEven=1
  // - With TPrescaler = 0xD3E = 3390 -> fTimer = 2kHz
  // TModeReg:
  // - Bit7    : TAuto=1: timer autostarts at end of transmission in all modes
  // - Bit6,5  : TGated=0: non-gated timer mode
  // - Bit4    : TAutoRestart=0: timer does not restart automatically
  // - Bit3..0 : TPrescalerHi=0x0D
  writeReg(TModeReg, 0x8D);    // TAuto=1: timer autostart at end of transmission ; f(Timer) = 6.78MHz/TPreScaler
  // TPrescalerReg
  // - Bit7..0 : TPrescalerLo=0x3E
  writeReg(TPrescalerReg, 0x3E);  //TModeReg[3..0] + TPrescalerReg
  // Timer Reload Value
  setTimer(mChipTimer);
  // Transmit modulation settings
  // TxASKReg
  // - Bit 6   : Force100ASK=1: force 100% ASK modulation independent of ModGsPReg setting
  writeReg(TxASKReg, 0x40);
  // TModeReg
  // - Bit7    : MSBFirst=0: CRC not reversed (MSB last)
  // - Bit5    : TXWaitRF=1: transmitter can only be started if an RF field is generated
  // - Bit3    : PolMFin=1: Polarity of MFIN is active HIGH
  // - Bit1,0  : CRCPreset=01: CRC Preset is 0x6363
  writeReg(ModeReg, 0x3D);

  //ClearBitMask(Status2Reg, 0x08); // MFCrypto1On=0
  //writeMFRC522(RxSelReg, 0x86); // RxWait = RxSelReg[5..0]
  //writeMFRC522(RFCfgReg, 0x7F); // RxGain = 48dB

  // no longer automatically!!
  //energyField(true); // Turn on RF field
}


void RFID522::setTimer(uint16_t aTimerReload)
{
  FOCUSOLOG("### setting timer reload to %d", aTimerReload);
  writeReg(TReloadRegL, aTimerReload & 0xFF);
  writeReg(TReloadRegH, (aTimerReload>>8) & 0xFF);
}




void RFID522::energyField(bool aEnable) {
  uint8_t temp;

  // Antenna driver settings
  // TxControlReg
  // - Bit1  : Tx2RFEn=1: TX2 drives modulated energy carrier
  // - Bit0  : Tx1RFEn=1: TX1 drives modulated energy carrier
  temp = readReg(TxControlReg);
  if (aEnable) {
    if (!(temp & 0x03)) {
      // none of the driver bits set -> set both
      FOCUSOLOG("+++ enabling energy field");
      setRegBits(TxControlReg, 0x03);
    }
  }
  else {
    if (temp & 0x03) {
      // at least one driver bits is set -> clear both
      FOCUSOLOG("--- disabling energy field");
      clrRegBits(TxControlReg, 0x03);
    }
  }
}


// MARK: ==== Low Level


void RFID522::returnToIdle()
{
  FOCUSOLOG("### return to idle (from mCmd=0x%02x)", mCmd);
  mCmd = PCD_IDLE;
  mIrqEn = 0x00;
  mWaitIrq = 0x00;
  mCmdStart = Never;
  writeReg(CommIEnReg, 0x80); // disable all interrupts, but keep polarity inverse!
  writeReg(CommandReg, PCD_IDLE); // Cancel previously pending command, if any
  writeReg(FIFOLevelReg, 0x80); // FlushBuffer=1, FIFO initialization
  if (mUseIrqWatchdog) {
    mIrqWatchdog.cancel();
  }
}


void RFID522::commandTimeout()
{
  FOCUSOLOG("!!!! command timed out -> cancel");
  returnToIdle();
  execResult(Error::err<RFIDError>(RFIDError::IRQTimeout, "IRQ Timeout -> cancelled command"));
}


void RFID522::continueTransceiving()
{
  if (mCmd==PCD_TRANSCEIVE) {
    FOCUSOLOG("PCD_TRANSCEIVE still running: re-start data transmission");
    setRegBits(BitFramingReg, 0x80); // StartSend=1, transmission of data starts
  }
}



void RFID522::execPICCCmd(uint8_t aCmd, const string aTxData, ExecResultCB aResultCB)
{
  mExecResultCB = aResultCB;

  // prepare (clears mCmd, IRQ enable/wait, command start)
  returnToIdle();
  // IRQ enable bits:
  //            |    7   |   6   |   5   |    4    ||     3      |      2     |    1   |    0     |
  // CommIEnReg | IRqInv | TxIEn | RxIEn | IdleIEn || HiAlertIEn | LoAlertIEn | ErrIEn | TimerIEn |
  // CommIrqReg |  Set1  | TxIRq | RxIRq | IdleIRq || HiAlertIRq | LoAlertIRq | ErrIRq | TimerIRq |
  switch (aCmd) {
    case PCD_MFAUTHENT: {
      // MiFare authentication
      mIrqEn = IdleIRq_Mask+ErrIRq_Mask; // IdleIEn + ErrIEn interupt enable
      mWaitIrq = IdleIRq_Mask+TimerIRq_Mask; // wait for Idle (or Timer, even if not enabled)
      break;
    }
    case PCD_TRANSCEIVE: {
      // Transmit and then receive data
      //mIrqEn = TxIRq_Mask+RxIRq_Mask+IdleIRq_Mask+LoAlertIRq_Mask+ErrIRq_Mask+TimerIRq_Mask; // TxIen + RxIen + IdleIEn + LoAlertIEn + ErrIEn + TimerIEn interrupt enable
      mIrqEn = TxIRq_Mask+RxIRq_Mask+IdleIRq_Mask+ErrIRq_Mask+TimerIRq_Mask; // TxIen + RxIen + IdleIEn + ErrIEn + TimerIEn interrupt enable
      //mIrqEn = IdleIRq_Mask+ErrIRq_Mask+TimerIRq_Mask; // IdleIEn, ErrIEn, TimerIEn
      mWaitIrq = RxIRq_Mask+IdleIRq_Mask+TimerIRq_Mask; // wait for Idle, Rx, Timer
      break;
    }
    default: {
      execResult(Error::err<RFIDError>(RFIDError::UnknownCmd, "Unknown PICC command"));
      return;
    }
  }
  // now set new command running
  mCmd = aCmd;
  // put data into FIFO
  writeFIFO((uint8_t *)aTxData.c_str(), aTxData.size());
  // set up interrupts
  writeReg(CommIEnReg, mIrqEn|IrqLineInv_Mask); // also set IRqInv=1, IRQ line is inverted
  //clrRegBits(CommIrqReg, 0x80); // Clear all interrupt request bits %%% not really, probably bug
  writeReg(CommIrqReg, 0x7F); // Clear all interrupt request bits
  FOCUSOLOG("### debug: before issuning cmd, irqflags after clearing = 0x%02X, enabled = 0x%02X", readReg(CommIrqReg), readReg(CommIEnReg));
  // Execute the command
  FOCUSOLOG(">>> starting command 0x%02X with %lu data bytes, FIFO level = %d", mCmd, aTxData.size(), readReg(FIFOLevelReg));
  writeReg(CommandReg, mCmd);
  continueTransceiving();
  if (mUseIrqWatchdog) {
    // setup IRQ watchdog, wait for irqHandler() to get called
    mIrqWatchdog.executeOnce(boost::bind(&RFID522::irqTimeout, this, _1), mCmdTimeout);
  }
  else if (mCmd!=PCD_IDLE) {
    mCmdStart = MainLoop::now();
  }
  return; // wait for IRQ now
}


void RFID522::irqTimeout(MLTimer &aTimer)
{
  // actual interrupt might be unreliable, so just check irq here again
  bool pending = irqHandler();
  if (pending) {
    MainLoop::currentMainLoop().retriggerTimer(aTimer, mCmdTimeout);
  }
  else {
    commandTimeout();
  }
}


bool RFID522::irqHandler()
{
  FOCUSOLOG("\nirqHandler: mIqrEn=0x%02x", mIrqEn);
  // Bits: Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
  uint8_t irqflags;
  if (mIrqEn) irqflags = readReg(CommIrqReg) & 0x7F; // Set1 masked out (probably not needed because reads 0 anyway)
  else return false; // optimization: none enabled -> none pending without need to read flags
  if (irqflags & mIrqEn) {
    FOCUSOLOG(
      "### debug: found enabled IRQ: CommIrqReg=0x%02X, irqEn=0x%02X, CommIEnReg=0x%02X, waitIrq=0x%02X, status1=0x%02X, FIFOlevel=%d",
      irqflags, mIrqEn, readReg(CommIEnReg), mWaitIrq, readReg(Status1Reg), readReg(FIFOLevelReg)
    );
    writeReg(CommIrqReg, irqflags); // ALWAYS clear the flags that are set (Set1=0)
    if (irqflags & mWaitIrq) {
      // CommIrqReg |  Set1  | TxIRq | RxIRq | IdleIRq || HiAlertIRq | LoAlertIRq | ErrIRq | TimerIRq |
      ErrorPtr err;
      string response;
      uint16_t totalBits = 0;
      // one of the interrupts we should handle
      FOCUSOLOG("IRQ arrived we are waiting for: relevant bits in CommIrqReg = 0x%02X", irqflags & mWaitIrq);
      // - any of BufferOvfl Collerr CRCErr ProtecolErr ?
      uint8_t errReg = readReg(ErrorReg);
      if (errReg & 0x1B) {
        // error overrides everything
        err = Error::err<RFIDError>(RFIDError::ChipErr, "chip error register = 0x%02X", errReg);
      }
      else {
        // no error
        if ((irqflags&mIrqEn) & (IdleIRq_Mask+RxIRq_Mask)) {
          // idle or Rx IRQ, means command has executed (note: PCD_TRANSCEIVE never gets idle by itself, so Rx is essential!)
          // NOTE: this has PRECEDENCE over timer
          if (mCmd==PCD_TRANSCEIVE) {
            // end of transceive, get data
            uint8_t receivedBytes = readReg(FIFOLevelReg); // number of bytes (including possibly partially valid last byte)
            uint8_t lastBits = readReg(ControlReg) & 0x07; // number of valid bits in last byte, 0=all
            FOCUSOLOG("<<< end of PCD_TRANSCEIVE, receivedBytes=%d, lastBits=%d", receivedBytes, lastBits);
            // number of bits
            if (lastBits) {
              // not complete
              totalBits = (receivedBytes-1)*8 + lastBits;
            }
            else {
              totalBits = receivedBytes*8;
            }
            // get actual data
            if (receivedBytes==0) {
              receivedBytes = 1; // still read one byte. Why?? %%%
            }
            else if (receivedBytes > MAX_LEN) {
              receivedBytes = MAX_LEN;
            }
            uint8_t data[MAX_LEN];
            readFIFO(data, receivedBytes);
            response.assign((char *)data, receivedBytes);
          }
        }
        else if ((irqflags&mIrqEn) & TimerIRq_Mask) {
          // we were waiting for timer IRQ and have NOT seen the end of a command
          err = Error::err<RFIDError>(RFIDError::ChipTimeout, "chip timer timeout");
        }
      }
      // done with command
      mWaitIrq = 0;
      if (mUseIrqWatchdog) {
        mIrqWatchdog.cancel();
      }
      else {
        mCmdStart = Never;
      }
      // Only report if not idle
      if (mCmd!=PCD_IDLE) execResult(err, totalBits, response);
    }
  }
  else if (!mUseIrqWatchdog && mCmdStart) {
    // no IRQ and command started: check command timeout
    if (mCmd && (MainLoop::now()>mCmdStart+mCmdTimeout)) {
      commandTimeout();
    }
  }
  FOCUSOLOG("irqHandler() done with CommIrqReg=0x%02X, waitIrq=0x%02X\n", irqflags, mWaitIrq);
  // still pending
  return mWaitIrq!=0;
}


void RFID522::execResult(ErrorPtr aErr, uint16_t aResultBits, const string aResult)
{
  FOCUSOLOG("### execResult: mCmd=0x%02x, resultBits=%d, result=%s, err=%s, callback=%s", mCmd, aResultBits, binaryToHexString(aResult).c_str(), Error::text(aErr), mExecResultCB ? "YES" : "NO");
  if (mExecResultCB) {
    ExecResultCB cb = mExecResultCB;
    mExecResultCB = NoOP;
    cb(aErr, aResultBits, aResult);
  }
}


void RFID522::requestPICC(uint8_t aReqCmd, bool aWait, StatusCB aStatusCB)
{
  writeReg(BitFramingReg, 0x07); // TxLastBists: BitFramingReg[2..0]=7: alignment: start at bit7 of first byte, then continue in next byte
  string data;
  data.append(1,aReqCmd);
  execPICCCmd(PCD_TRANSCEIVE, data, boost::bind(&RFID522::requestResponse, this, aReqCmd, aStatusCB, aWait, _1, _2, _3));
}


void RFID522::requestResponse(uint8_t aReqCmd, StatusCB aStatusCB, bool aWait, ErrorPtr aErr, uint16_t aResultBits, const string aResult)
{
  if (Error::isOK(aErr)) {
    if (aResultBits!=16) {
      aErr = Error::err<RFIDError>(RFIDError::BadAnswer, "bad ATQ answer: bits = %d: data=%s", aResultBits, binaryToHexString(aResult).c_str());
    }
  }
  else if (aWait && aErr->isError(RFIDError::domain(), RFIDError::ChipTimeout)) {
    // just timeout, try again
    RFID522::requestPICC(aReqCmd, true, aStatusCB);
    return;
  }
  if (aStatusCB) aStatusCB(aErr);
}


// MARK: ==== High level

void RFID522::probeTypeA(StatusCB aStatusCB, bool aWait)
{
  requestPICC(PICC_REQA, aWait, aStatusCB);
}


void RFID522::antiCollision(ExecResultCB aResultCB, bool aStoreNUID)
{
  //ClearBitMask(Status2Reg, 0x08); // TempSensclear
  //ClearBitMask(CollReg,0x80); // ValuesAfterColl
  writeReg(BitFramingReg, 0x00); // TxLastBists = BitFramingReg[2..0]
  string cmd;
  cmd.append(1,PICC_ANTICOLL);
  cmd.append(1,0x20);
  execPICCCmd(PCD_TRANSCEIVE, cmd, boost::bind(&RFID522::anticollResponse, this, aResultCB, aStoreNUID, _1, _2, _3));
}


void RFID522::anticollResponse(ExecResultCB aResultCB, bool aStoreNUID, ErrorPtr aErr, uint16_t aResultBits, const string aResult)
{
  if (Error::isOK(aErr)) {
    // check validity, BCC (5th byte) must be XOR of previous 4 bytes
    if (aResult.size()>=5) {
      uint8_t bcc = 0;
      for (int i=0; i<4; i++) {
        bcc ^= (uint8_t)aResult[i];
      }
      if (bcc!=(uint8_t)aResult[4]) {
        aErr = Error::err<RFIDError>(RFIDError::BadAnswer, "anticollision BCC error: bits = %d: data=%s", aResultBits, binaryToHexString(aResult).c_str());
      }
      // correct serial
      if (aStoreNUID) {
        memcpy(serNum, aResult.c_str(), 5);
      }
    }
    else {
      aErr = Error::err<RFIDError>(RFIDError::BadAnswer, "bad anticollision answer: bits = %d: data=%s", aResultBits, binaryToHexString(aResult).c_str());
    }
  }
  if (aResultCB) aResultCB(aErr, aResultBits, aResult);
}



void RFID522::calculateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData)
{
  uint8_t i, n;

  clrRegBits(DivIrqReg, 0x04);      //CRCIrq = 0
  setRegBits(FIFOLevelReg, 0x80);      //Claro puntero FIFO
  //Write_MFRC522(CommandReg, PCD_IDLE);

  //Escribir datos en el FIFO
  for (i=0; i<len; i++)
  {
    writeReg(FIFODataReg, *(pIndata+i));
  }
  writeReg(CommandReg, PCD_CALCCRC);

  // Waiting for the CRC calculation to be completed
  i = 0xFF;
  do
  {
    n = readReg(DivIrqReg);
    i--;
  }
  while ((i!=0) && !(n&0x04));      //CRCIrq = 1

  //Lea el calculo de CRC
  pOutData[0] = readReg(CRCResultRegL);
  pOutData[1] = readReg(CRCResultRegM);
}


/*
/// MFRC522Auth -> auth
/// Check the auth of the card
/// @param authMode Authentication mode from password
/// - 0x60 = A 0x60 = KeyA validation
/// - 0x61 = B 0x61 = KeyB validation
/// @param BlockAddr address block
/// @param Sectorkey sector contraseca
/// @param serNum Serial number of the card, 4 bytes
/// @return successful return MI_OK
uint8_t RFID522::auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum)
{
  uint8_t status;
  uint16_t recvBits;
  uint8_t i;
  uint8_t buff[12];

  //????+???+????+???? Verifique la direccion de comandos de bloques del sector + + contraseca + numero de la tarjeta de serie
  buff[0] = authMode;
  buff[1] = BlockAddr;
  for (i=0; i<6; i++)
  {
    buff[i+2] = *(Sectorkey+i);
  }
  for (i=0; i<4; i++)
  {
    buff[i+8] = *(serNum+i);
  }
  status = execPICCCmd(PCD_MFAUTHENT, buff, 12, buff, recvBits);

  if ((status != MI_OK) || (!(readReg(Status2Reg) & 0x08)))
  {
    status = MI_ERR;
  }

  return status;
}

/// MFRC522Read -> read
/// Reading block data
/// @param blockAddr block address; recvData - read a block of data
/// @return when successful return MI_OK
uint8_t RFID522::read(uint8_t blockAddr, uint8_t *recvData)
{
  uint8_t status;
  uint16_t unLen;

  recvData[0] = PICC_READ;
  recvData[1] = blockAddr;
  calculateCRC(recvData,2, &recvData[2]);
  status = execPICCCmd(PCD_TRANSCEIVE, recvData, 4, recvData, unLen);

  if ((status != MI_OK) || (unLen != 0x90))
  {
    status = MI_ERR;
  }

  return status;
}

/// MFRC522Write -> write
/// Writing block data
/// @param blockAddr block address
/// @param writeData to write 16 bytes of the data block
/// @return successful return MI_OK
uint8_t RFID522::write(uint8_t blockAddr, uint8_t *writeData)
{
  uint8_t status;
  uint16_t recvBits;
  uint8_t i;
  uint8_t buff[18];

  buff[0] = PICC_WRITE;
  buff[1] = blockAddr;
  calculateCRC(buff, 2, &buff[2]);
  status = execPICCCmd(PCD_TRANSCEIVE, buff, 4, buff, recvBits);

  if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))
  {
    status = MI_ERR;
  }

  if (status == MI_OK)
  {
    for (i=0; i<16; i++)    //?FIFO?16Byte?? Datos a la FIFO 16Byte escribir
    {
      buff[i] = *(writeData+i);
    }
    calculateCRC(buff, 16, &buff[16]);
    status = execPICCCmd(PCD_TRANSCEIVE, buff, 18, buff, recvBits);

    if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))
    {
      status = MI_ERR;
    }
  }

  return status;
}


/// MFRC522Halt -> halt
void RFID522::halt()
{
  uint8_t status;
  uint16_t unLen;
  uint8_t buff[4];

  buff[0] = PICC_HALT;
  buff[1] = 0;
  calculateCRC(buff, 2, &buff[2]);

  clrRegBits(Status2Reg, 0x08); // turn off encryption

  status = execPICCCmd(PCD_TRANSCEIVE, buff, 4, buff,unLen);
}

*/

#endif // ENABLE_RFID

