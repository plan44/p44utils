//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 0

#include "midi.hpp"

#if ENABLE_MIDI

#include "application.hpp" // for userlevel check
#include "utils.hpp"

using namespace p44;

#if ENABLE_MIDI_SCRIPT_FUNCS
using namespace P44Script;
#endif



// MARK: - MidiBus

MidiBus::MidiBus() :
  mReceiveState(idle)
{
  mLastSentStatus = MidiStatus::none;
  mLastReceivedStatus = MidiStatus::none;
  memset(&mControlMSBCache, 0xFF, numChannels*num14BitControls); // impossible MSBs
  mFirstData = 0;
  mFinalData = 0;
}


MidiBus::~MidiBus()
{
  close();
}


void MidiBus::setMidiDataHandler(MidiDataCB aMidiDataCB)
{
  mMidiDataCB = aMidiDataCB;
}


ErrorPtr MidiBus::open(const string aMidiConnectionSpec)
{
  ErrorPtr err;
  close(); // closes/deletes existing mMidiDevice
  mMidiDevice = new SerialComm;
  mMidiDevice->setConnectionSpecification(aMidiConnectionSpec.c_str(), 2077, "none");
  mMidiDevice->setDeviceOpParams(O_RDWR|O_NONBLOCK, true); // do not rely on numBytesReady()
  err = mMidiDevice->establishConnection();
  if (Error::isOK(err)) {
    // successfully open, start monitoring
    mMidiDevice->setReceiveHandler(boost::bind(&MidiBus::midiDataHandler, this, _1));
    mMidiDevice->makeNonBlocking();
  }
  return err;
}


void MidiBus::close()
{
  if (mMidiDevice) {
    mMidiDevice->stopMonitoringAndClose();
    mMidiDevice->setReceiveHandler(NoOP);
    mMidiDevice->setTransmitHandler(NoOP);
    mMidiDevice = nullptr;
  }
}


static ssize_t numMidiDataBytes(MidiStatus aStatus)
{
  if (aStatus==system_exclusive) {
    return -1; // undefined length
  }
  else if (aStatus==system_eox) {
    return 0; // no data (any more)
  }
  else if ((aStatus & system_real_time_mask)==system_real_time_prefix) {
    // status-only system real time command
    return 0; // no data (any more)
  }
  else if ((aStatus & system_common_mask)==system_common_mask) {
    // system common command
    if (aStatus==song_position_ptr) return 2; // song position has 2 bytes data
    else if (aStatus==time_code_qf || aStatus==song_select) return 1; // 1 bytes data
    return 0;
  }
  else if ((aStatus & cvcmd_mask)==program_change || (aStatus & cvcmd_mask)==channel_pressure) {
    // single data byte expected
    return 1;
  }
  else {
    // two data bytes expected
    return 2;
  }
}


uint8_t& MidiBus::controlMSBCache(MidiStatus aStatus, uint8_t aControlNumber)
{
  // mask down to prevent overflow
  uint16_t channel = aStatus & channel_mask;
  aControlNumber &= (num14BitControls-1);
  return mControlMSBCache[channel*num14BitControls + aControlNumber];
}


