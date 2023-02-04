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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "dnssd.hpp"
#include "macaddress.hpp"


using namespace p44;

#define NETWORK_RETRY_DELAY (30*Second) // how long to wait before retrying to start avahi server when failed because of missing network
#define SERVICE_RESTART_DELAY (2*Minute) // how long to wait before restarting the service by default


// MARK: - DnsSdServiceInfo

DnsSdServiceInfo::DnsSdServiceInfo()
{
  reset();
}

void DnsSdServiceInfo::reset()
{
  disappeared = false;
  lookupFlags = 0;
  type.clear();
  name.clear();
  domain.clear();
  hostname.clear();
  hostaddress.clear();
  ipv6 = false;
  ifIndex = AVAHI_IF_UNSPEC;
  port = 0;
  txtRecords.clear();
}


string DnsSdServiceInfo::hostPart(bool aURLFormat)
{
  string h;
  if (ipv6) {
    h = string_format("[%s", hostaddress.c_str());
    if (ifIndex!=AVAHI_IF_UNSPEC && strucmp(hostaddress.c_str(), "fe80:", 5)==0) {
      // link local, need interface index
      string_format_append(h, "%s%d", aURLFormat ? "%25" : "%", ifIndex);
    }
    string_format_append(h, "]:%d", port);
  }
  else {
    h = string_format("%s:%d", hostaddress.c_str(), port);
  }
  return h;
}


// Note: known service types see here: http://www.dns-sd.org/servicetypes.html

string DnsSdServiceInfo::url(Tristate aSecure, bool aURLFormat)
{
  string proto = "http"; // default
  if (type=="_https._tcp") {
    proto = "http";
    aSecure = yes;
  }
  else if (type=="_http._tcp") {
    proto = "http";
  }
  else if (type=="_ssh._tcp") {
    proto = "ssh";
    aSecure = no;
  }
  else if (type=="_ftp._tcp") {
    proto = "ftp";
    aSecure = no;
  }
  else if (type=="_sftp-ssh._tcp") {
    proto = "sftp";
    aSecure = no;
  }
  // assume auto http/https for all which have undefined aSecure at this point
  if ((aSecure==undefined && port==443) || aSecure==yes) {
    proto += "s";
  }
  string url = string_format("%s://%s", proto.c_str(), hostPart(aURLFormat).c_str());
  TxtRecordsMap::iterator p = txtRecords.find("path");
  if (p!=txtRecords.end()) {
    url += p->second.c_str();
  }
  return url;
}


// MARK: - DnsSdManager

static DnsSdManager *sharedDnsSdManagerP = NULL;

DnsSdManager &DnsSdManager::sharedDnsSdManager()
{
  if (!sharedDnsSdManagerP) {
    sharedDnsSdManagerP = new DnsSdManager();
  }
  return *sharedDnsSdManagerP;
}


DnsSdManager::DnsSdManager() :
  mSimplePoll(NULL),
  mService(NULL),
  mUseIPv4(true),
  mUseIPv6(false)
{
  // register a cleanup handler
  MainLoop::currentMainLoop().registerCleanupHandler(boost::bind(&DnsSdManager::stopService, this));
  #if USE_AVAHI_CORE
  // route avahi logs to our own log system
  avahi_set_log_function(&DnsSdManager::avahi_log);
  #endif
}


void DnsSdManager::avahi_log(AvahiLogLevel level, const char *txt)
{
  // show all avahi log stuff only when we have focus
  FOCUSPOLOG(sharedDnsSdManagerP, "avahi(%d): %s", level, txt);
}


DnsSdManager::~DnsSdManager()
{
  // full stop
  deinitialize();
}


void DnsSdManager::deinitialize()
{
  if (mSimplePoll) {
    // unregister idle handler
    mPollTicket.cancel();
    stopService();
    // stop polling
    avahi_simple_poll_quit(mSimplePoll);
    avahi_simple_poll_free(mSimplePoll);
    mSimplePoll = NULL;
  }
}


ErrorPtr DnsSdManager::initialize(const char* aHostname, bool aUseIPv6, bool aUseIPv4)
{
  ErrorPtr err;

  if (!mSimplePoll) {
    mUseIPv4 = aUseIPv4;
    mUseIPv6 = aUseIPv6;
    #if USE_AVAHI_CORE
    if (aHostname) {
      // use specified hostname
      mHostname = aHostname;
    }
    else {
      // generate hostname from macaddress
      mHostname = string_format("plan44-%s", macAddressToString(macAddress()).c_str());
    }
    #endif
    // allocate the simple-poll object
    if (!(mSimplePoll = avahi_simple_poll_new())) {
      err = Error::err<DnsSdError>(DnsSdError::Fatal, "Avahi: Failed to create simple poll object.");
    }
    if (Error::isOK(err)) {
      // start polling
      mPollTicket.executeOnce(boost::bind(&DnsSdManager::avahi_poll, this, _1));
    }
  }
  return err;
}


