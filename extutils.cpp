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

#include "extutils.hpp"

#include <string.h>
#include <stdio.h>
#include <sys/types.h> // for ssize_t, size_t etc.

using namespace p44;

#ifndef ESP_PLATFORM

ErrorPtr p44::string_fromfile(const string aFilePath, string &aData)
{
  ErrorPtr err;
  FILE* f = fopen(aFilePath.c_str(), "r");
  if (f==NULL) {
    err = SysError::errNo();
  }
  else {
    if (!string_fgetfile(f, aData)) {
      err = SysError::errNo();
    }
    fclose(f);
  }
  return err;
}


ErrorPtr p44::string_tofile(const string aFilePath, const string &aData)
{
  ErrorPtr err;
  FILE* f = fopen(aFilePath.c_str(), "w");
  if (f==NULL) {
    err = SysError::errNo();
  }
  else {
    if (fwrite(aData.c_str(), aData.size(), 1, f)<1) {
      err = SysError::errNo();
    }
    fclose(f);
  }
  return err;
}

#endif // !ESP_PLATFORM


// MARK: - WindowEvaluator


WindowEvaluator::WindowEvaluator(MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime, EvaluationType aEvalType) :
  windowTime(aWindowTime),
  dataPointCollTime(aDataPointCollTime),
  evalType(aEvalType)
{
}




void WindowEvaluator::addValue(double aValue, MLMicroSeconds aTimeStamp)
{
  if (aTimeStamp==Never) aTimeStamp = MainLoop::now();
  // clean away outdated datapoints
  while (!dataPoints.empty()) {
    if (dataPoints.front().timestamp<aTimeStamp-windowTime) {
      // this one is outdated (lies more than windowTime in the past), remove it
      dataPoints.pop_front();
    }
    else {
      break;
    }
  }
  // add new value
  if (!dataPoints.empty()) {
    // check if we should collect into last existing datapoint
    DataPoint &last = dataPoints.back();
    if (collStart+dataPointCollTime>aTimeStamp) {
      // still in collection time window (from start of datapoint collection
      switch (evalType) {
        case eval_max: {
          if (aValue>last.value) last.value = aValue;
          break;
        }
        case eval_min: {
          if (aValue<last.value) last.value = aValue;
          break;
        }
        case eval_timeweighted_average: {
          MLMicroSeconds timeWeight = aTimeStamp-last.timestamp; // between last subdatapoint collected into this datapoint and new timestamp
          if (collDivisor<=0 || timeWeight<=0) { // 0 or negative timeweight should not happen, safety only!
            // first section
            last.value = (last.value + aValue)/2;
            collDivisor = timeWeight;
          }
          else {
            double v = (last.value*collDivisor + aValue*timeWeight);
            collDivisor += timeWeight;
            last.value = v/collDivisor;
          }
          break;
        }
        case eval_average:
        default: {
          if (collDivisor<=0) collDivisor = 1;
          double v = (last.value*collDivisor+aValue);
          collDivisor++;
          last.value = v/collDivisor;
          break;
        }
      }
      last.timestamp = aTimeStamp; // timestamp represents most recent sample in datapoint
      return; // done
    }
  }
  // accumulation of value in previous datapoint complete (or none available at all)
  // -> start new datapoint
  DataPoint dp;
  dp.value = aValue;
  dp.timestamp = aTimeStamp;
  dataPoints.push_back(dp);
  collStart = aTimeStamp;
  collDivisor = 0;
}


double WindowEvaluator::evaluate()
{
  double result = 0;
  double divisor = 0;
  int count = 0;
  MLMicroSeconds lastTs = Never;
  for (DataPointsList::iterator pos = dataPoints.begin(); pos != dataPoints.end(); ++pos) {
    switch (evalType) {
      case eval_max: {
        if (count==0 || pos->value>result) result = pos->value;
        divisor = 1;
        break;
      }
      case eval_min: {
        if (count==0 || pos->value<result) result = pos->value;
        divisor = 1;
        break;
      }
      case eval_timeweighted_average: {
        if (count==0) {
          // the first datapoint's time weight reaches back to beginning of window
          lastTs = dataPoints.back().timestamp-windowTime;
        }
        MLMicroSeconds timeWeight = pos->timestamp-lastTs;
        result += pos->value*timeWeight;
        divisor += timeWeight;
        // next datapoint's time weight will reach back to this datapoint's time
        lastTs = pos->timestamp;
        break;
      }
      case eval_average:
      default: {
        result += pos->value;
        divisor++;
        break;
      }
    }
    count++;
  }
  return divisor ? result/divisor : 0;
}
