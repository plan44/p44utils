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

#include "macaddress.hpp"

#include <stdlib.h>
#include <string.h>

using namespace p44;

#ifdef __APPLE__

#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_dl.h>

#include <ifaddrs.h>
#include <arpa/inet.h>

// always use interface en0 (every Apple device has one, it's considered the main interface. Nowadays it's usually WiFi)
#define APPLE_DEFAULT_IF_NAME "en0"

bool p44::getIfInfo(uint64_t *aMacAddressP, uint32_t *aIPv4AddressP, int *aIfIndexP)
{
  bool found = false;

  // MAC address
  if (aMacAddressP) {
    int mgmtInfoBase[6];
    char *msgBuffer = NULL;
    size_t length;
    unsigned char macAddress[6];
    struct if_msghdr *interfaceMsgStruct;
    struct sockaddr_dl *socketStruct;
    // Setup the management Information Base (mib)
    mgmtInfoBase[0] = CTL_NET; // Request network subsystem
    mgmtInfoBase[1] = AF_ROUTE; // Routing table info
    mgmtInfoBase[2] = 0;
    mgmtInfoBase[3] = AF_LINK; // Request link layer information
    mgmtInfoBase[4] = NET_RT_IFLIST; // Request all configured interfaces
    // With all configured interfaces requested, get handle index
    if ((mgmtInfoBase[5] = if_nametoindex(APPLE_DEFAULT_IF_NAME)) == 0) {
      return false; // failed
    }
    else {
      // Get the size of the data available (store in len)
      if (sysctl(mgmtInfoBase, 6, NULL, &length, NULL, 0) < 0) {
        return false; // failed
      }
      else {
        // Alloc memory based on above call
        if ((msgBuffer = (char *)malloc(length)) == NULL) {
          return false; // failed
        }
        else {
          // Get system information, store in buffer
          if (sysctl(mgmtInfoBase, 6, msgBuffer, &length, NULL, 0) < 0) {
            free(msgBuffer); // Release the buffer memory
            return false; // failed
          }
        }
      }
    }
    // Map msgbuffer to interface message structure
    interfaceMsgStruct = (struct if_msghdr *) msgBuffer;
    // Map to link-level socket structure
    socketStruct = (struct sockaddr_dl *) (interfaceMsgStruct + 1);
    // Copy link layer address data in socket structure to an array
    memcpy(&macAddress, socketStruct->sdl_data + socketStruct->sdl_nlen, 6);
    free(msgBuffer); // Release the buffer memory
    // compose int64
    uint64_t mac = 0;
    for (int i=0; i<6; ++i) {
      mac = (mac<<8) + macAddress[i];
    }
    if (mac!=0) {
      found = true;
      *aMacAddressP = mac;
      // save the interface index
      if (aIfIndexP) *aIfIndexP = mgmtInfoBase[5];
    }
  }
  // IPv4 address
  if (aIPv4AddressP) {
    // From: http://zachwaugh.me/posts/programmatically-retrieving-ip-address-of-iphone/ (MIT licensed)
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;
    int success = 0;
    uint32_t ip = 0;
    // retrieve the current interfaces - returns 0 on success
    success = getifaddrs(&interfaces);
    if (success == 0) {
      // Loop through linked list of interfaces
      temp_addr = interfaces;
      while (temp_addr != NULL) {
        if(temp_addr->ifa_addr->sa_family == AF_INET) {
          // get IP from first IF which is not the loopback ("lo0" on Apple)
          if (strcmp(temp_addr->ifa_name,"lo0")!=0) {
            uint8_t *addr = (uint8_t *)&(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr.s_addr);
            ip = (addr[0]<<24) + (addr[1]<<16) + (addr[2]<<8) + addr[3];
            if (ip!=0) {
              *aIPv4AddressP = ip;
              found = true;
              break;
            }
          }
        }
        temp_addr = temp_addr->ifa_next;
      }
    }
    // Free memory
    freeifaddrs(interfaces);
  }
  return found;
}


#include "mainloop.hpp"