#define AVAHI_POLL_INTERVAL (30*MilliSecond)
#define AVAHI_POLL_TOLERANCE (15*MilliSecond)

void DnsSdManager::avahi_poll(MLTimer &aTimer)
{
  if (mSimplePoll) {
    avahi_simple_poll_iterate(mSimplePoll, 0);
  }
  // schedule next execution
  MainLoop::currentMainLoop().retriggerTimer(aTimer, AVAHI_POLL_INTERVAL, AVAHI_POLL_TOLERANCE);
}


// MARK: - Basic service (avahi client or server)


void DnsSdManager::requestService(ServiceStatusCB aServiceStatusCB, MLMicroSeconds aStartupDelay)
{
  if (serviceRunning()) {
    // already running, can use it right away
    if (aServiceStatusCB) {
      if (aServiceStatusCB(ErrorPtr())) {
        // callback requests keep receiving updates
        mServiceCallbacks.push_back(aServiceStatusCB);
      }
    }
  }
  else {
    // service not yet running
    if (aServiceStatusCB) mServiceCallbacks.push_back(aServiceStatusCB);
    if (!mService) {
      // service not instantiated yet
      mServiceStartTicket.executeOnce(boost::bind(&DnsSdManager::initiateService, this), aStartupDelay);
    }
  }
}


void DnsSdManager::initiateService()
{
  ErrorPtr status;
  // - make sure we are initialized
  status = initialize(NULL); // previously set or default settings (hostname, IPv4,v6 flags)
  if (Error::isOK(status)) {
    #if USE_AVAHI_CORE
    // single avahi instance for embedded use, no other process uses avahi
    OLOG(LOG_NOTICE, "starting avahi core service");
    int avahiErr;
    AvahiServerConfig config;
    avahi_server_config_init(&config);
    // basic info
    config.host_name = avahi_strdup(mHostname.c_str()); // unique hostname
    #ifdef __APPLE__
    config.disallow_other_stacks = 0; // on macOS, we always have a mDNS, so allow more than one for testing
    #else
    config.disallow_other_stacks = 1; // we wants to be the only mdNS (also avoids problems with SO_REUSEPORT on older Linux kernels)
    #endif
    // TODO: IPv4 only at this time!
    config.use_ipv4 = (int)mUseIPv4;
    config.use_ipv6 = (int)mUseIPv6;
    // publishing options
    config.publish_aaaa_on_ipv4 = 0; // prevent publishing IPv6 AAAA record on IPv4
    config.publish_a_on_ipv6 = 0; // prevent publishing IPv4 A on IPV6
    config.publish_hinfo = 0; // no CPU specifics
    config.publish_addresses = 1; // publish addresses
    config.publish_workstation = 0; // no workstation
    config.publish_domain = 1; // announce the local domain for browsing
    // create server with prepared config
    mService = avahi_server_new(avahi_simple_poll_get(mSimplePoll), &config, avahi_server_callback, this, &avahiErr);
    avahi_server_config_free(&config); // don't need it any more
    if (!mService) {
      if (avahiErr==AVAHI_ERR_NO_NETWORK) {
        // no network to publish to - might be that it is not yet up, try again later
        status = Error::err<DnsSdError>(DnsSdError::NoNetwork, "avahi: no network available to publish services now");
      }
      else {
        // other problem, report it
        status = Error::err<DnsSdError>(DnsSdError::Fatal, "avahi: failed to create server: %s (%d)", avahi_strerror(avahiErr), avahiErr);
      }
    }
    #else
    // Use client
    OLOG(LOG_NOTICE, "starting avahi client");
    int avahiErr;
    // create client
    mService = avahi_client_new(avahi_simple_poll_get(mSimplePoll), (AvahiClientFlags)AVAHI_CLIENT_NO_FAIL, avahi_client_callback, this, &avahiErr);
    if (!mService) {
      if (avahiErr==AVAHI_ERR_NO_NETWORK || avahiErr==AVAHI_ERR_NO_DAEMON) {
        // no network or no daemon to publish to - might be that it is not yet up, try again later
        status = Error::err<DnsSdError>(DnsSdError::NoNetwork, "avahi: no network available to publish services now");
      }
      else {
        // other problem, report it
        status = Error::err<DnsSdError>(DnsSdError::Fatal, "avahi: failed to create client: %s (%d)", avahi_strerror(avahiErr), avahiErr);
      }
    }
    #endif
  }
  // report errors immediately. If no error at this point, callback will be delivered later
  if (Error::notOK(status)) {
    deliverServiceStatus(status);
  }
}


