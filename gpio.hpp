//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__gpio__
#define __p44utils__gpio__

#include "p44utils_main.hpp"

#include "iopin.hpp"

#ifndef ENABLE_GPIOSYSFS
  #define ENABLE_GPIOSYSFS 1 // does not use any special APIs, can be on even on non-linux (just does not work then)
#endif

#ifndef ENABLE_GPIOD
  #define ENABLE_GPIOD 0
#endif

#if ENABLE_GPIOD
  #include <gpiod.h>
  // Note: this is a bit fragile, but as gpiod v1 and v2 have completely incompatible apis
  //   but no proper way to detect the API at compile time, we're relying on a macro that
  //   is defined in v1 only:
  #if defined(GPIOD_BIT)
    // indicates v1
    #define GPIOD_VERS 1
  #else
    #define GPIOD_VERS 2
  #endif
#else // ENABLE_GPIOD
  #define GPIOD_VERS 0
#endif // ENABLE_GPIOD


#ifdef ESP_PLATFORM
  #include "driver/gpio.h"
#endif

#ifndef GPION9XXX_DEVICES_BASEPATH
  #define GPION9XXX_DEVICES_BASEPATH "/dev/gpio/"
#endif


using namespace std;

namespace p44 {


  #ifndef ESP_PLATFORM

  /// Wrapper for LED output accessed via
  /// generic Linux kernel SysFS support for LEDs (
  class GpioLedPin : public IOPin
  {
    typedef IOPin inherited;

    int mLedFD;
    bool mLedState;

  public:

    /// Create general purpose I/O pin
    /// @param aLedName name or number of the led. If aLedName starts with digit, it is considered
    ///   an old-style numeric led and will be prefixed with "led" to form the LED name
    /// @param aInitialState initial state for the LED
    GpioLedPin(const char* aLedName, bool aInitialState);
    virtual ~GpioLedPin();

    /// get state of LED
    /// @return current state of LED
    virtual bool getState();

    /// set state of LED
    /// @param aState new state to set LED to
    virtual void setState(bool aState);
  };

  #endif // !ESP_PLATFORM


  #if ENABLE_GPIOSYSFS || defined(ESP_PLATFORM)

  /// Wrapper for General Purpose I/O pin as accessed via
  /// generic Linux kernel SysFS support for GPIOs, or ESP32-IDF GPIO routines
  class GpioPin : public IOPin
  {
    typedef IOPin inherited;

    bool mPinState;
    bool mOutput;
    #ifdef ESP_PLATFORM
    gpio_num_t mGpioNo;
    #else
    int mGpioNo;
    int mGpioFD;
    #endif

    bool stateChanged(int aPollFlags);

  public:

    /// Create general purpose I/O pin
    /// @param aGpioNo numberof the GPIO
    /// @param aOutput use as output
    /// @param aInitialState initial state assumed for inputs and enforced for outputs
    /// @param aPull yes = pull up, no = pull down, undefined = no pull
    GpioPin(int aGpioNo, bool aOutput, bool aInitialState, Tristate aPull=undefined);
    virtual ~GpioPin();

    /// get state of GPIO
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    virtual bool getState() P44_OVERRIDE;

    /// set state of output (NOP for inputs)
    /// @param aState new state to set output to
    virtual void setState(bool aState) P44_OVERRIDE;

