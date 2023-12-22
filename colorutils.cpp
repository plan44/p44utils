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

#include <math.h>

#include "colorutils.hpp"
#include "utils.hpp"

using namespace p44;

#if !REDUCED_FOOTPRINT

// MARK: - pixel color utilities

PixelColorComponent p44::dimVal(PixelColorComponent aVal, uint16_t aDim)
{
  uint32_t d = (aDim+1)*aVal;
  if (d>0xFFFF) return 0xFF;
  return d>>8;
}


void p44::dimPixel(PixelColor &aPix, uint16_t aDim)
{
  if (aDim==255) return; // 100% -> NOP
  aPix.r = dimVal(aPix.r, aDim);
  aPix.g = dimVal(aPix.g, aDim);
  aPix.b = dimVal(aPix.b, aDim);
}


PixelColor p44::dimmedPixel(const PixelColor aPix, uint16_t aDim)
{
  PixelColor pix = aPix;
  dimPixel(pix, aDim);
  return pix;
}


void p44::alpahDimPixel(PixelColor &aPix)
{
  if (aPix.a!=255) {
    dimPixel(aPix, aPix.a);
  }
}


void p44::reduce(uint8_t &aByte, uint8_t aAmount, uint8_t aMin)
{
  int r = aByte-aAmount;
  if (r<aMin)
    aByte = aMin;
  else
    aByte = (uint8_t)r;
}


void p44::increase(uint8_t &aByte, uint8_t aAmount, uint8_t aMax)
{
  int r = aByte+aAmount;
  if (r>aMax)
    aByte = aMax;
  else
    aByte = (uint8_t)r;
}


void assignPixelComponent(PixelColorComponent &aComponent, double aValue)
{
  if (aValue>255.5)
    aComponent = 255;
  else
    aComponent = (uint8_t)(aValue+0.5);
}



void p44::overlayPixel(PixelColor &aPixel, PixelColor aOverlay)
{
  if (aOverlay.a==255) {
    aPixel = aOverlay;
  }
  else {
    // mix in
    // - reduce original by alpha of overlay
    aPixel = dimmedPixel(aPixel, 255-aOverlay.a);
    // - reduce overlay by its own alpha
    aOverlay = dimmedPixel(aOverlay, aOverlay.a);
    // - add in
    addToPixel(aPixel, aOverlay);
  }
  aPixel.a = 255; // result is never transparent
}


void p44::averagePixelPower(FracValue& aR, FracValue& aG, FracValue& aB, FracValue& aA, FracValue& aTotalWeight, const PixelColor& aInput, FracValue aWeight)
{
  if (aWeight>0) {
    //FracValue powerA = FP_FROM_INT(brightnessToPwm(aInput.a));
    //FracValue powerA = FP_FROM_INT(((int16_t)brightnessToPwm(aInput.a)*256+128)/255);
    int powerA = brightnessToPwm(aInput.a); // 0..255
    FracValue powerFact = FP_FROM_INT(powerA*FP_FRACFACT/255)/FP_FRACFACT;
    aA += powerA*aWeight; // only one FracValue factor, no corr needed
    aR += FP_MUL_CORR(powerFact*brightnessToPwm(aInput.r)*aWeight);
    aG += FP_MUL_CORR(powerFact*brightnessToPwm(aInput.g)*aWeight);
    aB += FP_MUL_CORR(powerFact*brightnessToPwm(aInput.b)*aWeight);
  }
  aTotalWeight += aWeight;
}


#ifdef FP_FRACVALUE
  #define BOOST_SCALING 16 // to get higher accuray for alphaboost values near 1
#else
  #define BOOST_SCALING 1
#endif

