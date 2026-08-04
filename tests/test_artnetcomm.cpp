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

#include "catch_amalgamated.hpp"

#include "artnetcomm.hpp"

using namespace p44;
using namespace p44::ArtNet;

static vector<uint8_t> artDmxPacket(uint16_t aPortAddress, size_t aLength)
{
  vector<uint8_t> packet(18+aLength, 0);
  const uint8_t id[8] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0 };
  memcpy(&packet[0], id, sizeof(id));
  writeLE16(&packet[8], OpDmx);
  writeBE16(&packet[10], protocolVersion);
  packet[12] = 7;
  packet[13] = 1;
  writeLE16(&packet[14], aPortAddress);
  writeBE16(&packet[16], aLength);
  for (size_t i = 0; i<aLength; i++) {
    packet[18+i] = i & 0xFF;
  }
  return packet;
}


static vector<uint8_t> artPollPacket(uint8_t aTalkToMe)
{
  vector<uint8_t> packet(14, 0);
  const uint8_t id[8] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0 };
  memcpy(&packet[0], id, sizeof(id));
  writeLE16(&packet[8], OpPoll);
  writeBE16(&packet[10], protocolVersion);
  packet[12] = aTalkToMe;
  packet[13] = 0x10;
  return packet;
}


static vector<uint8_t> sacnPacket(uint16_t aUniverse, size_t aLength)
{
  vector<uint8_t> packet(126+aLength, 0);
  packet[0] = 0x00;
  packet[1] = 0x10;
  memcpy(&packet[4], "ASC-E1.17", 9);
  packet[13] = 0;
  packet[14] = 0;
  packet[15] = 0;
  writeBE16(&packet[16], 0x7000 | (packet.size()-16));
  packet[21] = 0x04; // VECTOR_ROOT_E131_DATA
  writeBE16(&packet[38], 0x7000 | (packet.size()-38));
  packet[43] = 0x02; // VECTOR_E131_DATA_PACKET
  memcpy(&packet[44], "lux", 3);
  packet[108] = 100;
  packet[111] = 9;
  writeBE16(&packet[113], aUniverse);
  writeBE16(&packet[115], 0x7000 | (packet.size()-115));
  packet[117] = 0x02;
  packet[118] = 0xA1;
  writeBE16(&packet[121], 1);
  writeBE16(&packet[123], aLength+1);
  packet[125] = 0;
  for (size_t i = 0; i<aLength; i++) {
    packet[126+i] = (i+1) & 0xFF;
  }
  return packet;
}


TEST_CASE("Art-Net endian and universe helpers", "[artnet]")
{
  uint8_t b[2];
  writeLE16(b, 0x1234);
  REQUIRE(b[0]==0x34);
  REQUIRE(b[1]==0x12);
  REQUIRE(readLE16(b)==0x1234);
  writeBE16(b, 0x1234);
  REQUIRE(b[0]==0x12);
  REQUIRE(b[1]==0x34);
  REQUIRE(readBE16(b)==0x1234);

  uint16_t pa = makeArtNetPortAddress(0x12, 0x03, 0x04);
  REQUIRE(pa==0x1200+0x30+0x04);
  REQUIRE(artNetNet(pa)==0x12);
  REQUIRE(artNetSubNet(pa)==0x03);
  REQUIRE(artNetUniverse(pa)==0x04);
}


TEST_CASE("Art-Net fixed strings are zero filled and truncated", "[artnet]")
{
  uint8_t field[5];
  writeFixedString(field, sizeof(field), "abcdef");
  REQUIRE(field[0]=='a');
  REQUIRE(field[1]=='b');
  REQUIRE(field[2]=='c');
  REQUIRE(field[3]=='d');
  REQUIRE(field[4]==0);
}


TEST_CASE("ArtDmx decoder validates packet bounds and fields", "[artnet]")
{
  vector<uint8_t> packet = artDmxPacket(0x1234, 512);
  ArtDmxView dmx;
  REQUIRE(decodeArtDmx(&packet[0], packet.size(), dmx));
  REQUIRE(dmx.portAddress==0x1234);
  REQUIRE(dmx.sequence==7);
  REQUIRE(dmx.physical==1);
  REQUIRE(dmx.length==512);
  REQUIRE(dmx.data[255]==0xFF);

  vector<uint8_t> minimum = artDmxPacket(0, 2);
  REQUIRE(decodeArtDmx(&minimum[0], minimum.size(), dmx));
  REQUIRE(dmx.portAddress==0);
  REQUIRE(dmx.length==2);

  vector<uint8_t> truncatedHeader = packet;
  truncatedHeader.resize(17);
  REQUIRE_FALSE(decodeArtDmx(&truncatedHeader[0], truncatedHeader.size(), dmx));

  vector<uint8_t> truncatedPayload = packet;
  truncatedPayload.resize(packet.size()-1);
  REQUIRE_FALSE(decodeArtDmx(&truncatedPayload[0], truncatedPayload.size(), dmx));

  vector<uint8_t> oddLength = artDmxPacket(0, 3);
  REQUIRE_FALSE(decodeArtDmx(&oddLength[0], oddLength.size(), dmx));

  vector<uint8_t> tooShort = artDmxPacket(0, 0);
  REQUIRE_FALSE(decodeArtDmx(&tooShort[0], tooShort.size(), dmx));

  vector<uint8_t> badPort = artDmxPacket(0x8000, 2);
  REQUIRE_FALSE(decodeArtDmx(&badPort[0], badPort.size(), dmx));

  packet[0] = 'X';
  REQUIRE_FALSE(decodeArtDmx(&packet[0], packet.size(), dmx));
}