bool p44::getMacAddressByIpv4(uint32_t aIPv4Address, uint64_t &aMacAddress)
{
  #warning "Q&D version using blocking system() and popen() calls"
  bool found = false;
  // ping to make sure we have the IP in the cache
  const size_t bufSize = 100;
  char buf[bufSize];
  snprintf(buf, bufSize, "ping -c 1 %d.%d.%d.%d",
    (aIPv4Address>>24) & 0xFF,
    (aIPv4Address>>16) & 0xFF,
    (aIPv4Address>>8) & 0xFF,
    aIPv4Address & 0xFF
  );
  system(buf);
  // use ARP to get MAC
  snprintf(buf, bufSize, "arp %d.%d.%d.%d",
    (aIPv4Address>>24) & 0xFF,
    (aIPv4Address>>16) & 0xFF,
    (aIPv4Address>>8) & 0xFF,
    aIPv4Address & 0xFF
  );
  FILE *fp = popen(buf, "r");
  if (fp==NULL) return false;
  // expect single line of the form:
  // ? (192.168.59.64) at 5c:cf:7f:12:4f:b5 on en9 ifscope [ethernet]
  short mb[6];
  if (fgets(buf, bufSize, fp) != NULL) {
    // we're only interested in the first line
    const char *p = strstr(buf, ") at ");
    if (p!=NULL) {
      found = sscanf(p+5,"%02hX:%02hX:%02hX:%02hX:%02hX:%02hX", &mb[0], &mb[1], &mb[2], &mb[3], &mb[4], &mb[5])==6;
    }
  }
  if (pclose(fp)==-1)
    found = false;
  if (found) {
    aMacAddress = 0;
    for (int i=0; i<6; i++) {
      aMacAddress = (aMacAddress<<8) + mb[i];
    }
  }
  return found;
}


#else

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <linux/sockios.h>


bool p44::getIfInfo(uint64_t *aMacAddressP, uint32_t *aIPv4AddressP, int *aIfIndex)
{
  int sock;
  int ifIndex;
  struct ifreq ifr;
  int res;
  uint64_t mac = 0;
  uint32_t ip = 0;
  bool found = false;

  // any socket type will do
  sock = socket(PF_INET, SOCK_DGRAM, 0);
  if (sock>=0) {
    // enumerate interfaces
    ifIndex = 1; // start with 1
    do {
      // - init struct
      memset(&ifr, 0x00, sizeof(ifr));
      // - get name of interface by index
      ifr.ifr_ifindex = ifIndex;
      res = ioctl(sock, SIOCGIFNAME, &ifr);
      if (res<0) {
        break; // no more names, end
      }
      // got name for index
      // - get flags for it
      if (ioctl(sock, SIOCGIFFLAGS, &ifr)>=0) {
        // skip loopback interfaces
        if ((ifr.ifr_flags & IFF_LOOPBACK)==0) {
          // not loopback
          // - now get HWADDR
          if (aMacAddressP && ioctl(sock, SIOCGIFHWADDR, &ifr)>=0) {
            // compose int64
            for (int i=0; i<6; ++i) {
              mac = (mac<<8) + ((uint8_t *)(ifr.ifr_hwaddr.sa_data))[i];
            }
            // this is our MAC unless it is zero
            if (mac!=0) {
              // save the interface index
              if (aIfIndex) *aIfIndex = ifIndex;
              *aMacAddressP = mac; // found, return it
              found=true; // done, use it (even if IP is 0)
            }
          }
          // - also get IPv4
          if (aIPv4AddressP && ioctl(sock, SIOCGIFADDR, &ifr)>=0) {
            for (int i=0; i<4; ++i) {
              if (ifr.ifr_addr.sa_family==AF_INET) {
                // is IPv4
                struct sockaddr_in *ipv4 = (struct sockaddr_in *)&(ifr.ifr_addr);
                ip = (ip<<8) + ((uint8_t *)&(ipv4->sin_addr.s_addr))[i];
              }
            }
            *aIPv4AddressP = ip;
            found = true;
          }
          // done if found something
          if (found)
            break;
        }
      }
      // next
      ifIndex++;
    } while(true);
    close(sock);
  }
  return found;
}