PixelColor p44::averagedPixelResult(FracValue& aR, FracValue& aG, FracValue& aB, FracValue& aA, FracValue aTotalWeight)
{
  if (aTotalWeight>0) {
    PixelColor pc;
    FracValue a = FP_DIV(aA,aTotalWeight); // in 0..255 scale
    if (a>0) {
      FracValue alphaboost = FP_DIV(aTotalWeight*BOOST_SCALING, aA); // in 1/256*BOOST_SCALING
      assignPixelComponent(pc.a, pwmToBrightness(FP_INT_VAL(a)));
      // no need for FP_ correction when we have a a multiplication followed by a division
      assignPixelComponent(pc.r, pwmToBrightness(FP_TIMES_FRACFACT_INT_VAL(aR*alphaboost/aTotalWeight/BOOST_SCALING)));
      assignPixelComponent(pc.g, pwmToBrightness(FP_TIMES_FRACFACT_INT_VAL(aG*alphaboost/aTotalWeight/BOOST_SCALING)));
      assignPixelComponent(pc.b, pwmToBrightness(FP_TIMES_FRACFACT_INT_VAL(aB*alphaboost/aTotalWeight/BOOST_SCALING)));
      return pc;
    }
  }
  // nothing
  return transparent;
}


void p44::mixinPixel(PixelColor &aMainPixel, PixelColor aOutsidePixel, PixelColorComponent aAmountOutside)
{
  if (aAmountOutside>0) {
    if (aMainPixel.a!=255 || aOutsidePixel.a!=255) {
      // mixed transparency
      PixelColorComponent alpha = dimVal(aMainPixel.a, pwmToBrightness(255-aAmountOutside)) + dimVal(aOutsidePixel.a, pwmToBrightness(aAmountOutside));
      if (alpha>0) {
        // calculation only needed for not totallay transparent result
        // - alpha boost compensates for energy
        uint16_t ab = 65025/alpha;
        // Note: aAmountOutside is on the energy scale, not brightness, so need to add in PWM scale!
        uint16_t r_e = ( (((uint16_t)brightnessToPwm(dimVal(aMainPixel.r, aMainPixel.a))+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(dimVal(aOutsidePixel.r, aOutsidePixel.a))+1)*(aAmountOutside)) )>>8;
        uint16_t g_e = ( (((uint16_t)brightnessToPwm(dimVal(aMainPixel.g, aMainPixel.a))+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(dimVal(aOutsidePixel.g, aOutsidePixel.a))+1)*(aAmountOutside)) )>>8;
        uint16_t b_e = ( (((uint16_t)brightnessToPwm(dimVal(aMainPixel.b, aMainPixel.a))+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(dimVal(aOutsidePixel.b, aOutsidePixel.a))+1)*(aAmountOutside)) )>>8;
        // - back to brightness, add alpha boost
        uint16_t r = (((uint16_t)pwmToBrightness(r_e)+1)*ab)>>8;
        uint16_t g = (((uint16_t)pwmToBrightness(g_e)+1)*ab)>>8;
        uint16_t b = (((uint16_t)pwmToBrightness(b_e)+1)*ab)>>8;
        // - check max brightness
        uint16_t m = r; if (g>m) m = g; if (b>m) m = b;
        if (m>255) {
          // more brightness requested than we have
          // - scale down to make max=255
          uint16_t cr = 65025/m;
          r = (r*cr)>>8;
          g = (g*cr)>>8;
          b = (b*cr)>>8;
          // - increase alpha by reduction of components
          alpha = (((uint16_t)alpha+1)*m)>>8;
          aMainPixel.r = r>255 ? 255 : r;
          aMainPixel.g = g>255 ? 255 : g;
          aMainPixel.b = b>255 ? 255 : b;
          aMainPixel.a = alpha;
        }
        else {
          // brightness below max, just convert back
          aMainPixel.r = r;
          aMainPixel.g = g;
          aMainPixel.b = b;
          aMainPixel.a = alpha;
        }
      }
      else {
        // resulting alpha is 0, fully transparent pixel
        aMainPixel = transparent;
      }
    }
    else {
      // no transparency on either side, simplified case
      uint16_t r_e = ( (((uint16_t)brightnessToPwm(aMainPixel.r)+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(aOutsidePixel.r)+1)*(aAmountOutside)) )>>8;
      uint16_t g_e = ( (((uint16_t)brightnessToPwm(aMainPixel.g)+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(aOutsidePixel.g)+1)*(aAmountOutside)) )>>8;
      uint16_t b_e = ( (((uint16_t)brightnessToPwm(aMainPixel.b)+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(aOutsidePixel.b)+1)*(aAmountOutside)) )>>8;
      aMainPixel.r = r_e>255 ? 255 : pwmToBrightness(r_e);
      aMainPixel.g = g_e>255 ? 255 : pwmToBrightness(g_e);
      aMainPixel.b = b_e>255 ? 255 : pwmToBrightness(b_e);
      aMainPixel.a = 255;
    }
  }
}