ErrorPtr MidiBus::sendMidi(const MidiMessage& aMidiMessage, bool aRunningStatus, const string* aSysExData)
{
  if (mMidiDevice) {
    size_t msgSize = 0;
    uint8_t mididata[3];
    ErrorPtr err;
    if ((aMidiMessage.status & statusbit)==0) return TextError::err("invalid command byte");
    if (aMidiMessage.status==system_exclusive) {
      // handle sysex sending
      mMidiDevice->transmitBytes(1, mididata, err);
      if (Error::notOK(err)) return err;
      if (aSysExData) {
        if (!mMidiDevice->transmitString(*aSysExData)) return TextError::err("error sending sysex data");
      }
      // transmit the EOX
      mididata[0] = system_eox;
      mMidiDevice->transmitBytes(1, mididata, err);
      return err;
    }
    // non-SysEx
    if (!aRunningStatus || mLastSentStatus!=aMidiMessage.status) {
      // need to send status, cannot rely on running status
      mididata[msgSize++] = aMidiMessage.status;
    }
    if ((aMidiMessage.status & cvcmd_mask)==control_change) {
      mididata[msgSize++] = aMidiMessage.key & data_mask;
      if (aMidiMessage.key<num14BitControls) {
        // we might need to expand into MSB/LSB and might omit MSB
        if ((aMidiMessage.value>>7) != controlMSBCache(aMidiMessage.status, aMidiMessage.key)) {
          // need to send MSB first
          mididata[msgSize] = (aMidiMessage.value>>7) & data_mask;
          controlMSBCache(aMidiMessage.status, aMidiMessage.key) = mididata[msgSize]; // update MSB cache
          mMidiDevice->transmitBytes(msgSize+1, mididata, err);
          if (Error::notOK(err)) return err;
          if ((aMidiMessage.value & data_mask)==0) return err; // done, sending MSB is sufficient
        }
      }
      // all other controls are LSBs (7-bit values only, or LSB of 14-bit ones)
      mididata[msgSize++] = aMidiMessage.value & data_mask;
    }
    else if ((aMidiMessage.status & cvcmd_mask)==pitch_bend || (aMidiMessage.status & cvcmd_mask)==song_position_ptr) {
      // 2-byte data representing 14-bit values
      mididata[msgSize++] = aMidiMessage.value & data_mask; // LSB
      mididata[msgSize++] = (aMidiMessage.value>>7) & data_mask; // MSB
    }
    else {
      // other commands
      ssize_t dsz = numMidiDataBytes(aMidiMessage.status);
      if (dsz<0) return TextError::err("sysex sending not yet implemented"); // FIXME: cannot send sysex yet
      if (dsz>0) {
        if (dsz>1) {
          mididata[msgSize++] = aMidiMessage.key & data_mask;
        }
        mididata[msgSize++] = aMidiMessage.value & data_mask;
      }
    }
    mMidiDevice->transmitBytes(msgSize+2, mididata, err);
    return err;
  }
  else {
    return TextError::err("midi bus not open");
  }
}



void MidiBus::midiDataHandler(ErrorPtr aStatus)
{
  ErrorPtr err;
  // gobble up all available bytes
  uint8_t by;
  while (mMidiDevice->receiveBytes(1, &by, err)>0) {
    OLOG(LOG_DEBUG, "got midi byte = 0x%02x", by);
    if (Error::notOK(err)) break;
    size_t dsz = 0;
    // byte received
    switch (mReceiveState) {
      case idle:
      case running:
        if ((by & MidiStatus::statusbit)==0) {
          if (mReceiveState==running) goto initialData; // evaluate as first data according to last received status
          // no running status, cannot process data
          OLOG(LOG_WARNING, "expecting status, got 0x%02x", by);
          mReceiveState = idle;
          break;
        }
        // is a new status
        goto newstatus;
      resync:
        OLOG(LOG_WARNING, "was expecting data, got new status 0x%02x -> re-sync", by);
        mReceiveState = idle;
        // process new status
      newstatus:
        // new status
        mLastReceivedStatus = static_cast<MidiStatus>(by);
        if (mLastReceivedStatus==system_eox) {
          mReceiveState = idle;
        }
        else if (mLastReceivedStatus==system_exclusive) {
          mReceiveState = sysex;
        }
        else {
          dsz = numMidiDataBytes(mLastReceivedStatus);
          if (dsz==0) {
            // status-only system real time command
            processMidiCommand();
            mReceiveState = idle;
          }
          else {
            mReceiveState = initialData;
          }
        }
        break;
      case initialData:
      initialData:
        if (by & MidiStatus::statusbit) goto resync;
        mFirstData = by; // is first byte
        mFinalData = by; // and possibly final as well for 1-byte commands
        dsz = numMidiDataBytes(mLastReceivedStatus);
        if (dsz==1) goto process; // single byte command, process now
        // second data byte expected
        mFinalData = 0; // we don't have the final data
        mReceiveState = secondData;
        break;
      case secondData:
        mFinalData = by;
        goto process; // second byte found, process now
      case sysex:
        // expecting sysex data
        if (by & MidiStatus::statusbit) {
          if (by!=system_eox) goto resync; // only EOX is expected -> resync
          // proper EOX
          // TODO: maybe later: process sysex
          goto newstatus;
        }
        // TODO: maybe later: collect sysex data
        break;
      process:
        // data collected, process command
        processMidiCommand();
        mReceiveState = running;
        break;
    }
  }
}


