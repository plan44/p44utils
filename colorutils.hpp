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

#include "p44utils_minimal.hpp"

#include <stdint.h>

#ifndef __p44utils__colorutils__
#define __p44utils__colorutils__



namespace p44 {

  /// @name color space conversions
  /// @{

  typedef double Row3[3];
  typedef double Matrix3x3[3][3];

  extern void matrix3x3_copy(const Matrix3x3 &aFrom, Matrix3x3 &aTo);
  extern const Matrix3x3 sRGB_d65_calibration;

  bool matrix3x3_inverse(const Matrix3x3 &matrix, Matrix3x3 &em);

  bool XYZtoRGB(const Matrix3x3 &calib, const Row3 &XYZ, Row3 &RGB);
  bool RGBtoXYZ(const Matrix3x3 &calib, const Row3 &RGB, Row3 &XYZ);

  bool XYZtoxyV(const Row3 &XYZ, Row3 &xyV);
  bool xyVtoXYZ(const Row3 &xyV, Row3 &XYZ);

  bool RGBtoHSV(const Row3 &RGB, Row3 &HSV);
  bool HSVtoRGB(const Row3 &HSV, Row3 &RGB);

  bool HSVtoxyV(const Row3 &HSV, Row3 &xyV);
  bool xyVtoHSV(const Row3 &xyV, Row3 &HSV);

  bool CTtoxyV(double mired, Row3 &xyV);
  bool xyVtoCT(const Row3 &xyV, double &mired);

  /// @}

  /// @name transfer of brightness from RGB to a separate distinct color, such as white or amber
  /// @{

  /// transfer brightness from RGB into a specific color
  /// @param aCol the specific color separate from R,G,B (such as a flavor of white, or amber)
  /// @param aRed on input, the original red component. On output, the red amount still needed
  /// @param aGreen on input, the original green component. On output, the green amount still needed
  /// @param aBlue on input, the original blue component. On output, the blue amount still needed
  /// @return the amount of aCol that should be used together with adjusted values in aRed, aGreen, aBlue
  double transferToColor(const Row3 &aCol, double &aRed, double &aGreen, double &aBlue);

/// transfer brightness from a specific color into RGB
/// @param aCol the specific color separate from R,G,B (such as a flavor of white, or amber)
/// @param aAmount the amount of the separate color to transfer
/// @param aRed on input, the original red component. On output, the red amount now needed when aCol is missing
/// @param aGreen on input, the original green component. On output, the green amount now needed when aCol is missing
/// @param aBlue on input, the original blue component. On output, the blue amount now needed when aCol is missing
  void transferFromColor(const Row3 &aCol, double aAmount, double &aRed, double &aGreen, double &aBlue);

  /// @}


  /// @name PWM/brightness conversions
  /// @{

  /// convert PWM value to brightness
  /// @param aPWM PWM (energy) value 0..255
  /// @return brightness 0..255
  uint8_t pwmToBrightness(uint8_t aPWM);

  /// convert brightness value to PWM
  /// @param aBrightness brightness 0..255
  /// @return PWM (energy) value 0..255
  uint8_t brightnessToPwm(uint8_t aBrightness);

  /// lookup tables to use for time critical conversions (as used by pwmToBrightness/brightnessToPwm)
  extern const uint8_t pwmtable[256]; ///< brightness 0..255 to PWM 0..255 lookup table
  extern const uint8_t brightnesstable[256]; ///< pwm 0..255 to brightness 0..255 lookup table

  /// @}

} // namespace p44

#endif /* defined(__p44utils__colorutils__) */