void p44::addToPixel(PixelColor &aPixel, PixelColor aPixelToAdd)
{
  increase(aPixel.r, aPixelToAdd.r);
  increase(aPixel.g, aPixelToAdd.g);
  increase(aPixel.b, aPixelToAdd.b);
}


PixelColor p44::hsbToPixel(double aHue, double aSaturation, double aBrightness, bool aBrightnessAsAlpha)
{
  PixelColor p;
  Row3 RGB, HSV = { aHue, aSaturation, aBrightnessAsAlpha ? 1.0 : aBrightness };
  HSVtoRGB(HSV, RGB);
  p.r = RGB[0]*255;
  p.g = RGB[1]*255;
  p.b = RGB[2]*255;
  p.a = aBrightnessAsAlpha ? aBrightness*255: 255;
  return p;
}


void p44::pixelToHsb(PixelColor aPixelColor, double &aHue, double &aSaturation, double &aBrightness, bool aIncludeAlphaIntoBrightness)
{
  Row3 HSV, RGB = { (double)aPixelColor.r/255, (double)aPixelColor.g/255, (double)aPixelColor.b/255 };
  RGBtoHSV(RGB, HSV);
  aHue = HSV[0];
  aSaturation = HSV[1];
  if (aIncludeAlphaIntoBrightness)
    aBrightness = HSV[2]*aPixelColor.a/255;
  else
    aBrightness = HSV[2];
}


PixelColor p44::webColorToPixel(const string aWebColor)
{
  PixelColor res = transparent;
  size_t i = 0;
  size_t n = aWebColor.size();
  if (n>0 && aWebColor[0]=='#') { i++; n--; } // skip optional #
  uint32_t h;
  if (sscanf(aWebColor.c_str()+i, "%x", &h)==1) {
    if (n<=4) {
      // short form RGB or ARGB
      res.a = 255;
      if (n==4) { res.a = (h>>12)&0xF; res.a |= res.a<<4; }
      res.r = (h>>8)&0xF; res.r |= res.r<<4;
      res.g = (h>>4)&0xF; res.g |= res.g<<4;
      res.b = (h>>0)&0xF; res.b |= res.b<<4;
    }
    else {
      // long form RRGGBB or AARRGGBB
      res.a = 255;
      if (n==8) { res.a = (h>>24)&0xFF; }
      res.r = (h>>16)&0xFF;
      res.g = (h>>8)&0xFF;
      res.b = (h>>0)&0xFF;
    }
  }
  return res;
}


string p44::pixelToWebColor(const PixelColor aPixelColor, bool aWithHash)
{
  string w;
  if (aWithHash) w = "#";
  if (aPixelColor.a!=255) string_format_append(w, "%02X", aPixelColor.a);
  string_format_append(w, "%02X%02X%02X", aPixelColor.r, aPixelColor.g, aPixelColor.b);
  return w;
}


void p44::pixelToRGB(PixelColor aPixelColor, Row3 &aRGB)
{
  aRGB[0] = ((double)aPixelColor.r * aPixelColor.a)/255/255;
  aRGB[1] = ((double)aPixelColor.g * aPixelColor.a)/255/255;
  aRGB[2] = ((double)aPixelColor.b * aPixelColor.a)/255/255;
}

#endif // !REDUCED_FOOTPRINT



// MARK: - color space conversions

// sRGB with D65 reference white calibration matrix
// [[Xr,Xg,Xb],[Yr,Yg,Yb],[Zr,Zg,Zb]]
const Matrix3x3 p44::sRGB_d65_calibration = {
  { 0.4123955889674142161, 0.3575834307637148171, 0.1804926473817015735 },
  { 0.2125862307855955516, 0.7151703037034108499, 0.0722004986433362269 },
  { 0.0192972154917469448, 0.1191838645808485318, 0.9504971251315797660 }
};


static void swapRows(int r1, int r2, Matrix3x3 matrix)
{
  double temp;
  for (int c=0; c<3; c++) {
    temp = matrix[r1][0];
    matrix[r1][0] = matrix[r2][0];
    matrix[r2][0] = temp;
  }
}