void DnsSdManager::terminateService()
{
  mServiceStartTicket.cancel(); // prevent already scheduled restarts
  if (mService) {
    // freeing will invalidate all related entry groups and browsers,
    // so we need to null them in case someone still holds those
    for (ServiceBrowsersList::iterator pos = mServiceBrowsers.begin(); pos!=mServiceBrowsers.end(); ++pos) {
      (*pos)->invalidate();
    }
    for (ServiceGroupsList::iterator pos = mServiceGroups.begin(); pos!=mServiceGroups.end(); ++pos) {
      (*pos)->invalidate();
    }
    #if USE_AVAHI_CORE
    avahi_server_free(mService);
    #else
    avahi_client_free(mService);
    #endif
    mService = NULL;
    // ...and release the objects
    mServiceBrowsers.clear();
    mServiceGroups.clear();
  }
}


void DnsSdManager::stopService()
{
  // actually terminate the service
  terminateService();
  // finally, inform all former service requesters, forget callbacks
  ServiceStatusCBList cbl = mServiceCallbacks;
  mServiceCallbacks.clear();
  ErrorPtr status = Error::err<DnsSdError>(DnsSdError::Stopped, "avahi service stopped");
  for (ServiceStatusCBList::iterator pos = cbl.begin(); pos!=cbl.end(); ++pos) {
    (*pos)(status);
  }
}


void DnsSdManager::restartService(MLMicroSeconds aRestartDelay)
{
  if (aRestartDelay<0) aRestartDelay = SERVICE_RESTART_DELAY;
  OLOG(LOG_INFO, "requested to re-start in %lld seconds", aRestartDelay/Second);
  if (mServiceStartTicket) {
    // seems already scheduled, just reschedule
    if (mServiceStartTicket.reschedule(aRestartDelay)) {
      return; // was still pending, reschedule successful
    }
    // not scheduled
  }
  // no start scheduled yet, terminate if currently up and running
  terminateService();
  // restart
  mServiceStartTicket.executeOnce(boost::bind(&DnsSdManager::initiateService, this), aRestartDelay);
}


void DnsSdManager::restartServiceBecause(ErrorPtr aError)
{
  MLMicroSeconds delay = SERVICE_RESTART_DELAY;
  if (Error::isDomain(aError, DnsSdError::domain())) {
    switch (aError->getErrorCode()) {
      case DnsSdError::Stopped:
        OLOG(LOG_NOTICE, "stopped");
        return; // publishing stopped entirely, no automatic restart!
      case DnsSdError::NoNetwork:
        delay = NETWORK_RETRY_DELAY;
        break;
    }
  }
  // restart
  OLOG(LOG_NOTICE, "restarting in %lld seconds because: %s", delay/Second, Error::text(aError));
  restartService(delay);
}



void DnsSdManager::deliverServiceStatus(ErrorPtr aStatus)
{
  ServiceStatusCBList::iterator pos = mServiceCallbacks.begin();
  while (pos!=mServiceCallbacks.end()) {
    bool keep = (*pos)(aStatus);
    if (!keep) {
      pos = mServiceCallbacks.erase(pos);
      continue;
    }
    ++pos;
  }
}


#if USE_AVAHI_CORE

// C stubs for avahi callbacks

void DnsSdManager::avahi_server_callback(AvahiServer *s, AvahiServerState state, void* userdata)
{
  DnsSdManager *DnsSdManager = static_cast<class DnsSdManager*>(userdata);
  DnsSdManager->server_callback(s, state);
}

