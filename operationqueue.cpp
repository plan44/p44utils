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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "operationqueue.hpp"

using namespace p44;


Operation::Operation() :
  completionCB(NULL),
  initiated(false),
  aborted(false),
  timeout(0), // no timeout
  timesOutAt(0), // no timeout time set
  initiationDelay(0), // no initiation delay
  initiatesNotBefore(0), // no initiation time
  inSequence(true) // by default, execute in sequence
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
  completionCB = NULL;
  // release chained object
  if (chainedOp) {
    chainedOp->reset(); // reset contents, break ownership loops held trough callbacks
  }
  chainedOp.reset(); // release object early
}


void Operation::setTimeout(MLMicroSeconds aTimeout)
{
  timeout = aTimeout;
}


void Operation::setInitiationDelay(MLMicroSeconds aInitiationDelay)
{
  initiationDelay = aInitiationDelay;
  initiatesNotBefore = 0;
}


void Operation::setInitiatesAt(MLMicroSeconds aInitiatesAt)
{
  initiatesNotBefore = aInitiatesAt;
}


void Operation::setChainedOperation(OperationPtr aChainedOp)
{
  chainedOp = aChainedOp;
}


void Operation::setCompletionCallback(StatusCB aCompletionCB)
{
  completionCB = aCompletionCB;
}



// check if can be initiated
bool Operation::canInitiate()
{
  MLMicroSeconds now = MainLoop::now();
  if (initiationDelay>0) {
    DBGFOCUSLOG("Operation 0x%pX: requesting initiation delay of %lld uS", this, initiationDelay);
    if (initiatesNotBefore==0) {
      // first time queried, start delay now
      initiatesNotBefore = now+initiationDelay;
      DBGFOCUSLOG("- now is %lld, will initiate at %lld uS", now, initiatesNotBefore);
      initiationDelay = 0; // consumed
    }
  }
  // can be initiated when delay is over
  return initiatesNotBefore==0 || initiatesNotBefore<now;
}



// call to initiate operation
bool Operation::initiate()
{
  if (!canInitiate()) return false;
  initiated = true;
  if (timeout!=0)
    timesOutAt = MainLoop::now()+timeout;
  else
    timesOutAt = 0;
  return initiated;
}


// check if already initiated
bool Operation::isInitiated()
{
  return initiated;
}


// check if already aborted
bool Operation::isAborted()
{
  return aborted;
}


// call to check if operation has completed
bool Operation::hasCompleted()
{
  return true;
}


bool Operation::hasTimedOutAt(MLMicroSeconds aRefTime)
{
  if (timesOutAt==0) return false;
  return aRefTime>=timesOutAt;
}



OperationPtr Operation::finalize()
{
  OperationPtr keepMeAlive(this); // make sure this object lives until routine terminates
  // callback (only if not chaining)
  if (completionCB) {
    StatusCB cb = completionCB;
    completionCB = NULL; // call once only (or never when chaining)
    if (!chainedOp) {
      // no error and not chained (yet, callback might still set chained op) - callback now
      cb(ErrorPtr());
    }
  }
  OperationPtr next = chainedOp;
  chainedOp.reset(); // make sure it is not chained twice
  return next;
}


void Operation::abortOperation(ErrorPtr aError)
{
  OperationPtr keepMeAlive(this); // make sure this object lives until routine terminates
  if (!aborted) {
    aborted = true;
    if (completionCB) {
      StatusCB cb = completionCB;
      completionCB = NULL; // call once only
      if (aError) cb(aError); // prevent callback if no error is given
    }
  }
  // abort chained operation as well
  if (chainedOp) {
    OperationPtr op = chainedOp;
    chainedOp = NULL;
    op->abortOperation(aError);
  }
  // make sure no links are held
  reset();
}




// MARK: ===== OperationQueue


#define QUEUE_RECHECK_INTERVAL (30*MilliSecond)
#define QUEUE_RECHECK_TOLERANCE (15*MilliSecond)

// create operation queue into specified mainloop
OperationQueue::OperationQueue(MainLoop &aMainLoop) :
  mainLoop(aMainLoop),
  processingQueue(false)
{
  // register with mainloop
  recheckTicket = mainLoop.executeOnce(boost::bind(&OperationQueue::queueRecheck, this, _1));
}


