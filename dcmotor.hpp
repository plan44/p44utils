//
//  Copyright (c) 2017-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44ayabd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44ayabd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44ayabd. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __p44wiperd__dcmotordriver__
#define __p44wiperd__dcmotordriver__

#include "p44utils_common.hpp"

#if ENABLE_P44SCRIPT && !defined(ENABLE_DCMOTOR_SCRIPT_FUNCS)
  #define ENABLE_DCMOTOR_SCRIPT_FUNCS 1
#endif

#if ENABLE_DCMOTOR_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif

#include "serialcomm.hpp"
#include "digitalio.hpp"
#include "analogio.hpp"


using namespace std;

namespace p44 {

  class DcMotorDriverError : public Error
  {
  public:
    enum {
      OK,
      overcurrentStop,
      endswitchStop,
      timedStop,
      numErrorCodes
    };
    typedef uint16_t ErrorCodes;

    static const char *domain() { return "DCMotorDriver"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return DcMotorDriverError::domain(); };
    DcMotorDriverError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "overcurrentStop",
      "endswitchStop",
      "timedStop",
    };
    #endif // ENABLE_NAMED_ERRORS
  };

  class DcMotorDriver;
  namespace P44Script { class DcMotorStatusObj; }

  typedef boost::function<void (double aCurrentPower, int aDirection, ErrorPtr aError)> DCMotorStatusCB;

  typedef boost::intrusive_ptr<DcMotorDriver> DcMotorDriverPtr;
  class DcMotorDriver :
    public P44LoggingObj
    #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
    , public P44Script::EventSource
    #endif
  {
    typedef P44Obj inherited;
    #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
    friend class P44Script::DcMotorStatusObj;
    #endif

    AnalogIoPtr mPwmOutput;
    DigitalIoPtr mCWdirectionOutput;
    DigitalIoPtr mCCWdirectionOutput;

    int mCurrentDirection;
    double mCurrentPower;
    ErrorPtr mStopCause;
    DCMotorStatusCB mRampDoneCB; ///< called when ramp is done or motor was stopped by endswitch/overcurrent

    MLTicket mSequenceTicket;

    AnalogIoPtr mCurrentSensor;
    MLMicroSeconds mSampleInterval; ///< current sampling interval
    double mStopCurrent; ///< current limit that will stop motor
    MLMicroSeconds mCurrentLimiterHoldoffTime; ///< delay after applying power until current limiter kicks in (to allow surges at powerup)
    double mMaxStartCurrent; ///< if >0, maximum allowd current during current limiter holdoff time
    MLMicroSeconds mStartMonitoring;
    DCMotorStatusCB mStoppedCB; ///< called when motor was stopped by endswitch/overcurrent

    DigitalIoPtr mPositiveEndInput;
    DigitalIoPtr mNegativeEndInput;

  public:

    /// Create a motor controller
    /// @param aPWMOutput a 0..100 Analog output controlling the PWM signal for the DC motor
    /// @param aCWDirectionOutput a digital output enabling clockwise motor operation.
    ///   If no CCW output is set, this is assumed to just switch the direction (1=CW, 0=CCW)
    ///   If no CW output is set, this is assumed to be a unidirectional motor which is only controlled via the PWM
    /// @param aCCWDirectionOutput a digital output enabling counter clockwise motor operation.
    ///   If this is set, CW and CCW are assumed to each control one of the half bridges,
    ///   so using CCW!=CW will drive the motor, and CCW==CW will brake it
    DcMotorDriver(AnalogIoPtr aPWMOutput, DigitalIoPtr aCWDirectionOutput = NULL, DigitalIoPtr aCCWDirectionOutput = NULL);
    virtual ~DcMotorDriver();

    /// set stop callback
    /// @param aStopCB this will be called whenever the motor stops by itself.
    ///   If no error is passed, the stop is just because a ramp/sequence has completed
    ///   An error indicates a unexpeced stop (end switch or current limit)
    /// @note the stop callback will receive the power and direction values present BEFORE the motor stopped;
    ///   but actual power will be 0 (as we have stopped)
    void setStopCallback(DCMotorStatusCB aStoppedCB);

    /// Enable current monitoring for stopping at mechanical endpoints and/or obstacles (prevent motor overload)
    /// @param aCurrentSensor a analog input sensing the current used by the motor to allow stopping on overcurrent.
    ///    The current limiter will use the processedValue() of the analog input, so averaged current can be used to eliminate spikes
    /// @param aSampleInterval the sample interval for the current
    void setCurrentSensor(AnalogIoPtr aCurrentSensor, MLMicroSeconds aSampleInterval);

    /// @param aStopCurrent sensor value that will stop the motor
    /// @param aHoldOffTime how long to suspend current limiting after beginning a powerup ramp
    /// @param aMaxStartCurrent max current allowed during aHoldOffTime, 0 = no limit
    void setCurrentLimits(double aStopCurrent, MLMicroSeconds aHoldOffTime = 0, double aMaxStartCurrent = 0);

    /// Enable monitoring for end switches
    /// @param aPositiveEnd a digital input which signals motor at the positive end of its movement
    /// @param aNegativeEnd a digital input which signals motor at the negative end of its movement
    /// @param aPollInterval interval for polling the input (only if needed, that is when HW does not have edge detection anyway). 0 = default poll strategy
    void setEndSwitches(DigitalIoPtr aPositiveEnd, DigitalIoPtr aNegativeEnd = NULL, MLMicroSeconds aDebounceTime = 0, MLMicroSeconds aPollInterval = 0);

    /// ramp motor from current power to another power
    /// @param aPower 0..100 new brake or drive power to apply
    /// @param aDirection driving direction: 1 = CW, -1 = CCW, 0 = hold/brake
    /// @param aRampTime number of seconds for running this ramp.
    ///   If negative, this specifies the time for a full scale (0..100 or vice versa) power change, actual time will
    ///   be proportional to power range actually run trough.
    ///   Note that ramping from one aDirection to another will execute two separate ramps in sequence
    /// @param aRampExp ramp exponent (0=linear, + or - = logarithmic bulging up or down)
    /// @param aRampDoneCB will be called at end of ramp
    /// @note aRampDoneCB callback will receive the current power and direction as active at the end of the ramp
    ///    (if the motor runs into a stop condition during ramp, power and direction will be 0)
    void rampToPower(double aPower, int aDirection, double aRampTime = 0, double aRampExp = 0, DCMotorStatusCB aRampDoneCB = NULL);

    /// stop immediately, no braking
    void stop();

    /// stop ramps and sequences, but do not turn off motor
    void stopSequences();

    /// run sequence
    typedef struct {
      double power; ///< power to ramp to, negative = step list terminator
      int direction; ///< new direction
      double rampTime; ///< ramp speed
      double rampExp; ///< ramp exponent (0=linear, + or - = logarithmic bulging up or down)
      double runTime; ///< time to run
    } SequenceStep;

    typedef std::list<SequenceStep> SequenceStepList;

    /// ramp motor from current power to another power
    /// @param aSteps list of sequence steps
    /// @param aSequenceDoneCB will be called at end of ramp
    void runSequence(SequenceStepList aSteps, DCMotorStatusCB aSequenceDoneCB = NULL);

    #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
    /// get a motor status object. This is also what is sent to event sinks
    P44Script::ScriptObjPtr getStatusObj();
    #endif

  private:

    void setPower(double aPower, int aDirection);
    void setDirection(int aDirection);
    void rampStep(double aStartPower, double aTargetPower, int aNumSteps, int aStepNo , double aRampExp);
    void sequenceStepDone(SequenceStepList aSteps, DCMotorStatusCB aSequenceDoneCB, ErrorPtr aError);
    void checkCurrent();
    void endSwitch(bool aPositiveEnd, bool aNewState);
    void autoStopped(double aPower, int aDirection, ErrorPtr aError);
    void motorStatusUpdate(ErrorPtr aStopCause);

  };

  #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

  namespace P44Script {

    class DcMotorObj;

    /// represents a DC motor state
    class DcMotorStatusObj : public JsonValue
    {
      typedef JsonValue inherited;
      DcMotorDriverPtr mDcMotorDriver;
    public:
      DcMotorStatusObj(DcMotorDriverPtr aDcMotorDriver);
      virtual void deactivate() P44_OVERRIDE;
      virtual string getAnnotation() const P44_OVERRIDE;
      virtual TypeInfo getTypeInfo() const P44_OVERRIDE;
      virtual EventSource *eventSource() const P44_OVERRIDE;
      virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    };

    /// represents a DC motor
    /// @note is an event source, but does not expose it directly, only via DcMotorEventObjs
    class DcMotorObj : public StructuredLookupObject, public EventSource
    {
      typedef StructuredLookupObject inherited;
      DcMotorDriverPtr mDCMotor;
    public:
      DcMotorObj(DcMotorDriverPtr aDCMotor);
      virtual string getAnnotation() const P44_OVERRIDE { return "DC motor"; };
      DcMotorDriverPtr dcMotor() { return mDCMotor; }
    };


    /// represents the global objects related to DC motors
    class DcMotorLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      DcMotorLookup();
    };

  }

  #endif // ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT


} // namespace p44

#endif /* defined(__p44wiperd__dcmotordriver__) */