// actual avahi server callback implementation
void DnsSdManager::server_callback(AvahiServer *s, AvahiServerState state)
{
  ErrorPtr status;
  // set member var early, because this callback can happen BEFORE avahi_server_new() returns!
  mService = s;
  // Avahi server state has changed
  switch (state) {
    case AVAHI_SERVER_RUNNING: {
      OLOG(LOG_INFO, "avahi server now running");
      // The server has started up successfully and registered its hostname.
      // Signal success to service status subscribers, which must now (re-)publish or (re-)browse services.
      break;
    }
    case AVAHI_SERVER_COLLISION: {
      // Host name collision detected
      // - create alternative name
      char *newName = avahi_alternative_host_name(avahi_server_get_host_name(s));
      OLOG(LOG_WARNING, "host name collision, retrying with '%s'", newName);
      int avahiErr = avahi_server_set_host_name(s, newName);
      avahi_free(newName);
      if (avahiErr<0) {
        status = Error::err<DnsSdError>(DnsSdError::HostNameFail, "dns-sd: avahi: cannot set new hostname");
        break;
      }
      // otherwise fall through to AVAHI_SERVER_REGISTERING
    }
    case AVAHI_SERVER_REGISTERING: {
      // just informative
      FOCUSOLOG("host records are being registered");
      return; // No callback
    }
    case AVAHI_SERVER_FAILURE: {
      // fatal server failure
      status = Error::err<DnsSdError>(DnsSdError::Fatal, "avahi: server failure: %s", avahi_strerror(avahi_server_errno(s)));
      break;
    }
    case AVAHI_SERVER_INVALID:
      status = Error::err<DnsSdError>(DnsSdError::Fatal, "avahi: invalid state, server not started");
      break;
  }
  // deliver status
  deliverServiceStatus(status);
}


bool DnsSdManager::serviceRunning()
{
  if (!mService) return false;
  return avahi_server_get_state(mService)==AVAHI_SERVER_RUNNING;
}

#else

// C stub for avahi client callback
void DnsSdManager::avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata)
{
  DnsSdManager *DnsSdManager = static_cast<class DnsSdManager*>(userdata);
  DnsSdManager->client_callback(c, state);
}


// actual avahi client callback implementation
void DnsSdManager::client_callback(AvahiClient *c, AvahiClientState state)
{
  ErrorPtr status;
  // set member var early, because this callback can happen BEFORE avahi_client_new() returns!
  mService = c;
  // Avahi client state has changed
  switch (state) {
    case AVAHI_CLIENT_S_RUNNING: {
      OLOG(LOG_INFO, "avahi client reports server running");
      // The client reports that server has started up successfully and registered its hostname.
      // Signal success to service status subscribers, which must now (re-)publish or (re-)browse services.
      break;
    }
    case AVAHI_CLIENT_S_REGISTERING: {
      // just informative
      FOCUSOLOG("host records are being registered");
      return; // No callback
    }
    case AVAHI_CLIENT_S_COLLISION: {
      // fall through to AVAHI_CLIENT_FAILURE
    }
    case AVAHI_CLIENT_FAILURE: {
      status = Error::err<DnsSdError>(DnsSdError::Fatal, "dns-sd: avahi: client failure: %s", avahi_strerror(avahi_client_errno(mService)));
      break;
    }
    case AVAHI_CLIENT_CONNECTING:
      // just informative
      FOCUSOLOG("avahi client connecting to server");
      return; // No callback
  }
  // deliver status
  deliverServiceStatus(status);
}


bool DnsSdManager::serviceRunning()
{
  if (!mService) return false;
  return avahi_client_get_state(mService)==AVAHI_CLIENT_S_RUNNING;
}

#endif // !USE_AVAHI_CORE


// MARK: - advertising

DnsSdServiceGroupPtr DnsSdManager::newServiceGroup()
{
  DnsSdServiceGroupPtr sg;
  if (serviceRunning()) {
    sg = DnsSdServiceGroupPtr(new DnsSdServiceGroup(*this));
    mServiceGroups.push_back(sg);
  }
  return sg;
}


DnsSdServiceGroup::DnsSdServiceGroup(DnsSdManager& aManager) :
  mManager(aManager),
  mEntryGroup(NULL)
{
  if (!(mEntryGroup = avahi_entry_group_new(mManager.mService, avahi_entry_group_callback, this))) {
    mManager.deliverServiceStatus(Error::err<DnsSdError>(DnsSdError::Fatal, "avahi_entry_group_new() failed: %s", avahi_strerror(avahi_service_errno(mManager.mService))));
  }
}


DnsSdServiceGroup::~DnsSdServiceGroup()
{
  invalidate();
}


void DnsSdServiceGroup::invalidate()
{
  // detach from actual service group
  mEntryGroup = NULL;
}


void DnsSdServiceGroup::free()
{
  if (mEntryGroup) {
    avahi_entry_group_free(mEntryGroup);
  }
  invalidate();
}


