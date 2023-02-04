//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__dnssd__
#define __p44utils__dnssd__

#include "p44utils_main.hpp"

// Avahi includes
#if USE_AVAHI_CORE
// - directly using core, good for small embedded with single process using avahi
#include <avahi-core/core.h>
#include <avahi-core/publish.h>
#include <avahi-core/lookup.h>
#define AvahiService AvahiServer
//#define AvahiServiceState AvahiServerState
#define avahi_service_errno avahi_server_errno
#define avahi_add_service avahi_server_add_service
#define avahi_entry_group_commit avahi_s_entry_group_commit
#define avahi_entry_group_reset avahi_s_entry_group_reset
#define AvahiEntryGroup AvahiSEntryGroup
#define AvahiServiceBrowser AvahiSServiceBrowser
#define AvahiServiceBrowser AvahiSServiceBrowser
#define AvahiServiceResolver AvahiSServiceResolver
#define avahi_entry_group_new avahi_s_entry_group_new
#define avahi_entry_group_reset avahi_s_entry_group_reset
#define avahi_entry_group_free avahi_s_entry_group_free
#define avahi_service_browser_new avahi_s_service_browser_new
#define avahi_service_browser_free avahi_s_service_browser_free
#define avahi_service_resolver_new avahi_s_service_resolver_new
#define avahi_service_resolver_free avahi_s_service_resolver_free
#else
// - use avahi client, desktop/larger embedded (which uses system wide avahi server, together with other clients)
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#define AvahiService AvahiClient
//#define AvahiServiceState AvahiClientState
#define avahi_service_errno avahi_client_errno
#define avahi_add_service(srv,eg,...) avahi_entry_group_add_service(eg,##__VA_ARGS__)
#endif

#include <avahi-core/log.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/domain.h>


#if ENABLE_P44SCRIPT && !defined(ENABLE_DNSSD_SCRIPT_FUNCS)
  #define ENABLE_DNSSD_SCRIPT_FUNCS 1
#endif

#if ENABLE_DNSSD_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif

using namespace std;

namespace p44 {


