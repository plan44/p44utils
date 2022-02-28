//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// by default use interface en0 (every Apple device has one, it's considered the main interface. Nowadays it's usually WiFi)
#define APPLE_DEFAULT_IF_NAME "en0"

bool p44::getIfInfo(uint64_t *aMacAddressP, uint32_t *aIPv4AddressP, int *aIfIndexP, const char *aIfName)
{
  bool found = false;

  if (aIfName && *aIfName==0) aIfName = NULL;
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
    if ((mgmtInfoBase[5] = if_nametoindex(aIfName ? aIfName : APPLE_DEFAULT_IF_NAME)) == 0) {
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
          // get IP from specified interface (if any) or from
          // first IF which is not the loopback ("lo0" on Apple)
          if (
            (aIfName && strcmp(temp_addr->ifa_name, aIfName)==0) ||
            ((!aIfName) && strcmp(temp_addr->ifa_name,"lo0")!=0)
          ) {
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


#include <stdio.h>

bool p44::getMacAddressByIpv4(uint32_t aIPv4Address, uint64_t &aMacAddress)
{
  #warning "Q&D version using blocking system() and popen() calls, not working when more than one network interface connects to the same subnet (e.g. WiFi+cable)"
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

#elif P44_BUILD_WIN

#warning "On windows, getIfInfo(), getMacAddressByIpv4() are dummies never returning anything useful"
bool p44::getIfInfo(uint64_t *aMacAddressP, uint32_t *aIPv4AddressP, int *aIfIndex, const char *aIfName)
{
  return false;
}

bool p44::getMacAddressByIpv4(uint32_t aIPv4Address, uint64_t &aMacAddress)
{
  return false;
}


#else

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <linux/sockios.h>
#include <errno.h>

#define LL_DEBUG 0 // set to 1 to allow low level debug info printed to stdout

#if LL_DEBUG
#include <stdio.h>
#endif

bool p44::getIfInfo(uint64_t *aMacAddressP, uint32_t *aIPv4AddressP, int *aIfIndex, const char *aIfName)
{
  int sock;
  int ifIndex;
  struct ifreq ifr;
  int res;
  uint64_t mac = 0;
  uint32_t ip = 0;
  bool foundIf = false;
  bool foundMAC = false;
  bool foundIPv4 = false;
  bool foundRequested = false;

  if (aIfName && *aIfName==0) aIfName = NULL;
  // any socket type will do
  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
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
        if (ifIndex>20 || errno!=ENODEV) break; // error or no more names -> end
        ifIndex++; continue; // otherwise, just skip (indices aren't necessarily contiguous)
      }
      // got name for index
      if (aIfName) {
        // name must match
        if (strcmp(aIfName, ifr.ifr_name)==0) {
          // name matches, use this and only this interface
          foundIf = true;
        }
      }
      // - get flags for it
      if (ioctl(sock, SIOCGIFFLAGS, &ifr)>=0) {
        // skip loopback interfaces (unless specified by name)
        if (foundIf || (!aIfName && (ifr.ifr_flags & IFF_LOOPBACK)==0)) {
          // found by name or not loopback
          // - now get HWADDR
          if (!foundMAC && aMacAddressP && ioctl(sock, SIOCGIFHWADDR, &ifr)>=0) {
            // compose int64
            for (int i=0; i<6; ++i) {
              mac = (mac<<8) + ((uint8_t *)(ifr.ifr_hwaddr.sa_data))[i];
            }
            // this is our MAC unless it is zero (or interface name was specified)
            if (mac!=0 || foundIf) {
              // save the interface index
              if (aIfIndex) *aIfIndex = ifIndex;
              // save the mac address
              *aMacAddressP = mac; // found, return it
              foundMAC = true; // done, use it (even if IP is 0)
            }
          }
          // - also get IPv4
          if (!foundIPv4 && aIPv4AddressP) {
            if (ioctl(sock, SIOCGIFADDR, &ifr)>=0) {
              if (ifr.ifr_addr.sa_family==AF_INET) {
                // is IPv4
                struct sockaddr_in *ipv4 = (struct sockaddr_in *)&(ifr.ifr_addr);
                for (int i=0; i<4; ++i) {
                  ip = (ip<<8) + ((uint8_t *)&(ipv4->sin_addr.s_addr))[i];
                }
              }
            }
            // when interface is specified, we want IP only from this interface
            if (ip!=0 || foundIf) {
              *aIPv4AddressP = ip;
              foundIPv4 = true;
            }
          }
        }
      }
      foundRequested =
        foundIf || ( // specified interface name found aborts any further search
          (!aIPv4AddressP || foundIPv4) &&
          (!aMacAddressP || foundMAC)
        );
      #if LL_DEBUG
      printf("ifIndex=%d, name='%s', flags=%X: foundIf=%d, foundMAC=%d, foundIPv4=%d, foundRequested=%d, ip=%04X, mac=%06llX\n", ifIndex, ifr.ifr_name, ifr.ifr_flags, foundIf, foundMAC, foundIPv4, foundRequested, ip, mac);
      #endif
      if (foundRequested) {
        // found everything that was requested
        break;
      }
      // next
      ifIndex++;
    } while(true);
    close(sock);
  }
  return foundRequested;
}


bool p44::getMacAddressByIpv4(uint32_t aIPv4Address, uint64_t &aMacAddress)
{
  int sfd;
  bool ok = false;
  struct ifreq ifr;

  // SIOCGARP works on any AF_INET socket
  if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) != -1) {
    // search interface which has the correct subnet
    int ifIndex = 0; // actual index starts with 1, but we increment first
    do {
      ifIndex++;
      // - init struct
      memset(&ifr, 0x00, sizeof(ifr));
      // - get name of interface by index
      ifr.ifr_ifindex = ifIndex;
      if (ioctl(sfd, SIOCGIFNAME, &ifr)<0)
        break; // no more names, end
      // check flags
      if (ioctl(sfd, SIOCGIFFLAGS, &ifr)<0)
        break; // can't get flags, end
      if ((ifr.ifr_flags & IFF_LOOPBACK)!=0)
        continue; // skip loopback
      // get IF address
      if (ioctl(sfd, SIOCGIFADDR, &ifr)<0)
        break;
      uint32_t ifaddr = ntohl(((struct sockaddr_in *)&(ifr.ifr_addr))->sin_addr.s_addr);
      // get IF netmask
      if (ioctl(sfd, SIOCGIFNETMASK, &ifr)<0)
        break;
      uint32_t mask = ntohl(((struct sockaddr_in *)&(ifr.ifr_netmask))->sin_addr.s_addr);
      // check subnet
      if ((aIPv4Address & mask)==(ifaddr & mask)) {
        // matching subnet
        ok = true;
        break;
      }
    } while (true);
    if (ok) {
      // got name, now do SIOCGARP
      ok = false;
      struct arpreq areq;
      struct sockaddr_in *sin;
      memset(&areq, 0, sizeof(areq));
      sin = (struct sockaddr_in *) &areq.arp_pa;
      sin->sin_family = AF_INET;
      sin->sin_addr.s_addr = htonl(aIPv4Address);
      sin = (struct sockaddr_in *) &areq.arp_ha;
      sin->sin_family = ARPHRD_ETHER;
      strncpy(areq.arp_dev, ifr.ifr_name, 15);
      // issue request
      if (ioctl(sfd, SIOCGARP, (caddr_t) &areq) != -1) {
        // assign MAC address
        aMacAddress = 0;
        for (int i=0; i<6; i++) {
          aMacAddress = (aMacAddress<<8) + ((uint8_t *)&(areq.arp_ha.sa_data))[i];
        }
        ok = true;
      }
    }
    close(sfd);
  }
  return ok;
}

#endif // non-apple



uint64_t p44::macAddress(const char *aIfName)
{
  uint64_t mac;
  if (getIfInfo(&mac, NULL, NULL, aIfName))
    return mac;
  return 0; // none
}


uint32_t p44::ipv4Address(const char *aIfName)
{
  uint32_t ip;
  if (getIfInfo(NULL, &ip, NULL, aIfName))
    return ip;
  return 0; // none
}
