//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2026 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 0

#include "artnetcomm.hpp"

#if ENABLE_ARTNET

#include <algorithm>
#include <arpa/inet.h>

#ifdef ESP_PLATFORM
  #warning "Art-Net receiver is Linux-first; TODO: review ESP32 UDP binding and advertised address handling before enabling on ESP32"
#endif

using namespace p44;
using namespace p44::ArtNet;

static const uint8_t artNetId[8] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0 };


// MARK: - Art-Net codec helpers

uint16_t ArtNet::readLE16(const uint8_t *aBytes)
{
  return (uint16_t)aBytes[0] | ((uint16_t)aBytes[1]<<8);
}


uint16_t ArtNet::readBE16(const uint8_t *aBytes)
{
  return ((uint16_t)aBytes[0]<<8) | (uint16_t)aBytes[1];
}


void ArtNet::writeLE16(uint8_t *aBytes, uint16_t aValue)
{
  aBytes[0] = aValue & 0xFF;
  aBytes[1] = (aValue>>8) & 0xFF;
}


void ArtNet::writeBE16(uint8_t *aBytes, uint16_t aValue)
{
  aBytes[0] = (aValue>>8) & 0xFF;
  aBytes[1] = aValue & 0xFF;
}


void ArtNet::writeFixedString(uint8_t *aDestination, size_t aFieldSize, const string &aValue)
{
  memset(aDestination, 0, aFieldSize);
  if (aFieldSize==0) return;
  size_t n = std::min(aFieldSize-1, aValue.size());
  memcpy(aDestination, aValue.data(), n);
}


uint8_t ArtNet::artNetNet(uint16_t aPortAddress)
{
  return (aPortAddress>>8) & 0x7F;
}


uint8_t ArtNet::artNetSubNet(uint16_t aPortAddress)
{
  return (aPortAddress>>4) & 0x0F;
}


uint8_t ArtNet::artNetUniverse(uint16_t aPortAddress)
{
  return aPortAddress & 0x0F;
}


uint16_t ArtNet::makeArtNetPortAddress(uint8_t aNet, uint8_t aSubNet, uint8_t aUniverse)
{
  return (((uint16_t)aNet & 0x7F)<<8) | (((uint16_t)aSubNet & 0x0F)<<4) | ((uint16_t)aUniverse & 0x0F);
}


ArtNetAdvertisementInfo::ArtNetAdvertisementInfo() :
  portAddress(0),
  oemCode(0xFFFF),
  estaManufacturerCode(0x0000),
  versionInfo(0x0001),
  status1(0xD0),
  status2(0x08),
  status3(0x00),
  style(0x00)
{
  shortName = "p44utils";
  longName = "p44utils Art-Net receiver";
  nodeReport = "#0001 [0000] OK";
}


static bool hasArtNetId(const uint8_t *aPacket, size_t aPacketSize)
{
  return aPacketSize>=8 && memcmp(aPacket, artNetId, sizeof(artNetId))==0;
}


static bool supportedProtocolVersion(const uint8_t *aPacket)
{
  return readBE16(aPacket+10)>=protocolVersion;
}


bool ArtNet::decodeOpCode(const uint8_t *aPacket, size_t aPacketSize, uint16_t &aOpCode)
{
  if (!aPacket || aPacketSize<10 || !hasArtNetId(aPacket, aPacketSize)) {
    return false;
  }
  aOpCode = readLE16(aPacket+8);
  return true;
}


bool ArtNet::decodeArtPoll(const uint8_t *aPacket, size_t aPacketSize, ArtPollView &aPoll)
{
  uint16_t opCode;
  if (!decodeOpCode(aPacket, aPacketSize, opCode) || opCode!=OpPoll || aPacketSize<14) {
    return false;
  }
  if (!supportedProtocolVersion(aPacket)) {
    return false;
  }
  aPoll.talkToMe = aPacket[12];
  aPoll.priority = aPacket[13];
  return true;
}


bool ArtNet::decodeArtDmx(const uint8_t *aPacket, size_t aPacketSize, ArtDmxView &aDmx)
{
  uint16_t opCode;
  if (!decodeOpCode(aPacket, aPacketSize, opCode) || opCode!=OpDmx || aPacketSize<18) {
    return false;
  }
  if (!supportedProtocolVersion(aPacket)) {
    return false;
  }
  size_t payloadLength = readBE16(aPacket+16);
  if (payloadLength<2 || payloadLength>maxDmxSlots || (payloadLength & 1)!=0) {
    return false;
  }
  if (aPacketSize<18+payloadLength) {
    return false;
  }
  uint16_t portAddress = readLE16(aPacket+14);
  if (portAddress>maxPortAddress) {
    return false;
  }
  aDmx.portAddress = portAddress;
  aDmx.sequence = aPacket[12];
  aDmx.physical = aPacket[13];
  aDmx.data = aPacket+18;
  aDmx.length = payloadLength;
  return true;
}


