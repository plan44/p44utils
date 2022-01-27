//
//  Copyright (c) 2014-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__analogio__
#define __p44utils__analogio__

#include "p44utils_common.hpp"

#ifndef ENABLE_ANALOGIO_COLOR_SUPPORT
  #define ENABLE_ANALOGIO_COLOR_SUPPORT 1
#endif
#if ENABLE_ANALOGIO_COLOR_SUPPORT
  #include "colorutils.hpp"
#endif

#if ENABLE_P44SCRIPT && !defined(ENABLE_ANALOGIO_SCRIPT_FUNCS)
  #define ENABLE_ANALOGIO_SCRIPT_FUNCS 1
#endif

#if ENABLE_ANALOGIO_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif



#include "iopin.hpp"
#include "valueanimator.hpp"
#include "extutils.hpp"

using namespace std;

namespace p44 {

  /// Generic analog I/O
  class AnalogIo :
    public P44Obj
    #if ENABLE_ANALOGIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
    , public P44Script::EventSource
    #endif
  {
    AnalogIOPinPtr mIoPin; ///< the actual hardware interface to the pin

    string mPinSpec;
    bool mOutput;
    double mLastValue;
    MLTicket mAutoPollTicket;
    WindowEvaluatorPtr mWindowEvaluator;
    bool mUpdating;

  public:
    /// Create general purpose analog I/O, such as PWM or D/A output, or A/D input
    /// @param aPinSpec specification of the IO; form is usually [busX.device.]pin
    /// @param aOutput use as output
    /// @param aInitialValue initial value (to set for output, to expect without triggering change for input)
    /// @note possible pin types are
    /// - "missing" : dummy (non-connected) pin
    /// - "pwmchipN.channelNo.pwmPeriod" : numbered Linux/ESP32 PWM output on channelNo of chip/gpio N with overall period (in nS) of pwmPeriod
    /// - "i2cN.DEVICE[-options]@i2caddr.pinNumber" : numbered pin of DEVICE at i2caddr on i2c bus N
    ///     (DEVICE is name of chip, such as PCA9685)
    ///     (no bus-level options for i2c, options are device specific, such as I and O in PCA9685 for inverted and opendrain operation)
    /// - "spiXY.DEVICE[-options]@spiaddr.pinNumber" : numbered pin of DEVICE at spiaddr on spidevX.Y
    ///     (DEVICE is name of chip, such as MCP3008. It can also be "generic" to directly access the bus via getBus())
    ///     possible generic SPI options are (devices might have additional specific ones)
    ///     - H: use inverted phase (compared to original microwire SPI)
    ///     - P: use inverted polarity (compared to original microwire SPI)
    ///     - C: chip select high
    ///     - N: no chip select
    ///     - 3: 3 wire
    ///     - R: SPI ready, slave pulls low to pause
    ///     - S: slow speed (1/10 of bus' normal speed)
    ///     - s: very slow speed (1/100 of bus' normal speed)
    AnalogIo(const char* aPinSpec, bool aOutput, double aInitialValue);
    virtual ~AnalogIo();

    /// get name
    string getName() { return mPinSpec.c_str(); };

    /// check for output
    bool isOutput() { return mOutput; };

    /// get state of analog input
    /// @return current raw value (from actual pin for inputs, from last set value for outputs)
    /// @note if processing is enabled, calling value() adds a sample to the processing
    double value();

    /// @return most recently sampled raw value, without actually triggering a sample
    /// @note initially, returns the initial value set at creation
    double lastValue();

    /// @return processed value (is same as lastValue() when no averaging is set with setFilter())
    /// @note when autopoll is enabled, this will not actually read a new value from hardware, but rely on autopoll to
    ///    updated the value processor. If autopoll is not active, a new sample will be taken before returning the processed value
    double processedValue();

    /// set state of output (NOP for inputs)
    /// @param aValue new state to set output to
    void setValue(double aValue);

    /// get range and resolution of this input
    /// @param aMin minimum value
    /// @param aMax maximum value
    /// @param aResolution resolution (LSBit value)
    /// @return false if no range information is available (arguments are not touched then)
    bool getRange(double &aMin, double &aMax, double &aResolution);

    /// get value setter for animations
    ValueSetterCB getValueSetter(double& aCurrentValue);

    /// get animator
    ValueAnimatorPtr animator();

    /// setup automatic polling
    /// @param aPollInterval if set to <=0, polling will stop
    /// @param aTolerance timing tolerance
    /// @note every poll cycle generates an event in the EventSource
    void setAutopoll(MLMicroSeconds aPollInterval, MLMicroSeconds aTolerance = 0);

    /// setup value filtering
    /// @param aEvalType the type of filtering to perform
    /// @param aWindowTime width (timespan) of evaluation window
    /// @param aDataPointCollTime within that timespan, new values reported will be collected into a single datapoint
    void setFilter(WinEvalMode aEvalType, MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime);

    #if ENABLE_ANALOGIO_SCRIPT_FUNCS && ENABLE_P44SCRIPT
    /// get a analog input value object. This is also what is sent to event sinks
    P44Script::ScriptObjPtr getValueObj();
    #endif

  private:

    void pollhandler(MLMicroSeconds aPollInterval, MLMicroSeconds aTolerance, MLTimer &aTimer);

  };
  typedef boost::intrusive_ptr<AnalogIo> AnalogIoPtr;


