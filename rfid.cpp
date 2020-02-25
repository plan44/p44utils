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


RFID522::RFID522(int aSpiBusAndCSNo, int aSelectAddress, SelectCB aReaderSelectFunc)
{
  spidev = SPIManager::sharedManager().getDevice(aSpiBusAndCSNo, "generic@0");
  selectAddress = aSelectAddress;
  readerSelectFunc = aReaderSelectFunc;
}


bool RFID522::isCard()
{
  uint8_t status;
  uint8_t str[MAX_LEN];

  status = MFRC522Request(PICC_REQIDL, str);
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


void RFID522::init()
{
//  notResetPowerdown->set(true);

  reset();

  //Timer: TPrescaler*TreloadVal/6.78MHz = 24ms
  writeMFRC522(TModeReg, 0x8D);    //Tauto=1; f(Timer) = 6.78MHz/TPreScaler
  writeMFRC522(TPrescalerReg, 0x3E);  //TModeReg[3..0] + TPrescalerReg
  writeMFRC522(TReloadRegL, 30);
  writeMFRC522(TReloadRegH, 0);

  writeMFRC522(TxAutoReg, 0x40);    //100%ASK
  writeMFRC522(ModeReg, 0x3D);    // CRC valor inicial de 0x6363

  //ClearBitMask(Status2Reg, 0x08);  //MFCrypto1On=0
  //writeMFRC522(RxSelReg, 0x86);    //RxWait = RxSelReg[5..0]
  //writeMFRC522(RFCfgReg, 0x7F);     //RxGain = 48dB

  antennaOn();    //Abre  la antena
}


void RFID522::reset()
{
  writeMFRC522(CommandReg, PCD_RESETPHASE);
}


void RFID522::writeMFRC522(uint8_t addr, uint8_t val)
{
  uint8_t out[2];
  out[0] = (addr<<1)&0x7E;
  out[1] = val;
  if (readerSelectFunc) readerSelectFunc(selectAddress);
  spidev->SPIRawWriteRead(2, out, 0, NULL);
  if (readerSelectFunc) readerSelectFunc(Deselect);
}


uint8_t RFID522::readMFRC522(uint8_t addr)
{
  uint8_t val, ad;
  ad = ((addr<<1)&0x7E) | 0x80;
  if (readerSelectFunc) readerSelectFunc(selectAddress);
  spidev->SPIRawWriteRead(1, &ad, 1, &val);
  if (readerSelectFunc) readerSelectFunc(Deselect);
  return val;
}


void RFID522::antennaOn(void)
{
  uint8_t temp;

  temp = readMFRC522(TxControlReg);
  if (!(temp & 0x03))
  {
    setBitMask(TxControlReg, 0x03);
  }
}


void RFID522::setBitMask(uint8_t reg, uint8_t mask)
{
  uint8_t tmp;
  tmp = readMFRC522(reg);
  writeMFRC522(reg, tmp | mask);  // set bit mask
}

void RFID522::clearBitMask(uint8_t reg, uint8_t mask)
{
  uint8_t tmp;
  tmp = readMFRC522(reg);
  writeMFRC522(reg, tmp & (~mask));  // clear bit mask
}


void RFID522::calculateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData)
{
  uint8_t i, n;

  clearBitMask(DivIrqReg, 0x04);      //CRCIrq = 0
  setBitMask(FIFOLevelReg, 0x80);      //Claro puntero FIFO
  //Write_MFRC522(CommandReg, PCD_IDLE);

  //Escribir datos en el FIFO
  for (i=0; i<len; i++)
  {
    writeMFRC522(FIFODataReg, *(pIndata+i));
  }
  writeMFRC522(CommandReg, PCD_CALCCRC);

  // Waiting for the CRC calculation to be completed
  i = 0xFF;
  do
  {
    n = readMFRC522(DivIrqReg);
    i--;
  }
  while ((i!=0) && !(n&0x04));      //CRCIrq = 1

  //Lea el calculo de CRC
  pOutData[0] = readMFRC522(CRCResultRegL);
  pOutData[1] = readMFRC522(CRCResultRegM);
}


