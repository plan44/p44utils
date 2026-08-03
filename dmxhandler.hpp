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

#ifndef __p44utils__dmxhandler__
#define __p44utils__dmxhandler__

#include "p44utils_main.hpp"
#include "artnetcomm.hpp"

#if !defined(ENABLE_DMXHANDLER) && (ENABLE_ARTNET || ENABLE_DMXINPUT)
  #define ENABLE_DMXHANDLER 1
#endif

#if ENABLE_DMXHANDLER

#if ENABLE_P44SCRIPT && !defined(ENABLE_DMX_SCRIPT_FUNCS)
  #define ENABLE_DMX_SCRIPT_FUNCS 1
#endif
#if ENABLE_DMX_SCRIPT_FUNCS && !ENABLE_P44SCRIPT
  #error "ENABLE_P44SCRIPT required when ENABLE_DMX_SCRIPT_FUNCS is set"
#endif

#if ENABLE_DMX_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif

using namespace std;

namespace p44 {

  #if ENABLE_DMX_SCRIPT_FUNCS
  namespace P44Script {
    class DmxHandlerObj;
    typedef boost::intrusive_ptr<DmxHandlerObj> DmxHandlerObjPtr;
  }
  #endif // ENABLE_DMX_SCRIPT_FUNCS


  typedef struct {
    uint8_t current;
    uint8_t reported;
    bool monitor;
    bool processing;
  } DMXChannel;


  class DmxHandler : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;

    #if ENABLE_DMX_SCRIPT_FUNCS
    P44Script::DmxHandlerObjPtr mRepresentingObj; ///< the (singleton) ScriptObj representing the DMX handler
    #endif

    #if ENABLE_ARTNET
    ArtNetReceiverPtr mArtNetReceiver;
    #endif
    #if ENABLE_DMXINPUT
    #error "todo: implement serial HW based DMX reception"
    #endif

    static const int cUniverseSize = 512;

    DMXChannel mUniverse[cUniverseSize];

    bool mProcessingFrame;

  public:

    DmxHandler();
    virtual ~DmxHandler();

    /// @return the object type (used for context descriptions such as logging context)
    virtual string contextType() const P44_OVERRIDE { return "DMX handler"; };

    /// open a DMX handler
    /// @param aDmxInputSpec the DMX input specification
    ///    (either /dev/xxx for HW DMX serial port or artnet receiver specs)
    /// @return error, if any
    ErrorPtr open(const string aDmxInputSpec);

    /// close the midi interface
    virtual void close();

    /// @param aChannelNo DMX channel number, 1..cUniverseSize. 0 is mapped to 1, values>cUniverseSize as cUniverseSize
    /// @return reference to live channel struct
    DMXChannel& getDmxChannel(uint16_t aChannelNo);

    #if ENABLE_DMX_SCRIPT_FUNCS
    /// @return a singleton script object, representing this midi bus, which can be registered as named member in a scripting domain
    P44Script::DmxHandlerObjPtr representingScriptObj();
    #endif

  private:

    void handleArtNetDmx(uint16_t aPortAddress, const uint8_t *aData, size_t aLength, const ArtNetSource &aSource);

    void handleDmxFrame(const uint8_t *aData, size_t aLength);

  };
  typedef boost::intrusive_ptr<DmxHandler> DmxHandlerPtr;


  #if ENABLE_DMX_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a set of dmx channels
    class DmxChannelsObj : public ScriptObj
    {
      typedef IntegerValue inherited;
      uint16_t mStartChannel; // 1-based
      uint16_t mEndChannel; // 1-based
      DmxHandlerPtr mDmxHandler;
      bool mAutoConfirm;
    public:
      DmxChannelsObj(DmxHandlerPtr aDmxHandler) : mDmxHandler(aDmxHandler), mStartChannel(0), mEndChannel(0), mAutoConfirm(false) {};
      void setRange(uint16_t aStartChannel, uint16_t aEndChannel, bool aAutoConfirm) { mStartChannel = aStartChannel; mEndChannel = aEndChannel; mAutoConfirm = aAutoConfirm; }
      virtual string getAnnotation() const P44_OVERRIDE { return string_format("DMX channels %hd-%hd",mStartChannel, mEndChannel); };
      virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return numeric|freezable|keeporiginal; };
      virtual bool isEventSource() const P44_OVERRIDE { return true; };
      virtual ScriptObjPtr actualValue() const P44_OVERRIDE;
      virtual void registerForFilteredEvents(EventSink* aEventSink, intptr_t aRegId = 0) P44_OVERRIDE;
      DmxHandlerPtr dmxHandler() const { return mDmxHandler; };
    };


    /// represents a DMX Handler
    class DmxHandlerObj : public StructuredLookupObject, public EventSource
    {
      typedef StructuredLookupObject inherited;
      friend class p44::DmxHandler;

      DmxHandlerPtr mDmxHandler;
    public:
      DmxHandlerObj(DmxHandlerPtr aDmxHandler);
      virtual ~DmxHandlerObj();
      virtual void deactivate() P44_OVERRIDE;
      virtual string getAnnotation() const P44_OVERRIDE { return "DMX handler"; };
      virtual P44LoggingObj* loggingContext() const P44_OVERRIDE { return mDmxHandler.get(); };
      DmxHandlerPtr dmxHandler() { return mDmxHandler; }
    private:
      void gotChannelChanges();
    };

    // get global builtins
    const BuiltinMemberDescriptor* dmxhandlerGlobals();

  } // namespace P44Script
  #endif // ENABLE_DMX_SCRIPT_FUNCS


} // namespace p44

#endif // ENABLE_DMXHANDLER
#endif // !__p44utils__dmxhandler__