void p44::matrix3x3_copy(const Matrix3x3 &aFrom, Matrix3x3 &aTo)
{
  for (int r=0; r<3; r++) { for (int c=0; c<3; c++) { aTo[r][c] = aFrom[r][c]; }};
}



// Matrix Inverse
// Guass-Jordan Elimination Method
// Reduced Row Eshelon Form (RREF)
bool p44::matrix3x3_inverse(const Matrix3x3 &inmatrix, Matrix3x3 &em)
{
  Matrix3x3 matrix;
  matrix3x3_copy(inmatrix, matrix);
  // init result with unity matrix
  em[0][0] = 1; em[0][1] = 0; em[0][2] = 0;
  em[1][0] = 0; em[1][1] = 1; em[1][2] = 0;
  em[2][0] = 0; em[2][1] = 0; em[2][2] = 1;
  // calc
  int lead = 0;
  int rowCount = 3;
  int columnCount = 3;
  for (int r = 0; r < rowCount; r++) {
    if (lead >= columnCount)
      break;
    int i = r;
    while (matrix[i][lead] == 0) {
      i++;
      if (i==rowCount) {
        i = r;
        lead++;
        if (lead==columnCount) {
          return false; // error
        }
      }
    }
    // swap rows i and r in input matrix
    swapRows(i,r,matrix);
    // swap rows i and r in unity matrix
    swapRows(i,r,em);

    double lv = matrix[r][lead];
    for (int j = 0; j < columnCount; j++) {
      matrix[r][j] = matrix[r][j] / lv;
      em[r][j] = em[r][j] / lv;
    }
    for (i = 0; i<rowCount; i++) {
      if (i!=r) {
        lv = matrix[i][lead];
        for (int j = 0; j<columnCount; j++) {
          matrix[i][j] -= lv * matrix[r][j];
          em[i][j] -= lv * em[r][j];
        }
      }
    }
    lead++;
  }
  // success, em contains result
  return true;
}


bool p44::XYZtoRGB(const Matrix3x3 &calib, const Row3 &XYZ, Row3 &RGB)
{
  Matrix3x3 m_inv;
  double r,g,b; // uncompanded
  if (!matrix3x3_inverse(calib, m_inv)) return false;
  r = m_inv[0][0]*XYZ[0] + m_inv[0][1]*XYZ[1] + m_inv[0][2]*XYZ[2];
  g = m_inv[1][0]*XYZ[0] + m_inv[1][1]*XYZ[1] + m_inv[1][2]*XYZ[2];
  b = m_inv[2][0]*XYZ[0] + m_inv[2][1]*XYZ[1] + m_inv[2][2]*XYZ[2];
  // apply gamma companding
  // see http://www.brucelindbloom.com/index.html?ColorCalculator.html, math section
  double gamma = 2.2; // 2.2 is CIE RGB, or approximately like sRGB, or like 1998 Adobe RGB
  double power = 1/gamma;
  // Note: correct expansion would be:
  //  V = sign(v)*pow(abs(v), power)
  // However, as negative RGB does not make practical sense, we clip them to 0 here already
  RGB[0] = r>0 ? pow(r, power) : 0;
  RGB[1] = g>0 ? pow(g, power) : 0;
  RGB[2] = b>0 ? pow(b, power) : 0;
  return true;
}


bool p44::RGBtoXYZ(const Matrix3x3 &calib, const Row3 &RGB, Row3 &XYZ)
{
  XYZ[0] = calib[0][0]*RGB[0] + calib[0][1]*RGB[1] + calib[0][2]*RGB[2];
  XYZ[1] = calib[1][0]*RGB[0] + calib[1][1]*RGB[1] + calib[1][2]*RGB[2];
  XYZ[2] = calib[2][0]*RGB[0] + calib[2][1]*RGB[1] + calib[2][2]*RGB[2];
  return true;
}

bool p44::XYZtoxyV(const Row3 &XYZ, Row3 &xyV)
{
  if ((XYZ[0]+XYZ[1]+XYZ[2]) == 0) {
    xyV[0] = 0;
    xyV[1] = 0;
    xyV[2] = 0;
  } else {
    xyV[0] = XYZ[0]/(XYZ[0]+XYZ[1]+XYZ[2]);
    xyV[1] = XYZ[1]/(XYZ[0]+XYZ[1]+XYZ[2]);
    xyV[2] = XYZ[1];
  }
  return true;
}