ErrorPtr DnsSdServiceGroup::addService(DnsSdServiceInfoPtr aService)
{
  ErrorPtr err;
  if (!mEntryGroup || !aService) {
    err = Error::err<DnsSdError>(DnsSdError::WrongUsage, "service group not valid or no service info");
  }
  else {
    const size_t maxtxtrecs = 5;
    string txtrecs[maxtxtrecs];
    size_t i = 0;
    for (DnsSdServiceInfo::TxtRecordsMap::iterator pos = aService->txtRecords.begin(); i<maxtxtrecs && pos!=aService->txtRecords.end(); pos++) {
      if (pos->second.empty()) txtrecs[i] = pos->first.c_str(); // just key as flag, no value
      else txtrecs[i] = string_format("%s=%s", pos->first.c_str(), pos->second.c_str()); // key=value
    }
    int avahiErr;
    // limit service name in case it is too long (otherwise service cannot be installed)
    string name = aService->name;
    if (name.size()>=AVAHI_LABEL_MAX) {
      const size_t hn = (AVAHI_LABEL_MAX-4)/2;
      name = name.substr(0,hn)+"..."+name.substr(name.size()-hn,hn) ; // shorten in the middle, assuming user-specified name part there
    }
    if ((avahiErr = avahi_add_service(
      mManager.mService,
      mEntryGroup,
      AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, // all interfaces and protocols (as enabled at server level)
      (AvahiPublishFlags)0, // no flags
      name.c_str(),
      aService->type.c_str(),
      NULL, // no separate domain
      NULL, // no separate host
      aService->port, // port
      // txt records
      txtrecs[0].empty() ? NULL : txtrecs[0].c_str(),
      txtrecs[1].empty() ? NULL : txtrecs[1].c_str(),
      txtrecs[2].empty() ? NULL : txtrecs[2].c_str(),
      txtrecs[3].empty() ? NULL : txtrecs[3].c_str(),
      txtrecs[4].empty() ? NULL : txtrecs[4].c_str(),
      NULL // TXT record terminator
    ))<0) {
      err = Error::err<DnsSdError>(DnsSdError::Fatal, "failed to add service: %s", avahi_strerror(avahiErr));
    }
  }
  return err;
}


void DnsSdServiceGroup::startAdvertising(StatusCB aAdvertisingStatusCB)
{
  ErrorPtr err;
  int avahiErr;
  if (!mEntryGroup) {
    err = Error::err<DnsSdError>(DnsSdError::WrongUsage, "service group is no longer valid");
  }
  else if ((avahiErr = avahi_entry_group_commit(mEntryGroup)) < 0) {
    err = Error::err<DnsSdError>(DnsSdError::Fatal, "failed to commit entry_group: %s", avahi_strerror(avahiErr));
  }
  if (aAdvertisingStatusCB && Error::notOK(err)) {
    aAdvertisingStatusCB(err);
    return;
  }
  // all ok so far wait for callback
  mAdvertisingStatusCB = aAdvertisingStatusCB;
}


void DnsSdServiceGroup::reset()
{
  if (mEntryGroup) {
    avahi_entry_group_reset(mEntryGroup);
  }
}


#if USE_AVAHI_CORE

// C stubs for avahi callbacks

void DnsSdServiceGroup::avahi_entry_group_callback(AvahiServer *s, AvahiSEntryGroup *g, AvahiEntryGroupState state, void* userdata)
{
  DnsSdServiceGroup* serviceGroup = static_cast<DnsSdServiceGroup*>(userdata);
  serviceGroup->entry_group_callback(s, g, state);
}

#else

void DnsSdServiceGroup::avahi_entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void* userdata)
{
  DnsSdServiceGroup* serviceGroup = static_cast<DnsSdServiceGroup*>(userdata);
  serviceGroup->entry_group_callback(serviceGroup->mManager.mService, g, state);
}

#endif // !USE_AVAHI_CORE


void DnsSdServiceGroup::entry_group_callback(AvahiService *aService, AvahiEntryGroup *g, AvahiEntryGroupState state)
{
  ErrorPtr err;
  // set member var early, because this callback can happen BEFORE avahi_entry_group_new() returns!
  mEntryGroup = g;
  // entry group state has changed
  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED: {
      SOLOG(mManager, LOG_INFO, "successfully published services");
      break;
    }
    case AVAHI_ENTRY_GROUP_COLLISION: {
      // service name collision detected
      // Note: we don't handle this as it can't really happen (publishedName contains the deviceId or the vdcHost dSUID which MUST be unique)
      err = Error::err<DnsSdError>(DnsSdError::Fatal, "entry group name collision");
      break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE: {
      SOLOG(mManager, LOG_INFO, "failed publishing entry group: %s", avahi_strerror(avahi_service_errno(aService)));
      err = Error::err<DnsSdError>(DnsSdError::Fatal, "failed publishing entry group: %s", avahi_strerror(avahi_service_errno(aService)));
      break;
    }
    default:
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
      return; // TODO: maybe handle these later
  }
  if (mAdvertisingStatusCB) {
    StatusCB cb = mAdvertisingStatusCB;
    mAdvertisingStatusCB = NoOP;
    cb(err);
  }
}


