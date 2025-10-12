//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2020-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include <sys/stat.h> // for mkdir
#include <math.h> // for fabs

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


/// make sure directory exists, otherwise make it (like mkdir -p)
/// @param aDirPath path for directory to create
ErrorPtr p44::ensureDirExists(const string aDirPath, int aMaxDepth, mode_t aCreationMode)
{
  int ret = access(aDirPath.c_str(), F_OK);
  if (ret==0) return ErrorPtr(); // exists -> fine
  if (aMaxDepth==0) {
    // cannot create more directories -> not found
    return SysError::err(ENOENT);
  }
  // does not exist
  size_t n = aDirPath.find_last_of('/');
  if (n!=string::npos && n!=0) { // slash at beginning does not count as dir
    // - there is a parent
    string parent = aDirPath.substr(0,n);
    if (aDirPath.substr(n)=="." || aDirPath.substr(n)=="..") return SysError::err(ENOENT); // do not mess with "." and ".."!
    ErrorPtr err = ensureDirExists(parent, aMaxDepth<0 ? aMaxDepth : aMaxDepth-1);
    if (Error::notOK(err)) return err; // abort
  }
  // does not yet exist, create now
  return SysError::err(mkdir(aDirPath.c_str(), aCreationMode));
}



// MARK: - WindowEvaluator


WindowEvaluator::WindowEvaluator(MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime, WinEvalMode aEvalMode) :
  mWindowTime(aWindowTime),
  mDataPointCollTime(aDataPointCollTime),
  mWinEvalMode(aEvalMode)
{
}




void WindowEvaluator::addValue(double aValue, MLMicroSeconds aTimeStamp)
{
  if (aTimeStamp==Never) aTimeStamp = MainLoop::now();
  // process options
  if (mWinEvalMode & eval_option_abs) {
    aValue = fabs(aValue);
  }
  // clean away outdated datapoints
  while (!mDataPoints.empty()) {
    if (mDataPoints.front().timestamp<aTimeStamp-mWindowTime) {
      // this one is outdated (lies more than windowTime in the past), remove it
      mDataPoints.pop_front();
    }
    else {
      break;
    }
  }
  // add new value
  if (!mDataPoints.empty()) {
    // check if we should collect into last existing datapoint
    DataPoint &last = mDataPoints.back();
    if (mCollStart+mDataPointCollTime>aTimeStamp) {
      // still in collection time window (from start of datapoint collection
      switch (mWinEvalMode & eval_type_mask) {
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
          if (mCollDivisor<=0 || timeWeight<=0) { // 0 or negative timeweight should not happen, safety only!
            // first section
            last.value = (last.value + aValue)/2;
            mCollDivisor = timeWeight;
          }
          else {
            double v = (last.value*mCollDivisor + aValue*timeWeight);
            mCollDivisor += timeWeight;
            last.value = v/mCollDivisor;
          }
          break;
        }
        case eval_average:
        default: {
          if (mCollDivisor<=0) mCollDivisor = 1;
          double v = (last.value*mCollDivisor+aValue);
          mCollDivisor++;
          last.value = v/mCollDivisor;
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
  mDataPoints.push_back(dp);
  mCollStart = aTimeStamp;
  mCollDivisor = 0;
}


bool WindowEvaluator::hasData()
{
  return !mDataPoints.empty();
}


MLMicroSeconds WindowEvaluator::latest()
{
  if (!hasData()) return Never;
  return mDataPoints.back().timestamp;
}


double WindowEvaluator::evaluate(bool aPerNow)
{
  double result = 0;
  double divisor = 0;
  int count = 0;
  MLMicroSeconds lastTs = Never;
  if (aPerNow && !mDataPoints.empty()) {
    // re-add latest datapoint right now, so result will be per now (vs per last added datapoint)
    addValue(mDataPoints.back().value);
  }
  for (DataPointsList::iterator pos = mDataPoints.begin(); pos != mDataPoints.end(); ++pos) {
    switch (mWinEvalMode & eval_type_mask) {
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
          lastTs = mDataPoints.back().timestamp-mWindowTime;
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
  return divisor!=0 ? result/divisor : 0;
}
