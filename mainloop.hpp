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

#ifndef __p44utils__mainloop__
#define __p44utils__mainloop__

#include "p44utils_common.hpp"

#include <poll.h>
#include <pthread.h>

// if set to non-zero, mainloop will have some code to record statistics
#define MAINLOOP_STATISTICS 1

using namespace std;

namespace p44 {

  class MainLoop;
  class ChildThreadWrapper;

  typedef boost::intrusive_ptr<MainLoop> MainLoopPtr;
  typedef boost::intrusive_ptr<ChildThreadWrapper> ChildThreadWrapperPtr;

  // Mainloop timing unit
  typedef long long MLMicroSeconds; ///< Mainloop time in microseconds
  const MLMicroSeconds Never = 0;
  const MLMicroSeconds Infinite = -1;
  const MLMicroSeconds MicroSecond = 1;
  const MLMicroSeconds MilliSecond = 1000;
  const MLMicroSeconds Second = 1000*MilliSecond;
  const MLMicroSeconds Minute = 60*Second;
  const MLMicroSeconds Hour = 60*Minute;
  const MLMicroSeconds Day = 24*Hour;


  /// subthread/maintthread communication signals (sent via pipe)
  enum {
    threadSignalNone,
    threadSignalCompleted, ///< sent to parent when child thread terminates
    threadSignalFailedToStart, ///< sent to parent when child thread could not start
    threadSignalCancelled, ///< sent to parent when child thread was cancelled
    threadSignalUserSignal ///< first user-specified signal
  };
  typedef uint8_t ThreadSignals;


  typedef long MLTicketNo; ///< Mainloop timer ticket number
  class MLTimer;


  /// @name Mainloop callbacks
  /// @{

  /// Generic handler without any arguments
  typedef boost::function<void ()> SimpleCB;

  /// Generic handler or returning a status (ok or error)
  typedef boost::function<void (ErrorPtr aError)> StatusCB;

  /// Handler for timed processing
  typedef boost::function<void (MLTimer &aTimer, MLMicroSeconds aNow)> TimerCB;

  /// Handler for getting signalled when child process terminates
  /// @param aPid the PID of the process that has terminated
  /// @param aStatus the exit status of the process that has terminated
  typedef boost::function<void (pid_t aPid, int aStatus)> WaitCB;

  /// Handler called when fork_and_execve() or fork_and_system() terminate
  /// @param aOutputString the stdout output of the executed command
  typedef boost::function<void (ErrorPtr aError, const string &aOutputString)> ExecCB;

  /// I/O callback
  /// @param aFD the file descriptor that was signalled and has caused this call
  /// @param aPollFlags the poll flags describing the reason for the callback
  /// @return should true if callback really handled some I/O, false if it only checked flags and found nothing to do
  typedef boost::function<bool (int aFD, int aPollFlags)> IOPollCB;

  /// thread routine, will be called on a separate thread
  /// @param aThread the object that wraps the thread and allows sending signals to the parent thread
  ///   Use this pointer to call signalParentThread() on
  /// @note when this routine exits, a threadSignalCompleted will be sent to the parent thread
  typedef boost::function<void (ChildThreadWrapper &aThread)> ThreadRoutine;

  /// thread signal handler, will be called from main loop of parent thread when child thread uses signalParentThread()
  /// @param aChildThread the ChildThreadWrapper object which sent the signal
  /// @param aSignalCode the signal received from the child thread
  typedef boost::function<void (ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)> ThreadSignalHandler;

  /// @}


  /// subprocess execution error
  class ExecError : public Error
  {
  public:
    typedef int ErrorCodes;

    static const char *domain() { return "ExecError"; };
    virtual const char *getErrorDomain() const { return ExecError::domain(); };
    ExecError(int aExitStatus) : Error(ErrorCode(aExitStatus)) {};
    static ErrorPtr exitStatus(int aExitStatus, const char *aContextMessage = NULL);
  };


  class MainLoop;

  class FdStringCollector;
  typedef boost::intrusive_ptr<FdStringCollector> FdStringCollectorPtr;



  class MLTimer P44_FINAL {
    friend class MainLoop;
    MLTicketNo ticketNo;
    MLMicroSeconds executionTime;
    MLMicroSeconds tolerance;
    TimerCB callback;
    bool reinsert; // if set after running a callback, the timer was re-triggered and must be re-inserted into the timer queue
  public:
    MLTicketNo getTicket() { return ticketNo; };
  };


  class MLTicket
  {
    MLTicketNo ticketNo;

