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

#ifndef __p44utils__digitalio__
#define __p44utils__digitalio__

#include "p44utils_common.hpp"

#include "iopin.hpp"

using namespace std;

namespace p44 {

  /// Generic digital I/O
  class DigitalIo : public P44Obj
  {
    IOPinPtr ioPin; ///< the actual hardware interface to the pin

    string pinSpec;
    bool output;
    bool inverted;
    Tristate pull; // yes = pull up, no = pull down, undefined = no pull

  public:
    /// Create general purpose I/O
    /// @param aPinSpec specification of the IO; form is [+][-][/][bus.device.]pin,
    ///   where optional leading plus '+'/'-' enable pullup/down (if device supports it)
    ///   optional slash '/' inverts the polarity, and where bus & device can be omitted for normal GPIOs.
    /// @param aOutput use as output
    /// @param aInitialState initial state (to set for output, to expect without triggering change for input)
    ///   Note: aInitialState is logic state (pin state is inverse if aPinSpec is slash-prefixed)
    /// @note possible pin specifications are
    ///   "missing" : dummy (non-connected) pin
    ///   "gpio.N" or just "N": standard Linux or ESP32 GPIO number N
    ///   "led.N": standard Linux LED number N
    ///   "gpioNS9XXXX.NAME" : DigiESP Linux GPIO named NAME
    ///   "i2cN.DEVICE@i2caddr.pinNumber" : numbered pin of DEVICE at i2caddr on i2c bus N
    ///     (DEVICE is name of chip, such as PCF8574 or TCA9555)
    ///   "spiXY.DEVICE[-options]@spiaddr.pinNumber" : numbered pin of DEVICE at spiaddr on spidevX.Y
    ///     (DEVICE is name of chip, such as MCP23S17)
    ///     possible options are:
    ///     H: use inverted phase (compared to original microwire SPI)
    ///     P: use inverted polarity (compared to original microwire SPI)
    ///     C: chip select high
    ///     N: no chip select
    ///     3: 3 wire
    ///     R: SPI ready, slave pulls low to pause
    ///     S: slow speed (1/10 of bus' normal speed)
    ///     s: very slow speed (1/100 of bus' normal speed)
    DigitalIo(const char* aPinSpec, bool aOutput, bool aInitialState = false);
    virtual ~DigitalIo();

    /// get name
    string getName();

    /// check for output
    bool isOutput() { return output; };

    /// get state of GPIO
    /// @return current state (from actual GPIO pin for inputs, from last set state for outputs)
    bool isSet();

    /// set state of output (NOP for inputs)
    /// @param aState new state to set output to
    void set(bool aState);

    /// set state to true
    void on();

    /// set state to false
    void off();

    /// toggle state of output and return new state
    /// @return new state of output after toggling (for inputs, just returns state like isSet() does)
    bool toggle();

    /// install state change detector
    /// @param aInputChangedCB will be called when the input state changes. Passing NULL disables input state change reporting.
    /// @param aDebounceTime after a reported state change, next input sampling will take place only after specified interval
    /// @param aPollInterval if <0 (Infinite), the state change detector only works if the input pin supports state change
    ///   detection without polling (e.g. with GPIO edge trigger). If aPollInterval is >=0, and the
    ///   input pin does not support edge detection, the state detection will be implemented via polling
    ///   on the current mainloop - if pollInterval==0 then polling will be done in a mainloop idle handler, otherwise in
    ///   a timed handler according to the specified interval
    /// @return true if input supports the type of state change detection requested
    virtual bool setInputChangedHandler(InputChangedCB aInputChangedCB, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval);


  };
  typedef boost::intrusive_ptr<DigitalIo> DigitalIoPtr;



  /// GPIO used as pushbutton
  class ButtonInput : public DigitalIo
  {
    typedef DigitalIo inherited;

  public:
    /// button event handler
    /// @param aState the current state of the button (relevant when handler was installed with aPressAndRelease set)
    /// @param aHasChanged set when reporting a state change, cleared when reporting the same state again (when repeatActiveReport set)
    /// @param aTimeSincePreviousChange time passed since previous button state change (to easily detect long press actions etc.)
    typedef boost::function<void (bool aState, bool aHasChanged, MLMicroSeconds aTimeSincePreviousChange)> ButtonHandlerCB;

  private:
    MLMicroSeconds lastChangeTime;
    bool reportPressAndRelease;
    ButtonHandlerCB buttonHandler;
    MLMicroSeconds repeatActiveReport;
    MLTicket activeReportTicket;

    void inputChanged(bool aNewState);
    void repeatStateReport();


  public:
    /// Create pushbutton
    /// @param aPinSpec specification of the pin where the pushbutton is connected (can be prefixed with slash to invert, plus to enable pullup)
    ButtonInput(const char* aPinSpec);

    /// destructor
    virtual ~ButtonInput();


    /// set handler to be called on pushbutton events
    /// @param aButtonHandler handler for pushbutton events
    /// @param aPressAndRelease if set, both pressing and releasing button generates event.
    ///   Otherwise, only one event is issued per button press (on button release)
    /// @param aRepeatActiveReport time after which a still pressed button is reported again (to detect long presses without extra timers)
    void setButtonHandler(ButtonHandlerCB aButtonHandler, bool aPressAndRelease, MLMicroSeconds aRepeatActiveReport=p44::Never);

  };
  typedef boost::intrusive_ptr<ButtonInput> ButtonInputPtr;



  /// GPIO used for indicator (e.g. LED)
  class IndicatorOutput : public DigitalIo
  {
    typedef DigitalIo inherited;

    bool nextTimedState; // what state should be set next time the timer triggers
    MLMicroSeconds blinkOnTime;
    MLMicroSeconds blinkOffTime;
    MLMicroSeconds blinkUntilTime;

    MLTicket timedOpTicket;

    void timer(MLTimer &aTimer);

  public:
    /// Create indicator output
    /// @param aPinSpec specification of the pin where the pushbutton is connected (can be prefixed with slash to invert)
    /// @param aInitiallyOn initial state (on or off) of the indicator
    IndicatorOutput(const char* aPinSpec, bool aInitiallyOn = false);

    /// destructor
    virtual ~IndicatorOutput();

    /// activate the output for a certain time period, then switch off again
    /// @param aOnTime how long indicator should stay active
    void onFor(MLMicroSeconds aOnTime);

    /// blink indicator for a certain time period, with a given blink period and on ratio
    /// @param aOnTime how long indicator should stay active, or p44::infinite to keep blinking
    /// @param aBlinkPeriod how fast the blinking should be
    /// @param aOnRatioPercent how many percents of aBlinkPeriod the indicator should be on
    void blinkFor(MLMicroSeconds aOnTime, MLMicroSeconds aBlinkPeriod = 600*MilliSecond, int aOnRatioPercent = 50);

    /// stop blinking/timed activation immediately, but do not change state of the indicator
    void stop();

    /// stop blinking/timed activation immediately and turn indicator off
    void steady(bool aState);

    /// stop blinking/timed activation immediately and turn indicator off
    void steadyOff();

    /// stop blinking/timed activation immediately and turn indicator on
    void steadyOn();


  };
  typedef boost::intrusive_ptr<IndicatorOutput> IndicatorOutputPtr;



} // namespace p44

#endif /* defined(__p44utils__digitalio__) */
