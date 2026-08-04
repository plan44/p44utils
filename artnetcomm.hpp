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

#ifndef __p44utils__artnetcomm__
#define __p44utils__artnetcomm__

#include "p44utils_main.hpp"

#ifndef ENABLE_ARTNET
  // Including this file usually means Art-Net support is wanted.
  // Define ENABLE_ARTNET to 0 globally to build variants without it.
  #define ENABLE_ARTNET 1
#endif

#if ENABLE_ARTNET

#include "socketcomm.hpp"

#include <array>
#include <vector>

using namespace std;

namespace p44 {

  namespace ArtNet {

    static const uint16_t defaultPort = 6454;
    static const uint16_t protocolVersion = 14;
    static const uint16_t maxPortAddress = 0x7FFF;
    static const size_t maxDmxSlots = 512;
    static const size_t pollReplySize = 239;

    typedef enum : uint16_t {
      OpPoll = 0x2000,
      OpPollReply = 0x2100,
      OpDmx = 0x5000,
    } OpCode;

    uint16_t readLE16(const uint8_t *aBytes);
    uint16_t readBE16(const uint8_t *aBytes);
    void writeLE16(uint8_t *aBytes, uint16_t aValue);
    void writeBE16(uint8_t *aBytes, uint16_t aValue);
    void writeFixedString(uint8_t *aDestination, size_t aFieldSize, const string &aValue);
    uint8_t artNetNet(uint16_t aPortAddress);
    uint8_t artNetSubNet(uint16_t aPortAddress);
    uint8_t artNetUniverse(uint16_t aPortAddress);
    uint16_t makeArtNetPortAddress(uint8_t aNet, uint8_t aSubNet, uint8_t aUniverse);

    typedef struct {
      uint8_t talkToMe;
      uint8_t priority;
    } ArtPollView;

    typedef struct {
      uint16_t portAddress;
      uint8_t sequence;
      uint8_t physical;
      const uint8_t *data;
      size_t length;
    } ArtDmxView;

    typedef struct ArtNetAdvertisementInfo {
      string shortName;
      string longName;
      string nodeReport;
      string ipAddress;
      string macAddress;
      uint16_t portAddress;
      uint16_t oemCode;
      uint16_t estaManufacturerCode;
      uint16_t versionInfo;
      uint8_t status1;
      uint8_t status2;
      uint8_t status3;
      uint8_t style;
      ArtNetAdvertisementInfo();
    } ArtNetAdvertisementInfo;

    bool decodeOpCode(const uint8_t *aPacket, size_t aPacketSize, uint16_t &aOpCode);
    bool decodeArtPoll(const uint8_t *aPacket, size_t aPacketSize, ArtPollView &aPoll);
    bool decodeArtDmx(const uint8_t *aPacket, size_t aPacketSize, ArtDmxView &aDmx);
    bool encodeArtPollReply(const ArtNetAdvertisementInfo &aInfo, vector<uint8_t> &aPacket);

  }

  namespace Sacn {

    static const uint16_t defaultPort = 5568;
    static const uint16_t maxUniverse = 63999;
    static const size_t maxDmxSlots = 512;

    typedef struct {
      uint16_t universe;
      uint8_t priority;
      uint8_t sequence;
      const uint8_t *data;
      size_t length;
    } SacnDmxView;

    uint16_t sacnUniverseFromPortAddress(uint16_t aPortAddress);
    void sacnMulticastAddress(uint16_t aUniverse, string &aAddress);
    bool decodeSacnDmx(const uint8_t *aPacket, size_t aPacketSize, SacnDmxView &aDmx);

  }


  typedef struct {
    string address;
    uint8_t sequence;
    MLMicroSeconds receivedAt;
  } ArtNetSource;

  typedef boost::function<void (uint16_t aPortAddress, const uint8_t *aData, size_t aLength, const ArtNetSource &aSource)> ArtNetDmxCB;

  class ArtNetReceiver : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;

    SocketCommPtr mSocket;
    SocketCommPtr mSacnSocket;
    MainLoop &mMainLoop;
    ArtNetDmxCB mDmxHandler;
    ArtNet::ArtNetAdvertisementInfo mAdvertisementInfo;
    string mBindAddress;
    string mInterfaceName;
    uint16_t mPortAddress;
    MLMicroSeconds mSourceTimeout;
    ArtNetSource mActiveSource;
    bool mHasActiveSource;
    uint8_t mLastSequence;
    uint64_t mReceivedFrames;
    uint64_t mMalformedPackets;
    MLTicket mSourceTimeoutTicket;

  public:
    ArtNetReceiver(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~ArtNetReceiver();

    virtual string contextType() const P44_OVERRIDE { return "artnet receiver"; };

    /// Configure the receiver. Art-Net Port-Address is zero based on the wire, 0..32767.
    void setConnectionParams(uint16_t aPortAddress = 0, const char *aBindAddress = NULL, const char *aInterfaceName = NULL, MLMicroSeconds aSourceTimeout = 2500*MilliSecond);
    void setAdvertisementInfo(const ArtNet::ArtNetAdvertisementInfo &aInfo);
    void setDmxHandler(ArtNetDmxCB aDmxHandler);

    ErrorPtr startArtNet();
    void stopArtNet();
    bool isRunning() const;

    uint16_t portAddress() const { return mPortAddress; };
    bool hasActiveSource() const { return mHasActiveSource; };
    ArtNetSource activeSource() const { return mActiveSource; };
    uint64_t receivedFrames() const { return mReceivedFrames; };
    uint64_t malformedPackets() const { return mMalformedPackets; };

  private:
    void socketStatusHandler(ErrorPtr aError);
    void receiveHandler(ErrorPtr aError);
    void sacnSocketStatusHandler(ErrorPtr aError);
    void sacnReceiveHandler(ErrorPtr aError);
    ErrorPtr startSacn();
    void stopSacn();
    ErrorPtr joinSacnMulticast();
    void processPacket(const uint8_t *aData, size_t aSize, const string &aSenderAddress, const string &aSenderPort);
    void processSacnPacket(const uint8_t *aData, size_t aSize, const string &aSenderAddress);
    void handlePoll(const ArtNet::ArtPollView &aPoll, const string &aSenderAddress, const string &aSenderPort);
    void handleDmx(const ArtNet::ArtDmxView &aDmx, const string &aSenderAddress);
    void handleDmxData(uint16_t aPortAddress, uint8_t aSequence, const uint8_t *aData, size_t aLength, const string &aSenderAddress);
    void restartSourceTimeout();
    void sourceTimedOut();
  };
  typedef boost::intrusive_ptr<ArtNetReceiver> ArtNetReceiverPtr;

}

#endif // ENABLE_ARTNET
#endif // __p44utils__artnetcomm__