    MLTicket(MLTicket &aTicket); ///< private copy constructor, must not be used

  public:
    MLTicket();
    ~MLTicket();

    // conversion operator, get as MLTicket (number only)
    operator MLTicketNo() const;

    // get as bool to check if ticket is running
    operator bool() const;

    // assign ticket number (cancels previous ticket, if any)
    MLTicketNo operator= (MLTicketNo aTicketNo);

    // cancel current ticket
    void cancel();

    /// reschedule existing execution request
    /// @param aDelay delay from now when to reschedule execution (approximately)
    /// @param aTolerance how precise the timer should be, 0=as precise as possible (for timer coalescing)
    /// @return true if the execution specified with aTicketNo was still pending and could be rescheduled
    bool reschedule(MLMicroSeconds aDelay, MLMicroSeconds aTolerance = 0);

    /// reschedule existing execution request
    /// @param aExecutionTime to when to reschedule execution (approximately), in now() timescale
    /// @param aTolerance how precise the timer should be, 0=as precise as possible (for timer coalescing)
    /// @return true if the execution specified with aTicketNo was still pending and could be rescheduled
    bool rescheduleAt(MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance = 0);

    /// have handler called from the mainloop once with an optional delay from now.
    /// If ticket was already active, it will be cancelled before
    /// @param aTimerCallback the functor to be called when timer fires
    /// @param aExecutionTime when to execute (approximately), in now() timescale
    /// @param aTolerance how precise the timer should be, default=0=as precise as possible (for timer coalescing)
    void executeOnceAt(TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance = 0);

    /// have handler called from the mainloop once with an optional delay from now
    /// If ticket was already active, it will be cancelled before
    /// @param aTimerCallback the functor to be called when timer fires
    /// @param aDelay delay from now when to execute (approximately)
    /// @param aTolerance how precise the timer should be, default=0=as precise as possible (for timer coalescing)
    void executeOnce(TimerCB aTimerCallback, MLMicroSeconds aDelay = 0, MLMicroSeconds aTolerance = 0);

  };


  /// A main loop for a thread
  class MainLoop : public P44Obj
  {
    friend class ChildThreadWrapper;
    friend class MLTicket;

    // clean up handlers
    typedef std::list<SimpleCB> CleanupHandlersList;
    CleanupHandlersList cleanupHandlers;

    // timers
    typedef std::list<MLTimer> TimerList;
    TimerList timers;
    bool timersChanged;
    MLTicketNo ticketNo;

    // wait handlers
    typedef struct {
      pid_t pid;
      WaitCB callback;
    } WaitHandler;
    typedef std::map<pid_t, WaitHandler> WaitHandlerMap;
    WaitHandlerMap waitHandlers;

    // IO poll handlers
    typedef struct {
      int monitoredFD;
      int pollFlags;
      IOPollCB pollHandler;
    } IOPollHandler;
    typedef std::map<int, IOPollHandler> IOPollHandlerMap;
    IOPollHandlerMap ioPollHandlers;

    // Configuration
    MLMicroSeconds maxSleep; ///< how long to sleep maximally per mainloop cycle, can be set to Infinite to allow unlimited sleep
    MLMicroSeconds throttleSleep; ///< how long to sleep after a mainloop cycle that had no chance to sleep at all. Can be 0.
    MLMicroSeconds maxRun; ///< how long to run maximally without any interruption. Note that this cannot limit the runtime for a single handler.
    MLMicroSeconds maxCoalescing; ///< how much to shift timer execution points maximally (always within limits given by timer's tolerance) to coalesce executions
    MLMicroSeconds waitCheckInterval; ///< max interval between checks for termination of running child processes

  protected:

    bool hasStarted;
    bool terminated;
    int exitCode;

    #if MAINLOOP_STATISTICS
    MLMicroSeconds statisticsStartTime;
    size_t maxTimers;
    MLMicroSeconds ioHandlerTime;
    MLMicroSeconds timedHandlerTime;
    MLMicroSeconds maxTimerExecutionDelay;
    long timesTimersRanToLong;
    long timesThrottlingApplied;
    MLMicroSeconds waitHandlerTime;
    MLMicroSeconds threadSignalHandlerTime;
    #endif


    // protected constructor
    MainLoop();

  public:

    /// returns or creates the current thread's mainloop
    /// @return the mainloop for this thread
    static MainLoop &currentMainLoop();

    /// returns the current microsecond in "Mainloop" time (monotonic as long as app runs, but not necessarily anchored with real time)
    /// @return mainloop time in microseconds
    static MLMicroSeconds now();