static bool parseIPv4(const string &aAddress, uint8_t *aBytes)
{
  struct in_addr addr;
  if (inet_pton(AF_INET, aAddress.c_str(), &addr)!=1) {
    return false;
  }
  memcpy(aBytes, &addr.s_addr, 4);
  return true;
}


static bool parseMac(const string &aAddress, uint8_t *aBytes)
{
  unsigned int b[6];
  if (sscanf(aAddress.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5])!=6) {
    return false;
  }
  for (int i = 0; i<6; i++) {
    if (b[i]>0xFF) return false;
    aBytes[i] = b[i];
  }
  return true;
}


bool ArtNet::encodeArtPollReply(const ArtNetAdvertisementInfo &aInfo, vector<uint8_t> &aPacket)
{
  if (aInfo.portAddress>maxPortAddress) {
    return false;
  }
  aPacket.assign(pollReplySize, 0);
  memcpy(&aPacket[0], artNetId, sizeof(artNetId));
  writeLE16(&aPacket[8], OpPollReply);
  if (!aInfo.ipAddress.empty()) {
    if (!parseIPv4(aInfo.ipAddress, &aPacket[10])) {
      return false;
    }
  }
  writeLE16(&aPacket[14], defaultPort);
  writeBE16(&aPacket[16], aInfo.versionInfo);
  aPacket[18] = artNetNet(aInfo.portAddress);
  aPacket[19] = artNetSubNet(aInfo.portAddress);
  writeBE16(&aPacket[20], aInfo.oemCode);
  aPacket[22] = 0; // UBEA not present
  aPacket[23] = aInfo.status1;
  writeLE16(&aPacket[24], aInfo.estaManufacturerCode);
  writeFixedString(&aPacket[26], 18, aInfo.shortName);
  writeFixedString(&aPacket[44], 64, aInfo.longName);
  writeFixedString(&aPacket[108], 64, aInfo.nodeReport);
  writeBE16(&aPacket[172], 1); // one configured universe
  aPacket[174] = 0x80; // Port can output DMX512 data received from Art-Net
  aPacket[182] = 0x80; // data is being output from network to endpoint
  aPacket[190] = artNetUniverse(aInfo.portAddress);
  aPacket[200] = aInfo.style;
  if (!aInfo.macAddress.empty()) {
    if (!parseMac(aInfo.macAddress, &aPacket[201])) {
      return false;
    }
  }
  if (!aInfo.ipAddress.empty()) {
    memcpy(&aPacket[207], &aPacket[10], 4);
  }
  aPacket[211] = 1; // BindIndex, root device
  aPacket[212] = aInfo.status2;
  aPacket[217] = aInfo.status3;
  return true;
}


// MARK: - ArtNetReceiver

ArtNetReceiver::ArtNetReceiver(MainLoop &aMainLoop) :
  inherited(),
  mMainLoop(aMainLoop),
  mPortAddress(0),
  mSourceTimeout(2500*MilliSecond),
  mHasActiveSource(false),
  mLastSequence(0),
  mReceivedFrames(0),
  mMalformedPackets(0)
{
  mAdvertisementInfo.portAddress = mPortAddress;
}


ArtNetReceiver::~ArtNetReceiver()
{
  stopArtNet();
}


void ArtNetReceiver::setConnectionParams(uint16_t aPortAddress, const char *aBindAddress, const char *aInterfaceName, MLMicroSeconds aSourceTimeout)
{
  mPortAddress = aPortAddress & maxPortAddress;
  mAdvertisementInfo.portAddress = mPortAddress;
  mBindAddress = nonNullCStr(aBindAddress);
  mInterfaceName = nonNullCStr(aInterfaceName);
  mSourceTimeout = aSourceTimeout;
}


void ArtNetReceiver::setAdvertisementInfo(const ArtNetAdvertisementInfo &aInfo)
{
  mAdvertisementInfo = aInfo;
  mAdvertisementInfo.portAddress = mPortAddress;
}


void ArtNetReceiver::setDmxHandler(ArtNetDmxCB aDmxHandler)
{
  mDmxHandler = aDmxHandler;
}


ErrorPtr ArtNetReceiver::startArtNet()
{
  stopArtNet();
  mSocket = new SocketComm(mMainLoop);
  mSocket->setConnectionParams(mBindAddress.empty() ? NULL : mBindAddress.c_str(), "6454", SOCK_DGRAM, AF_INET, IPPROTO_UDP, mInterfaceName.empty() ? NULL : mInterfaceName.c_str());
  mSocket->setAllowNonlocalConnections(true);
  mSocket->setDatagramOptions(true, true);
  mSocket->setReceiveHandler(boost::bind(&ArtNetReceiver::receiveHandler, this, _1));
  mSocket->setConnectionStatusHandler(boost::bind(&ArtNetReceiver::socketStatusHandler, this, _2));
  ErrorPtr err = mSocket->initiateConnection();
  if (Error::isOK(err)) {
    OLOG(LOG_INFO, "Art-Net receiver starting on UDP port 6454, Port-Address %u", mPortAddress);
  }
  return err;
}


