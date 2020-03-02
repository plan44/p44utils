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


#include "rfid.hpp"

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

// MF522 Error codes
#define MI_OK                 0
#define MI_TIMEOUT            1
#define MI_ERR                2

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





RFID522::RFID522(SPIDevicePtr aSPIGenericDev, int aReaderIndex, SelectCB aReaderSelectFunc)
{
  spidev = aSPIGenericDev;
  readerIndex = aReaderIndex;
  readerSelectFunc = aReaderSelectFunc;
}


// MARK: ==== Basic register access


void RFID522::writeReg(uint8_t aReg, uint8_t aVal)
{
  uint8_t out[2];
  out[0] = (aReg<<1)&0x7E;
  out[1] = aVal;
  if (readerSelectFunc) readerSelectFunc(readerIndex);
  spidev->SPIRawWriteRead(2, out, 0, NULL);
  if (readerSelectFunc) readerSelectFunc(Deselect);
}

void RFID522::writeFIFO(const uint8_t* aData, size_t aNumBytes)
{
  const size_t maxBytes = 64; // FIFO size
  uint8_t buf[maxBytes+1]; // one for command
  buf[0] = (FIFODataReg<<1)&0x7E;
  if (aNumBytes>maxBytes) aNumBytes = maxBytes;
  memcpy(buf+1, aData, aNumBytes);
  if (readerSelectFunc) readerSelectFunc(readerIndex);
  spidev->SPIRawWriteRead((unsigned int )(aNumBytes+1), buf, 0, NULL);
  if (readerSelectFunc) readerSelectFunc(Deselect);
}


uint8_t RFID522::readReg(uint8_t addr)
{
  uint8_t val, ad;
  ad = ((addr<<1)&0x7E) | 0x80; // WR=Bit7, addr=Bit6..1, bit0=0
  if (readerSelectFunc) readerSelectFunc(readerIndex);
  spidev->SPIRawWriteRead(1, &ad, 1, &val);
  if (readerSelectFunc) readerSelectFunc(Deselect);
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
  if (readerSelectFunc) readerSelectFunc(readerIndex);
  spidev->SPIRawWriteRead((unsigned int )(aNumBytes+1), obuf, (unsigned int )(aNumBytes+1), ibuf, true);
  if (readerSelectFunc) readerSelectFunc(Deselect);
  memcpy(aData, ibuf+1, aNumBytes);
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
  // Soft reset, all registers set to reset values, buffer unchanged
  writeReg(CommandReg, PCD_SOFTRESET);
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
  // - Bit3..0 : TPrescalerHi=0x8D
  writeReg(TModeReg, 0x8D);    // TAuto=1: timer autostart at  ; f(Timer) = 6.78MHz/TPreScaler
  // TPrescalerReg
  // - Bit7..0 : TPrescalerLo=0x3E
  writeReg(TPrescalerReg, 0x3E);  //TModeReg[3..0] + TPrescalerReg
  // Timer Reload Value
  writeReg(TReloadRegL, 30);
  writeReg(TReloadRegH, 0);
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

  antennaOn(); // Turn on RF field
}


void RFID522::antennaOn(void)
{
  uint8_t temp;

  // Antenna driver settings
  // TxControlReg
  // - Bit1  : Tx2RFEn=1: TX2 drives modulated energy carrier
  // - Bit0  : Tx1RFEn=1: TX1 drives modulated energy carrier
  temp = readReg(TxControlReg);
  if (!(temp & 0x03)) {
    // none of the driver bits set -> set both
    setRegBits(TxControlReg, 0x03);
  }
}


// MARK: ==== Low Level