// destructor
OperationQueue::~OperationQueue()
{
  terminate();
}


void OperationQueue::terminate()
{
  // unregister from mainloop
  mainLoop.cancelExecutionTicket(recheckTicket);
  // silently reset all operations
  abortOperations();
}



// queue a new operation
void OperationQueue::queueOperation(OperationPtr aOperation)
{
  operationQueue.push_back(aOperation);
}



void OperationQueue::queueRecheck(MLTimer &aTimer)
{
  processOneOperation();
  mainLoop.retriggerTimer(aTimer, QUEUE_RECHECK_INTERVAL, QUEUE_RECHECK_TOLERANCE);
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
  if (processingQueue) {
    // already processing, avoid recursion
    return true;
  }
  OperationQueuePtr keepMeAlive(this); // make sure this object lives until routine terminates
  processingQueue = true; // protect agains recursion
  bool pleaseCallAgainSoon = false; // assume nothing to do
  if (!operationQueue.empty()) {
    MLMicroSeconds now = MainLoop::now();
    OperationList::iterator pos;
    #if FOCUSLOGGING
    // Statistics
    if (FOCUSLOGENABLED) {
      int numTimedOut = 0;
      int numInitiated = 0;
      int numCompleted = 0;
      for (pos = operationQueue.begin(); pos!=operationQueue.end(); ++pos) {
        OperationPtr op = *pos;
        if (op->hasTimedOutAt(now)) numTimedOut++;
        if (op->isInitiated()) {
          numInitiated++;
          if (op->hasCompleted()) numCompleted++;
        }
      }
      FOCUSLOG("OperationQueue stats: size=%lu, pending: initiated=%d, completed=%d, timedout=%d", operationQueue.size(), numInitiated, numCompleted, numTimedOut);
    }
    #endif
    // (re)start with first element in queue
    for (pos = operationQueue.begin(); pos!=operationQueue.end(); ++pos) {
      OperationPtr op = *pos;
      if (op->hasTimedOutAt(now)) {
        // remove from list
        operationQueue.erase(pos);
        // abort with timeout
        op->abortOperation(ErrorPtr(new OQError(OQError::TimedOut)));
        // restart with start of (modified) queue
        pleaseCallAgainSoon = true;
        break;
      }
      if (!op->isInitiated()) {
        // initiate now
        if (!op->initiate()) {
          // cannot initiate this one now, check if we can continue with others
          if (op->inSequence) {
            // this op needs to be initiated before others can be checked
            pleaseCallAgainSoon = false; // as we can't initate right now, let mainloop cycle pass
            break;
          }
        }
      }
      if (op->isAborted()) {
        // just remove from list
        operationQueue.erase(pos);
        // restart with start of (modified) queue
        pleaseCallAgainSoon = true;
        break;
      }
      if (op->isInitiated()) {
        // initiated, check if already completed
        if (op->hasCompleted()) {
          // operation has completed
          // - remove from list
          OperationList::iterator nextPos = operationQueue.erase(pos);
          // - finalize. This might push new operations in front or back of the queue
          OperationPtr nextOp = op->finalize();
          if (nextOp) {
            operationQueue.insert(nextPos, nextOp);
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
          if (op->inSequence) {
            // this op needs to be complete before others can be checked
            pleaseCallAgainSoon = false; // as we can't initate right now, let mainloop cycle pass
            break;
          }
        }
      }
    } // for all ops in queue
  } // queue not empty
  processingQueue = false;
  // if not everything is processed we'd like to process, return false, causing main loop to call us ASAP again
  return !pleaseCallAgainSoon;
};



// abort all pending operations
void OperationQueue::abortOperations(ErrorPtr aError)
{
  OperationQueuePtr keepMeAlive(this); // make sure this object lives until routine terminates
  for (OperationList::iterator pos = operationQueue.begin(); pos!=operationQueue.end(); ++pos) {
    (*pos)->abortOperation(aError);
  }
  // empty queue
  operationQueue.clear();
}