    /// install state change detector
    /// @param aInputChangedCB will be called when the input state changes. Passing NULL disables input state change reporting.
    /// @param aInverted if set, the state will be reported inverted to what getState() would report. This is a shortcut
    ///   for efficient implementation of higher level classes (which support inverting), to avoid two stage callbacks
    /// @param aInitialState the initial state (of the pin) assumed present when callback is installed
    /// @param aDebounceTime after a reported state change, next input sampling will take place only after specified interval
    /// @param aPollInterval if <0 (Infinite), the state change detector only works if the input pin supports state change
    ///   detection without polling (e.g. with GPIO edge trigger). If aPollInterval is >=0, and the
    ///   input pin does not support edge detection, the state detection will be implemented via polling
    ///   on the current mainloop - if pollInterval==0 then polling will be done in a mainloop idle handler, otherwise in
    ///   a timed handler according to the specified interval
    /// @return true if input supports the type of state change detection requested
    virtual bool setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval) P44_OVERRIDE;

  };

  #endif // ENABLE_GPIOSYSFS || defined(ESP_PLATFORM)

  #if GPIOD_VERS==1

  /// Wrapper for General Purpose I/O pin as accessed via
  /// modern Linux Gpiod API (libgpiod v1)
  class GpiodPin : public IOPin
  {
    typedef IOPin inherited;

    struct gpiod_chip* mChip;
    struct gpiod_line* mLine;
    int mLineFlags;

    bool mPinState;
    bool mOutput;

    int requestLineType(int aRequestType);

    bool stateChanged(int aPollFlags);

  public:

    /// Create general purpose I/O pin
    /// @param aChipNo gpio chip index (corresponds with /dev/gpiochipN)
    /// @param aPinName gpiod name or number of the gpiochip line
    /// @param aOutput use as output
    /// @param aInitialState initial state assumed for inputs and enforced for outputs
    /// @param aPull yes = pull up, no = pull down, undefined = no pull
    GpiodPin(int aChipNo, const char* aPinName, bool aOutput, bool aInitialState, Tristate aPull=undefined);
    virtual ~GpiodPin();

    /// get state of GPIO
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    virtual bool getState() P44_OVERRIDE;

    /// set state of output (NOP for inputs)
    /// @param aState new state to set output to
    virtual void setState(bool aState) P44_OVERRIDE;

    /// install state change detector
    /// @param aInputChangedCB will be called when the input state changes. Passing NULL disables input state change reporting.
    /// @param aInverted if set, the state will be reported inverted to what getState() would report. This is a shortcut
    ///   for efficient implementation of higher level classes (which support inverting), to avoid two stage callbacks
    /// @param aInitialState the initial state (of the pin) assumed present when callback is installed
    /// @param aDebounceTime after a reported state change, next input sampling will take place only after specified interval
    /// @param aPollInterval if <0 (Infinite), the state change detector only works if the input pin supports state change
    ///   detection without polling (e.g. with GPIO edge trigger). If aPollInterval is >=0, and the
    ///   input pin does not support edge detection, the state detection will be implemented via polling
    ///   on the current mainloop - if pollInterval==0 then polling will be done in a mainloop idle handler, otherwise in
    ///   a timed handler according to the specified interval
    /// @return true if input supports the type of state change detection requested
    virtual bool setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval) P44_OVERRIDE;

  };

  #endif // GPIOD_VERS==1


  #if GPIOD_VERS==2

  /// Wrapper for General Purpose I/O pin as accessed via
  /// modern Linux Gpiod API (libgpiod v2)
  class GpiodPin : public IOPin
  {
    typedef IOPin inherited;

    struct gpiod_chip* mChip;
    struct gpiod_line_request* mLineRequest; // request for the line (could be multiple, but we only ever request one)
    unsigned int mOffset; // line offset within chip
    gpiod_line_bias mLineBias;

    struct gpiod_edge_event_buffer* mEventBuffer; // for edge events

    bool mPinState;
    bool mOutput;

    int  requestLine(bool aEdgeDetection);
    void releaseLine();
    bool stateChanged(int aPollFlags);

  public:

    /// Create general purpose I/O pin
    /// @param aChipNo gpio chip index (corresponds with /dev/gpiochipN)
    /// @param aPinName gpiod name or number of the gpiochip line
    /// @param aOutput use as output
    /// @param aInitialState initial state assumed for inputs and enforced for outputs
    /// @param aPull yes = pull up, no = pull down, undefined = no pull
    GpiodPin(int aChipNo, const char* aPinName, bool aOutput, bool aInitialState, Tristate aPull=undefined);
    virtual ~GpiodPin();

    /// get state of GPIO
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    virtual bool getState() P44_OVERRIDE;

    /// set state of output (NOP for inputs)
    /// @param aState new state to set output to
    virtual void setState(bool aState) P44_OVERRIDE;

    /// install state change detector
    /// @param aInputChangedCB will be called when the input state changes. Passing NULL disables input state change reporting.
    /// @param aInverted if set, the state will be reported inverted to what getState() would report. This is a shortcut
    ///   for efficient implementation of higher level classes (which support inverting), to avoid two stage callbacks
    /// @param aInitialState the initial state (of the pin) assumed present when callback is installed
    /// @param aDebounceTime after a reported state change, next input sampling will take place only after specified interval
    /// @param aPollInterval if <0 (Infinite), the state change detector only works if the input pin supports state change
    ///   detection without polling (e.g. with GPIO edge trigger). If aPollInterval is >=0, and the
    ///   input pin does not support edge detection, the state detection will be implemented via polling
    ///   on the current mainloop - if pollInterval==0 then polling will be done in a mainloop idle handler, otherwise in
    ///   a timed handler according to the specified interval
    /// @return true if input supports the type of state change detection requested
    virtual bool setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval) P44_OVERRIDE;

  };

  #endif // GPIOD_VERS==2


  #if P44_BUILD_DIGI
  /// Wrapper for General Purpose I/O pin as accessed via NS9XXX kernel module
  /// and SysFS from Userland (Digi ME 9210 LX)
  class GpioNS9XXXPin : public IOPin
  {
    typedef IOPin inherited;

    int mGpioFD;
    bool mPinState;
    bool mOutput;
    string mName;

  public:

    /// Create general purpose I/O pin
    /// @param aGpioName name of the GPIO (files found in GPIO_DEVICES_BASEPATH)
    /// @param aOutput use as output
    /// @param aInitialState initial state assumed for inputs and enforced for outputs
    GpioNS9XXXPin(const char* aGpioName, bool aOutput, bool aInitialState);
    virtual ~GpioNS9XXXPin();

    /// get state of GPIO
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    virtual bool getState();

    /// set state of output (NOP for inputs)
    /// @param aState new state to set output to
    virtual void setState(bool aState);

  };
  #endif // P44_BUILD_DIGI

} // namespace p44

#endif /* defined(__p44utils__gpio__) */