uint8_t RFID522::execPICCCmd(uint8_t aCmd, uint8_t *aTxDataP, uint8_t aTxBytes, uint8_t *aRxDataP, uint16_t &aRxBits)
{
  uint8_t status = MI_ERR;
  uint8_t irqEn = 0x00;
  uint8_t waitIRq = 0x00;

  switch (aCmd)
  {
    case PCD_MFAUTHENT: {
      // MiFare authentication
      irqEn = 0x12; // IdleIEn + ErrIEn interupt enable
      waitIRq = 0x10; // wait for Idle IRQ
      break;
    }
    case PCD_TRANSCEIVE: {
      // Transmit and then receive data
      irqEn = 0x77; // TxIen + RxIen + IdleIEn + LoAlertIEn + ErrIEn + TimerIEn interrupt enable
      waitIRq = 0x30; // wait for Idle or Rx IRQ
      break;
    }
    default:
      break;
  }
  // set up interrupts
  writeReg(CommIEnReg, irqEn|0x80); // also set IRqInv=1, IRQ line is inverted
  clrRegBits(CommIrqReg, 0x80);       // Clear all interrupt request bits
  // prepare
  setRegBits(FIFOLevelReg, 0x80);       // FlushBuffer=1, FIFO initialization
  writeReg(CommandReg, PCD_IDLE);   // Cancel previously pending commands
  // put data into FIFO
  writeFIFO(aTxDataP, aTxBytes);
//
//
//  //Escribir datos en el FIFO
//  for (i=0; i<sendLen; i++)
//  {
//    writeReg(FIFODataReg, sendData[i]);
//  }

  // Execute the command
  writeReg(CommandReg, aCmd);
  if (aCmd == PCD_TRANSCEIVE) {
    setRegBits(BitFramingReg, 0x80); // StartSend=1, transmission of data starts
  }

  // %%% ugly busy wait
  int busyWaitCount = 2000;  // According to the clock frequency setting, the maximum waiting time operation M1 25ms card???
  uint8_t irqflags;
  do {
    // CommIrqReg[7..0]
    // Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
    irqflags = readReg(CommIrqReg);
    busyWaitCount--;
    // while
    // - busy loop not exhaused
    // - Timer IRQ not happened yet
    // - none of the other IRQs we are waiting for not happened yet
  } while ((busyWaitCount!=0) && !(irqflags&0x01) && !(irqflags&waitIRq));
  clrRegBits(BitFramingReg, 0x80); // StartSend=0

  if (busyWaitCount!=0) {
    // actually got some IRQ
    // - any of BufferOvfl Collerr CRCErr ProtecolErr ?
    if(!(readReg(ErrorReg) & 0x1B)) {
      // no, everything ok
      status = MI_OK;
      if (irqflags&irqEn&0x01) {
        // Timer IRQ
        status = MI_TIMEOUT;
      }
      if (aCmd==PCD_TRANSCEIVE) {
        uint8_t receivedBytes = readReg(FIFOLevelReg); // number of bytes (including possibly partially valid last byte)
        uint8_t lastBits = readReg(ControlReg) & 0x07; // number of valid bits in last byte, 0=all
        // number of bits
        if (lastBits) {
          // not complete
          aRxBits = (receivedBytes-1)*8 + lastBits;
        }
        else {
          aRxBits = receivedBytes*8;
        }
        // get actual data
        if (receivedBytes==0) {
          receivedBytes = 1; // still read one byte. Why?? %%%
        }
        else if (receivedBytes > MAX_LEN) {
          receivedBytes = MAX_LEN;
        }
        readFIFO(aRxDataP, receivedBytes);
//        //??FIFO??????? Read the data received in the FIFO
//        for (i=0; i<n; i++)
//        {
//          backData[i] = readReg(FIFODataReg);
//        }
      }
    }
    else
    {
      status = MI_ERR;
    }

  }

  //SetBitMask(ControlReg,0x80);           //timer stops
  //Write_MFRC522(CommandReg, PCD_IDLE);

  return status;
}


/// Function name: MFRC522_Request
/// Description: Search for letters, read the card type
/// @param reqMode - find the card mode,
/// @param TagType - Return card type
/// - 0x4400 = Mifare_UltraLight
/// - 0x0400 = Mifare_One(S50)
/// - 0x0200 = Mifare_One(S70)
/// - 0x0800 = Mifare_Pro(X)
/// - 0x4403 = Mifare_DESFire
/// @return successful return MI_OK
uint8_t  RFID522::MFRC522Request(uint8_t reqMode, uint8_t *TagType)
{
  uint8_t status;
  uint16_t backBits;      //   Recibio bits de datos

  writeReg(BitFramingReg, 0x07);    //TxLastBists = BitFramingReg[2..0]  ???

  TagType[0] = reqMode;
  status = execPICCCmd(PCD_TRANSCEIVE, TagType, 1, TagType, backBits);

  if ((status != MI_OK) || (backBits != 0x10))
  {
    status = MI_ERR;
  }

  return status;
}




// MARK: ==== High level


bool RFID522::isCard()
{
  uint8_t status;
  uint8_t str[MAX_LEN];

  status = MFRC522Request(PICC_REQA, str); // probe TypeA cards in the field
  if (status == MI_OK) {
    return true;
  } else {
    return false;
  }
}

bool RFID522::readCardSerial()
{
  uint8_t status;
  uint8_t str[MAX_LEN];

  // Anti-collision, get 4 bytes Card number
  status = anticoll(str);
  memcpy(serNum, str, 5);

  if (status == MI_OK) {
    return true;
  } else {
    return false;
  }
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



/// MFRC522Anticoll -> anticoll
/// Anti-collision detection, read card serial number
/// @param serNum returns the serial 4-byte card nnumber, the first 5 bytes of parity bytes
/// @return successful return MI_OK
uint8_t RFID522::anticoll(uint8_t *serNum)
{
  uint8_t status;
  uint8_t i;
  uint8_t serNumCheck=0;
  uint16_t unLen;


  //ClearBitMask(Status2Reg, 0x08);    //TempSensclear
  //ClearBitMask(CollReg,0x80);      //ValuesAfterColl
  writeReg(BitFramingReg, 0x00);    //TxLastBists = BitFramingReg[2..0]

  serNum[0] = PICC_ANTICOLL;
  serNum[1] = 0x20;
  status = execPICCCmd(PCD_TRANSCEIVE, serNum, 2, serNum, unLen);

  if (status == MI_OK)
  {
    //?????? Compruebe el numero de serie de la tarjeta
    for (i=0; i<4; i++)
    {
      serNumCheck ^= serNum[i];
    }
    if (serNumCheck != serNum[i])
    {
      status = MI_ERR;
    }
  }

  //SetBitMask(CollReg, 0x80);    //ValuesAfterColl=1

  return status;
}

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