bool p44::xyVtoXYZ(const Row3 &xyV, Row3 &XYZ)
{
  double divisor = xyV[1];
  if (divisor < 0.01)
    divisor = 0.01; // do not divide by 0
  XYZ[0] = xyV[0]*(xyV[2]/divisor);
  XYZ[1] = xyV[2];
  XYZ[2] = (1-xyV[0]-divisor)*(xyV[2]/divisor);
  return true;
}


bool p44::RGBtoHSV(const Row3 &RGB, Row3 &HSV)
{
  // calc min/max
  int maxt = 0;
  double max = RGB[0];
  double min = RGB[0];
  for (int i=1; i<3; i++) {
    if (RGB[i] > max) {
      maxt = i;
      max = RGB[i];
    }
    if (RGB[i] < min) {
      min = RGB[i];
    }
  }
  // Hue
  if (max==min) {
    HSV[0] = 0;
  } else {
    switch (maxt) {
      case 0: // max = R?
        HSV[0] = 60*(0+(RGB[1]-RGB[2])/(max-min)); break;
      case 1: // max = G?
        HSV[0] = 60*(2+(RGB[2]-RGB[0])/(max-min)); break;
      case 2: // max = B?
        HSV[0] = 60*(4+(RGB[0]-RGB[1])/(max-min)); break;
    }
  }
  if (HSV[0] < 0)
    HSV[0] += 360;
  // Saturation
  if (max == 0) {
    HSV[1] = 0;
  } else {
    HSV[1] = (max-min) / max;
  }
  // Value (brightness)
  HSV[2] = max;
  return true;
}


bool p44::HSVtoRGB(const Row3 &HSV, Row3 &RGB)
{
  double hue = HSV[0];
  if (hue<0 || hue>=360) {
    hue = ((int)(hue*1000) % (360*1000))/1000;
  }
  int hi = (int)floor(hue / 60) % 6;
  double f = (hue / 60 - hi);
  double p = HSV[2] * (1 - HSV[1]);
  double q = HSV[2] * (1 - (HSV[1]*f));
  double t = HSV[2] * (1 - (HSV[1]*(1-f)));
  switch (hi) {
    default: // should not occur, hi should be 0..5
    case 6: // should not occur, hi should be 0..5
    case 0:
      RGB[0] = HSV[2];
      RGB[1] = t;
      RGB[2] = p;
      break;
    case 1:
      RGB[0] = q;
      RGB[1] = HSV[2];
      RGB[2] = p;
      break;
    case 2:
      RGB[0] = p;
      RGB[1] = HSV[2];
      RGB[2] = t;
      break;
    case 3:
      RGB[0] = p;
      RGB[1] = q;
      RGB[2] = HSV[2];
      break;
    case 4:
      RGB[0] = t;
      RGB[1] = p;
      RGB[2] = HSV[2];
      break;
    case 5:
      RGB[0] = HSV[2];
      RGB[1] = p;
      RGB[2] = q;
      break;
  }
  return true;
}


bool p44::HSVtoxyV(const Row3 &HSV, Row3 &xyV)
{
  Row3 RGB;
  HSVtoRGB(HSV, RGB);
  Row3 XYZ;
  RGBtoXYZ(sRGB_d65_calibration, RGB, XYZ);
  XYZtoxyV(XYZ, xyV);
  return true;
}


bool p44::xyVtoHSV(const Row3 &xyV, Row3 &HSV)
{
  Row3 XYZ;
  xyVtoXYZ(xyV, XYZ);
  Row3 RGB;
  XYZtoRGB(sRGB_d65_calibration, XYZ, RGB);
  RGBtoHSV(RGB, HSV);
  return true;
}