void ArtNetReceiver::stopArtNet()
{
  mSourceTimeoutTicket.cancel();
  mHasActiveSource = false;
  if (mSocket) {
    mSocket->clearCallbacks();
    mSocket->closeConnection();
    mSocket = NULL;
  }
}


bool ArtNetReceiver::isRunning() const
{
  return mSocket && mSocket->connected();
}


void ArtNetReceiver::socketStatusHandler(ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    OLOG(LOG_WARNING, "Art-Net socket status: %s", aError->text());
  }
}


void ArtNetReceiver::receiveHandler(ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    OLOG(LOG_WARNING, "Art-Net receive error: %s", aError->text());
    return;
  }
  uint8_t buffer[1024];
  ErrorPtr err;
  while (mSocket) {
    size_t received = mSocket->receiveBytes(sizeof(buffer), buffer, err);
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "Art-Net receive failed: %s", err->text());
      break;
    }
    if (received==0) {
      break;
    }
    string senderAddress, senderPort;
    if (!mSocket->getDatagramOrigin(senderAddress, senderPort)) {
      senderAddress.clear();
    }
    processPacket(buffer, received, senderAddress);
  }
}


void ArtNetReceiver::processPacket(const uint8_t *aData, size_t aSize, const string &aSenderAddress)
{
  uint16_t opCode;
  if (!decodeOpCode(aData, aSize, opCode)) {
    mMalformedPackets++;
    return;
  }
  if (opCode==OpPoll) {
    ArtPollView poll;
    if (decodeArtPoll(aData, aSize, poll)) {
      handlePoll(poll, aSenderAddress);
    }
    else {
      mMalformedPackets++;
    }
  }
  else if (opCode==OpDmx) {
    ArtDmxView dmx;
    if (decodeArtDmx(aData, aSize, dmx)) {
      handleDmx(dmx, aSenderAddress);
    }
    else {
      mMalformedPackets++;
    }
  }
}


void ArtNetReceiver::handlePoll(const ArtPollView &aPoll, const string &aSenderAddress)
{
  (void)aPoll;
  vector<uint8_t> reply;
  if (!encodeArtPollReply(mAdvertisementInfo, reply)) {
    OLOG(LOG_WARNING, "cannot encode ArtPollReply, advertisement info is invalid");
    return;
  }
  ErrorPtr err;
  const char *destination = aSenderAddress.empty() ? "255.255.255.255" : aSenderAddress.c_str();
  mSocket->transmitDatagramTo(destination, "6454", reply.size(), &reply[0], err);
  if (Error::notOK(err)) {
    OLOG(LOG_WARNING, "cannot send ArtPollReply to %s: %s", destination, err->text());
  }
}


void ArtNetReceiver::handleDmx(const ArtDmxView &aDmx, const string &aSenderAddress)
{
  if (aDmx.portAddress!=mPortAddress) {
    return;
  }
  MLMicroSeconds now = MainLoop::now();
  if (mHasActiveSource && aSenderAddress!=mActiveSource.address) {
    if (mSourceTimeout>0 && now-mActiveSource.receivedAt>=mSourceTimeout) {
      OLOG(LOG_INFO, "Art-Net source %s timed out, accepting %s", mActiveSource.address.c_str(), aSenderAddress.c_str());
      mHasActiveSource = false;
    }
    else {
      return;
    }
  }
  if (!mHasActiveSource) {
    OLOG(LOG_INFO, "Art-Net source active: %s", aSenderAddress.c_str());
    mHasActiveSource = true;
  }
  mActiveSource.address = aSenderAddress;
  mActiveSource.sequence = aDmx.sequence;
  mActiveSource.receivedAt = now;
  mLastSequence = aDmx.sequence;
  mReceivedFrames++;
  restartSourceTimeout();
  if (mDmxHandler) {
    mDmxHandler(aDmx.portAddress, aDmx.data, aDmx.length, mActiveSource);
  }
}


void ArtNetReceiver::restartSourceTimeout()
{
  mSourceTimeoutTicket.cancel();
  if (mSourceTimeout>0 && mHasActiveSource) {
    mSourceTimeoutTicket.executeOnce(boost::bind(&ArtNetReceiver::sourceTimedOut, this), mSourceTimeout);
  }
}


void ArtNetReceiver::sourceTimedOut()
{
  if (!mHasActiveSource) return;
  if (MainLoop::now()-mActiveSource.receivedAt>=mSourceTimeout) {
    OLOG(LOG_INFO, "Art-Net source timed out: %s", mActiveSource.address.c_str());
    mHasActiveSource = false;
  }
  else {
    restartSourceTimeout();
  }
}

#endif // ENABLE_ARTNET
