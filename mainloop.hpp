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

#ifndef __p44utils__mainloop__
#define __p44utils__mainloop__

/* MARK: - C and C++ interfaces */

#ifdef __cplusplus
extern "C" {
#endif

long long _p44_now();
unsigned long _p44_millis();

#ifdef __cplusplus
};
#endif

#ifdef __cplusplus

// MARK: - C++ only interface

#include "p44utils_common.hpp"

#ifdef ESP_PLATFORM
  #include <sys/poll.h>
#else
  #include <poll.h>
  #include <pthread.h>
#endif

#if MAINLOOP_LIBEV_BASED
  #include <ev.h>
  #ifndef __APPLE__
    #include <sys/epoll.h>
  #endif
#endif


// if set to non-zero, mainloop will have some code to record statistics
#define MAINLOOP_STATISTICS 1

using namespace std;

namespace p44 {

  class MainLoop;
  class ChildThreadWrapper;

  typedef boost::intrusive_ptr<MainLoop> MainLoopPtr;
  typedef boost::intrusive_ptr<ChildThreadWrapper> ChildThreadWrapperPtr;

  /// subthread/maintthread communication signals (sent via pipe)
  enum {
    threadSignalNone,
    threadSignalCompleted, ///< sent to parent when child thread terminates
    threadSignalFailedToStart, ///< sent to parent when child thread could not start
    threadSignalCancelled, ///< sent to parent when child thread was cancelled
    threadSignalScheduleCall, ///< sent to parent to let it pick up execution request
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

  /// cross-thread call
  /// @param aThread the object that wraps a child thread and manages commonication with the parent thread
  typedef boost::function<ErrorPtr (ChildThreadWrapper &aWrapper)> CrossThreadCall;

  /// cross-thread call with asynchronous termination
  /// @param aThread the object that wraps a child thread and manages commonication with the parent thread
  /// @param aStatusCB the callback to invoke when the async call chain terminates
  typedef boost::function<void (ChildThreadWrapper &aThread, StatusCB aStatusCB)> CrossThreadAsyncCall;

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
    MLTicketNo mTicketNo;
    MLMicroSeconds mExecutionTime;
    MLMicroSeconds mTolerance;
    TimerCB mCallback;
    bool mReinsert; // if set after running a callback, the timer was re-triggered and must be re-inserted into the timer queue
  public:
    MLTicketNo getTicket() { return mTicketNo; };
  };


  class MLTicket
  {
    MLTicketNo mTicketNo;

    MLTicket(MLTicket &aTicket); ///< private copy constructor, must not be used

  public:
    MLTicket();
    ~MLTicket();

    /// reset the ticket number w/o cancelling the timer
    /// @note this might be needed to pass MLTickets around
    /// @return ticketNo present before defusing
    MLTicketNo defuse();

    /// conversion operator, get as MLTicketNo (number only)
    operator MLTicketNo() const;

    /// get as bool to check if ticket is running
    operator bool() const;

    /// assign ticket number (cancels previous ticket, if any)
    MLTicketNo operator= (MLTicketNo aTicketNo);

    /// cancel current ticket
    /// @return true if actually cancelled a scheduled timer
    bool cancel();

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

  class TicketObj : public P44Obj
  {
  public:
    MLTicket mTicket;
  };
  typedef boost::intrusive_ptr<TicketObj> TicketObjPtr;


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
    TimerList mTimers;
    bool mTimersChanged;
    MLTicketNo mTicketNo;

    // wait handlers
    typedef struct {
      pid_t pid;
      WaitCB callback;
    } WaitHandler;
    typedef std::map<pid_t, WaitHandler> WaitHandlerMap;
    WaitHandlerMap mWaitHandlers;

    // IO poll handlers
    #if MAINLOOP_LIBEV_BASED
    friend void libev_io_poll_handler(EV_P_ struct ev_io *, int);
    friend void libev_sleep_timer_done(EV_P_ struct ev_timer *t, int revents);
    class IOPollHandler P44_FINAL {
    public:
      struct ev_io mIoWatcher; // the actual IO watcher
      IOPollCB mPollHandler;
      #ifndef __APPLE__
      int mEpolledFd; // original FD polled via epoll in case we need to get events beyond POLLIN/POLLOUT
      #endif
      IOPollHandler();
      ~IOPollHandler();
      IOPollHandler& operator= (IOPollHandler& aReplacing);
    private:
      void deactivate();
    };
    #else
    typedef struct {
      int monitoredFD;
      int pollFlags;
      IOPollCB pollHandler;
    } IOPollHandler;
    #endif