// color temperature and y vs. x coordinate in 1/100 steps, from x=0.66 down to x=0.30
const int countCts = 37;
const double cts[countCts][2] = {
  { 948,0.33782873820708 },
  { 1019,0.34682388376817 },
  { 1091,0.35545575770743 },
  { 1163,0.36353287224500 },
  { 1237,0.37121206756052 },
  { 1312,0.37832319611070 },
  { 1388,0.38482574553216 },
  { 1466,0.39076326126528 },
  { 1545,0.39602948797950 },
  { 1626,0.40067257983490 },
  { 1708,0.40462758231674 },
  { 1793,0.40798078933257 },
  { 1880,0.41068017199236 },
  { 1969,0.41273637414613 },
  { 2061,0.41418105044123 },
  { 2157,0.41502718841801 },
  { 2256,0.41527448264726 },
  { 2359,0.41494487494675 },
  { 2466,0.41405903487263 },
  { 2579,0.41261744057645 },
  { 2698,0.41063633036979 },
  { 2823,0.40814486823430 },
  { 2957,0.40511150919122 },
  { 3099,0.40159310586449 },
  { 3252,0.39755898609813 },
  { 3417,0.39303263395499 },
  { 3597,0.38799332181520 },
  { 3793,0.38248898245784 },
  { 4010,0.37647311389569 },
  { 4251,0.36997922346483 },
  { 4522,0.36299131572450 },
  { 4831,0.35549007551420 },
  { 5189,0.34745303570846 },
  { 5609,0.33890583227018 },
  { 6113,0.32982098812739 },
  { 6735,0.32016657303155 },
  { 7530,0.30991572591376 }
};


bool p44::CTtoxyV(double mired, Row3 &xyV)
{
  double CT = 1000000/mired;
  if ((CT<cts[0][0]) || (CT>=cts[countCts-1][0])) {
    xyV[0] = 0.33;
    xyV[1] = 0.33; // CT < 948 || CT > 10115
  }
  else {
    for (int i=1; i<countCts; i++) {
      if (CT<cts[i][0]) {
        double fac = (CT-cts[i-1][0])/(cts[i][0]-cts[i-1][0]);
        xyV[1] = fac*(cts[i][1]-cts[i-1][1])+cts[i-1][1];
        xyV[0] = 0.66-((i-1)/100.0)-(fac/100);
        break;
      }
    }
  }
  xyV[2] = 1.0; // mired has no brightness information, assume 100% = 1.0
  return true;
}


bool p44::xyVtoCT(const Row3 &xyV, double &mired)
{
  // very rough approximation:
  // - CIE x 0.28 -> 10000K = 100mired
  // - CIE x 0.65 -> 1000K = 1000mired
  double x = xyV[0] - 0.28;
  if (x<0) x=0;
  mired = (x)/(0.65-0.28)*900 + 100;
  return true;
}

// MARK: - RGB to RGBW conversions

double p44::transferToColor(const Row3 &aCol, double &aRed, double &aGreen, double &aBlue)
{
  bool hasRed = aCol[0]>0;
  bool hasGreen = aCol[1]>0;
  bool hasBlue = aCol[2]>0;
  double fr = hasRed ? aRed/aCol[0] : 0;
  double fg = hasGreen ? aGreen/aCol[1] : 0;
  double fb = hasBlue ? aBlue/aCol[2] : 0;
  // - find non-zero fraction to use of external color
  double f = fg>fb && hasBlue ? fb : fg;
  f = fr>f && (hasBlue || hasGreen) ? f : fr;
  if (f>1) f=1; // limit to 1
  // - now subtract from RGB values what we've transferred to separate color
  if (hasRed) aRed = aRed - f*aCol[0];
  if (hasGreen) aGreen = aGreen - f*aCol[1];
  if (hasBlue) aBlue = aBlue - f*aCol[2];
  // - find fraction RGB HAS to contribute without loosing color information
  double u = 1-f*aCol[0]; // how much of red RGB needs to contribute
  if (1-f*aCol[1]>u) u = 1-f*aCol[1]; // how much of green
  if (1-f*aCol[2]>u) u = 1-f*aCol[2]; // how much of blue
  //   now scale RGB up to minimal fraction it HAS to contribute
  if (u>0) {
    u = 1/u;
    aRed *= u;
    aBlue *= u;
    aGreen *= u;
  }
  return f;
}


void p44::transferFromColor(const Row3 &aCol, double aAmount, double &aRed, double &aGreen, double &aBlue)
{
  // add amount from separate color
  aRed += aAmount*aCol[0];
  aGreen += aAmount*aCol[1];
  aBlue += aAmount*aCol[2];
  // scale down if we exceed 1
  double m = aRed>aGreen ? aRed : aGreen;
  m = aBlue>m ? aBlue : m;
  if (m>1) {
    aRed /= m;
    aGreen /= m;
    aBlue /= m;
  }
}


