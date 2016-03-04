//
//  Copyright (c) 2013-2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__igmp__
#define __p44utils__igmp__

#include <stddef.h>
#include <stdint.h>

#ifdef __APPLE__
  #define IGMP_MEMBERSHIP_QUERY 0 // dummy
  #define IGMP_V1_MEMBERSHIP_REPORT 0 // dummy
  #define IGMP_V2_MEMBERSHIP_REPORT 0 // dummy
  #define IGMP_V2_LEAVE_GROUP 0 // dummy
#else
  #include <netinet/igmp.h>	// Provides declarations for IGMP message
#endif

namespace p44 {

  /// get MAC address of this machine
  /// @param aType IGMP message type
  /// @param aMaxRespTime IGMP code / max response time field
  /// @param aGroupAddress NULL or string containing multicast group address
  /// @param aSourceAddress NULL to use default address, string to forge sender address
  /// @return size of generated datagram
  size_t sendIGMP(
    uint8_t aType,
    uint8_t aMaxRespTime,
    const char *aGroupAddress,
    const char* aSourceAddress
  );

  /// Send raw packet
  /// @param aSockAddrP pointer to a sockaddr_in that will be filled ready for use with sendto()
  /// @param aDatagramP must point to a buffer large enough to hold the IGMP packet (no checks!)
  /// @param aMaxDatagramSize size of buffer at aDatagramP
  int sendRawPacket(struct sockaddr_in *aSockAddrP, const char *aDatagramP, size_t aDatagramSize);


} // namespace p44


#endif /* defined(__p44utils__igmp__) */
