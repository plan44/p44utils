//
// p44_common.hpp
// p44utils
//
// Author: Lukas Zeller / luz@plan44.ch
// Copyright (c) 2012-2016 by plan44.ch/luz
//

#ifndef __p44utils__common__
#define __p44utils__common__

#include <list>
#include <vector>
#include <map>

#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

#ifndef __printflike
#define __printflike(...)
#endif

#include "p44obj.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "mainloop.hpp"

// build platform dependencies
#if P44_BUILD_OW
  #define DISABLE_I2C 1 // %%% for now
#endif
#if P44_BUILD_DIGI
  #define DISABLE_I2C 1 // no user space I2C support
  #define DISABLE_SPI 1 // no user space SPI support
#endif


#endif /* __p44utils__common__ */