void MidiBus::processMidiCommand()
{
  MidiMessage m;
  if ((mLastReceivedStatus & cvcmd_mask)!=system) {
    // channel voice messages
    m.status = mLastReceivedStatus;
    if ((mLastReceivedStatus & cvcmd_mask)==pitch_bend) {
      // pitch bend
      m.key = 0; // not a real control, but pitch bend
      m.value = mFirstData | (uint16_t)mFinalData<<7;
    }
    else if ((mLastReceivedStatus & cvcmd_mask)==control_change) {
      m.key = mFirstData;
      if (mFirstData<num14BitControls) {
        // MSB of possibly 14-bit resolution values 0..31 (with 32..63 = LSBs)
        m.value = mFinalData<<7; // MSB (with LSB implied 0 for now)
        controlMSBCache(mLastReceivedStatus, mFirstData) = mFinalData;
      }
      else if (mFirstData<num14BitControls*2) {
        // LSB for controller 0..31
        uint16_t cc = controlMSBCache(mLastReceivedStatus, mFirstData);
        if (cc & statusbit) cc = 0; // invalid cache entry, assume 0
        m.value = cc<<7 | mFinalData; // cache and this LSB
      }
      else {
        // 7-bit control value
        m.value = mFinalData; // deliver as-is
      }
    }
    else {
      // voice message
      m.value = mFinalData;
      if ((mLastReceivedStatus & cvcmd_mask)==program_change || (mLastReceivedStatus & cvcmd_mask)==channel_pressure) {
        m.key = 0;
      }
      else {
        m.key = mFirstData;
      }
    }
  }
  else {
    // system message
    m.key = 0; // not a real control
    m.value = 0; // no value by default
    if (mLastReceivedStatus==song_position_ptr) {
      // song position has a 14-bit value
      m.value = mFirstData | (uint16_t)mFinalData<<7;
    }
    else if (mLastReceivedStatus==system_exclusive) {
      // TODO: implement sysex
      return; // discard for now
    }
    else if (mLastReceivedStatus==time_code_qf || mLastReceivedStatus==song_select) {
      // these have a 7-bit value
      m.value = mFinalData;
    }
    // all others have no data
  }
  // report
  OLOG(LOG_INFO, "cmd=0x%02x, key(note/control)=0x%02x/%u, value(velocity/param)=0x%04x/%u (MSB only=%u)", m.status, m.key, m.key, m.value, m.value, m.value>>7);
  #if ENABLE_MIDI_SCRIPT_FUNCS
  if (mRepresentingObj) {
    mRepresentingObj->gotMessage(m);
  }
  #endif
  if (mMidiDataCB) {
    mMidiDataCB(m);
  }
}



#if ENABLE_MIDI_SCRIPT_FUNCS

// MARK: - midi scripting

P44Script::MidiBusObjPtr MidiBus::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new MidiBusObj(this);
  }
  return mRepresentingObj;
}


void MidiBusObj::gotMessage(const MidiMessage &aMessage)
{
  ScriptObjPtr midievent = new MidiMessageObj(aMessage);
  sendEvent(midievent);
}