  #if ENABLE_ANALOGIO_COLOR_SUPPORT

  /// Analog color output (RGB, RGBW, RGBWA)
  class AnalogColorOutput : public P44Obj
  {
    AnalogIoPtr mRGBWAOutputs[5]; ///< actual ouput channels
    int mMaxMilliWatts; ///< max milliwatts allowed
    int mRequestedMilliWatts; ///< currently requested milliwatts

    Row3 mHSV; ///< current HSV values
    Row3 mRGB; ///< current RGB values

  public:
    Row3 whiteRGB; ///< R,G,B relative intensities that can be replaced by a extra (cold)white channel
    Row3 amberRGB; ///< R,G,B relative intensities that can be replaced by a extra amber (warm white) channel
    int mOutputMilliWatts[5]; ///< milliwatts per channel @ 100%

    AnalogColorOutput(AnalogIoPtr aRedOutput, AnalogIoPtr aGreenOutput, AnalogIoPtr aBlueOutput, AnalogIoPtr aWhiteOutput = NULL, AnalogIoPtr aAmberOutput = NULL);

    /// set color as HSV
    /// @param aHSV color in HSV (hue: 0..360, saturation: 0..1, brightness 0..1)
    void setHSV(const Row3& aHSV);

    /// set color as RGB
    /// @param aRGB color in RGB (all channels 0..1)
    void setRGB(const Row3& aRGB);

    /// set color
    /// @param aHue hue (0..360)
    /// @param aSaturation saturation (0..1)
    void setColor(double aHue, double aSaturation);

    /// set brightness
    /// @param aBrightness brightness (0..1)
    void setBrightness(double aBrightness);

    /// limit total power, dim LED chain output accordingly
    /// @param aMilliWatts how many milliwatts (approximatively) the total RGB(WA) light may use, 0=no limit
    void setPowerLimit(int aMilliWatts);

    /// get current power limit
    /// @return currently set power limit in milliwatts, 0=no limit
    int getPowerLimit();

    /// Return the power it *would* need to display the current state (altough power limiting might actually reducing it)
    /// @return how many milliwatts (approximatively) the color light would use if not limited
    int getNeededPower();

    /// Return the current power (possibly limited)
    /// @return how many milliwatts (approximatively) the color light currently consumes
    int getCurrentPower();

    /// get value setter for animations
    /// @param aComponent name of the color component: "r", "g", "b", "hue", "saturation", "brightness"
    ValueSetterCB getColorComponentSetter(const string aComponent, double& aCurrentValue);

    /// get animator for a component
    /// @param aComponent name of the color component: "r", "g", "b", "hue", "saturation", "brightness"
    ValueAnimatorPtr animatorFor(const string aComponent);

  private:

    void outputHSV();
    void outputRGB();
    ValueSetterCB getHsvComponentSetter(double &aColorComponent, double &aCurrentValue);
    void hsvComponentSetter(double* aColorComponentP, double aNewValue);
    ValueSetterCB getRgbComponentSetter(double &aColorComponent, double &aCurrentValue);
    void rgbComponentSetter(double* aColorComponentP, double aNewValue);

  };
  typedef boost::intrusive_ptr<AnalogColorOutput> AnalogColorOutputPtr;

  #endif // ENABLE_ANALOGIO_COLOR_SUPPORT

  #if ENABLE_ANALOGIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

  namespace P44Script {

    class AnalogIoObj;

    /// represents a sampled value from an analog input
    class AnalogInputEventObj : public NumericValue
    {
      typedef NumericValue inherited;
      AnalogIoPtr mAnalogIo;
    public:
      AnalogInputEventObj(AnalogIoPtr aAnalogIo);
      virtual void deactivate() P44_OVERRIDE;
      virtual string getAnnotation() const P44_OVERRIDE;
      virtual TypeInfo getTypeInfo() const P44_OVERRIDE;
      virtual EventSource *eventSource() const P44_OVERRIDE;
    };


    /// represents an analog I/O
    class AnalogIoObj : public StructuredLookupObject
    {
      typedef StructuredLookupObject inherited;
      AnalogIoPtr mAnalogIo;
    public:
      AnalogIoObj(AnalogIoPtr aAnalogIo);
      virtual string getAnnotation() const P44_OVERRIDE { return "analogIO"; };
      AnalogIoPtr analogIo() { return mAnalogIo; }

      /// factory method to get an AnalogIo either by creating it from pinspec
      /// string or by using existing AnalogIoObj passed
      static AnalogIoPtr analogIoFromArg(ScriptObjPtr aArg, bool aOutput, double aInitialValue);
    };


    #if ENABLE_ANALOGIO_COLOR_SUPPORT

    /// represents an analog color light output
    class AnalogColorOutputObj : public StructuredLookupObject
    {
      typedef StructuredLookupObject inherited;
      AnalogColorOutputPtr mColorOutput;
    public:
      AnalogColorOutputObj(AnalogColorOutputPtr aColorOutput);
      virtual string getAnnotation() const P44_OVERRIDE { return "color output"; };
      AnalogColorOutputPtr colorOutput() { return mColorOutput; }
    };

    #endif // ENABLE_ANALOGIO_COLOR_SUPPORT

    /// represents the global objects related to analogio
    class AnalogIoLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      AnalogIoLookup();
    };

  }

  #endif // ENABLE_ANALOGIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

} // namespace p44

#endif /* defined(__p44utils__analogio__) */