/*

 // data series of transfers from RGB into RGBW assuming W has 1/3 brightness of R+G+B combined

 static const Row3 LEDwhite = { 1.0/3, 1.0/3, 1.0/3 };
 static void rgbwtest()
 {
   printf("R\tG\tB\tR1\tG1\tB1\tW\n");
   for (int r=0; r<256; r+=51) {
     for (int g=0; g<256; g+=51) {
       for (int b=0; b<256; b+=51) {
         double fr,fg,fb;
         fr = (double)r/255;
         fg = (double)g/255;
         fb = (double)b/255;
         double r1,g1,b1;
         r1 = fr;
         g1 = fg;
         b1 = fb;
         double w = transferToColor(LEDwhite, r1, g1, b1);
         printf("%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\n",fr,fg,fb,r1,g1,b1,w);
       }
     }
   }
 }

*/



// MARK: - PWM to brightness conversions

// brightness to PWM value conversion
const uint8_t p44::pwmtable[256] = {
  0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,
  6, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 11, 11, 11,
  11, 12, 12, 12, 12, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17,
  17, 18, 18, 18, 19, 19, 20, 20, 20, 21, 21, 22, 22, 22, 23, 23, 24, 24, 25, 25,
  26, 26, 26, 27, 27, 28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 34, 34, 35, 35, 36,
  37, 37, 38, 39, 39, 40, 41, 42, 42, 43, 44, 44, 45, 46, 47, 48, 49, 49, 50, 51,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 72,
  73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89, 90, 92, 93, 95, 97, 98, 100,
  101, 103, 105, 107, 108, 110, 112, 114, 116, 118, 120, 121, 123, 126, 128, 130,
  132, 134, 136, 138, 141, 143, 145, 148, 150, 152, 155, 157, 160, 163, 165, 168,
  171, 174, 176, 179, 182, 185, 188, 191, 194, 197, 201, 204, 207, 210, 214, 217,
  221, 224, 228, 232, 235, 239, 243, 247, 251, 255
};


const uint8_t p44::brightnesstable[256] = {
  0, 7, 18, 27, 36, 43, 49, 55, 61, 66, 70, 75, 79, 83, 86, 90, 93, 96, 99, 102, 104,
  107, 109, 112, 114, 116, 118, 121, 123, 124, 126, 128, 130, 132, 133, 135, 137, 138,
  140, 141, 143, 144, 145, 147, 148, 150, 151, 152, 153, 154, 156, 157, 158, 159, 160,
  161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177,
  177, 178, 179, 180, 181, 181, 182, 183, 184, 184, 185, 186, 187, 187, 188, 189, 190,
  190, 191, 192, 192, 193, 194, 194, 195, 195, 196, 197, 197, 198, 199, 199, 200, 200,
  201, 201, 202, 203, 203, 204, 204, 205, 205, 206, 206, 207, 207, 208, 208, 209, 210,
  210, 211, 211, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 216, 217, 217, 218,
  218, 218, 219, 219, 220, 220, 221, 221, 221, 222, 222, 223, 223, 224, 224, 224, 225,
  225, 226, 226, 226, 227, 227, 227, 228, 228, 229, 229, 229, 230, 230, 230, 231, 231,
  231, 232, 232, 233, 233, 233, 234, 234, 234, 235, 235, 235, 236, 236, 236, 237, 237,
  237, 238, 238, 238, 239, 239, 239, 240, 240, 240, 240, 241, 241, 241, 242, 242, 242,
  243, 243, 243, 244, 244, 244, 244, 245, 245, 245, 246, 246, 246, 246, 247, 247, 247,
  248, 248, 248, 248, 249, 249, 249, 249, 250, 250, 250, 251, 251, 251, 251, 252, 252,
  252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 255, 255, 255, 255
};


uint8_t p44::pwmToBrightness(uint8_t aPWM)
{
  return brightnesstable[aPWM];
}


uint8_t p44::brightnessToPwm(uint8_t aBrightness)
{
  return pwmtable[aBrightness];
}