uint8_t RFID522::MFRC522ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen)
{
  uint8_t status = MI_ERR;
  uint8_t irqEn = 0x00;
  uint8_t waitIRq = 0x00;
  uint8_t lastBits;
  uint8_t n;
  uint16_t i;

  switch (command)
  {
    case PCD_AUTHENT:    // Tarjetas de certificacion cerca
    {
      irqEn = 0x12;
      waitIRq = 0x10;
      break;
    }
    case PCD_TRANSCEIVE:  //La transmision de datos FIFO
    {
      irqEn = 0x77;
      waitIRq = 0x30;
      break;
    }
    default:
      break;
  }

  writeMFRC522(CommIEnReg, irqEn|0x80); //To request interruption
  clearBitMask(CommIrqReg, 0x80);       // Clear all interrupt request bits
  setBitMask(FIFOLevelReg, 0x80);       //FlushBuffer=1, FIFO initialization

  writeMFRC522(CommandReg, PCD_IDLE);   // NO action and cancel the command

  //Escribir datos en el FIFO
  for (i=0; i<sendLen; i++)
  {
    writeMFRC522(FIFODataReg, sendData[i]);
  }

  //???? ejecutar el comando
  writeMFRC522(CommandReg, command);
  if (command == PCD_TRANSCEIVE)
  {
    setBitMask(BitFramingReg, 0x80);    //StartSend=1,transmission of data starts
  }

  // A la espera de recibir datos para completar
  i = 2000;  //i????????,??M1???????25ms  ??? According to the clock frequency setting, the maximum waiting time operation M1 25ms card?
  do
  {
    //CommIrqReg[7..0]
    //Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
    n = readMFRC522(CommIrqReg);
    i--;
  }
  while ((i!=0) && !(n&0x01) && !(n&waitIRq));

  clearBitMask(BitFramingReg, 0x80);      //StartSend=0

  if (i != 0)
  {
    if(!(readMFRC522(ErrorReg) & 0x1B))  //BufferOvfl Collerr CRCErr ProtecolErr
    {
      status = MI_OK;
      if (n & irqEn & 0x01)
      {
        status = MI_NOTAGERR;      //??
      }

      if (command == PCD_TRANSCEIVE)
      {
        n = readMFRC522(FIFOLevelReg);
        lastBits = readMFRC522(ControlReg) & 0x07;
        if (lastBits)
        {
          *backLen = (n-1)*8 + lastBits;
        }
        else
        {
          *backLen = n*8;
        }

        if (n == 0)
        {
          n = 1;
        }
        if (n > MAX_LEN)
        {
          n = MAX_LEN;
        }

        //??FIFO??????? Read the data received in the FIFO
        for (i=0; i<n; i++)
        {
          backData[i] = readMFRC522(FIFODataReg);
        }
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
/// @param Tagtype - Return card type
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

  writeMFRC522(BitFramingReg, 0x07);    //TxLastBists = BitFramingReg[2..0]  ???

  TagType[0] = reqMode;
  status = MFRC522ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

  if ((status != MI_OK) || (backBits != 0x10))
  {
    status = MI_ERR;
  }

  return status;
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
  writeMFRC522(BitFramingReg, 0x00);    //TxLastBists = BitFramingReg[2..0]

  serNum[0] = PICC_ANTICOLL;
  serNum[1] = 0x20;
  status = MFRC522ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

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
  status = MFRC522ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);

  if ((status != MI_OK) || (!(readMFRC522(Status2Reg) & 0x08)))
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
  status = MFRC522ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen);

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
  status = MFRC522ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);

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
    status = MFRC522ToCard(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);

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

  clearBitMask(Status2Reg, 0x08); // turn off encryption

  status = MFRC522ToCard(PCD_TRANSCEIVE, buff, 4, buff,&unLen);
}