ScriptObjPtr MidiMessageObj::actualValue() const
{
  // create the actual object value from the midi data
  // (lazily in order to have filters applied BEFORE creating an expensive object value nobody needs)
  ScriptObjPtr o = new ObjectValue;
  if ((mMessage.status & cvcmd_mask)<=channel_cmd_max) {
    // - mask out channel for channel commands for simpler command matching (like `command == 0x80`)
    o->setMemberByName("command", new IntegerValue(mMessage.status & cvcmd_mask));
    // - provide channel separated from command
    o->setMemberByName("channel", new IntegerValue(mMessage.status & channel_mask));
  }
  else {
    // System commands do not have a channel
    o->setMemberByName("command", new IntegerValue(mMessage.status & cvcmd_mask));
  }
  // - separated channel
  o->setMemberByName("key", new IntegerValue(mMessage.key));
  o->setMemberByName("value", new IntegerValue(mMessage.value));
  return o;
}


class MidiMessageFilter : public EventFilter
{
  MidiStatus mCommandFilter; // statusbit must be set to enable
  uint8_t mChannelFilter; // statusbit must be set to enable
  uint8_t mKeyFilter; // statusbit must be set to enable
public:
  MidiMessageFilter(MidiStatus aCommandFilter, uint8_t aChannelFilter, uint8_t aKeyFilter) : mCommandFilter(aCommandFilter), mChannelFilter(aChannelFilter), mKeyFilter(aKeyFilter) {};

  virtual bool filteredEventObj(ScriptObjPtr &aEventObj) P44_OVERRIDE
  {
    if (!aEventObj) return false;
    MidiMessageObj* m = dynamic_cast<MidiMessageObj*>(aEventObj.get());
    assert(m);
    if (mCommandFilter & statusbit) {
      // we have a command filter
      if ((mCommandFilter & cvcmd_mask)<=channel_cmd_max) {
        // channel voice command
        if (mCommandFilter==filter_note_on_off) {
          if ((m->message().status & note_mask)!=note_off) return false; // not note on or off
        }
        else {
          if ((mCommandFilter & cvcmd_mask)!=(m->message().status & cvcmd_mask)) return false; // CV command does not pass
        }
      }
      else {
        // other command without channel, entire status must match
        if (mCommandFilter!=m->message().status) return false; // system command does not pass
      }
    }
    if (mChannelFilter & statusbit) {
      // we have a channel filter
      if ((m->message().status & cvcmd_mask)>channel_cmd_max) return false; // message does not have a channel -> does not pass
      else if ((mChannelFilter & channel_mask)!=(m->message().status & channel_mask)) return false; // wrong channel, does not pass
    }
    if (mKeyFilter & statusbit) {
      // we have a key (note/control number) filter
      if ((mKeyFilter & data_mask)!=m->message().key) return false; // wrong key, does not pass
    }
    // message passes filter, can be forwarded as-is
    return true;
  }
};


// message()
// message(commandfilter, channelfilter, keyfilter)
FUNC_ARG_DEFS(message, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg });
static void message_impl_func(BuiltinFunctionContextPtr f, MidiStatus aCmdFilter, int aCmdArg, int aChannelArg, int aKeyArg)
{
  EventSource* es = dynamic_cast<EventSource*>(f->thisObj().get());
  assert(es);
  uint8_t chFilter = 0;
  uint8_t keyFilter = 0;
  if (aCmdArg>=0 && f->arg(aCmdArg)->defined()) aCmdFilter = static_cast<MidiStatus>(f->arg(aCmdArg)->intValue() | statusbit);
  if (aChannelArg>=0 && f->arg(aChannelArg)->defined()) chFilter = f->arg(aChannelArg)->intValue() | statusbit;
  if (aKeyArg>=0 && f->arg(aKeyArg)->defined()) keyFilter = f->arg(aKeyArg)->intValue() | statusbit;
  f->finish(new OneShotEventNullValue(es, "midi message", new MidiMessageFilter(aCmdFilter, chFilter, keyFilter)));
}
static void message_func(BuiltinFunctionContextPtr f)
{
  message_impl_func(f, MidiStatus::none, 0, 1, 2);
}

// control(channel, controlno)
FUNC_ARG_DEFS(control, { numeric|optionalarg }, { numeric|optionalarg });
static void control_func(BuiltinFunctionContextPtr f)
{
  message_impl_func(f, MidiStatus::control_change, -1, 0, 1);
}