    /// returns the Unix epoch time in mainloop time scaling (microseconds)
    /// @return unix epoch time, in microseconds
    static MLMicroSeconds unixtime();

    /// convert a mainloop timestamp to unix epoch time
    /// @param aMLTime a mainloop timestamp in MLMicroSeconds
    /// @return Unix epoch time (in microseconds)
    static MLMicroSeconds mainLoopTimeToUnixTime(MLMicroSeconds aMLTime);

    /// convert a unix epoch time to mainloop timestamp
    /// @param aUnixTime Unix epoch time (in microseconds)
    /// @return mainloop timestamp in MLMicroSeconds
    static MLMicroSeconds unixTimeToMainLoopTime(const MLMicroSeconds aUnixTime);

    /// sleeps for given number of microseconds
    static void sleep(MLMicroSeconds aSleepTime);


    /// @name register timed handlers (fired at specified time)
    /// @{

    /// have handler called from the mainloop once with an optional delay from now
    /// @param aTicket this ticket will be cancelled if active beforehand. On exit, this contains the new ticket
    /// @param aTimerCallback the functor to be called when timer fires
    /// @param aExecutionTime when to execute (approximately), in now() timescale
    /// @param aTolerance how precise the timer should be, default=0=as precise as possible (for timer coalescing)
    void executeTicketOnceAt(MLTicket &aTicket, TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance = 0);

    /// have handler called from the mainloop once with an optional delay from now
    /// @param aTicketNo this ticket will be cancelled if active beforehand. On exit, this contains the new ticket
    /// @param aTimerCallback the functor to be called when timer fires
    /// @param aDelay delay from now when to execute (approximately)
    /// @param aTolerance how precise the timer should be, default=0=as precise as possible (for timer coalescing)
    void executeTicketOnce(MLTicket &aTicketNo, TimerCB aTimerCallback, MLMicroSeconds aDelay = 0, MLMicroSeconds aTolerance = 0);


    /// execute something on the mainloop without delay, usually to unwind call stack in long chains of operations
    /// @note: this is the only call we allow to start w/o a ticket. It still can go wrong if the object which calls
    ///   it immediately gets destroyed *before* the mainloop executes the callback, but probability is low.
    /// @param aTimerCallback the functor to be called from mainloop
    void executeNow(TimerCB aTimerCallback);

    /// cancel pending execution by ticket number
    /// @param aTicket ticket of pending execution to cancel. Will be reset on return
    void cancelExecutionTicket(MLTicket &aTicket); // use ticket.cancel() instead




    /// special values for retriggerTimer() aSkip parameter
    enum {
      from_now_if_late = -1, ///< automatically use from_now when we're already too late to trigger relative to last execution
      from_now = -2, ///< reschedule next trigger time at aInterval from now (rather than from pervious execution time)
      absolute = -3 ///< treat aInterval as absolute MainLoop::now() time when to reschedule. If this is in the past, reschedule ASAP
    };

    /// re-arm timer to fire again after a given interval relative to the time of the currently scheduled (or being executed right now)
    /// ticket.
    /// @note This is indended to be called exclusively from TimerCB callbacks, in particular to implement periodic timer callbacks.
    /// @param aTimer the timer handler as passed to TimerCB
    /// @param aInterval the interval for re-triggering, relative to the scheduled execution time of the timer (which might be
    ///   in the past, due to time spent for code execution between the scheduled time, the callback and the call to this method)
    /// @param aTolerance how precise the timer should be, default=0=as precise as possible (for timer coalescing)
    /// @param aSkip if set to from_now_if_late (default) timer is recheduled from current time into the future if we are too late
    ///   to schedule the interval relative to the last execution time of the timer.
    ///   If set to from_now, timer is always rescheduled from current time.
    ///   If set to absolute, aInterval is taken as the absolute (MainLoop::now()) time when to fire the timer next. If the
    ///   specified point in time has already passed, the timer is fired ASAP again.
    ///   Otherwise, if >=0, aSKIP determines how many extra aInterval periods can be inserted maximally to reach an execution point in the future.
    /// @return returns the number of aIntervals that were left out to reach a time in the future.
    ///   Return value < 0 means that the timer could not be set to re-trigger for the future within the aMaxSkip limits.
    ///   In that case, the timer execution time still was advanced by aInterval*(aMaxSkip+1). This allows for repeatedly
    ///   calling retriggerTimer to find an execution point in the future.
    ///   When aSkip is set to from_now_if_late, result is 0 when the next interval could be scheduled without delaying, and 1 if
    ///   the interval had to be added from current time rather than last timer execution.
    ///   When aSkip is set to absolute, result is 0 when the specified absolute time is in the future, and 1 if
    ///   the time is in the past and the timer will fire ASAP.
    /// @note This is different from rescheduleExecutionTicket as the retrigger time is relative to the previous execution
    ///   time of the ticket.
    int retriggerTimer(MLTimer &aTimer, MLMicroSeconds aInterval, MLMicroSeconds aTolerance = 0, int aSkip = MainLoop::from_now_if_late);

