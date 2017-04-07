//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__valueunits__
#define __p44utils__valueunits__

#include <string>
#include <stdarg.h>
#include <stdint.h>

#ifndef __printflike
#define __printflike(...)
#endif

using namespace std;

namespace p44 {

  /// value (physical) units
  #define VALUE_UNIT(u, s) ((((uint16_t)((uint8_t)s)&0xFF)<<8)+(uint8_t)u)
  #define VALUE_UNIT_ONLY(u) ((ValueBaseUnit)(u & 0xFF))
  #define VALUE_SCALING_ONLY(u) ((UnitScale)((u>>8) & 0xFF))

  typedef enum {
    unit_unknown = 0,
    valueUnit_none = 1, ///< no unit
    valueUnit_percent,
    valueUnit_ppm,
    // basic SI units
    valueUnit_meter,
    valueUnit_gram, ///< we use gram to make it work
    valueUnit_second,
    valueUnit_ampere,
    valueUnit_kelvin,
    valueUnit_mole,
    valueUnit_candle,
    valueUnit_bequerel,
    // derived units
    valueUnit_watt,
    valueUnit_voltampere,
    valueUnit_celsius,
    valueUnit_volt,
    valueUnit_lux,
    valueUnit_liter,
    valueUnit_joule, ///< or watt-second
    valueUnit_pascal,
    valueUnit_degree, ///< angle
    valueUnit_bel, ///< 10*dezibel
    // combined units
    valueUnit_molpercubicmeter,
    valueUnit_bequerelperm3,
    valueUnit_gramperm3,
    valueUnit_meterpersecond,
    valueUnit_meterperm2,
    valueUnit_literpersecond,
    // non-SI scaled units
    valueUnit_minute,
    valueUnit_hour,
    valueUnit_day,
    valueUnit_watthour,
    valueUnit_literpermin,
    numValueUnits,
  } ValueBaseUnit;


  /// scaling factors
  /// @note these are combined into
  typedef enum {
    unitScaling_yotta,
    unitScaling_zetta,
    unitScaling_exa,
    unitScaling_peta,
    unitScaling_tera,
    unitScaling_giga,
    unitScaling_mega,
    unitScaling_kilo,
    unitScaling_hecto,
    unitScaling_deca,
    unitScaling_1,
    unitScaling_deci,
    unitScaling_centi,
    unitScaling_milli,
    unitScaling_micro,
    unitScaling_nano,
    unitScaling_pico,
    unitScaling_femto,
    unitScaling_atto,
    unitScaling_zepto,
    unitScaling_yocto,
    numUnitScalings
  } UnitScale;
  
  /// unit description
  /// @note combination of ValueBaseUnit and UnitScale
  typedef uint16_t ValueUnit;
  

  /// get unit name or symbol for a given ValueUnit
  /// @param aValueUnit the value unit to get the name for
  /// @param aAsSymbol if set, the name is returned as symbol (m), otherwise as full text (meter)
  /// @return unit name or symbol including scaling
  string valueUnitName(ValueUnit aValueUnit, bool aAsSymbol);

  /// get value unit from a given string
  /// @param aValueUnitName a value unit specification string (consisting of unit and optional scaling prefix)
  /// @return value unit (unit_unknown when string does not match)
  ValueUnit stringToValueUnit(const string aValueUnitName);



} // namespace p44

#endif /* defined(__p44utils__valueunits__) */
