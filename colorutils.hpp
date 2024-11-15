//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "fixpoint_macros.h"

#include <string>

#ifndef __p44utils__colorutils__
#define __p44utils__colorutils__


using namespace std;

namespace p44 {

  typedef double Row3[3];
  typedef double Matrix3x3[3][3];


  /// @name PWM scale definitions
  /// @{

  #define PWM8BIT_GLUE 1 // if set, glue routines for 8bit PWM value access are still available
  #define PWMBITS 16 // number of PWM bit resolution

  #define PWMMAX ((1<<PWMBITS)-1)

  #if PWMBITS>8
  typedef uint16_t PWMColorComponent;
  #else
  typedef uint8_t PWMColorComponent;
  #endif

  /// @}


  /// @name pixel color, utilities
  /// @{

  typedef uint8_t PixelColorComponent;

  #if !REDUCED_FOOTPRINT

  typedef struct {
    PixelColorComponent r;
    PixelColorComponent g;
    PixelColorComponent b;
    PixelColorComponent a; // alpha
  } PixelColor;

  #define PIXELMAX 255

  const PixelColor transparent = { .r=0, .g=0, .b=0, .a=0 };
  const PixelColor black = { .r=0, .g=0, .b=0, .a=255 };
  const PixelColor white = { .r=255, .g=255, .b=255, .a=255 };

  /// dim down (or light up) value
  /// @param aVal 0..255 value to dim up or down
  /// @param aDim 0..255: dim, >255: light up (255=100%)
  /// @return dimmed value, limited to max==255
  PixelColorComponent dimVal(PixelColorComponent aVal, uint16_t aDim);

  /// dim down (or light up) power value
  /// @param aVal 0..PWMMAX power value to dim up or down
  /// @param aDim 0..255: dim, >255: light up (255=100%)
  /// @return dimmed value, limited to max==PWMMAX
  PWMColorComponent dimPower(PWMColorComponent aVal, uint16_t aDim);

  /// dim  r,g,b values of a pixel (alpha unaffected)
  /// @param aPix the pixel
  /// @param aDim 0..255: dim, >255: light up (255=100%)
  void dimPixel(PixelColor &aPix, uint16_t aDim);

  /// return dimmed pixel (alpha same as input)
  /// @param aPix the pixel
  /// @param aDim 0..255: dim, >255: light up (255=100%)
  /// @return dimmed pixel
  PixelColor dimmedPixel(const PixelColor aPix, uint16_t aDim);

  /// dim pixel r,g,b down by its alpha value, but alpha itself is not changed!
  /// @param aPix the pixel
  void alpahDimPixel(PixelColor &aPix);

  /// reduce a value by given amount, but not below minimum
  /// @param aByte value to be reduced
  /// @param aAmount amount to reduce
  /// @param aMin minimum value (default=0)
  void reduce(uint8_t &aByte, uint8_t aAmount, uint8_t aMin = 0);

  /// increase a value by given amount, but not above maximum
  /// @param aByte value to be increased
  /// @param aAmount amount to increase
  /// @param aMax maximum value (default=255)
  void increase(uint8_t &aByte, uint8_t aAmount, uint8_t aMax = 255);

  /// add color of one pixel to another
  /// @note does not check for color component overflow/wraparound!
  /// @param aPixel the pixel to add to
  /// @param aPixelToAdd the pixel to add
  void addToPixel(PixelColor &aPixel, PixelColor aPixelToAdd);

  /// overlay a pixel on top of a pixel (based on alpha values)
  /// @param aPixel the original pixel to add an ovelay to
  /// @param aOverlay the pixel to be laid on top
  void overlayPixel(PixelColor &aPixel, PixelColor aOverlay);

  /// @name calculate pixel average in power domain
  /// @{
  /// init the averaging variables (convenience inline)
  inline void prepareAverage(FracValue& aR, FracValue& aG, FracValue& aB, FracValue& aA, FracValue& aTotalWeight) { aR=0; aG=0; aB=0; aA=0; aTotalWeight=0; };
  /// add a pixel to the average with specified weight
  void averagePixelPower(FracValue& aR, FracValue& aG, FracValue& aB, FracValue& aA, FracValue& aTotalWeight, const PixelColor& aInput, FracValue aWeight);
  /// calculate resulting pixel from average
  PixelColor averagedPixelResult(FracValue& aR, FracValue& aG, FracValue& aB, FracValue& aA, FracValue aTotalWeight);
  /// @}

