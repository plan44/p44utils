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
#define FOCUSLOGLEVEL 7

#include "dmxhandler.hpp"

#if ENABLE_ARTNET

#include "utils.hpp"

using namespace p44;

#if ENABLE_DMX_SCRIPT_FUNCS
using namespace P44Script;
#endif



// MARK: - DmxHandler

DmxHandler::DmxHandler() :
  mProcessingFrame(false)
{
}


DmxHandler::~DmxHandler()
{
  close();
}


/*
void MidiBus::setMidiDataHandler(MidiDataCB aMidiDataCB)
{
  mMidiDataCB = aMidiDataCB;
}
*/


ErrorPtr DmxHandler::open(const string aDmxInputSpec)
{
  ErrorPtr err;
  close(); // closes/deletes existing receiver
  // clear the universe
  for(int i=0; i<cUniverseSize; i++) {
    mUniverse[i].current = 0;
    mUniverse[i].reported = 0;
    mUniverse[i].monitor = 0;
    mUniverse[i].processing = 0;
  }
  #if ENABLE_ARTNET
  if (uequals(aDmxInputSpec, "artnet:", 7)) {
    // set up artnet
    mArtNetReceiver = new ArtNetReceiver;
    // TODO: parse more connection infos
    // for now, just: artnet:<portaddress>, use default binding + timeout
    const char *p = aDmxInputSpec.c_str()+7;
    string part;
    int portAddress = 0;
    if (nextPart(p, part, ':')) {
      portAddress = atoi(part.c_str());
    }
    // TODO: parse more parts
    // set params
    mArtNetReceiver->setConnectionParams(portAddress);
    mArtNetReceiver->setDmxHandler(boost::bind(&DmxHandler::handleArtNetDmx, this, _1, _2, _3, _4));
    // metadata
    ArtNet::ArtNetAdvertisementInfo info;
    // TODO: update info from application
    mArtNetReceiver->setAdvertisementInfo(info);
    // start
    err = mArtNetReceiver->startArtNet();
  }
  else
  #endif // ENABLE_ARTNET
  #if ENABLE_DMXINPUT
  if (aDmxInputSpec.find('/')==0) {
    // TODO: set up serial port
    #error tbd.
  }
  else
  #endif // ENABLE_DMXINPUT
  {
    err = TextError::err("unknown dmx input");
  }
  return err;
}


void DmxHandler::close()
{
  #if ENABLE_ARTNET
  if (mArtNetReceiver) {
    mArtNetReceiver->stopArtNet();
    mArtNetReceiver.reset();
  }
  #endif // ENABLE_ARTNET
  #if ENABLE_DMXINPUT
  if (mDMXSerial) {
    #error tbd.
  }
  #endif // ENABLE_DMXINPUT
}


void DmxHandler::handleArtNetDmx(uint16_t aPortAddress, const uint8_t *aData, size_t aLength, const ArtNetSource &aSource)
{
  // report
  FOCUSLOG("ArtNet: portaddress=%hd, datalength=%zd", aPortAddress, aLength);
  handleDmxFrame(aData, aLength);
}


void DmxHandler::handleDmxFrame(const uint8_t *aData, size_t aLength)
{
  if (aLength>cUniverseSize) aLength = cUniverseSize; // in case ours is smaller
  if (mProcessingFrame) {
    OLOG(LOG_DEBUG, "previous frame not processed -> skip one");
    return;
  }
  mProcessingFrame = true;
  bool needEvent = false;
  for(uint16_t channelIdx=0; channelIdx<aLength; channelIdx++) {
    uint8_t newValue = aData[channelIdx];
    DMXChannel& ch = mUniverse[channelIdx];
    // just detect changes
    if (newValue!=ch.current) {
      ch.current = newValue; // update anyway
      // has changed since last reporting
      if (ch.monitor && !ch.processing && newValue!=ch.reported) {
        // monitored and changed since last report -> report
        ch.reported = newValue;
        ch.processing = true; // "is new value" marker for event filter
        needEvent = true; // at least one change
      } // new to-be-reported value
    } // new value
  }
  #if ENABLE_DMX_SCRIPT_FUNCS
  if (needEvent && mRepresentingObj) {
    // inform event sinks registered with dmxhandler, let *those* filter by channel changes
    mRepresentingObj->gotChannelChanges();
  }
  #endif // ENABLE_DMX_SCRIPT_FUNCS
  mProcessingFrame = false;
}


DMXChannel& DmxHandler::getDmxChannel(uint16_t aChannelNo)
{
  if (aChannelNo>0) aChannelNo--; // make 0-based, DMX is traditionally 1-based.
  if (aChannelNo>cUniverseSize) aChannelNo=cUniverseSize-1; // limit to max channel we do have
  return mUniverse[aChannelNo];
}



#if ENABLE_DMX_SCRIPT_FUNCS

// MARK: - DMX handler scripting

P44Script::DmxHandlerObjPtr DmxHandler::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new DmxHandlerObj(this);
  }
  return mRepresentingObj;
}


ScriptObjPtr DmxChannelsObj::actualValue() const
{
  // create the actual object value from the dmx frame
  // (lazily in order to have filters applied BEFORE creating an expensive object value nobody needs)
  ArrayValuePtr a = new ArrayValue;
  for (uint16_t cn = mStartChannel; cn>0 && cn<=mEndChannel; cn++) {
    a->appendMember(new IntegerValue(dmxHandler()->getDmxChannel(cn).current));
  }
  return a;
}