    /// reschedule existing execution request
    /// @param aTicketNo ticket of execution to reschedule.
    /// @param aDelay delay from now when to reschedule execution (approximately)
    /// @param aTolerance how precise the timer should be, 0=as precise as possible (for timer coalescing)
    /// @return true if the execution specified with aTicketNo was still pending and could be rescheduled
    bool rescheduleExecutionTicket(MLTicketNo aTicketNo, MLMicroSeconds aDelay, MLMicroSeconds aTolerance = 0);

    /// reschedule existing execution request
    /// @param aTicketNo ticket of execution to reschedule.
    /// @param aExecutionTime to when to reschedule execution (approximately), in now() timescale
    /// @param aTolerance how precise the timer should be, 0=as precise as possible (for timer coalescing)
    /// @return true if the execution specified with aTicketNo was still pending and could be rescheduled
    bool rescheduleExecutionTicketAt(MLTicketNo aTicketNo, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance = 0);

    /// @}


    /// @name start subprocesses and register handlers for returning subprocess status
    /// @{

    /// execute external binary or interpreter script in a separate process
    /// @param aCallback the functor to be called when execution is done (failed to start or completed)
    /// @param aPath the path to the binary or script
    /// @param aArgv a NULL terminated array of arguments, first should be program name
    /// @param aEnvp a NULL terminated array of environment variables, or NULL to let child inherit parent's environment
    /// @param aPipeBackStdOut if true, stdout of the child is collected via a pipe by the parent and passed back in aCallBack
    /// @return the child's PID (can be used to send signals to it), or -1 if fork fails
    pid_t fork_and_execve(ExecCB aCallback, const char *aPath, char *const aArgv[], char *const aEnvp[] = NULL, bool aPipeBackStdOut = false);

    /// execute command line in external shell
    /// @param aCallback the functor to be called when execution is done (failed to start or completed)
    /// @param aCommandLine the command line to execute
    /// @param aPipeBackStdOut if true, stdout of the child is collected via a pipe by the parent and passed back in aCallBack
    /// @return the child's PID (can be used to send signals to it), or -1 if fork fails
    pid_t fork_and_system(ExecCB aCallback, const char *aCommandLine, bool aPipeBackStdOut = false);


    /// have handler called when a specific process delivers a state change
    /// @param aCallback the functor to be called when given process delivers a state change, NULL to remove callback
    /// @param aPid the process to wait for
    void waitForPid(WaitCB aCallback, pid_t aPid);

    /// @}


    /// @name register handlers for I/O events
    /// @{

    /// register handler to be called for activity on specified file descriptor
    /// @param aFD the file descriptor to poll
    /// @param aPollFlags POLLxxx flags to specify events we want a callback for
    /// @param aPollEventHandler the functor to be called when poll() reports an event for one of the flags set in aPollFlags
    void registerPollHandler(int aFD, int aPollFlags, IOPollCB aPollEventHandler);

    /// change the poll flags for an already registered handler
    /// @param aFD the file descriptor
    /// @param aSetPollFlags POLLxxx flags to be enabled for this file descriptor
    /// @param aClearPollFlags POLLxxx flags to be disabled for this file descriptor
    void changePollFlags(int aFD, int aSetPollFlags, int aClearPollFlags=-1);

    /// unregister poll handlers for this file descriptor
    /// @param aFD the file descriptor
    void unregisterPollHandler(int aFD);
    
    /// @}


    /// @name run handler in separate thread
    /// @{

    /// execute handler in a separate thread
    /// @param aThreadRoutine the routine to be executed in a separate thread
    /// @param aThreadSignalHandler will be called from main loop of parent thread when child thread uses signalParentThread()
    /// @return wrapper object for child thread.
    ChildThreadWrapperPtr executeInThread(ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler);

    /// @}


    /// @name mainloop phases as separate calls, allows integrating this mainloop into another mainloop
    /// @{

    /// Start running the main loop
    /// @param aRestart if set, the mainloop will start again even if terminate() was called before
    void startupMainLoop(bool aRestart = false);