    typedef std::map<int, IOPollHandler> IOPollHandlerMap;
    IOPollHandlerMap mIoPollHandlers;

    // Configuration
    MLMicroSeconds mMaxSleep; ///< how long to sleep maximally per mainloop cycle, can be set to Infinite to allow unlimited sleep
    MLMicroSeconds mThrottleSleep; ///< how long to sleep after a mainloop cycle that had no chance to sleep at all. Can be 0.
    MLMicroSeconds mMaxRun; ///< how long to run maximally without any interruption. Note that this cannot limit the runtime for a single handler.
    MLMicroSeconds mMaxCoalescing; ///< how much to shift timer execution points maximally (always within limits given by timer's tolerance) to coalesce executions
    MLMicroSeconds mWaitCheckInterval; ///< max interval between checks for termination of running child processes

    #ifdef ESP_PLATFORM
    TaskHandle_t mTaskHandle; ///< task handle of task that started this mainloop
    SemaphoreHandle_t mTimersLock; ///< semaphore for timers list
    int evFsFD; ///< the filedescriptor that is signalled when another task posts timer events
    #endif

    #if MAINLOOP_LIBEV_BASED
    struct ev_loop* mLibEvLoopP;
    struct ev_timer mLibEvTimer;
    #endif

  protected:

    MLMicroSeconds mStartedAt;
    bool mTerminated;
    int mExitCode;

    #if MAINLOOP_STATISTICS
    MLMicroSeconds mStatisticsStartTime;
    size_t mMaxTimers;
    MLMicroSeconds mIoHandlerTime;
    MLMicroSeconds mTimedHandlerTime;
    MLMicroSeconds mMaxTimerExecutionDelay;
    long mTimesTimersRanToLong;
    long mTimesThrottlingApplied;
    MLMicroSeconds mWaitHandlerTime;
    MLMicroSeconds mThreadSignalHandlerTime;
    #endif


    // protected constructor
    MainLoop();

  public:

    /// returns or creates the current thread's mainloop
    /// @return the mainloop for this thread
    static MainLoop &currentMainLoop();

    /// @name time related static utility functions
    /// @{

    /// returns the current microsecond in "Mainloop" time (monotonic as long as app runs, but not necessarily anchored with real time)
    /// @return mainloop time in microseconds
    static MLMicroSeconds now();

    /// returns the Unix epoch time in mainloop time scaling (microseconds)
    /// @return unix epoch time, in microseconds
    static MLMicroSeconds unixtime();

    /// get now as localtime (struct tm)
    /// @param aLocalTime time components will be updated to represent aUnixTime in localtime
    /// @param aFractionalSecondsP if not NULL, the fractional seconds part will be returned [0..1[
    /// @param aUnixTime optional unix time to calculate local time from. Defaults to current unixtime()
    /// @param aGMT if set, conversion to localtime happens in GMT(UTC)
    static void getLocalTime(struct tm& aLocalTime, double* aFractionalSecondsP = NULL, MLMicroSeconds aUnixTime = unixtime(), bool aGMT = false);

    /// convert a mainloop timestamp to unix epoch time
    /// @param aMLTime a mainloop timestamp in MLMicroSeconds
    /// @return Unix epoch time (in microseconds)
    static MLMicroSeconds mainLoopTimeToUnixTime(MLMicroSeconds aMLTime);

    /// convert a unix epoch time to mainloop timestamp
    /// @param aUnixTime Unix epoch time (in microseconds)
    /// @return mainloop timestamp in MLMicroSeconds
    static MLMicroSeconds unixTimeToMainLoopTime(const MLMicroSeconds aUnixTime);

    /// convert mainloop time into localtime (struct tm)
    /// @param aMLTime a mainloop timestamp in MLMicroSeconds
    /// @param aLocalTime time components will be updated to represent aMLTime in localtime
    /// @param aFractionalSecondsP if not NULL, the fractional seconds part will be returned [0..1[
    static void mainLoopTimeTolocalTime(MLMicroSeconds aMLTime, struct tm& aLocalTime, double* aFractionalSecondsP = NULL);

    /// convert a struct tm to mainloop timestamp
    /// @param aLocalTime local time compontents in a struct tm
    /// @return mainloop time in MLMicroSeconds
    static MLMicroSeconds localTimeToMainLoopTime(const struct tm& aLocalTime);

    /// convert a struct timeval to mainloop timestamp
    /// @param aTimeValP pointer to a struct timeval
    /// @return mainloop time in MLMicroSeconds
    static MLMicroSeconds timeValToMainLoopTime(struct timeval *aTimeValP);