  /// mix two pixels
  /// @param aMainPixel the original pixel which will be modified to contain the mix
  /// @param aOutsidePixel the pixel to mix in
  /// @param aAmountOutside 0..255 (= 0..100%) value to determine how much weight the outside pixel should get in the result
  void mixinPixel(PixelColor &aMainPixel, PixelColor aOutsidePixel, PixelColorComponent aAmountOutside);

  /// get RGB pixel from HSB
  /// @param aHue hue, 0..360 degrees
  /// @param aSaturation saturation, 0..1
  /// @param aBrightness brightness, 0..1
  /// @param aBrightnessAsAlpha if set, brightness is returned in alpha, while RGB will be set for full brightness
  PixelColor hsbToPixel(double aHue, double aSaturation = 1.0, double aBrightness = 1.0, bool aBrightnessAsAlpha = false);

  /// get HSB from pixel
  /// @param aPixelColor the pixel color
  /// @param aHue receives hue, 0..360 degrees
  /// @param aSaturation receives saturation, 0..1
  /// @param aBrightness receives brightness, 0..1
  /// @param aIncludeAlphaIntoBrightness if set, alpha is included in aBrightness returned
  void pixelToHsb(PixelColor aPixelColor, double &aHue, double &aSaturation, double &aBrightness, bool aIncludeAlphaIntoBrightness = false);

  /// convert Web color to pixel color
  /// @param aWebColor web style #ARGB or #AARRGGBB color, alpha (A, AA) is optional, "#" is also optional
  /// @return pixel color. If Alpha is not specified, it is set to fully opaque = 255.
  PixelColor webColorToPixel(const string aWebColor);

  /// convert pixel color to web color
  /// @param aPixelColor pixel color
  /// @param aWithHash if set, output is prefixed with hash, as in css: #RRGGBB or #AARRGGBB
  /// @return web color in RRGGBB style or AARRGGBB when alpha is not fully opaque (==255)
  string pixelToWebColor(const PixelColor aPixelColor, bool aWithHash);


  /// convert pixel color to RGB color components in 0..1 double range
  /// @param aPixelColor pixel color, alpha will be applied to dim output
  /// @param aRGB will receive R,G,B scaled to 0..1 range
  void pixelToRGB(const PixelColor aPixelColor, Row3 &aRGB);

  /// convert RGB values in 0..1 range to pixel color
  /// @param aRGB will R,G,B values in 0..1 range
  /// @return pixel color, alpha set to 255
  PixelColor rgbToPixel(const Row3 &aRGB);


  /// @}

  #endif // !REDUCED_FOOTPRINT


  /// @name color space conversions
  /// @{

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
  /// @param aPWM PWM (energy) value 0..PWMMAX
  /// @return brightness 0..255
  PixelColorComponent pwmToBrightness(PWMColorComponent aPWM);

  /// convert brightness value to PWM
  /// @param aBrightness brightness 0..255
  /// @return PWM (energy) value 0..PWMMAX
  PWMColorComponent brightnessToPwm(PixelColorComponent aBrightness);

  /// get an 8-bit value from a given PWM value
  /// @param aPWM PWM (energy) value 0..PWMMAX
  /// @return 8-bit PWM (energy) value 0..255
  uint8_t pwmTo8Bits(PWMColorComponent aPWM);

  /// get an 8-bit value from a given PWM value
  /// @param aPWM8 8-bit PWM (energy) value 0..255
  /// @return PWM (energy) value 0..PWMMAX
  PWMColorComponent pwmFrom8Bits(uint8_t aPWM8);

  #if PWM8BIT_GLUE
  PixelColorComponent pwm8BitToBrightness(uint8_t aPWM8);
  uint8_t brightnessTo8BitPwm(uint8_t aBrightness);
  #endif

  /// @}



} // namespace p44

#endif /* defined(__p44utils__colorutils__) */