    /// Run one mainloop cycle
    /// @return true if cycle had the chance to sleep. This can be used to sleep a bit extra between calls to throttle CPU usage
    bool mainLoopCycle();

    /// Finalize running the main loop
    /// @return returns a exit code
    int finalizeMainLoop();

    /// @}


    /// terminate the mainloop
    /// @param aExitCode the code to return from run()
    void terminate(int aExitCode);

    /// ask if mainloop has already been asked to terminate
    /// @return returns true if terminate() has been called before
    bool isTerminated() { return terminated; };

    /// ask if mainloop is normally running
    /// @return will return false as soon as mainloop has been requested to terminate, or before run() has been called
    bool isRunning() { return hasStarted && !terminated; };

    /// register a cleanup handler, which will be called when the main loop has terminated
    /// @param aCleanupHandler the routine to be called
    /// @note the code in cleanup handlers cannot use mailoop services any more, because at time of calling the
    ///   mainloop has already terminated running
    void registerCleanupHandler(SimpleCB aCleanupHandler);

    /// run the mainloop until it terminates.
    /// @param aRestart if set, the mainloop will start again even if terminate() was called before
    /// @note this essentially calls startupMainLoop(), mainLoopCycle() and finalizeMainLoop(). Use these
    ///   separately to integrate the mainloop into a higher level mainloop
    /// @return returns a exit code
    int run(bool aRestart = false);

    /// description (shows some mainloop key numbers)
    string description();

    /// reset statistics
    void statistics_reset();


  private:

    // we don't want timers to be used without a MLTicket taking care of cancelling when the called object is deleted
    MLTicketNo executeOnceAt(TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance);
    MLTicketNo executeOnce(TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance);
    bool cancelExecutionTicket(MLTicketNo aTicketNo);

    MLMicroSeconds checkTimers(MLMicroSeconds aTimeout);
    void scheduleTimer(MLTimer &aTimer);
    bool checkWait();
    bool handleIOPoll(MLMicroSeconds aTimeout);

    void execChildTerminated(ExecCB aCallback, FdStringCollectorPtr aAnswerCollector, pid_t aPid, int aStatus);
    void childAnswerCollected(ExecCB aCallback, FdStringCollectorPtr aAnswerCollector, ErrorPtr aError);
    void IOPollHandlerForFd(int aFD, IOPollHandler &h);

  };



  class ChildThreadWrapper : public P44Obj
  {
    typedef P44Obj inherited;

    pthread_t pthread; ///< the pthread
    bool threadRunning; ///< set if thread is active

    MainLoop &parentThreadMainLoop; ///< the parent mainloop which created this thread
    int childSignalFd; ///< the pipe used to transmit signals from the child thread
    int parentSignalFd; ///< the pipe monitored by parentThreadMainLoop to get signals from child

    ThreadSignalHandler parentSignalHandler; ///< the handler to call to deliver signals to the main thread
    ThreadRoutine threadRoutine; ///< the actual thread routine to run

    ChildThreadWrapperPtr selfRef;

    bool terminationPending; ///< set if termination has been requested by requestTermination()

    MainLoop *myMainLoopP; ///< the (optional) mainloop of this thread


  public:

    /// constructor
    ChildThreadWrapper(MainLoop &aParentThreadMainLoop, ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler);

    /// destructor
    virtual ~ChildThreadWrapper();

    /// @name methods to call from child thread
    /// @{

    /// check if termination is requested
    bool shouldTerminate() { return terminationPending; }

    /// signal parent thread
    /// @param aSignalCode a signal code to be sent to the parent thread
    void signalParentThread(ThreadSignals aSignalCode);

    /// returns (and creates, if not yet existing) the thread's mainloop
    /// @note MUST be called from the thread itself to get the correct mainloop!
    MainLoop &threadMainLoop();

    /// confirm termination
    void confirmTerminated();

    /// @}


    /// @name methods to call from parent thread
    /// @{

    /// request termination
    /// @note this does not actually cancel thread execution, but relies on the thread routine to check
    ///    shouldTerminate() and finish running by itself. If a mainloop was installed using
    ///    threadMainloop(), it will be requested to terminate with exit code 0
    void terminate();

    /// cancel execution and wait for cancellation to complete
    void cancel();

    /// @}

    /// method called from thread_start_function from this child thread
    void *startFunction();

  private:

    bool signalPipeHandler(int aPollFlags);
    void finalizeThreadExecution();

  };


} // namespace p44

#endif /* defined(__p44utils__mainloop__) */