// MARK: - browsing

DnsSdServiceBrowserPtr DnsSdManager::newServiceBrowser()
{
  DnsSdServiceBrowserPtr sb;
  if (serviceRunning()) {
    sb = DnsSdServiceBrowserPtr(new DnsSdServiceBrowser(*this));
    mServiceBrowsers.push_back(sb);
  }
  return sb;
}


void DnsSdManager::browse(const char *aServiceType, DnsSdServiceBrowserCB aServiceBrowserCB)
{
  if (!aServiceBrowserCB) return;
  // auto-start service
  requestService(boost::bind(&DnsSdManager::doBrowse, this, _1, aServiceType, aServiceBrowserCB), 0);
}

bool DnsSdManager::doBrowse(ErrorPtr aStatus, const char *aServiceType, DnsSdServiceBrowserCB aServiceBrowserCB)
{
  if (Error::notOK(aStatus)) {
    aServiceBrowserCB(aStatus, DnsSdServiceInfoPtr());
    return false; // no more updates!
  }
  DnsSdServiceBrowserPtr sb = newServiceBrowser();
  if (sb) sb->browse(aServiceType, aServiceBrowserCB);
  return false; // no more updates!
}


DnsSdServiceBrowser::DnsSdServiceBrowser(DnsSdManager& aManager) :
  mManager(aManager),
  mServiceBrowser(NULL),
  mResolving(0),
  mAllForNow(false)
{
}


void DnsSdServiceBrowser::invalidate()
{
  // detach from actual service browser because that no longer exists
  mServiceBrowser = NULL;
}

void DnsSdServiceBrowser::deactivate()
{
  if (mServiceBrowser) {
    avahi_service_browser_free(mServiceBrowser);
    invalidate();
  }
}


void DnsSdServiceBrowser::stopBrowsing()
{
  mServiceBrowserCB = NoOP;
  deactivate();
  // remove myself from manager's list
  for (DnsSdManager::ServiceBrowsersList::iterator pos = mManager.mServiceBrowsers.begin(); pos!=mManager.mServiceBrowsers.end(); ++pos) {
    if (pos->get() == this) {
      mManager.mServiceBrowsers.erase(pos);
      break;
    }
  }
}


DnsSdServiceBrowser::~DnsSdServiceBrowser()
{
  if (mServiceBrowser) {
    avahi_service_browser_free(mServiceBrowser);
    invalidate();
  }
}


void DnsSdServiceBrowser::browse(const char *aServiceType, DnsSdServiceBrowserCB aServiceBrowserCB)
{
  if (mServiceBrowser) {
    avahi_service_browser_free(mServiceBrowser);
    invalidate();
  }
  mResolving = 0;
  mAllForNow = false;
  mServiceBrowser = avahi_service_browser_new(mManager.mService, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, aServiceType, NULL, (AvahiLookupFlags)0, avahi_browse_callback, this);
  if (!mServiceBrowser && mServiceBrowserCB) {
    ErrorPtr err = Error::err<DnsSdError>(DnsSdError::Fatal, "failed creating service browser: %s", avahi_strerror(avahi_service_errno(mManager.mService)));
    mServiceBrowserCB(err, DnsSdServiceInfoPtr());
    return;
  }
  // all ok so far wait for callbacks
  mServiceBrowserCB = aServiceBrowserCB;
}


void DnsSdServiceBrowser::avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata)
{
  DnsSdServiceBrowser *serviceBrowser = static_cast<DnsSdServiceBrowser*>(userdata);
  serviceBrowser->browse_callback(b, interface, protocol, event, name, type, domain, flags);
}

