//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__p44obj__
#define __p44utils__p44obj__

#include "p44utils_minimal.hpp"

#include <boost/intrusive_ptr.hpp>

namespace p44 {

  class P44Obj;

  void intrusive_ptr_add_ref(P44Obj* o);
  void intrusive_ptr_release(P44Obj* o);

  class P44Obj {
    friend void intrusive_ptr_add_ref(P44Obj* o);
    friend void intrusive_ptr_release(P44Obj* o);

    int mRefCount;

  public:

    /// Call this method for P44Objs that are allocated as member variable of another object
    /// @note this is required to avoid destruction is attempted when a member object
    ///   is referenced by a intrusive_ptr and then this pointer goes out of scope. Refcount would then
    ///   reach 0 and destruction would be attempted.
    void isMemberVariable();

    /// access to the refcount for debug logging
    int refCount() { return mRefCount; }

  protected:
    P44Obj() : mRefCount(0) {};
    virtual ~P44Obj() {}; // important for multiple inheritance
  };

  typedef boost::intrusive_ptr<P44Obj> P44ObjPtr;



} // namespace p44


#endif /* defined(__p44utils__p44obj__) */
