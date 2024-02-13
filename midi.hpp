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

#ifndef __p44utils__midi__
#define __p44utils__midi__

#include "p44utils_main.hpp"

#ifndef ENABLE_MIDI
  // We assume that including this file in a build usually means that modbus support is actually needed.
  // Still, ENABLE_MODBUS can be set to 0 to create build variants w/o removing the file from the project/makefile
  #define ENABLE_MIDI 1
#endif

#if ENABLE_MIDI

#include "serialcomm.hpp"

#include <stdio.h>

#if ENABLE_P44SCRIPT && !defined(ENABLE_MIDI_SCRIPT_FUNCS)
  #define ENABLE_MIDI_SCRIPT_FUNCS 1
#endif
#if ENABLE_MIDI_SCRIPT_FUNCS && !ENABLE_P44SCRIPT
  #error "ENABLE_P44SCRIPT required when ENABLE_MIDI_SCRIPT_FUNCS is set"
#endif

#if ENABLE_MIDI_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif

using namespace std;

namespace p44 {

  typedef enum : uint8_t {
    none = 0,
    statusbit = 0x80,
    data_mask = 0x7F,
    // channel voice commands
    cvcmd_mask = 0xF0,
    channel_mask = 0x0F,
    filter_note_on_off = 0x89, // filter only: note_on or note_off
    note_off = 0x80,
    note_on = 0x90,
    note_mask = 0xE0,
    poly_key_pressure = 0xA0,
    control_change = 0xB0, // also channel mode
    program_change = 0xC0, // has only 1 data byte
    channel_pressure = 0xD0, // has only 1 data byte
    pitch_bend = 0xE0,
    channel_cmd_max = pitch_bend,
    // system commands
    system = 0xF0,
    // - system common
    system_common_mask = 0xF8,
    system_common_prefix = 0xF0,
    time_code_qf = 0xF1, // MIDI Time Code Quarter Frame
    song_position_ptr = 0xF2, // Song Position Pointer
    song_select = 0xF3, // Song Select
    tune_request = 0xF6, // Tune Request
    // - system exclusive
    system_exclusive = 0xF0,
    system_eox = 0xF7, // EOX (End of Exclusive)
    // - system real time
    system_real_time_mask = 0xF8,
    system_real_time_prefix = 0xF8,
    timing_clock = 0xF8, // Timing Clock
    start = 0xFA, // Start
    cont = 0xFB, // Continue
    stop = 0xFC,  // Stop
    active_sensing = 0xFE, // Active Sensing
    system_reset = 0xFF // System Reset
  } MidiStatus;


  typedef struct {
    MidiStatus status;
    uint8_t key; ///< note or controller number. 0 for messages without either
    uint16_t value; ///< 14bit for pitch bend, control 0..31 (LSB 32..63), song position pointer, 7 bit for all others
  } MidiMessage;


  /// Callback executed when midi data arrives
  /// @param aMidiData the midi data received
  typedef boost::function<void (MidiMessage aMidiData)> MidiDataCB;

  #if ENABLE_MIDI_SCRIPT_FUNCS
  namespace P44Script {
    class MidiBusObj;
    typedef boost::intrusive_ptr<MidiBusObj> MidiBusObjPtr;
  }
  #endif

  class MidiBus : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;

    #if ENABLE_MIDI_SCRIPT_FUNCS
    P44Script::MidiBusObjPtr mRepresentingObj; ///< the (singleton) ScriptObj representing this midi interface
    #endif

    SerialCommPtr mMidiDevice;

    MidiDataCB mMidiDataCB;

    MidiStatus mLastReceivedStatus; ///< last status received for running status
    MidiStatus mLastSentStatus; ///< last status sent for sending with running status
    static const int numChannels = 16;
    static const int num14BitControls = 32;
    uint8_t mControlMSBCache[numChannels*num14BitControls]; ///< cache for MSBs of all 32 14bit controls in all 16 channels
    uint8_t mFirstData; ///< first (maybe only) data byte
    uint8_t mFinalData; ///< second (or only, in that case a copy of mFirstData) data byte

    enum {
      idle, // waiting for status
      running, // running status, new status or data
      initialData, // waiting for first or final data byte
      secondData, // waiting for second and final data byte
      sysex, // system exclusive, just swallow data
    } mReceiveState;

  public:

    MidiBus();
    virtual ~MidiBus();

    /// @return the object type (used for context descriptions such as logging context)
    virtual string contextType() const P44_OVERRIDE { return "midi bus"; };

    /// open a midi interface device
    /// @param aMidiConnectionSpec the connection specification
    ///    (usually a simple /dev/xxx, but can also be a IP socket level connection)
    /// @return error, if any
    ErrorPtr open(const string aMidiConnectionSpec);

    /// close the midi interface
    virtual void close();

    /// set a midi data handler
    /// @param aMidiDataCB is called whenever midi data is received
    void setMidiDataHandler(MidiDataCB aMidiDataCB);

    /// send midi data
    /// @param aMidiMessage midi message to be sent
    /// @param aRunningStatus if set, and previous command allows, running status
    ///   (not sending the status/command byte again) will be used
    /// @param aSysExData if not null, this is system_exclusive data. Command must be actually system_exclusive to send this.
    ///    A system_eox will be automatically appended
    ErrorPtr sendMidi(const MidiMessage& aMidiMessage, bool aRunningStatus, const string* aSysExData = nullptr);

    #if ENABLE_MIDI_SCRIPT_FUNCS
    /// @return a singleton script object, representing this midi bus, which can be registered as named member in a scripting domain
    P44Script::MidiBusObjPtr representingScriptObj();
    #endif

  private:
    
    uint8_t& controlMSBCache(MidiStatus aStatus, uint8_t aControlNumber);
    void midiDataHandler(ErrorPtr aStatus);
    void processMidiCommand();

  };
  typedef boost::intrusive_ptr<MidiBus> MidiBusPtr;


  #if ENABLE_MIDI_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a midi message
    class MidiMessageObj : public ScriptObj
    {
      typedef ScriptObj inherited;
      MidiMessage mMessage;
    public:
      MidiMessageObj(const MidiMessage& aMessage) : mMessage(aMessage) {};
      virtual string getAnnotation() const P44_OVERRIDE { return "midi message"; };
      virtual ScriptObjPtr actualValue() const P44_OVERRIDE;
      const MidiMessage& message() { return mMessage; };
    };


    /// represents a midi bus
    class MidiBusObj : public StructuredLookupObject, public EventSource
    {
      typedef StructuredLookupObject inherited;
      friend class p44::MidiBus;

      MidiBusPtr mMidiBus;
    public:
      MidiBusObj(MidiBusPtr aMidiBus);
      virtual ~MidiBusObj();
      virtual void deactivate() P44_OVERRIDE;
      virtual string getAnnotation() const P44_OVERRIDE { return "midi bus"; };
      MidiBusPtr midibus() { return mMidiBus; }
    private:
      void gotMessage(const MidiMessage &aMessage);
    };


    /// represents the global objects related to midi
    class MidiLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      MidiLookup();
    };

  } // namespace P44Script
  #endif // ENABLE_MIDI_SCRIPT_FUNCS


} // namespace p44

#endif // ENABLE_MIDI
#endif // __p44utils__midi__