void DnsSdServiceBrowser::browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags)
{
  ErrorPtr err;
  // set member var early, because this callback can happen BEFORE avahi_service_browser_new() returns!
  mServiceBrowser = b;
  // Called whenever a new services becomes available on the LAN or is removed from the LAN
  // - may use global "service" var, because browsers are no set up within server/client callbacks, but only afterwards, when "service" is defined
  switch (event) {
    case AVAHI_BROWSER_FAILURE:
      err = Error::err<DnsSdError>(DnsSdError::Fatal, "service browser failure: %s", avahi_strerror(avahi_service_errno(mManager.mService)));
      break;
    case AVAHI_BROWSER_NEW:
      // filter IP versions we're not interested in
      if ((protocol==AVAHI_PROTO_INET && !mManager.mUseIPv4) || (protocol==AVAHI_PROTO_INET6 && !mManager.mUseIPv6)) {
        FOCUSSOLOG(mManager, "Ignoring IPv%d browser result", protocol==AVAHI_PROTO_INET ? 4 : 6);
        return;
      }
      SOLOG(mManager, LOG_INFO, "browsing: NEW service '%s' of type '%s' in domain '%s' -> resolving now", name, type, domain);
      // Note: the returned resolver object can be ignored, it is freed in the callback
      //   if the server terminates before the callback has been executes, the server deletes the resolver.
      if (!(avahi_service_resolver_new(mManager.mService, interface, protocol, name, type, domain, protocol /* resolve to same proto as browsed */, (AvahiLookupFlags)0, avahi_resolve_callback, this))) {
        err = Error::err<DnsSdError>(DnsSdError::Fatal, "failed to create resolver browser failure: %s", avahi_strerror(avahi_service_errno(mManager.mService)));
        break;
      }
      mResolving++; // resolving in progress
      return; // resolver callback will continue
    case AVAHI_BROWSER_REMOVE:
      SOLOG(mManager, LOG_INFO, "browsing: VANISHED service '%s' of type '%s' in domain '%s'", name, type, domain);
      if (mServiceBrowserCB) {
        // create info object
        DnsSdServiceInfoPtr bi = DnsSdServiceInfoPtr(new DnsSdServiceInfo);
        bi->disappeared = true;
        bi->name = name;
        bi->domain = domain;
        bi->port = 0;
        bi->lookupFlags = flags;
        // report service having disappeared
        mServiceBrowserCB(ErrorPtr(), bi);
        return; // wait for resolved entry
      }
      break;
    case AVAHI_BROWSER_ALL_FOR_NOW:
      if (mResolving>0) {
        // still resolves pending, postpone reporting
        mAllForNow = true;
        return;
      }
      else {
        err = Error::err<DnsSdError>(DnsSdError::AllForNow, "service browser: all for now");
      }
      break;
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
      FOCUSSOLOG(mManager, "browsing: cache exhausted");
      return;
    default:
      return;
  }
  // something to report
  if (mServiceBrowserCB) {
    bool keepBrowsing = mServiceBrowserCB(err, DnsSdServiceInfoPtr());
    if (!keepBrowsing) {
      avahi_service_browser_free(mServiceBrowser);
      mServiceBrowser = NULL;
    }
  }
}


void DnsSdServiceBrowser::avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata)
{
  DnsSdServiceBrowser *serviceBrowser = static_cast<DnsSdServiceBrowser*>(userdata);
  serviceBrowser->resolve_callback(r, interface, protocol, event, name, type, domain, host_name, a, port, txt, flags);

}

void DnsSdServiceBrowser::resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags)
{
  ErrorPtr err;
  int avahiErr;
  bool keepBrowsing = true;
  switch (event) {
    case AVAHI_RESOLVER_FAILURE:
      avahiErr = avahi_service_errno(mManager.mService);
      SOLOG(mManager, LOG_INFO, "ServiceBrowser: failed to resolve service '%s' of type '%s' in domain '%s': %s", name, type, domain, avahi_strerror(avahiErr));
      // but otherwise no operation (we only want complete resolved entries, unresolvable ones are to be ignored)
      break;
    case AVAHI_RESOLVER_FOUND: {
      char addrtxt[AVAHI_ADDRESS_STR_MAX];
      avahi_address_snprint(addrtxt, sizeof(addrtxt), a);
      FOCUSSOLOG(mManager, "browsing: resolved service '%s' of type '%s' in domain '%s' at %s:", name, type, domain, addrtxt);
      if (mServiceBrowserCB) {
        // create info object
        DnsSdServiceInfoPtr bi = DnsSdServiceInfoPtr(new DnsSdServiceInfo);
        bi->disappeared = false;
        bi->type = type;
        bi->name = name;
        bi->domain = domain;
        bi->hostname = host_name;
        bi->ipv6 = a->proto==AVAHI_PROTO_INET6;
        bi->ifIndex = interface;
        bi->hostaddress = addrtxt;
        bi->port = port;
        bi->lookupFlags = flags;
        // copy txt records, if any
        while (txt) {
          string t,k,v;
          t.assign((const char *)avahi_string_list_get_text(txt), (size_t)avahi_string_list_get_size(txt));
          if(keyAndValue(t, k, v, '=')) {
            bi->txtRecords[k] = v;
          }
          else {
            bi->txtRecords[t] = "";
          }
          txt = avahi_string_list_get_next(txt);
        }
        // report service found
        keepBrowsing = mServiceBrowserCB(ErrorPtr(), bi);
      }
      break;
    }
  }
  // resolving done
  mResolving--;
  if (mResolving<=0) {
    // all resolving finished, report if we've seen allfornow in the meantime
    if (mAllForNow && keepBrowsing) {
      keepBrowsing = mServiceBrowserCB(Error::err<DnsSdError>(DnsSdError::AllForNow, "all dns-sd entries for now"), DnsSdServiceInfoPtr());
    }
    mAllForNow = false;
  }
  avahi_service_resolver_free(r);
  // maybe also kill browser now
  if (!keepBrowsing) {
    stopBrowsing();
  }
}


