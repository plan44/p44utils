//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "igmp.hpp"

using namespace p44;

#ifdef __APPLE__

#warning "IGMP tools currently not supported on Apple platforms - only dummy"

size_t p44::sendIGMP(uint8_t aType, uint8_t aMaxRespTime, const char *aGroupAddress, const char* aSourceAddress)
{
  return 0; // dummy
}

int p44::sendRawPacket(struct sockaddr_in *aSockAddrP, const char *aDatagramP, size_t aDatagramSize)
{
  return 0; // dummy
}

#else

// Linux

#include <string.h> // memset
#include <arpa/inet.h> // for inet_addr
#include <unistd.h> // For getopt/optarg
#include <sys/socket.h>	// for socket ofcourse
#include <stdlib.h> // for exit(0);
#include <errno.h> // For errno - the error number
#include <netinet/ip.h>	// Provides declarations for IP header
#include <linux/igmp.h> // provides IGMP group addresses such as IGMP_ALL_HOSTS


// Generic checksum calculation function
static unsigned short csum(unsigned short *ptr,int nbytes)
{
  register long sum;
  unsigned short oddbyte;
  register short answer;

  sum=0;
  while(nbytes>1) {
    sum+=*ptr++;
    nbytes-=2;
  }
  if(nbytes==1) {
    oddbyte=0;
    *((unsigned char *)&oddbyte)=*(unsigned char *)ptr;
    sum+=oddbyte;
  }

  sum = (sum>>16)+(sum & 0xffff);
  sum = sum + (sum>>16);
  answer=(short)~sum;

  return(answer);
}


size_t p44::sendIGMP(uint8_t aType, uint8_t aMaxRespTime, const char *aGroupAddress, const char* aSourceAddress)
{
  const size_t maxDatagramSize = 50;
  char datagram[maxDatagramSize];
  struct sockaddr_in sin;

  // zero out the packet buffer
  memset(datagram, 0, maxDatagramSize);

  // IP header
  struct iphdr *iph = (struct iphdr *)datagram;

  // IGMP header (actually, there's nothing but headers for IGMP)
  struct igmp *igmph = (struct igmp *)(datagram + sizeof(struct iphdr));

  in_addr_t groupAddress = 0; // default to no group address
  if (aGroupAddress) {
    // specific group
    groupAddress = ::inet_addr(aGroupAddress);
  }

  in_addr_t sourceAddress = 0; // default to no source address
  if (aSourceAddress) {
    // specific group
    sourceAddress = ::inet_addr(aSourceAddress);
  }

  // Fill in the IP Header
  iph->ihl = 5;
  iph->version = 4;
  iph->tos = 0;
  iph->tot_len = sizeof(struct iphdr) + sizeof(struct igmp); // total length of packet
  iph->id = htonl (rand() & 0xFFFF);	// Id of this packet
  iph->frag_off = 0;
  iph->ttl = 1; // do not cross LAN boundary!
  iph->protocol = IPPROTO_IGMP;
  iph->check = 0; // must be 0 for checksum calculation
  iph->saddr = sourceAddress; // source
  // destination
  if (aType==IGMP_MEMBERSHIP_QUERY) {
    // group specific queries go to the group, general ones to all hosts
    iph->daddr = aGroupAddress ? groupAddress : IGMP_ALL_HOSTS;
  }
  else if (aType==IGMP_V2_LEAVE_GROUP) {
    // leave messages go to all routers (older linux send it wrongly to the group, though)
    iph->daddr = IGMP_ALL_ROUTER;
  }
  else {
    // must be V1 or V2 membership report, goes to the group
    iph->daddr = groupAddress;
  }
  // IP header checksum
  iph->check = csum((unsigned short *)iph, sizeof(struct iphdr));

  // Fill in IGMP
  igmph->igmp_type = aType; // 0x11 = query, 0x12 = host membership report, 0x17 = leave group message
  igmph->igmp_code = aMaxRespTime; // max response time code in 1/10 seconds, unused==0 in IGMP v1
  igmph->igmp_cksum = 0; // must be 0 for checksum calculation
  igmph->igmp_group.s_addr = groupAddress; // 0==general query, group address when query is to be group specific
  // IGMP checksum
  igmph->igmp_cksum = csum((unsigned short *)igmph, sizeof(struct igmp));

  // fill address struct needed for sendto()
  sin.sin_family = AF_INET;
  sin.sin_port = htons(80); // dummy
  sin.sin_addr.s_addr = iph->daddr; // destination address

  // send packet
  return sendRawPacket(&sin, datagram, iph->tot_len);
}



int p44::sendRawPacket(struct sockaddr_in *aSockAddrP, const char *aDatagramP, size_t aDatagramSize)
{
  // Create a raw socket
  int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
  if(s == -1) {
    return errno;
  }
  // IP_HDRINCL to tell the kernel that headers are included in the packet
  int one = 1;
  const int *val = &one;
  if (setsockopt (s, IPPROTO_IP, IP_HDRINCL, val, sizeof (one)) < 0) {
    close(s);
    return errno;
  }
  // Send the packet
  if (sendto (s, aDatagramP, aDatagramSize, 0 , (struct sockaddr *)aSockAddrP, sizeof (*aSockAddrP)) < 0) {
    close(s);
    return errno;
  }
  close(s);
  return 0;
}


#endif // not __APPLE__
