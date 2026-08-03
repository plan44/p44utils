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
#define FOCUSLOGLEVEL 6

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
  }


  #if ENABLE_ARTNET
  if (aDmxInputSpec.find("artnet")==0) {
    // set up artnet
    mArtNetReceiver = new ArtNetReceiver;
    // TODO: parse spec like: artnet[:<portaddress>[:<....]]
    //   for now, just run with defaults
    mArtNetReceiver->setConnectionParams();
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
  for(uint16_t channelNo=0; channelNo<aLength; channelNo++) {
    uint8_t newValue = aData[channelNo];
    if (newValue!=mUniverse[channelNo].current) {
      // report
      mUniverse[channelNo].current = newValue;
      DMXChannelChange cc;
      cc.channelNo = channelNo;
      cc.value = newValue;
      #if ENABLE_DMX_SCRIPT_FUNCS
      if (mRepresentingObj) {
        mRepresentingObj->gotChannelChange(cc);
      }
      #endif
      /*
      if (mDMXChangeCB) {
       mDMXChangeCB(cc);
      }
      */
    }
  }
  mProcessingFrame = false;
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


void DmxHandlerObj::gotChannelChange(const DMXChannelChange &aChannel)
{
  if (hasSinks()) { // optimisation: prevent creating unused objects
    ScriptObjPtr dmxevent = new DmxChannelChangeObj(aChannel);
    sendEvent(dmxevent);
  }
}


ScriptObjPtr DmxChannelChangeObj::actualValue() const
{
  // create the actual object value from the DMX channel data
  // (lazily in order to have filters applied BEFORE creating an expensive object value nobody needs)
  ScriptObjPtr o = new IntegerValue(mChannelChange.value);
  return o;
}


class DmxChannelFilter : public EventFilter
{
  uint16_t mChannelFilter;

public:
  DmxChannelFilter(uint16_t aChannelFilter) : mChannelFilter(aChannelFilter) {};

  virtual bool filteredEventObj(ScriptObjPtr &aEventObj) P44_OVERRIDE
  {
    if (!aEventObj) return false;
    DmxChannelChangeObj* c = dynamic_cast<DmxChannelChangeObj*>(aEventObj.get());
    assert(c);
    if (mChannelFilter!=c->change().channelNo) return false; // wrong channel
    // message passes filter, can be forwarded as-is
    return true;
  }
};


// channel(dmxchannel)
FUNC_ARG_DEFS(channel, { numeric });
static void channel_func(BuiltinFunctionContextPtr f)
{
  uint16_t channelNo = (uint16_t)f->arg(0)->intValue();
  EventSource* es = dynamic_cast<EventSource*>(f->thisObj().get());
  assert(es);
  // TODO: also allow just querying the current value, so:
  //   return a IntegerValue with types=numeric|freezable|keeporiginal
  //   which carries the channel value instead of OneShotEventNullValue
  f->finish(new OneShotEventNullValue(es, "dmx channel change", new DmxChannelFilter(channelNo)));
}


static const BuiltinMemberDescriptor dmxHandlerMembers[] = {
  FUNC_DEF_W_ARG(channel, executable|null),
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