//  #include <linux/if_packet.h>
//  #include <linux/if_ether.h>
//  #include <linux/if_arp.h>
//
//
//  #define PROTO_ARP 0x0806
//  #define ETH2_HEADER_LEN 14
//  #define HW_TYPE 1
//  #define PROTOCOL_TYPE 0x800
//  #define MAC_LENGTH 6
//  #define IPV4_LENGTH 4
//  #define ARP_REQUEST 0x01
//  #define ARP_REPLY 0x02
//  #define BUF_SIZE 60
//
//  struct arp_header {
//    unsigned short hardware_type;
//    unsigned short protocol_type;
//    unsigned char hardware_len;
//    unsigned char  protocol_len;
//    unsigned short opcode;
//    unsigned char sender_mac[MAC_LENGTH];
//    unsigned char sender_ip[IPV4_LENGTH];
//    unsigned char target_mac[MAC_LENGTH];
//    unsigned char target_ip[IPV4_LENGTH];
//  };
//
//
//  bool p44::getMacAddressByIpv4(uint32_t *aIPv4Address, uint64_t *aMacAddressP)
//  {
//    int sd;
//    unsigned char buffer[BUF_SIZE];
//    unsigned char source_ip[IPV4_LENGTH];
//    unsigned char target_ip[IPV4_LENGTH];
//    struct ifreq ifr;
//    struct ethhdr *send_req = (struct ethhdr *)buffer;
//    struct ethhdr *rcv_resp= (struct ethhdr *)buffer;
//    struct arp_header *arp_req = (struct arp_header *)(buffer+ETH2_HEADER_LEN);
//    struct arp_header *arp_resp = (struct arp_header *)(buffer+ETH2_HEADER_LEN);
//    struct sockaddr_ll socket_address;
//    int ret,length=0,ifindex;
//
//    // get my own IP, MAC and interface index
//    uint64_t myMAC;
//    uint32_t myIPv4;
//    int myIfIndex;
//    getIfInfo(&myMAC, &myIPv4, &myIfIndex);
//
//    // fill IP source
//    for (int i = 0; i<IPV4_LENGTH; i++) source_ip[i] = (myIPv4>>(8*(IPV4_LENGTH-i))) & 0xFF;
//    // fill IP destination
//    for (int i = 0; i<IPV4_LENGTH; i++) target_ip[i] = (aIPv4Address>>(8*(IPV4_LENGTH-i))) & 0xFF;
//    // fill all required buffers with MAC
//    for (int i = 0; i<MAC_LENGTH; i++) {
//      uint8_t macByte = (myMAC>>(8*(MAC_LENGTH-i))) & 0xFF;
//      // fill broadcast and unknown
//      send_req->h_dest[i] = (unsigned char)0xff;
//      arp_req->target_mac[i] = (unsigned char)0x00;
//      // fill source MAC in header, request and address
//      send_req->h_source[i] = macByte;
//      arp_req->sender_mac[i] = macByte;
//      socket_address.sll_addr[i] = macByte;
//    }
//    printf("Successfully got eth1 MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
//           send_req->h_source[0],send_req->h_source[1],send_req->h_source[2],
//           send_req->h_source[3],send_req->h_source[4],send_req->h_source[5]
//           );
//    printf(" arp_reqMAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
//           arp_req->sender_mac[0],arp_req->sender_mac[1],arp_req->sender_mac[2],
//           arp_req->sender_mac[3],arp_req->sender_mac[4],arp_req->sender_mac[5]
//           );
//    printf("socket_address MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
//           socket_address.sll_addr[0],socket_address.sll_addr[1],socket_address.sll_addr[2],
//           socket_address.sll_addr[3],socket_address.sll_addr[4],socket_address.sll_addr[5]
//           );
//
//    // prepare sockaddr_ll
//    socket_address.sll_family = AF_PACKET;
//    socket_address.sll_protocol = htons(ETH_P_ARP);
//    socket_address.sll_ifindex = myIfIndex;
//    socket_address.sll_hatype = htons(ARPHRD_ETHER);
//    socket_address.sll_pkttype = (PACKET_BROADCAST);
//    socket_address.sll_halen = MAC_LENGTH;
//    socket_address.sll_addr[6] = 0x00;
//    socket_address.sll_addr[7] = 0x00;
//    // set protocol
//    send_req->h_proto = htons(ETH_P_ARP);
//    // create ARP request
//    arp_req->hardware_type = htons(HW_TYPE);
//    arp_req->protocol_type = htons(ETH_P_IP);
//    arp_req->hardware_len = MAC_LENGTH;
//    arp_req->protocol_len = IPV4_LENGTH;
//    arp_req->opcode = htons(ARP_REQUEST);
//    for(int i = 0; i<IPV4_LENGTH; i++) {
//      arp_req->sender_ip[i]=(unsigned char)source_ip[i];
//      arp_req->target_ip[i]=(unsigned char)target_ip[i];
//    }
//    // get a raw socket descriptor.
//    if ((sd = socket (PF_PACKET, SOCK_RAW, htons (ETH_P_ALL))) < 0) {
//      perror ("socket() failed ");
//      return false;
//    }
//    buffer[32]=0x00;
//    ret = sendto(sd, buffer, 42, 0, (struct  sockaddr*)&socket_address, sizeof(socket_address));
//    if (ret == -1) {
//      perror("sendto():");
//      return false;
//    }
//    else {
//      printf(" Sent the ARP REQ \n\t");
//      for(int i = 0; i<42; i++) {
//        printf("%02X ",buffer[i]);
//        if(i % 16 ==0 && i !=0) {
//          printf("\n\t");
//        }
//      }
//    }
//    printf("\n\t");
//    memset(buffer,0x00,60);
//    while(1) {
//      length = recvfrom(sd, buffer, BUF_SIZE, 0, NULL, NULL);
//      if (length == -1) {
//        perror("recvfrom():");
//        return false;
//      }
//      if(htons(rcv_resp->h_proto) == PROTO_ARP) {
//        //if( arp_resp->opcode == ARP_REPLY )
//        printf(" RECEIVED ARP RESP len=%d \n",length);
//        printf(" Sender IP :");
//        for(index=0;index<4;index++)
//          printf("%u.",(unsigned int)arp_resp->sender_ip[index]);
//
//        printf("\n Sender MAC :");
//        for(index=0;index<6;index++)
//          printf(" %02X:",arp_resp->sender_mac[index]);
//
//        printf("\nReceiver  IP :");
//        for(index=0;index<4;index++)
//          printf(" %u.",arp_resp->target_ip[index]);
//
//        printf("\n Self MAC :");
//        for(index=0;index<6;index++)
//          printf(" %02X:",arp_resp->target_mac[index]);
//        printf("\n  :");
//        break;
//      }
//    }
//    return true;
//  }

