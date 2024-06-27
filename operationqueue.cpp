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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "operationqueue.hpp"

using namespace p44;


Operation::Operation() :
  mCompletionCB(NoOP),
  mInitiated(false),
  mAborted(false),
  mTimeout(0), // no timeout
  mTimesOutAt(0), // no timeout time set
  mInitiationDelay(0), // no initiation delay
  mFromLastInitiation(false),
  mInitiatesNotBefore(0), // no initiation time
  mInSequence(true) // by default, execute in sequence
{
  FOCUSLOG("+++++ Creating Operation   %p", this);
}


Operation::~Operation()
{
  FOCUSLOG("----- Deleteting Operation %p", this);
}


void Operation::reset()
{
  // release callback
  mCompletionCB = NoOP;
  // release chained object
  if (mChainedOp) {
    mChainedOp->reset(); // reset contents, break ownership loops held trough callbacks
  }
  mChainedOp.reset(); // release object early
}


void Operation::setTimeout(MLMicroSeconds aTimeout)
{
  mTimeout = aTimeout;
}


void Operation::setInitiationDelay(MLMicroSeconds aInitiationDelay, bool aFromLastInitiation)
{
  mInitiationDelay = aInitiationDelay;
  mFromLastInitiation = aFromLastInitiation;
  mInitiatesNotBefore = 0;
}


void Operation::setInitiatesAt(MLMicroSeconds aInitiatesAt)
{
  mInitiatesNotBefore = aInitiatesAt;
}


void Operation::setChainedOperation(OperationPtr aChainedOp)
{
  mChainedOp = aChainedOp;
}


void Operation::setCompletionCallback(StatusCB aCompletionCB)
{
  mCompletionCB = aCompletionCB;
}



// check if can be initiated
bool Operation::canInitiate(MLMicroSeconds aLastInitiation)
{
  MLMicroSeconds now = MainLoop::now();
  if (mInitiationDelay>0) {
    DBGFOCUSLOG("Operation %p: requesting initiation delay of %lld uS", this, mInitiationDelay);
    if (mInitiatesNotBefore==0) {
      // first time queried, start delay now
      mInitiatesNotBefore = (mFromLastInitiation ? aLastInitiation : now)+mInitiationDelay;
      DBGFOCUSLOG("- now is %lld, will initiate at %lld uS", now, mInitiatesNotBefore);
      mInitiationDelay = 0; // consumed
    }
  }
  // can be initiated when delay is over
  return mInitiatesNotBefore==0 || mInitiatesNotBefore<now;
}



// call to initiate operation
bool Operation::initiate()
{
  mInitiated = true;
  if (mTimeout!=0)
    mTimesOutAt = MainLoop::now()+mTimeout;
  else
    mTimesOutAt = 0;
  return mInitiated;
}


// check if already initiated
bool Operation::isInitiated()
{
  return mInitiated;
}


// check if already aborted
bool Operation::isAborted()
{
  return mAborted;
}


// call to check if operation has completed
bool Operation::hasCompleted()
{
  return true;
}


bool Operation::hasTimedOutAt(MLMicroSeconds aRefTime)
{
  if (mTimesOutAt==0) return false;
  return aRefTime>=mTimesOutAt;
}



OperationPtr Operation::finalize()
{
  OperationPtr keepMeAlive(this); // make sure this object lives until routine terminates
  // callback (only if not chaining)
  if (mCompletionCB) {
    StatusCB cb = mCompletionCB;
    mCompletionCB = NoOP; // call once only (or never when chaining)
    if (!mChainedOp) {
      // no error and not chained (yet, callback might still set chained op) - callback now
      cb(ErrorPtr());
    }
  }
  OperationPtr next = mChainedOp;
  mChainedOp.reset(); // make sure it is not chained twice
  return next;
}


void Operation::abortOperation(ErrorPtr aError)
{
  OperationPtr keepMeAlive(this); // make sure this object lives until routine terminates
  if (!mAborted) {
    mAborted = true;
    if (mCompletionCB) {
      StatusCB cb = mCompletionCB;
      mCompletionCB = NoOP; // call once only
      if (aError) cb(aError); // prevent callback if no error is given
    }
  }
  // abort chained operation as well
  if (mChainedOp) {
    OperationPtr op = mChainedOp;
    mChainedOp = NULL;
    op->abortOperation(aError);
  }
  // make sure no links are held
  reset();
}




// MARK: - OperationQueue


#define QUEUE_RECHECK_INTERVAL (30*MilliSecond)
#define QUEUE_RECHECK_TOLERANCE (15*MilliSecond)

// create operation queue into specified mainloop
OperationQueue::OperationQueue(MainLoop &aMainLoop) :
  mMainLoop(aMainLoop),
  mIsProcessingQueue(false),
  mLastInitiation(Never)
{
  // register with mainloop
  mMainLoop.executeTicketOnce(mRecheckTicket, boost::bind(&OperationQueue::queueRecheck, this, _1));
}


