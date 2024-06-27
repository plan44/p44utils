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

#ifndef __p44utils__operationqueue__
#define __p44utils__operationqueue__

#include "p44utils_main.hpp"


using namespace std;

namespace p44 {


  class OQError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Aborted,
      TimedOut,
      numErrorCodes
    } ErrorCodes;
    
    static const char *domain() { return "OperationQueue"; };
    virtual const char *getErrorDomain() const P44_OVERRIDE { return OQError::domain(); };
    OQError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "Aborted",
      "TimedOut",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  class Operation;
  class OperationQueue;


  /// Operation
  typedef boost::intrusive_ptr<Operation> OperationPtr;
  class Operation : public P44Obj
  {
  protected:

    bool mInitiated;
    bool mAborted;
    MLMicroSeconds mTimeout; ///< timeout
    MLMicroSeconds mTimesOutAt; ///< absolute time for timeout
    MLMicroSeconds mInitiationDelay; ///< how much to delay initiation after first attempt to initiate (or after last initiation, see below)
    bool mFromLastInitiation; ///< if set, initiationDelay counts from last initiation happened on this queue
    MLMicroSeconds mInitiatesNotBefore; ///< absolute time for earliest initiation

    StatusCB mCompletionCB; ///< completion callback
    OperationPtr mChainedOp; ///< operation to insert once this operation has finalized


  public:

    /// if this flag is set, no operation queued after this operation will execute
    bool mInSequence;

    /// constructor
    Operation();

    virtual ~Operation();

    /// reset operation (clear callbacks to break ownership loops)
    /// @note no callbacks are called
    void reset();

    /// set completion callback
    /// @param aCompletionCB will be called when operation completes or fails
    void setCompletionCallback(StatusCB aCompletionCB);

    /// chain another operation
    /// @note after this operation has finalized, the specified operation will be inserted
    ///   into the queue in place of this operation
    /// @note when an operation is chained, the completion callback will not be called.
    ///   Still it makes sense to set it in case the original operation is aborted.
    void setChainedOperation(OperationPtr aChainedOp);

    /// set delay for initiation (after first attempt to initiate)
    /// @param aInitiationDelay how long to delay initiation of the operation minimally
    /// @param aFromLastInitiation if set, delay is measured from last initiation on this queue
    void setInitiationDelay(MLMicroSeconds aInitiationDelay, bool aFromLastInitiation = false);

    /// set earliest time to execute
    /// @param aInitiatesAt when to initiate the operation earliest
    void setInitiatesAt(MLMicroSeconds aInitiatesAt);

    /// set timeout (from initiation)
    /// @param aTimeout after initiation, how long to wait for completion without timeout. Can be Never
    void setTimeout(MLMicroSeconds aTimeout);

    /// check if already initiated
    bool isInitiated();

    /// check if already aborted
    bool isAborted();

    /// called to check if operation has timed out
    bool hasTimedOutAt(MLMicroSeconds aRefTime = MainLoop::now());

    /// Methods to override by concrete subclasses of Operation
    /// @{

    /// check if can be initiated
    /// @param aLastInitiation time when last operation was initiated on the queue (or Never)
    /// @return false if cannot be initiated now and must be retried
    /// @note base class implementation implements initiation delay here.
    ///   Derived classes might check other criteria in addition
    virtual bool canInitiate(MLMicroSeconds aLastInitiation);

    /// call to initiate operation
    /// @note base class starts timeout when initiation has occurred
    /// @note must only be called after canInitiate() returns true!
    /// @return false if could not be initiated despite canInitiate() having returned true
    ///   (canInitiate() and initiate() will be retried in this case)
    virtual bool initiate();
    
    /// call to check if operation has completed (after being initiated)
    /// @return true if completed
    /// @note base class alwayys returns true.
    ///   Derived classes can signal operation in process by returning true
    virtual bool hasCompleted();

    /// call to execute after completion, can chain another operation by returning it
    /// @note base class calls callback if one was set by setCompletionCallback(),
    ///   and then chains operation set by setChainedOperation();
    virtual OperationPtr finalize();

    /// abort operation
    /// @param aError if set, abortion might be reported back by callbacks. If NULL, no callback will happen
    /// @note base class calls callback if one was set by setCompletionCallback()
    virtual void abortOperation(ErrorPtr aError);

    /// @}

  };



  /// Operation queue
  typedef boost::intrusive_ptr<OperationQueue> OperationQueuePtr;
  class OperationQueue : public P44LoggingObj
  {
    MainLoop &mMainLoop;
    bool mIsProcessingQueue; ///< set when queue is currently processing
    MLTicket mRecheckTicket; ///< regular checking of the queue
    MLMicroSeconds mLastInitiation; ///< time when last initiation was fired

  protected:

    typedef list<OperationPtr> OperationList;
    OperationList mOperationQueue;

  public:

    /// create operation queue linked into specified mainloop
    OperationQueue(MainLoop &aMainLoop = MainLoop::currentMainLoop());

    /// destructor
    virtual ~OperationQueue();

    /// terminate
    virtual void terminate();

    /// queue a new operation
    /// @param aOperation the operation to queue
    void queueOperation(OperationPtr aOperation);

    /// process immediately pending operations now
    void processOperations();

    /// abort all pending operations
    /// @param aError if set, this will be passed on to each operation, which might cause them to execute call backs.
    void abortOperations(ErrorPtr aError = ErrorPtr());
    
  private:

    /// periodic re-check of operation queue
    void queueRecheck(MLTimer &aTimer);

    /// process at most one operation
    /// @return true if operations processed for now, i.e. no need to call again immediately
    ///   false if processOperations() should be called ASAP again (with no or little delay, if possible)
    bool processOneOperation();

  };

} // namespace p44


#endif /* defined(__p44utils__operationqueue__) */