    /// strftime from mainloop time with output to std::string
    /// @param aFormat strftime-style format string
    /// @param aTime time in mainloop now() scale
    /// @param aFractionals number of fractional second digits to append at end of string
    /// @return formatted time string (in local time)
    static string string_fmltime(const char *aFormat, MLMicroSeconds aTime, int aFractionals = 0) __strftimelike(1);

    /// format mainloop time as localtime in YYYY-MM-DD HH:MM:SS format with output to std::string
    /// @param aTime time in mainloop now() scale
    /// @param aFractionals number of fractional second digits to show
    /// @return formatted time string (in local time)
    static string string_mltime(MLMicroSeconds aTime, int aFractionals = 0);


    /// sleeps for given number of microseconds
    static void sleep(MLMicroSeconds aSleepTime);

    /// @}


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
    /// @note this is the only call we allow to start w/o a ticket. It still can go wrong if the object which calls
    ///   it immediately gets destroyed *before* the mainloop executes the callback, but probability is low.
    /// @param aTimerCallback the functor to be called from mainloop
    void executeNow(TimerCB aTimerCallback);

    #ifdef ESP_PLATFORM
    /// execute something on this mainloop without delay initiated by another task (thread not maintained by p44utils, like callbacks in ESP32)
    /// @param aTimerCallback the functor to be called from this mainloop (rather than the caller's)
    void executeNowFromForeignTask(TimerCB aTimerCallback);
    #endif

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
    /// @note This is intended to be called exclusively from TimerCB callbacks, in particular to implement periodic timer callbacks.
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


    #ifndef ESP_PLATFORM
    /// @name start subprocesses and register handlers for returning subprocess status
    /// @{

    /// execute external binary or interpreter script in a separate process
    /// @param aCallback the functor to be called when execution is done (failed to start or completed)
    /// @param aPath the path to the binary or script
    /// @param aArgv a NULL terminated array of arguments, first should be program name
    /// @param aEnvp a NULL terminated array of environment variables, or NULL to let child inherit parent's environment
    /// @param aPipeBackStdOut if true, stdout of the child is collected via a pipe by the parent and passed back in aCallBack (or can be read using aPipeBackFdP)
    /// @param aPipeBackFdP if not NULL, and aPipeBackStdOut is set, this will be set to the file descriptor of the pipe,
    /// @param aStdErrFd if >0, stderr of the child process is set to it; if 0, stderr of the child is redirected to /dev/null
    /// @param aStdInFd if >0, stdin of the child process is set to it;  if 0, stdin of the child is redirected to /dev/null
    ///   so caller can handle output data of the process. The caller is responsible for closing the fd.
    /// @param aParentDeathSig Linux only: if >0, this signal is sent to the child process when parent exits
    /// @return the child's PID (can be used to send signals to it), or -1 if fork fails
    pid_t fork_and_execve(ExecCB aCallback, const char *aPath, char *const aArgv[], char *const aEnvp[] = NULL, bool aPipeBackStdOut = false, int* aPipeBackFdP = NULL, int aStdErrFd = -1, int aStdInFd = -1, int aParentDeathSig = -1);

    /// execute command line in external shell
    /// @param aCallback the functor to be called when execution is done (failed to start or completed)
    /// @param aCommandLine the command line to execute
    /// @param aPipeBackStdOut if true, stdout of the child is collected via a pipe by the parent and passed back in aCallBack
    /// @param aStdOutFdP if not NULL, and aPipeBackStdOut is set, this will be set to the file descriptor of the pipe,
    ///   so caller can handle output data of the process. The caller is responsible for closing the fd.
    /// @param aStdErrFd if >0, stderr of the child process is set to it; if 0, stderr of the child is redirected to /dev/null
    /// @param aStdInFd if >0, stdin of the child process is set to it;  if 0, stdin of the child is redirected to /dev/null
    /// @param aParentDeathSig Linux only: if >0, this signal is sent to the child process when parent exits
    /// @return the child's PID (can be used to send signals to it), or -1 if fork fails
    pid_t fork_and_system(ExecCB aCallback, const char *aCommandLine, bool aPipeBackStdOut = false, int* aStdOutFdP = NULL, int aStdErrFd = -1, int aStdInFd = -1, int aParentDeathSig = -1);


    /// have handler called when a specific process delivers a state change
    /// @param aCallback the functor to be called when given process delivers a state change, NULL to remove callback
    /// @param aPid the process to wait for
    void waitForPid(WaitCB aCallback, pid_t aPid);

    /// @}
    #endif // !ESP_PLATFORM


    /// @name register handlers for I/O events
    /// @{