// MARK: - script support

#if ENABLE_DNSSD_SCRIPT_FUNCS && ENABLE_P44SCRIPT

using namespace P44Script;

// dnssdbrowse(type [,host])
static const BuiltInArgDesc dnssdbrowse_args[] = { { text } , { text|optionalarg } };
static const size_t dnssdbrowse_numargs = sizeof(dnssdbrowse_args)/sizeof(BuiltInArgDesc);
// handler
static bool dnssdbrowsehandler(BuiltinFunctionContextPtr f, JsonObjectPtr aBrowsingresults, ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo)
{
  if (Error::notOK(aError)) {
    if (aError->isError(DnsSdError::domain(), p44::DnsSdError::AllForNow)) {
      // allForNow: return the list we have collected so far
      f->finish(new JsonValue(aBrowsingresults));
    }
    else {
      f->finish(new ErrorValue(aError));
    }
    return false; // stop browsing
  }
  else {
    // got some result
    if (f->numArgs()>1) {
      // must match hostname
      if (aServiceInfo->hostname!=f->arg(1)->stringValue()) {
        // not the host we are looking for
        return true; // continue browsing
      }
    }
    if (!aServiceInfo->disappeared) {
      // actually existing service, add it to our results
      JsonObjectPtr r = JsonObject::newObj();
      r->add("name", JsonObject::newString(aServiceInfo->name));
      r->add("hostname", JsonObject::newString(aServiceInfo->hostname));
      r->add("hostaddress", JsonObject::newString(aServiceInfo->hostaddress));
      r->add("ipv6", JsonObject::newBool(aServiceInfo->ipv6));
      r->add("port", JsonObject::newInt32(aServiceInfo->port));
      r->add("interface", JsonObject::newInt32(aServiceInfo->ifIndex));
      r->add("url", JsonObject::newString(aServiceInfo->url()));
      JsonObjectPtr txts = JsonObject::newObj();
      for (DnsSdServiceInfo::TxtRecordsMap::iterator pos = aServiceInfo->txtRecords.begin(); pos!=aServiceInfo->txtRecords.end(); ++pos) {
        txts->add(pos->first.c_str(), JsonObject::newString(pos->second));
      }
      r->add("txts", txts);
      aBrowsingresults->arrayAppend(r);
    }
    return true; // continue collecting until AllForNow
  }
}
// abort
void dnssdbrowse_abort(DnsSdServiceBrowserPtr aDnssdbrowser)
{
  aDnssdbrowser->stopBrowsing();
}
// initiator
static void dnssdbrowse_func(BuiltinFunctionContextPtr f)
{
  DnsSdServiceBrowserPtr dnssdbrowser = DnsSdManager::sharedDnsSdManager().newServiceBrowser();
  if (!dnssdbrowser) {
    f->finish(new AnnotatedNullValue("DNS-SD services not available"));
    return;
  }
  f->setAbortCallback(boost::bind(&dnssdbrowse_abort, dnssdbrowser));
  JsonObjectPtr browsingresults = JsonObject::newArray();
  dnssdbrowser->browse(
    f->arg(0)->stringValue().c_str(), // type
    boost::bind(&dnssdbrowsehandler, f, browsingresults, _1, _2)
  );
}


static const BuiltinMemberDescriptor dnssdGlobals[] = {
  { "dnssdbrowse", executable|async|json, dnssdbrowse_numargs, dnssdbrowse_args, &dnssdbrowse_func },
  { NULL } // terminator
};

DnsSdLookup::DnsSdLookup() :
  inherited(dnssdGlobals)
{
}

#endif // ENABLE_HTTP_SCRIPT_FUNCS && ENABLE_P44SCRIPT
