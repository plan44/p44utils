//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__extutils__
#define __p44utils__extutils__

#include "p44utils_common.hpp"

using namespace std;

/// Extended utilities that have dependencies on other p44utils classes (such as p44::Error)
/// @note utilities that DO NOT depends on other p44utils classes are in "utils"

namespace p44 {

  #ifndef ESP_PLATFORM

  /// reads string from file
  /// @param aFilePath the path of the file to read
  /// @param aData the string to store the contents of the file to
  /// @return ok or error
  ErrorPtr string_fromfile(const string aFilePath, string &aData);

  /// saves string to file
  /// @param aFilePath the path of the file to write
  /// @param aData the string to store in the file
  /// @return ok or error
  ErrorPtr string_tofile(const string aFilePath, const string &aData);

  #endif


  typedef enum {
    eval_none, ///< no evaluation, disabled
    eval_average, ///< average over data points added within window time
    eval_timeweighted_average, ///< average over data points, but weighting them by the time passed since last data point (assuming datapoints are averages over past time anyway)
    eval_max, ///< maximum within the window time
    eval_min ///< minimum within the window time
  } EvaluationType;

  // Sliding window data evaluator.
  // Features:
  // - allows irregular time intervals between data points
  // - can aggregate multiple samples into one datapoint for the sliding window
  class WindowEvaluator : public P44Obj
  {
    typedef struct {
      double value; ///< value of the datapoint (might be updated while accumulating)
      MLMicroSeconds timestamp; ///< time when datapoint's value became final (when accumulating average, this is the time of the last added sub-datapoint)
    } DataPoint;

    typedef std::list<DataPoint> DataPointsList;

    // state
    DataPointsList dataPoints;
    MLMicroSeconds collStart; ///< start of current datapoint collection
    double collDivisor; ///< divisor for collection of current datapoint

  public:

    // settings
    MLMicroSeconds windowTime;
    MLMicroSeconds dataPointCollTime;
    EvaluationType evalType;

    /// create a sliding window evaluator
    /// @param aWindowTime width (timespan) of evaluation window
    /// @param aDataPointCollTime within that timespan, new values reported will be collected into a single datapoint
    /// @param aEvalType the type of evaluation to perform
    WindowEvaluator(MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime, EvaluationType aEvalType);

    /// Add a new value to the evaluator.
    /// @param aValue the value to add
    /// @param aTimeStamp the timestamp, must be increasing for every call, default==Never==now
    void addValue(double aValue, MLMicroSeconds aTimeStamp = Never);

    /// Get the current evaluation result
    /// @note will return 0 when no datapoints are accumulated at all
    double evaluate();

  };
  typedef boost::intrusive_ptr<WindowEvaluator> WindowEvaluatorPtr;

} // namespace p44

#endif /* defined(__p44utils__extutils__) */