class DmxChannelFilter : public EventFilter
{
  uint16_t mStartChannel;
  uint16_t mEndChannel;
  bool mAutoConfirm;

public:
  DmxChannelFilter(uint16_t aStartChannel, uint16_t aEndChannel, bool aAutoConfirm) : mStartChannel(aStartChannel), mEndChannel(aEndChannel), mAutoConfirm(aAutoConfirm) {};

  virtual bool filteredEventObj(ScriptObjPtr &aEventObj) P44_OVERRIDE
  {
    if (!aEventObj || mStartChannel<1 || mEndChannel<1 || mEndChannel<mStartChannel) return false;
    DmxChannelsObj* c = dynamic_cast<DmxChannelsObj*>(aEventObj.get());
    assert(c);
    // the filter is the authority for what objects should contain -> set object parameters
    c->setRange(mStartChannel, mEndChannel, mAutoConfirm);
    // check if any of the covered channels has a change
    bool anyChanged = false;
    for (uint16_t cn = mStartChannel; cn>0 && cn<=mEndChannel; cn++) {
      DMXChannel& ch = c->dmxHandler()->getDmxChannel(cn);
      if (ch.processing) {
        anyChanged = true;
        if (mAutoConfirm) ch.processing = false;
        break;
      }
    }
    return anyChanged; // at least one of my channels has changed
  }
};


void DmxHandlerObj::gotChannelChanges()
{
  if (hasSinks()) { // optimisation: prevent creating unused objects
    // this object is created by an event and will get its channel range from the filter
    ScriptObjPtr dmxevent = new DmxChannelsObj(dmxHandler());
    sendEvent(dmxevent);
  }
}


void DmxChannelsObj::registerForFilteredEvents(EventSink* aEventSink, intptr_t aRegId)
{
  if (mDmxHandler) {
    DmxHandlerObjPtr dmx = mDmxHandler->representingScriptObj();
    if (dmx) dmx->registerForEvents(aEventSink, aRegId, new DmxChannelFilter(mStartChannel, mEndChannel, mAutoConfirm)); // obj that registers is NOT an event and does have range set
  }
}


// channels(startchannel, endchannel, [,autoconfirm])
FUNC_ARG_DEFS(channels, { numeric }, { numeric }, { numeric|optionalarg });
static void channels_func(BuiltinFunctionContextPtr f)
{
  uint16_t startChannel = (uint16_t)f->arg(0)->intValue();
  uint16_t endChannel = (uint16_t)f->arg(1)->intValue();
  bool autoconfirm = f->arg(2)->boolValue();
  DmxHandlerObj* dmxObj = dynamic_cast<DmxHandlerObj*>(f->thisObj().get());
  assert(dmxObj);
  for (uint16_t cn = startChannel; cn>0 && cn<=endChannel; cn++) {
    dmxObj->dmxHandler()->getDmxChannel(cn).monitor = true; // quering a channel for the first time makes it monitored (forever)
  }
  DmxChannelsObj* co = new DmxChannelsObj(dmxObj->dmxHandler());
  co->setRange(startChannel, endChannel, autoconfirm);
  f->finish(co);
}


// confirm(startchannel, endchannel)
FUNC_ARG_DEFS(confirm, { numeric }, { numeric });
static void confirm_func(BuiltinFunctionContextPtr f)
{
  uint16_t startChannel = (uint16_t)f->arg(0)->intValue();
  uint16_t endChannel = (uint16_t)f->arg(1)->intValue();
  DmxHandlerObj* dmxObj = dynamic_cast<DmxHandlerObj*>(f->thisObj().get());
  assert(dmxObj);
  for (uint16_t cn = startChannel; cn>0 && cn<=endChannel; cn++) {
    dmxObj->dmxHandler()->getDmxChannel(cn).processing = false; // enable sending another event
  }
  f->finish();
}


static const BuiltinMemberDescriptor dmxHandlerMembers[] = {
  FUNC_DEF_W_ARG(channels, executable|null),
  FUNC_DEF_W_ARG(confirm, executable|null),
  BUILTINS_TERMINATOR
};

static BuiltInMemberLookup* sharedDmxHandlerFunctionLookupP = NULL;

DmxHandlerObj::DmxHandlerObj(DmxHandlerPtr aDmxHandler) :
  mDmxHandler(aDmxHandler)
{
  registerSharedLookup(sharedDmxHandlerFunctionLookupP, dmxHandlerMembers);
}


void DmxHandlerObj::deactivate()
{
  if (mDmxHandler) {
    mDmxHandler->close();
    mDmxHandler.reset();
  }
}


DmxHandlerObj::~DmxHandlerObj()
{
  deactivate();
}


// dmxinput(dmxinputspec)
FUNC_ARG_DEFS(dmxinput, { text } );
static void dmxinput_func(BuiltinFunctionContextPtr f)
{
  DmxHandlerPtr dmxHandler = new DmxHandler;
  ErrorPtr err = dmxHandler->open(f->arg(0)->stringValue());
  if (Error::isOK(err)) {
    f->finish(dmxHandler->representingScriptObj());
  }
  else {
    f->finish(new ErrorValue(err));
  }
}


static const BuiltinMemberDescriptor cDmxHandlerGlobals[] = {
  FUNC_DEF_W_ARG(dmxinput, executable|null),
  BUILTINS_TERMINATOR
};


const BuiltinMemberDescriptor* p44::P44Script::dmxhandlerGlobals()
{
  return cDmxHandlerGlobals;
}

#endif // ENABLE_DMX_SCRIPT_FUNCS

#endif // ENABLE_ARTNET