    /// register handler to be called for activity on specified file descriptor
    /// @param aFD the file descriptor to poll
    /// @param aPollFlags POLLxxx flags to specify events we want a callback for
    /// @param aPollEventHandler the functor to be called when poll() reports an event for one of the flags set in aPollFlags
    /// @note when based on libev, registering flags other than POLLIN and POLLOUT is only
    ///    possible on Linux by inserting a proxy epoll file descriptor. This should be avoided
    ///    except for special cases (such as detecting edges on GPIO FDs with POLLPRI)
    void registerPollHandler(int aFD, int aPollFlags, IOPollCB aPollEventHandler);

    /// change the poll flags for an already registered handler
    /// @param aFD the file descriptor
    /// @param aSetPollFlags POLLxxx flags to be enabled for this file descriptor
    /// @param aClearPollFlags POLLxxx flags to be disabled for this file descriptor
    /// @note when based on libev, only POLLIN and POLLOUT are supported. Setting other
    ///    flags will cause an assertion to fail and terminate the program.
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
    bool isTerminated() { return mTerminated; };

    /// ask if mainloop is normally running
    /// @return will return false as soon as mainloop has been requested to terminate, or before run() has been called
    bool isRunning() { return mStartedAt!=Never && !mTerminated; };

    /// @return mainloop time when started
    MLMicroSeconds startedAt() { return mStartedAt; }

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


    #if MAINLOOP_LIBEV_BASED
    /// the underlying libev main loop. Depending on the implementation the libev main loop might only be created
    /// when first queried via this method, or might exist as a base for p44utils mainloop all the time
    /// @return libev loop pointer to be used as "the mainloop" from code using libev mechanisms
    struct ev_loop* libevLoop();
    #endif


  private:

    // we don't want timers to be used without a MLTicket taking care of cancelling when the called object is deleted
    MLTicketNo executeOnceAt(TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance);
    MLTicketNo executeOnce(TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance);
    bool cancelExecutionTicket(MLTicketNo aTicketNo);

    MLMicroSeconds checkTimers(MLMicroSeconds aTimeout);
    void scheduleTimer(MLTimer &aTimer);

    void handleIOPoll(MLMicroSeconds aTimeout);

    #ifndef ESP_PLATFORM
    bool checkWait();
    void execChildTerminated(ExecCB aCallback, FdStringCollectorPtr aAnswerCollector, pid_t aPid, int aStatus);
    void childAnswerCollected(ExecCB aCallback, FdStringCollectorPtr aAnswerCollector, ErrorPtr aError);
    #endif // !ESP_PLATFORM

  };



  class ChildThreadWrapper : public P44Obj
  {
    typedef P44Obj inherited;

    pthread_t mPthread; ///< the pthread
    bool mThreadRunning; ///< set if thread is active

    MainLoop &mParentThreadMainLoop; ///< the parent mainloop which created this thread
    int mChildSignalFd; ///< the pipe used to transmit signals from the child thread
    int mParentSignalFd; ///< the pipe monitored by parentThreadMainLoop to get signals from child

    ThreadSignalHandler mParentSignalHandler; ///< the handler to call to deliver signals to the main thread
    ThreadRoutine mThreadRoutine; ///< the actual thread routine to run

    ChildThreadWrapperPtr mSelfRef;

    bool mTerminationPending; ///< set if termination has been requested by requestTermination()

    MainLoop *mMyMainLoopP; ///< the (optional) mainloop of this thread

    CrossThreadCall mCrossThreadCallRoutine; ///< the routine waiting to run or actually running
    pthread_mutex_t mCrossThreadCallMutex; ///< the mutex the caller waits on
    pthread_cond_t mCrossThreadCallCond; ///< the condition the caller waits on
    ErrorPtr mCrossThreadCallStatus; ///< the status of the cross thread call
    StatusCB mCrossThreadStatusCB; ///< the status callback

  public:

    /// constructor
    ChildThreadWrapper(MainLoop &aParentThreadMainLoop, ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler);

    /// destructor
    virtual ~ChildThreadWrapper();


    /// @name methods to call from child thread
    /// @{

    /// check if termination is requested
    bool shouldTerminate() { return mTerminationPending; }

    /// signal parent thread
    /// @param aSignalCode a signal code to be sent to the parent thread
    void signalParentThread(ThreadSignals aSignalCode);

    /// returns (and creates, if not yet existing) the thread's mainloop
    /// @note MUST be called from the thread itself to get the correct mainloop!
    MainLoop &threadMainLoop();

    /// confirm termination
    void confirmTerminated();

    /// disconnect this child wrapper, that is, prevent it from calling back via mParentSignalHandler
    /// @note thread will continue to run, but no longer call back
    void disconnect();