// note(channel, note, on)
FUNC_ARG_DEFS(note, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg });
static void note_func(BuiltinFunctionContextPtr f)
{
  MidiStatus cf = filter_note_on_off;
  if (f->arg(2)->defined()) cf = f->arg(2)->boolValue() ? note_on : note_off;
  message_impl_func(f, cf, -1, 0, 1);
}

// program(channel)
FUNC_ARG_DEFS(program, { numeric|optionalarg });
static void program_func(BuiltinFunctionContextPtr f)
{
  message_impl_func(f, program_change, -1, 0, -1);
}

// pitchbend(channel)
FUNC_ARG_DEFS(pitchbend, { numeric|optionalarg });
static void pitchbend_func(BuiltinFunctionContextPtr f)
{
  message_impl_func(f, pitch_bend, -1, 0, -1);
}


// send(command, value)
// send(command, key, value)
// send(sysex)
FUNC_ARG_DEFS(send, { numeric|text }, { numeric|optionalarg }, { numeric|optionalarg } );
static void send_func(BuiltinFunctionContextPtr f)
{
  MidiBusObj* o = dynamic_cast<MidiBusObj*>(f->thisObj().get());
  assert(o);
  MidiMessage m;
  ErrorPtr err;
  if (f->numArgs()==1 && f->arg(0)->hasType(text)) {
    // system exclusive
    m.status = system_exclusive;
    string sysexdata = f->arg(0)->stringValue();
    err = o->midibus()->sendMidi(m, false, &sysexdata);
  }
  else {
    // normal command
    m.status = static_cast<MidiStatus>(f->arg(0)->intValue() | statusbit);
    m.key = 0;
    if (f->numArgs()>2) {
      m.key = f->arg(1)->intValue();
      m.value = f->arg(2)->intValue();
    }
    else {
      m.value = f->arg(1)->intValue();
    }
    err = o->midibus()->sendMidi(m, false); // always send with status, scripted timing is not that precise anyway
  }
  f->finish(ErrorValue::nothingOrError(err));
}


static const BuiltinMemberDescriptor midiBusMembers[] = {
  FUNC_DEF_W_ARG(send, executable|null),
  FUNC_DEF_W_ARG(message, executable|null),
  FUNC_DEF_W_ARG(control, executable|objectvalue|null),
  FUNC_DEF_W_ARG(program, executable|objectvalue|null),
  FUNC_DEF_W_ARG(pitchbend, executable|objectvalue|null),
  FUNC_DEF_W_ARG(note, executable|objectvalue|null),
  BUILTINS_TERMINATOR
};

static BuiltInMemberLookup* sharedMidiBusFunctionLookupP = NULL;

MidiBusObj::MidiBusObj(MidiBusPtr aMidiBus) :
  mMidiBus(aMidiBus)
{
  registerSharedLookup(sharedMidiBusFunctionLookupP, midiBusMembers);
}


void MidiBusObj::deactivate()
{
  if (mMidiBus) {
    mMidiBus->close();
    mMidiBus.reset();
  }
}


MidiBusObj::~MidiBusObj()
{
  deactivate();
}


// midibus(midiconnectionspec)
FUNC_ARG_DEFS(midibus, { text } );
static void midibus_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  MidiBusPtr midibus = new MidiBus;
  ErrorPtr err = midibus->open(f->arg(0)->stringValue());
  if (Error::isOK(err)) {
    f->finish(midibus->representingScriptObj());
  }
  else {
    f->finish(new ErrorValue(err));
  }
}


static const BuiltinMemberDescriptor cMidiGlobals[] = {
  FUNC_DEF_W_ARG(midibus, executable|null),
  BUILTINS_TERMINATOR
};

const BuiltinMemberDescriptor* p44::P44Script::midiGlobals()
{
  return cMidiGlobals;
}

#endif // ENABLE_MIDI_SCRIPT_FUNCS

#endif // ENABLE_MIDI