// destructor
OperationQueue::~OperationQueue()
{
  terminate();
}


void OperationQueue::terminate()
{
  // unregister from mainloop
  mMainLoop.cancelExecutionTicket(mRecheckTicket);
  // silently reset all operations
  abortOperations();
}



// queue a new operation
void OperationQueue::queueOperation(OperationPtr aOperation)
{
  mOperationQueue.push_back(aOperation);
}



void OperationQueue::queueRecheck(MLTimer &aTimer)
{
  processOneOperation();
  mMainLoop.retriggerTimer(aTimer, QUEUE_RECHECK_INTERVAL, QUEUE_RECHECK_TOLERANCE);
}



// process all pending operations now
void OperationQueue::processOperations()
{
	bool completed = true;
	do {
		completed = processOneOperation();
	} while (!completed);
}



bool OperationQueue::processOneOperation()
{
  if (mIsProcessingQueue) {
    // already processing, avoid recursion
    return true;
  }
  OperationQueuePtr keepMeAlive(this); // make sure this object lives until routine terminates
  mIsProcessingQueue = true; // protect agains recursion
  bool pleaseCallAgainSoon = false; // assume nothing to do
  if (!mOperationQueue.empty()) {
    MLMicroSeconds now = MainLoop::now();
    OperationList::iterator pos;
    #if FOCUSLOGGING
    // Statistics
    if (FOCUSLOGENABLED) {
      int numTimedOut = 0;
      int numInitiated = 0;
      int numCompleted = 0;
      for (pos = mOperationQueue.begin(); pos!=mOperationQueue.end(); ++pos) {
        OperationPtr op = *pos;
        if (op->hasTimedOutAt(now)) numTimedOut++;
        if (op->isInitiated()) {
          numInitiated++;
          if (op->hasCompleted()) numCompleted++;
        }
      }
      FOCUSLOG("OperationQueue stats: size=%lu, pending: initiated=%d, completed=%d, timedout=%d", mOperationQueue.size(), numInitiated, numCompleted, numTimedOut);
    }
    #endif
    // (re)start with first element in queue
    for (pos = mOperationQueue.begin(); pos!=mOperationQueue.end(); ++pos) {
      OperationPtr op = *pos;
      if (op->hasTimedOutAt(now)) {
        // remove from list
        mOperationQueue.erase(pos);
        // abort with timeout
        op->abortOperation(ErrorPtr(new OQError(OQError::TimedOut)));
        // restart with start of (modified) queue
        pleaseCallAgainSoon = true;
        break;
      }
      if (!op->isInitiated()) {
        // try to initiate now
        if (op->canInitiate(mLastInitiation)) {
          if (op->initiate()) {
            mLastInitiation = now;
          }
        }
        else {
          // cannot initiate this one now, check if we can continue with others
          if (op->mInSequence) {
            // this op needs to be initiated before others can be checked
            pleaseCallAgainSoon = false; // as we can't initate right now, let mainloop cycle pass
            break;
          }
        }
      }
      if (op->isAborted()) {
        // just remove from list
        mOperationQueue.erase(pos);
        // restart with start of (modified) queue
        pleaseCallAgainSoon = true;
        break;
      }
      if (op->isInitiated()) {
        // initiated, check if already completed
        if (op->hasCompleted()) {
          // operation has completed
          // - remove from list
          OperationList::iterator nextPos = mOperationQueue.erase(pos);
          // - finalize. This might push new operations in front or back of the queue
          OperationPtr nextOp = op->finalize();
          if (nextOp) {
            mOperationQueue.insert(nextPos, nextOp);
            // immediately try to initiate already
            // Note: this is important to provide indivisible sequences of chained ops
            //   especially in context of send/receive. If a chained receive op was not initiated
            //   here, mainloop I/O events could cause data to be delivered before the chained
            //   receive op had a chance to get initiated and would miss the data.
            nextOp->initiate();
          }
          // restart with start of (modified) queue
          pleaseCallAgainSoon = true;
          break;
        }
        else {
          // operation has not yet completed
          if (op->mInSequence) {
            // this op needs to be complete before others can be checked
            pleaseCallAgainSoon = false; // as we can't initate right now, let mainloop cycle pass
            break;
          }
        }
      }
    } // for all ops in queue
  } // queue not empty
  mIsProcessingQueue = false;
  // if not everything is processed we'd like to process, return false, causing main loop to call us ASAP again
  return !pleaseCallAgainSoon;
};



// abort all pending operations
void OperationQueue::abortOperations(ErrorPtr aError)
{
  OperationQueuePtr keepMeAlive(this); // make sure this object lives until routine terminates
  for (OperationList::iterator pos = mOperationQueue.begin(); pos!=mOperationQueue.end(); ++pos) {
    (*pos)->abortOperation(aError);
  }
  // empty queue
  mOperationQueue.clear();
}