  class DnsSdError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      AllForNow,
      CacheExhausted,
      Fatal,
      WrongUsage,
      NoNetwork, ///< no network up and running
      HostNameFail, ///< host name conflict that could not be resolved
      Stopped, ///< service has been stopped, will not restart automatically
      numErrorCodes
    } ErrorCodes;

    static const char *domain() { return "DNS-SD"; };
    virtual const char *getErrorDomain() const P44_OVERRIDE { return DnsSdError::domain(); };
    DnsSdError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "AllForNow",
      "CacheExhausted",
      "Fatal",
      "WrongUsage",
      "NoNetwork",
      "HostNameFail",
      "Stopped",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  class DnsSdManager;
  typedef boost::intrusive_ptr<DnsSdManager> DnsSdManagerPtr;


  /// information about a service to publish or one that has appeared or disappeared in a service browser
  class DnsSdServiceInfo : public P44Obj
  {
    typedef P44Obj inherited;
    friend class DnsSdServiceBrowser;

  public:

    DnsSdServiceInfo();
    void reset();

    /// @return host address and port formatted for use in an URL
    /// @param aURLFormat if true, % (in IPv6 scope ids) are escaped as %25
    string hostPart(bool aURLFormat = false);

    /// try to create a URL from the service type
    /// @param aSecure can be set to Yes or No to force http/https (or other similar pairs), or undefined to automatically derive it
    /// @param aURLFormat if false, % (in IPv6 scope ids) are NOT escaped as %25 (e.g. wget does not like escaped)
    /// @return a url, including path (from "path" txt record, if any)
    /// @note when aHttps==undefined, https is returned for service types known to be https
    ///   and for other types when port is 443
    string url(Tristate aSecure = undefined, bool aURLFormat = true);

    bool disappeared; ///< if set, the browsed service has disappeared
    int lookupFlags; ///< avahi browse/lookup result flags
    string type; ///< service type (_xxx._yyy style)
    string name; ///< service name
    string domain; ///< domain
    string hostname; ///< hostname
    bool ipv6; ///< set if hostaddress is IPv6
    AvahiIfIndex ifIndex; ///< interface index
    string hostaddress; ///< resolved host address
    uint16_t port; ///< port
    typedef map<string,string> TxtRecordsMap;
    TxtRecordsMap txtRecords; ///< txt records
  };
  typedef boost::intrusive_ptr<DnsSdServiceInfo> DnsSdServiceInfoPtr;


  /// a group of services that are to be published together
  class DnsSdServiceGroup : public P44Obj
  {
    typedef P44Obj inherited;
    friend class DnsSdManager;

    DnsSdManager& mManager;
    AvahiEntryGroup* mEntryGroup;
    StatusCB mAdvertisingStatusCB;

    DnsSdServiceGroup(DnsSdManager& aManager);

  public:
    virtual ~DnsSdServiceGroup();

    /// @param aService information about the service to publish
    ErrorPtr addService(DnsSdServiceInfoPtr aService);

    /// Commit (actually publish) all services in the group
    /// @param aAdvertisingStatusCB called to signal success or failure of starting advertisement
    void startAdvertising(StatusCB aAdvertisingStatusCB);

    /// resets the entry group (need to re-add services and re-start advertising)
    void reset();

    /// deletes the service group from dns-sd
    /// @note: object becomes invalid (useless) after that
    void free();

  private:

    // internally called when service is stopped, unlinks this object from actual avahi object
    void invalidate();

    // callbacks
    #if USE_AVAHI_CORE
    static void avahi_entry_group_callback(AvahiServer *s, AvahiSEntryGroup *g, AvahiEntryGroupState state, void* userdata);
    #else
    static void avahi_entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void* userdata);
    #endif
    void entry_group_callback(AvahiService *aService, AvahiEntryGroup *g, AvahiEntryGroupState state);

  };
  typedef boost::intrusive_ptr<DnsSdServiceGroup> DnsSdServiceGroupPtr;



  /// callback for browser results
  /// @param aServiceInfo the result object of the service discovery
  /// @return must return true to continue looking for services or false
  typedef boost::function<bool (ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo)> DnsSdServiceBrowserCB;

  /// browser for a service
  class DnsSdServiceBrowser : public P44Obj
  {
    typedef P44Obj inherited;
    friend class DnsSdManager;

    DnsSdManager& mManager;
    AvahiServiceBrowser *mServiceBrowser;
    DnsSdServiceBrowserCB mServiceBrowserCB;
    int mResolving;
    bool mAllForNow;

    DnsSdServiceBrowser(DnsSdManager& aManager);

  public:

    virtual ~DnsSdServiceBrowser();

    /// browse third-party services
    /// @param aServiceType the service type such as "_http._tcp"
    /// @param aServiceBrowserCB will be called (possibly multiple times) to report found services
    /// @note while browsing, the DnsSdServiceBrowser is kept alive in the manager's list, calling stopBrowsing()
    ///    or returning false in the callback removes the DnsSdServiceBrowser from the list so it might get deleted
    ///    when not kept otherwise.
    void browse(const char *aServiceType, DnsSdServiceBrowserCB aServiceBrowserCB);

    /// stop browsing, no callback will happen
    void stopBrowsing();

  private:

    static void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata);
    void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags);
    static void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata);
    void resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags);

    // internally called when service is stopped, unlinks this object from actual avahi object
    void invalidate();

    // stop avahi level browsing, and then invalidate()
    void deactivate();

  };
  typedef boost::intrusive_ptr<DnsSdServiceBrowser> DnsSdServiceBrowserPtr;


  /// service status callback
  /// @param aError if NULL, this means that service came up and callee should now set up service advertisements or browsers
  /// @return true if callee wants to get further updates, false otherwise
  typedef boost::function<bool (ErrorPtr aError)> ServiceStatusCB;


  /// Implements service announcement and discovery (via avahi)
  class DnsSdManager : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;
    friend class DnsSdServiceBrowser;
    friend class DnsSdServiceGroup;

    AvahiSimplePoll *mSimplePoll;
    #if USE_AVAHI_CORE
    string mHostname;
    #endif
    bool mUseIPv4;
    bool mUseIPv6;
    MLTicket mPollTicket; // timer for avahi polling

    AvahiService *mService;
    typedef std::list<ServiceStatusCB> ServiceStatusCBList;
    ServiceStatusCBList mServiceCallbacks;
    MLTicket mServiceStartTicket; // timer for starting/restarting service

    typedef std::list<DnsSdServiceBrowserPtr> ServiceBrowsersList;
    ServiceBrowsersList mServiceBrowsers;
    typedef std::list<DnsSdServiceGroupPtr> ServiceGroupsList;
    ServiceGroupsList mServiceGroups;

    // private constructor, use sharedDnsSdManager() to obtain singleton
    DnsSdManager();

  public:

    /// get shared instance (singleton)
    static DnsSdManager &sharedDnsSdManager();

    virtual ~DnsSdManager();

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() P44_OVERRIDE { return "dns-sd"; };

    /// Initialize the DNS-SD manager
    /// @param aHostName the hostname. This is only relevant with USE_AVAHI_CORE. Otherwise, the hostname is determined by the avahi server deamon independently
    /// @param aUseIPv6 if set, advertising and browsing occurs on IPv6
    /// @param aUseIPv4 if set, advertising and browsing occurs on IPv4
    /// @note if hostname is set with USE_AVAHI_CORE, one will be automatically derived
    /// @note can be called multiple times, if already initialized, it is NOP and just returns success (arguments are relevant in first call only!)
    /// @note the indended usage pattern is that initialize() is called once early in app startup, defining the platform paramters
    ErrorPtr initialize(const char* aHostname, bool aUseIPv6 = false, bool aUseIPv4 = true);

    /// tear down entire operation
    void deinitialize();

    /// request discovery/advertising service
    /// @param aServiceStatusCB will be called when changes in basic service occur. Each service requester should use the
    ///   callback to start publishing or browsing whenever the service (re)starts
    /// @param aRestartDelay delay for actually starting
    /// @note can be called repeatedly to register multiple callbacks. aStartupDelay is effective for the initial call only.
    void requestService(ServiceStatusCB aServiceStatusCB, MLMicroSeconds aStartupDelay);

    /// stop advertising and scanning service
    /// @note all registered callbacks will receive a DnsSdError::Stopped error, and need to re-request the service
    ///   to make the service start again and receive callbacks again.
    void stopService();

    /// restart service
    /// @param aRestartDelay delay for restart, if set to <0 or omitted, a default restart delay
    ///   (intended for error-caused restarts) will be used.
    /// @note all registered callbacks will receive a callback with no Error when the service is up again (like
    ///    after initial requestService(), which should cause them to re-advertise or re-browse
    /// @note as restartService() is often called from a service callback, multiple service requesters might demand restarts.
    ///    The shortest delay requested among all requests will be the one that actually passes until service is started again
    void restartService(MLMicroSeconds aRestartDelay = -1);

    /// restart service with delay derived from error (handling some special cases with different delays)
    /// @param aError the error to be resolved by restarting
    /// @note: when aError is DnsSdError::Stopped, no restart will be scheduled, because that means the dns-sd was terminated intentionally.
    void restartServiceBecause(ErrorPtr aError);

    /// @return true if service is up and runningh
    bool serviceRunning();

    /// create a new service browser
    /// @return new service browser or NULL if none could be created
    /// @note the service must be started already before calling this
    DnsSdServiceBrowserPtr newServiceBrowser();

    /// convenience method to just instatiate and use an anonymous service browser
    /// @param aServiceType the service type such as "_http._tcp"
    /// @param aServiceBrowserCB will be called (possibly multiple times) to report found services.
    ///   Must return false to stop browsing, otherwise, further callbacks may happen and callback target must not get deleted.
    /// @note this will automatically request DNS-SD service to get started with default parameters, if not already started otherwise
    void browse(const char *aServiceType, DnsSdServiceBrowserCB aServiceBrowserCB);

    /// create a new service group
    /// @return new service group or NULL if none could be created
    /// @note the service must be started already before calling this
    DnsSdServiceGroupPtr newServiceGroup();

  private:

    void initiateService();
    void terminateService();

    void deliverServiceStatus(ErrorPtr aStatus);

    bool doBrowse(ErrorPtr aStatus, const char *aServiceType, DnsSdServiceBrowserCB aServiceBrowserCB);

    // callbacks
    static void avahi_log(AvahiLogLevel level, const char *txt);
    #if USE_AVAHI_CORE
    static void avahi_server_callback(AvahiServer *s, AvahiServerState state, void* userdata);
    void server_callback(AvahiServer *s, AvahiServerState state);
    #else
    static void avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata);
    void client_callback(AvahiClient *c, AvahiClientState state);
    #endif

    void avahi_poll(MLTimer &aTicket);

  };


  #if ENABLE_DNSSD_SCRIPT_FUNCS && ENABLE_P44SCRIPT
  namespace P44Script {

    /// represents the global objects related to http
    class DnsSdLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      DnsSdLookup();
    };

  }
  #endif


} // namespace p44

#endif // __p44utils__dnssd__
