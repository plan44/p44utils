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

#include "valueunits.hpp"

#include "utils.hpp"
#include <string.h>

using namespace p44;

typedef struct {
  const char *name;
  const char *symbol;
} ValueUnitDescriptor;
static const ValueUnitDescriptor valueUnitNames[numValueUnits] = {
  { "unknown", "?" },
  { "none", "" },
  { "percent", "%" },
  { "ppm", "ppm" },
  // basic SI units
  { "meter", "m" },
  { "gram", "g" },
  { "second", "S" },
  { "ampere", "A" },
  { "kelvin", "K" },
  { "mole", "mol" },
  { "candle", "cd" },
  { "bequerel", "Bq" },
  // derived units
  { "watt", "W" },
  { "voltampere", "VA" },
  { "celsius", "°C" },
  { "volt", "V" },
  { "lux", "lx" },
  { "liter", "l" },
  { "joule", "J" },
  { "pascal", "Pa" },
  { "degree", "°" },
  { "bel", "B" },
  { "Hertz", "Hz" },
  // combined units
  { "molpercubicmeter", "mol/m3" },
  { "frenchwaterhardness", "°fH" },
  { "bequerelpercubicmeter", "Bq/m3" },
  { "grampercubicmeter", "Bq/m3" },
  { "meterpersecond", "m/s" },
  { "mperm2", "m/m2" },
  { "literpersecond", "l/s" },
  // non-SI scaled units
  { "minute", "min" },
  { "hour", "h" },
  { "day", "d" },
  { "month", "mt" },
  { "year", "yr" },
  { "watthour", "Wh" },
  { "literperminute", "l/min" },
  { "mired", "mired" },
};
typedef struct {
  const char *name;
  const char *symbol;
  int8_t exponent;
} ValueScalingDescriptor;
static const ValueScalingDescriptor valueScalingNames[numUnitScalings] = {
  { "yotta", "Y", 24 },
  { "zetta", "Z", 21 },
  { "exa", "E", 18 },
  { "peta", "P", 15 },
  { "tera", "T", 12 },
  { "giga", "G", 9 },
  { "mega", "M", 6 },
  { "kilo", "k", 3 },
  { "hecto", "h", 2 },
  { "deca", "da", 1 },
  { "", "", 0 },
  { "deci", "d", -1 },
  { "centi", "c", -2 },
  { "milli", "m", -3 },
  { "micro", "µ", -6 },
  { "nano", "n", -9 },
  { "pico", "p", -12 },
  { "femto", "f", -15 },
  { "atto", "a", -18 },
  { "zepto", "z", -21 },
  { "yocto", "y", -24 }
};


string p44::valueUnitName(ValueUnit aValueUnit, bool aAsSymbol)
{
  ValueBaseUnit u = VALUE_UNIT_ONLY(aValueUnit);
  if (u>=numValueUnits) u=valueUnit_none;
  UnitScale s = VALUE_SCALING_ONLY(aValueUnit);
  if (s>numUnitScalings) s=unitScaling_1;
  return string_format("%s%s",
    aAsSymbol ?  valueScalingNames[s].symbol : valueScalingNames[s].name,
    aAsSymbol ? valueUnitNames[u].symbol : valueUnitNames[u].name
  );
}


ValueUnit p44::stringToValueUnit(const string aValueUnitName)
{
  // check for prefix
  UnitScale s = unitScaling_1;
  size_t n = 0;
  for (int i=0; i<numUnitScalings; i++) {
    n = strlen(valueScalingNames[i].name);
    if (n>0 && aValueUnitName.substr(0,n)==valueScalingNames[i].name) {
      s = (UnitScale)i;
      break;
    }
    n = 0;
  }
  // s = scale
  // n = size of prefix
  // now determine unit
  for (int i=0; i<numValueUnits; i++) {
    if (aValueUnitName.substr(n)==valueUnitNames[i].name) {
      return VALUE_UNIT(i, s);
    }
  }
  return unit_unknown;
}


typedef struct {
  ValueBaseUnit unit;
  double factor;
} DurationUnitDescriptor;
const int numDurationUnits = 6;
static const DurationUnitDescriptor durationUnitDescriptors[numDurationUnits] = {
  { valueUnit_second, 1 },
  { valueUnit_minute, 60 },
  { valueUnit_hour, 3600 },
  { valueUnit_day, 86400 },
  { valueUnit_month, 2628000 }, // approx, 1/12 year
  { valueUnit_year, 365*86400 }, // year
};


string p44::format_duration(double aSeconds, int aComponents, bool aAsSymbol)
{
  string t;
  format_duration_append(t, aSeconds, aComponents, aAsSymbol);
  return t;
}


void p44::format_duration_append(string &aString, double aSeconds, int aComponents, bool aAsSymbol)
{
  // find largest unit to use
  int uidx = numDurationUnits-1;
  if (aComponents<0) {
    // limit higher order components, not precision
    aComponents = -aComponents;
    if (uidx>aComponents-1) uidx = aComponents-1;
  }
  while (uidx>0) {
    if (aSeconds>=durationUnitDescriptors[uidx].factor)
      break; // this is the largest unit we need
    uidx--;
  }
  // render
  bool first = true;
  bool expandedbefore = false;
  while (aComponents>0 && uidx>=0) {
    const DurationUnitDescriptor &d = durationUnitDescriptors[uidx];
    long v = (aSeconds/d.factor);
    if (v>0 || (first&&uidx==0)) {
      if (aAsSymbol && d.unit==valueUnit_minute) {
        if (expandedbefore) aString+=" ";
        string_format_append(aString, "%ld'", v);
      }
      else if (aAsSymbol && d.unit==valueUnit_second) {
        if (expandedbefore) aString+=" ";
        string_format_append(aString, "%ld\"", v);
      }
      else {
        if (!first) aString+=" ";
        string_format_append(aString, "%ld%s%s",
          v,
          aAsSymbol ? "" : " ",
          valueUnitName(VALUE_UNIT(d.unit, unitScaling_1), aAsSymbol).c_str()
        );
        expandedbefore = true;
      }
      aSeconds -= v*d.factor;
      if (aSeconds<=0) break; // do not show smaller units when we've reached zero already with whole larger units
      first = false;
    }
    uidx--;
    aComponents--;
  }
}



