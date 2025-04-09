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

// for logging from C code

#define P44LOG(lvl,prefix,msg) p44log(lvl,prefix,msg)

#if defined(DEBUG) || ALWAYS_DEBUG
#define P44DBGLOG(lvl,prefix,msg) p44log(lvl,prefix,msg)
#else
#define P44DBGLOG(lvl,prefix,msg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void p44log(int aErrLevel, const char* aMsg);

#ifdef __cplusplus
}
#endif