    /// execute routine, BLOCKING the current (child) thread, on the parent thread
    /// @param aParentThreadRoutine the routine to be executed in the parent thread
    /// @note the calling child thread blocks until the callee returns.
    ///   So during the excution of the routine, the callee actually owns the child thread context
    ///   and can access data without needing extra locks. Of course, run time must be minimized!
    ErrorPtr executeOnParentThread(CrossThreadCall aParentThreadRoutine);

    /// check if we can call executeOnParentThread.
    /// @return true if ok to call, false otherwise
    /// @note normally this returns true because executeOnParentThread() is blocking on the child,
    ///   so no two parallel invocations are possible. However, in special cases it might still
    ///   not be possible to run executeOnParentThread():
    ///   - when calling from a third thread, not the child thread itself
    ///     (and the child thread is also executing on the parent right mow)
    ///   - when calling from within a executeOnChildThread() operation
    bool readyForExecuteOnParent();



    /// execute routine, BLOCKING current (child) thread, on the parent thread, and provide a callback for async termination
    /// @param aParentThreadRoutine the routine to be executed in the parent thread
    /// @param aStatusCB is intended to be passed into async operations aParentThreadRoutine might start, and
    ///   should be called when those terminate. The callback is then forwarded back to this thread using executeOnChildThread().
    /// @note the calling child thread blocks until the callee returns, which means it has started running.
    ///   In addition, the aStatusCB is called on this (child) thread later when the corresponding
    ///   callback is called on the parent thread.
    /// @note this requires the child thread to support startOnChildThread(), see details there.
    void executeOnParentThreadAsync(CrossThreadAsyncCall aParentAsyncRoutine, StatusCB aStatusCB);

    /// cross thread routine call processor
    /// This must be called in the thread routine to allow executeOnChildThread() and executeOnChildThreadAsync() calls
    /// @note this does not exit unless the child thread is terminated
    void crossThreadCallProcessor();


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

    /// execute routine, BLOCKING the current (parent) thread, on the child thread
    /// @param aChildThreadRoutine the routine to be executed in the child thread
    /// @note the calling parent thread blocks until the callee returns.
    ///   So during the excution of the routine, the callee actually owns the child thread context
    ///   and can access data without needing extra locks. Of course, run time must be minimized!
    /// @note use startOnChildThread() to avoid blocking, but then you are responsible for
    ///   not calling this routine again before the previous run has completed
    /// @note this requires that the child thread is running crossThreadCallProcessor().
    ErrorPtr executeOnChildThread(CrossThreadCall aChildThreadRoutine);

    /// start routine on the child thread, and invoke the callback when the child routine terminates
    /// @param aChildThreadRoutine the routine to be executed in the child thread
    /// @param aStatusCB when aChildThreadRoutine terminates, this callback is invoked by forwarding it
    ///   using executeOnParentThread().
    /// @note the semantics of this is NOT the same as executeOnParentThreadAsync(). Things running
    ///   in parent thread are required to not block substantially; however in contrast,
    ///   a child thread usually has the exact purpose to run longer-time blocking things,
    ///   so executeOnChildThreadAsync does NOT block the caller.
    /// @note this requires the child thread to support startOnChildThread(), see details there.
    void executeOnChildThreadAsync(CrossThreadCall aChildThreadRoutine, StatusCB aStatusCB);

    /// @}

    /// method called from thread_start_function from this child thread
    void *startFunction();

  private:

    bool signalPipeHandler(int aPollFlags);
    void finalizeThreadExecution();

    ErrorPtr asyncParentCallExecutor(CrossThreadAsyncCall aParentAsyncRoutine, StatusCB aStatusCB);

    /// start a routine on the child thread, NOT blocking current (parent) thread
    /// @param aChildThreadRoutine the routine to be executed in the child thread
    /// @param aStatusCB the callback to be posted to the parent thread when the routine
    ///   completes executing.
    /// @note the calling parent thread WILL block until the child thread has finished running
    ///   a previous routine started earlier. This method is primarily a building block
    ///   for executeOnChildThread() and executeOnChildThreadAsync(), so use the latter
    ///   for longer-running child thread routines to keep track of when the processing
    ///   terminates.
    /// @note this requires that the child thread is running crossThreadCallProcessor().
    void startOnChildThread(CrossThreadCall aChildThreadRoutine, StatusCB aStatusCB);

    void parentToChildCallback(ErrorPtr aStatus, StatusCB aFinalCallback);

    ErrorPtr crossThreadCallbackDelivery(ErrorPtr aStatus, StatusCB aFinalCallback);

  };


} // namespace p44

#endif // C++ only interface

#endif /* defined(__p44utils__mainloop__) */
