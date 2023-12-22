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

#ifndef __p44utils__fixpoint_macros__
#define __p44utils__fixpoint_macros__

/// Macros to switch between double arithmetics and fixed point integer representations
/// by changing FP\_FRACVALUE
/// @note
/// - enabling FP\_FRACVALUE limits the calculations that are possible
/// - multiplication results need to be corrected (scaled down) using FP_MUL_CORR()
/// - multiplication result int part is limited to number of bits in FP_TYPE minus
///   TWICE the number of bits in the fraction (FP_FRACBITS). To avoid this extra loss
///   of FP_FRACBITS for the result's magnitude, multiplying with a factor that is
///   known to be int should be done by simply multiplying, and NOT applying
///   FP_MUL_CORR() afterwards.
/// - Not setting FP\_FRACVALUE is intended for debugging problems with precision
///   and other fixed point arithmetics, not for real use. It allows to use the same
///   but using double arithmetics, with none of the bit shuffling needed otherwise.
///   Note that wrong usage of the macros might still render correct results in
///   double mode (as most of them become no-ops), but not the other way around.

/// define to use fixed point (FP) fractional value representation
#ifndef FP_FRACVALUE
  #define FP_FRACVALUE 1 // enabled by default (disabling is only intended for debugging)
#endif

#if FP_FRACVALUE
  // Definitions about how to map fractional values to an int type
  // Default to int32_t with 24 bits integer part and 8 bits fraction
  #ifndef FP_TYPE
    #define FP_TYPE int32_t
  #endif
  #ifndef FP_FRACBITS
    #define FP_FRACBITS 8
  #endif
#endif

#if FP_FRACVALUE
  typedef FP_TYPE FracValue;
  #define FP_FRACFACT (1<<FP_FRACBITS)
  #define FP_ROUNDOFFS (1<<(FP_FRACBITS-1))
  #define FP_FRACMASK (FP_FRACFACT-1)
  #define FP_MUL_CORR(f) (((f)+FP_ROUNDOFFS)/FP_FRACFACT) // division to keep sign
  #define FP_DIV(f1,f2) (((f1)<<FP_FRACBITS)/(f2)) // must always return 0 when dividend is 0
  #define FP_DBL_VAL(f) ((double)(f)/FP_FRACFACT) // division to keep sign
  #define FP_INT_VAL(f) ((f)/FP_FRACFACT) // division to keep sign
  #define FP_TIMES_FRACFACT_INT_VAL(f) (f) // integer value of FracValue times FP_FRACFACT = value-as-is
  #define FP_FROM_DBL(d) ((d)*(1<<FP_FRACBITS))
  #define FP_FROM_INT(i) (((FracValue)(i))<<FP_FRACBITS)
  #define FP_FACTOR_FROM_INT(i) ((FracValue)(i)) // integer part without shifting, can be used as factor in multiplications, which then don't need FP_MUL_CORR
  #define FP_INT_FLOOR(f) (((f) & ~FP_FRACMASK)/FP_FRACFACT) // division to keep sign
  #define FP_INT_CEIL(f) ((((f)+FP_FRACMASK) & ~FP_FRACMASK)/FP_FRACFACT) // division to keep sign
  #define FP_HASFRAC(f) (((f)&FP_FRACMASK)!=0)
#else // FP_FRACVALUE
  typedef double FracValue;
  #define FP_FRACFACT 1
  #define FP_MUL_CORR(f) (f)
  #define FP_DIV(f1,f2) ((f1)/(f2))
  #define FP_DBL_VAL(f) (f)
  #define FP_INT_VAL(f) ((int)(f))
  #define FP_INT_256TH_VAL(f) ((f)*FP_FRACFACT)
  #define FP_FROM_DBL(d) (d)
  #define FP_FROM_INT(i) ((double)(i))
  #define FP_FACTOR_FROM_INT(i) ((double)(i))
  #define FP_INT_FLOOR(f) floor(f)
  #define FP_INT_CEIL(f) ceil(f)
  #define FP_HASFRAC(f) (trunc(f)!=(f))
#endif // !FP_FRACVALUE


#endif // __p44utils__fixpoint_macros__