TEST_CASE("ArtPoll decoder validates protocol and talk-to-me", "[artnet]")
{
  vector<uint8_t> packet = artPollPacket(0x01);
  ArtPollView poll;
  REQUIRE(decodeArtPoll(&packet[0], packet.size(), poll));
  REQUIRE(poll.talkToMe==0x01);
  REQUIRE(poll.priority==0x10);

  vector<uint8_t> truncated = packet;
  truncated.resize(13);
  REQUIRE_FALSE(decodeArtPoll(&truncated[0], truncated.size(), poll));

  packet[11] = protocolVersion-1;
  REQUIRE_FALSE(decodeArtPoll(&packet[0], packet.size(), poll));
}


TEST_CASE("sACN helpers map Art-Net Port-Address to multicast universe", "[artnet][sacn]")
{
  string group;
  REQUIRE(Sacn::sacnUniverseFromPortAddress(0)==1);
  REQUIRE(Sacn::sacnUniverseFromPortAddress(299)==300);
  Sacn::sacnMulticastAddress(1, group);
  REQUIRE(group=="239.255.0.1");
  Sacn::sacnMulticastAddress(300, group);
  REQUIRE(group=="239.255.1.44");
}


TEST_CASE("sACN decoder validates Lux-shaped E1.31 data packets", "[artnet][sacn]")
{
  vector<uint8_t> packet = sacnPacket(1, 512);
  Sacn::SacnDmxView dmx;
  REQUIRE(Sacn::decodeSacnDmx(&packet[0], packet.size(), dmx));
  REQUIRE(dmx.universe==1);
  REQUIRE(dmx.priority==100);
  REQUIRE(dmx.sequence==9);
  REQUIRE(dmx.length==512);
  REQUIRE(dmx.data[0]==1);
  REQUIRE(dmx.data[255]==0);
  REQUIRE(dmx.data[511]==0);

  vector<uint8_t> minimum = sacnPacket(300, 1);
  REQUIRE(Sacn::decodeSacnDmx(&minimum[0], minimum.size(), dmx));
  REQUIRE(dmx.universe==300);
  REQUIRE(dmx.length==1);

  vector<uint8_t> truncated = packet;
  truncated.resize(packet.size()-1);
  REQUIRE_FALSE(Sacn::decodeSacnDmx(&truncated[0], truncated.size(), dmx));

  vector<uint8_t> badStartCode = packet;
  badStartCode[125] = 0xCC;
  REQUIRE_FALSE(Sacn::decodeSacnDmx(&badStartCode[0], badStartCode.size(), dmx));

  vector<uint8_t> badUniverse = sacnPacket(0, 1);
  REQUIRE_FALSE(Sacn::decodeSacnDmx(&badUniverse[0], badUniverse.size(), dmx));

  vector<uint8_t> badId = packet;
  badId[4] = 'X';
  REQUIRE_FALSE(Sacn::decodeSacnDmx(&badId[0], badId.size(), dmx));
}


TEST_CASE("ArtPollReply encoder builds expected fixed fields", "[artnet]")
{
  ArtNetAdvertisementInfo info;
  info.shortName = "short";
  info.longName = "long node";
  info.nodeReport = "#0001 [0000] OK";
  info.ipAddress = "192.168.23.42";
  info.macAddress = "02:11:22:33:44:55";
  info.portAddress = makeArtNetPortAddress(1, 2, 3);
  info.oemCode = 0x1234;
  info.estaManufacturerCode = 0x5678;
  info.versionInfo = 0x0102;
  info.status1 = 0xD0;
  info.status2 = 0x08;
  info.status3 = 0x01;
  info.style = 0x00;

  vector<uint8_t> reply;
  REQUIRE(encodeArtPollReply(info, reply));
  REQUIRE(reply.size()==pollReplySize);
  REQUIRE(memcmp(&reply[0], "Art-Net", 7)==0);
  REQUIRE(reply[7]==0);
  REQUIRE(readLE16(&reply[8])==OpPollReply);
  REQUIRE(reply[10]==192);
  REQUIRE(reply[11]==168);
  REQUIRE(reply[12]==23);
  REQUIRE(reply[13]==42);
  REQUIRE(readLE16(&reply[14])==defaultPort);
  REQUIRE(readBE16(&reply[16])==0x0102);
  REQUIRE(reply[18]==1);
  REQUIRE(reply[19]==2);
  REQUIRE(readBE16(&reply[20])==0x1234);
  REQUIRE(readLE16(&reply[24])==0x5678);
  REQUIRE(string((const char *)&reply[26])=="short");
  REQUIRE(string((const char *)&reply[44])=="long node");
  REQUIRE(readBE16(&reply[172])==1);
  REQUIRE(reply[190]==3);
  REQUIRE(reply[201]==0x02);
  REQUIRE(reply[206]==0x55);
  REQUIRE(reply[207]==192);
  REQUIRE(reply[211]==1);
  REQUIRE(reply[212]==0x08);
  REQUIRE(reply[217]==0x01);
}