bool p44::getMacAddressByIpv4(uint32_t aIPv4Address, uint64_t &aMacAddress)
{
  int sfd;
  bool ok = false;

  // SIOCGARP works on any AF_INET socket
  if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) != -1) {
    // construct the record for SIOCGARP
    struct arpreq areq;
    struct sockaddr_in *sin;
    memset(&areq, 0, sizeof(areq));
    sin = (struct sockaddr_in *) &areq.arp_pa;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(aIPv4Address);
    sin = (struct sockaddr_in *) &areq.arp_ha;
    sin->sin_family = ARPHRD_ETHER;
    // FIXME: I would say we don't need this, this is propably RETURNED
    //strncpy(areq.arp_dev, "eth0", 15);
    // issue request
    if (ioctl(sfd, SIOCGARP, (caddr_t) &areq) != -1) {
      // assign MAC address
      aMacAddress = 0;
      for (int i=0; i<6; i++) {
        aMacAddress = (aMacAddress<<8) + ((uint8_t *)addr->sa_data)[i];
      }
      ok = true;
    }
    close(sfd);
  }
  return ok;
}

#endif // non-apple



uint64_t p44::macAddress()
{
  uint64_t mac;
  if (getIfInfo(&mac, NULL, NULL))
    return mac;
  return 0; // none
}


uint32_t p44::ipv4Address()
{
  uint32_t ip;
  if (getIfInfo(NULL, &ip, NULL))
    return ip;
  return 0; // none
}
