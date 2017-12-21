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

#include "mainloop.hpp"

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#include <unistd.h>
#include <sys/param.h>
#include <sys/wait.h>

#include "fdcomm.hpp"

// MARK: ===== MainLoop default parameters

#define MAINLOOP_DEFAULT_MAXSLEEP Infinite // if really nothing to do, we can sleep
#define MAINLOOP_DEFAULT_MAXRUN (100*MilliSecond) // noticeable reaction time
#define MAINLOOP_DEFAULT_THROTTLE_SLEEP (20*MilliSecond) // limits CPU usage to about 85%
#define MAINLOOP_DEFAULT_WAIT_CHECK_INTERVAL (100*MilliSecond) // assuming no really tight timing when using external processes
#define MAINLOOP_DEFAULT_MAX_COALESCING (1*Second) // keep timing within second precision by default

// MARK: ===== MainLoop

using namespace p44;

// time reference in microseconds
MLMicroSeconds MainLoop::now()
{
  #ifdef __APPLE__
  static bool timeInfoKnown = false;
  static mach_timebase_info_data_t tb;
  if (!timeInfoKnown) {
    mach_timebase_info(&tb);
  }
  double t = mach_absolute_time();
  return t * (double)tb.numer / (double)tb.denom / 1e3; // uS
  #else
  struct timespec tsp;
  clock_gettime(CLOCK_MONOTONIC, &tsp);
  // return microseconds
  return ((uint64_t)(tsp.tv_sec))*1000000ll + tsp.tv_nsec/1000; // uS
  #endif
}


void MainLoop::sleep(MLMicroSeconds aSleepTime)
{
  // Linux has nanosleep in nanoseconds
  timespec sleeptime;
  sleeptime.tv_sec=aSleepTime/Second;
  sleeptime.tv_nsec=(aSleepTime % Second)*1000L; // nS = 1000 uS
  nanosleep(&sleeptime,NULL);
}



// the current thread's main looop
#if BOOST_DISABLE_THREADS
static MainLoop *currentMainLoopP = NULL;
#else
static __thread MainLoop *currentMainLoopP = NULL;
#endif

// get the per-thread singleton mainloop
MainLoop &MainLoop::currentMainLoop()
{
	if (currentMainLoopP==NULL) {
		// need to create it
		currentMainLoopP = new MainLoop();
	}
	return *currentMainLoopP;
}


#if MAINLOOP_STATISTICS
#define ML_STAT_START_AT(nw) MLMicroSeconds t = (nw);
#define ML_STAT_ADD_AT(tmr, nw) tmr += (nw)-t;
#define ML_STAT_START ML_STAT_START_AT(now());
#define ML_STAT_ADD(tmr) ML_STAT_ADD_AT(tmr, now());
#else
#define ML_STAT_START_AT(now)
#define ML_STAT_ADD_AT(tmr, nw);
#define ML_STAT_START
#define ML_STAT_ADD(tmr)
#endif



ErrorPtr ExecError::exitStatus(int aExitStatus, const char *aContextMessage)
{
  if (aExitStatus==0)
    return ErrorPtr(); // empty, no error
  return Error::err_cstr<ExecError>(aExitStatus, aContextMessage);
}



MainLoop::MainLoop() :
	terminated(false),
  hasStarted(false),
  exitCode(EXIT_SUCCESS),
  ticketNo(0),
  timersChanged(false)
{
  // default configuration
  maxSleep = MAINLOOP_DEFAULT_MAXSLEEP;
  maxRun = MAINLOOP_DEFAULT_MAXRUN;
  throttleSleep = MAINLOOP_DEFAULT_THROTTLE_SLEEP;
  waitCheckInterval = MAINLOOP_DEFAULT_WAIT_CHECK_INTERVAL;
  maxCoalescing = MAINLOOP_DEFAULT_MAX_COALESCING;
  #if MAINLOOP_STATISTICS
  statistics_reset();
  #endif
}



MLTicket MainLoop::executeOnce(TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
	MLMicroSeconds executionTime = now()+aDelay;
	return executeOnceAt(aTimerCallback, executionTime, aTolerance);
}


MLTicket MainLoop::executeOnceAt(TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
	MLTimer tmr;
  tmr.reinsert = false;
  tmr.ticketNo = ++ticketNo;
  tmr.executionTime = aExecutionTime;
  tmr.tolerance = aTolerance;
	tmr.callback = aTimerCallback;
  scheduleTimer(tmr);
  return tmr.ticketNo;
}


void MainLoop::executeTicketOnceAt(MLTicket &aTicketNo, TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
  cancelExecutionTicket(aTicketNo);
  aTicketNo = executeOnceAt(aTimerCallback, aExecutionTime);
}


void MainLoop::executeTicketOnce(MLTicket &aTicketNo, TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
  cancelExecutionTicket(aTicketNo);
  aTicketNo = executeOnce(aTimerCallback, aDelay, aTolerance);
}




void MainLoop::scheduleTimer(MLTimer &aTimer)
{
  #if MAINLOOP_STATISTICS
  size_t n = timers.size()+1;
  if (n>maxTimers) maxTimers = n;
  #endif
	// insert in queue before first item that has a higher execution time
	TimerList::iterator pos = timers.begin();
  while (pos!=timers.end()) {
    if (pos->executionTime>aTimer.executionTime) {
      timers.insert(pos, aTimer);
      timersChanged = true;
      return;
    }
    ++pos;
  }
  // none executes later than this one, just append
  timers.push_back(aTimer);
}


void MainLoop::cancelExecutionTicket(MLTicket &aTicketNo)
{
  if (aTicketNo==0) return; // no ticket, NOP
  for (TimerList::iterator pos = timers.begin(); pos!=timers.end(); ++pos) {
		if (pos->ticketNo==aTicketNo) {
			pos = timers.erase(pos);
      timersChanged = true;
      break;
		}
	}
  // reset the ticket
  aTicketNo = 0;
}


bool MainLoop::rescheduleExecutionTicket(MLTicket aTicketNo, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
	MLMicroSeconds executionTime = now()+aDelay;
	return rescheduleExecutionTicketAt(aTicketNo, executionTime, aTolerance);
}


bool MainLoop::rescheduleExecutionTicketAt(MLTicket aTicketNo, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
  if (aTicketNo==0) return false; // no ticket, no reschedule
  for (TimerList::iterator pos = timers.begin(); pos!=timers.end(); ++pos) {
		if (pos->ticketNo==aTicketNo) {
      MLTimer h = *pos;
      // remove from queue
			pos = timers.erase(pos);
      // reschedule
      h.executionTime = aExecutionTime;
      scheduleTimer(h);
      // reschedule was possible
      return true;
		}
	}
  // no ticket found, could not reschedule
  return false;
}


int MainLoop::retriggerTimer(MLTimer &aTimer, MLMicroSeconds aInterval, MLMicroSeconds aTolerance, int aSkip)
{
  MLMicroSeconds now = MainLoop::now();
  int skipped = 0;
  aTimer.tolerance = aTolerance;
  if (aSkip==from_now_if_late) {
    aTimer.executionTime += aInterval;
    if (aTimer.executionTime+aTimer.tolerance < now) {
      // too late (even taking allowed tolerance into account)
      aTimer.executionTime = now+aInterval;
      skipped = 1; // signal we skipped some time
    }
    // we're not yet too late to let this timer run within its tolerance -> re-insert it
    aTimer.reinsert = true;
    return skipped;
  }
  else if (aSkip==from_now) {
    // unconditionally relative to now
    aTimer.executionTime = now+aInterval;
    aTimer.reinsert = true;
    return skipped;
  }
  else {
    do {
      aTimer.executionTime += aInterval;
      if (aTimer.executionTime >= now) {
        // success
        aTimer.reinsert = true;
        return skipped;
      }
      skipped++;
    } while (skipped<=aSkip);
    // could not advance the timer enough
    return -1; // signal failure to retrigger within the specified limits
  }
}



void MainLoop::waitForPid(WaitCB aCallback, pid_t aPid)
{
  LOG(LOG_DEBUG, "waitForPid: requested wait for pid=%d", aPid);
  if (aCallback) {
    // install new callback
    WaitHandler h;
    h.callback = aCallback;
    h.pid = aPid;
    waitHandlers[aPid] = h;
  }
  else {
    WaitHandlerMap::iterator pos = waitHandlers.find(aPid);
    if (pos!=waitHandlers.end()) {
      // remove it from list
      waitHandlers.erase(pos);
    }
  }
}


extern char **environ;


pid_t MainLoop::fork_and_execve(ExecCB aCallback, const char *aPath, char *const aArgv[], char *const aEnvp[], bool aPipeBackStdOut)
{
  LOG(LOG_DEBUG, "fork_and_execve: preparing to fork for executing '%s' now", aPath);
  pid_t child_pid;
  int answerPipe[2]; /* Child to parent pipe */

  // prepare environment
  if (aEnvp==NULL) {
    aEnvp = environ; // use my own environment
  }
  // prepare pipe in case we want answer collected
  if (aPipeBackStdOut) {
    if(pipe(answerPipe)<0) {
      // pipe could not be created
      aCallback(SysError::errNo(),"");
      return -1;
    }
  }
  // fork child process
  child_pid = fork();
  if (child_pid>=0) {
    // fork successful
    if (child_pid==0) {
      // this is the child process (fork() returns 0 for the child process)
      //setpgid(0, 0); // Linux: set group PID to this process' PID, allows killing the entire group
      LOG(LOG_DEBUG, "forked child process: preparing for execve");
      if (aPipeBackStdOut) {
        dup2(answerPipe[1],STDOUT_FILENO); // replace STDOUT by writing end of pipe
        close(answerPipe[1]); // release the original descriptor (does NOT really close the file)
        close(answerPipe[0]); // close child's reading end of pipe (parent uses it!)
      }
      // close all non-std file descriptors
      int fd = getdtablesize();
      while (fd>STDERR_FILENO) close(fd--);
      // change to the requested child process
      execve(aPath, aArgv, aEnvp); // replace process with new binary/script
      // execv returns only in case of error
      exit(127);
    }
    else {
      // this is the parent process, wait for the child to terminate
      LOG(LOG_DEBUG, "fork_and_execve: parent: child pid=%d", child_pid);
      FdStringCollectorPtr ans;
      if (aPipeBackStdOut) {
        LOG(LOG_DEBUG, "fork_and_execve: parent will now set up pipe string collector");
        close(answerPipe[1]); // close parent's writing end (child uses it!)
        // set up collector for data returned from child process
        ans = FdStringCollectorPtr(new FdStringCollector(MainLoop::currentMainLoop()));
        ans->setFd(answerPipe[0]);
      }
      LOG(LOG_DEBUG, "fork_and_execve: now calling waitForPid(%d)", child_pid);
      waitForPid(boost::bind(&MainLoop::execChildTerminated, this, aCallback, ans, _1, _2), child_pid);
      return child_pid;
    }
  }
  else {
    if (aCallback) {
      // fork failed, call back with error
      aCallback(SysError::errNo(),"");
    }
  }
  return -1;
}


pid_t MainLoop::fork_and_system(ExecCB aCallback, const char *aCommandLine, bool aPipeBackStdOut)
{
  char * args[4];
  args[0] = (char *)"sh";
  args[1] = (char *)"-c";
  args[2] = (char *)aCommandLine;
  args[3] = NULL;
  return fork_and_execve(aCallback, "/bin/sh", args, NULL, aPipeBackStdOut);
}


void MainLoop::execChildTerminated(ExecCB aCallback, FdStringCollectorPtr aAnswerCollector, pid_t aPid, int aStatus)
{
  LOG(LOG_DEBUG, "execChildTerminated: pid=%d, aStatus=%d", aPid, aStatus);
  if (aCallback) {
    LOG(LOG_DEBUG, "- callback set, execute it");
    ErrorPtr err = ExecError::exitStatus(WEXITSTATUS(aStatus));
    if (aAnswerCollector) {
      LOG(LOG_DEBUG, "- aAnswerCollector: starting collectToEnd");
      aAnswerCollector->collectToEnd(boost::bind(&MainLoop::childAnswerCollected, this, aCallback, aAnswerCollector, err));
    }
    else {
      // call back directly
      LOG(LOG_DEBUG, "- no aAnswerCollector: callback immediately");
      aCallback(err, "");
    }
  }
}


void MainLoop::childAnswerCollected(ExecCB aCallback, FdStringCollectorPtr aAnswerCollector, ErrorPtr aError)
{
  LOG(LOG_DEBUG, "childAnswerCollected: error = %s", Error::isOK(aError) ? "none" : aError->description().c_str());
  // close my end of the pipe
  aAnswerCollector->stopMonitoringAndClose();
  // now get answer
  string answer = aAnswerCollector->collectedData;
  LOG(LOG_DEBUG, "- Answer = %s", answer.c_str());
  // call back directly
  aCallback(aError, answer);
}



void MainLoop::registerCleanupHandler(SimpleCB aCleanupHandler)
{
  cleanupHandlers.push_back(aCleanupHandler);
}



void MainLoop::terminate(int aExitCode)
{
  exitCode = aExitCode;
  terminated = true;
}


MLMicroSeconds MainLoop::checkTimers(MLMicroSeconds aTimeout)
{
  ML_STAT_START
  MLMicroSeconds nextTimer = Never;
  MLMicroSeconds runUntilMax = MainLoop::now() + aTimeout;
  do {
    nextTimer = Never;
    TimerList::iterator pos = timers.begin();
    timersChanged = false; // detect changes happening from callbacks
    while (pos!=timers.end()) {
      nextTimer = pos->executionTime;
      MLMicroSeconds now = MainLoop::now();
      // check for executing next timer
      MLMicroSeconds tl = pos->tolerance;
      if (tl>maxCoalescing) tl=maxCoalescing;
      if (nextTimer-tl>now) {
        // next timer not ready to run
        goto done;
      } else if (now>runUntilMax) {
        // we are running too long already
        #if MAINLOOP_STATISTICS
        timesTimersRanToLong++;
        #endif
        goto done;
      }
      else {
        // earliest allowed execution time for this timer is reached, execute it
        if (terminated) {
          nextTimer = Never; // no more timers to run if terminated
          goto done;
        }
        #if MAINLOOP_STATISTICS
        // update max delay from intented execution time
        MLMicroSeconds late = now-nextTimer-pos->tolerance;
        if (late>maxTimerExecutionDelay) maxTimerExecutionDelay = late;
        #endif
        // run this timer
        MLTimer runningTimer = *pos; // copy the timer object
        runningTimer.reinsert = false; // not re-inserting by default
        pos = timers.erase(pos); // remove timer from queue
        runningTimer.callback(runningTimer, now); // call handler
        if (runningTimer.reinsert) {
          // retriggering requested, do it now
          scheduleTimer(runningTimer);
        }
        if (timersChanged) {
          // callback or retrigger has caused change of onetime handlers list, pos gets invalid
          break; // quit loop now
        }
      }
    }
    // we get here when list was processed
  } while (timersChanged);
done:
  ML_STAT_ADD(timedHandlerTime);
  return nextTimer; // report to caller when we need to be called again to meet next timer
}


bool MainLoop::checkWait()
{
  if (waitHandlers.size()>0) {
    // check for process signal
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid>0) {
      LOG(LOG_DEBUG, "checkWait: child pid=%d reports exit status %d", pid, status);
      // process has status
      WaitHandlerMap::iterator pos = waitHandlers.find(pid);
      if (pos!=waitHandlers.end()) {
        // we have a callback
        WaitCB cb = pos->second.callback; // get it
        // remove it from list
        waitHandlers.erase(pos);
        // call back
        ML_STAT_START
        LOG(LOG_DEBUG, "- calling wait handler for pid=%d now with status=%d", pid, status);
        cb(pid, status);
        ML_STAT_ADD(waitHandlerTime);
        return false; // more process status could be ready, call soon again
      }
    }
    else if (pid<0) {
      // error when calling waitpid
      int e = errno;
      if (e==ECHILD) {
        // no more children
        LOG(LOG_WARNING, "checkWait: pending handlers but no children any more -> ending all waits WITH FAKE STATUS 0 - probably SIGCHLD ignored?");
        // - inform all still waiting handlers
        WaitHandlerMap oldHandlers = waitHandlers; // copy
        waitHandlers.clear(); // remove all handlers from real list, as new handlers might be added in handlers we'll call now
        ML_STAT_START
        for (WaitHandlerMap::iterator pos = oldHandlers.begin(); pos!=oldHandlers.end(); pos++) {
          WaitCB cb = pos->second.callback; // get callback
          LOG(LOG_DEBUG, "- calling wait handler for pid=%d now WITH FAKE STATUS 0", pos->second.pid);
          cb(pos->second.pid, 0); // fake status
        }
        ML_STAT_ADD(waitHandlerTime);
      }
      else {
        LOG(LOG_DEBUG, "checkWait: waitpid returns error %s", strerror(e));
      }
    }
  }
  return true; // all checked
}





void MainLoop::registerPollHandler(int aFD, int aPollFlags, IOPollCB aPollEventHandler)
{
  if (aPollEventHandler.empty())
    unregisterPollHandler(aFD); // no handler means unregistering handler
  // register new handler
  IOPollHandler h;
  h.monitoredFD = aFD;
  h.pollFlags = aPollFlags;
  h.pollHandler = aPollEventHandler;
	ioPollHandlers[aFD] = h;
}


void MainLoop::changePollFlags(int aFD, int aSetPollFlags, int aClearPollFlags)
{
  IOPollHandlerMap::iterator pos = ioPollHandlers.find(aFD);
  if (pos!=ioPollHandlers.end()) {
    // found fd to set flags for
    if (aClearPollFlags>=0) {
      // read modify write
      // - clear specified flags
      pos->second.pollFlags &= ~aClearPollFlags;
      pos->second.pollFlags |= aSetPollFlags;
    }
    else {
      // just set
      pos->second.pollFlags = aSetPollFlags;
    }
  }
}



void MainLoop::unregisterPollHandler(int aFD)
{
  ioPollHandlers.erase(aFD);
}



bool MainLoop::handleIOPoll(MLMicroSeconds aTimeout)
{
  // create poll structure
  struct pollfd *pollFds = NULL;
  size_t maxFDsToTest = ioPollHandlers.size();
  if (maxFDsToTest>0) {
    // allocate pollfd array (max, in case some are disabled, we'll need less)
    pollFds = new struct pollfd[maxFDsToTest];
  }
  // fill poll structure
  IOPollHandlerMap::iterator pos = ioPollHandlers.begin();
  size_t numFDsToTest = 0;
  // collect FDs
  while (pos!=ioPollHandlers.end()) {
    IOPollHandler h = pos->second;
    if (h.pollFlags) {
      // don't include handlers that are currently disabled (no flags set)
      struct pollfd *pollfdP = &pollFds[numFDsToTest];
      pollfdP->fd = h.monitoredFD; // analyzer: is wrong, pollfdP is always defined, because maxFDsToTest==ioPollHandlers.size()
      pollfdP->events = h.pollFlags;
      pollfdP->revents = 0; // no event returned so far
      ++numFDsToTest;
    }
    ++pos;
  }
  // block until input becomes available or timeout
  int numReadyFDs = 0;
  if (numFDsToTest>0) {
    // actual FDs to test
    numReadyFDs = poll(pollFds, (int)numFDsToTest, (int)(aTimeout/MilliSecond));
  }
  else {
    // nothing to test, just await timeout
    if (aTimeout>0) {
      usleep((useconds_t)aTimeout);
    }
  }
  // call handlers
  if (numReadyFDs>0) {
    // at least one of the flagged events has occurred in at least one FD
    // - find the FDs that are affected and call their handlers when needed
    for (int i = 0; i<numFDsToTest; i++) {
      struct pollfd *pollfdP = &pollFds[i];
      if (pollfdP->revents) {
        ML_STAT_START
        // an event has occurred for this FD
        // - get handler, note that it might have been deleted in the meantime
        IOPollHandlerMap::iterator pos = ioPollHandlers.find(pollfdP->fd);
        if (pos!=ioPollHandlers.end()) {
          // - there is a handler, call it
          pos->second.pollHandler(pollfdP->fd, pollfdP->revents);
        }
        ML_STAT_ADD(ioHandlerTime);
      }
    }
  }
  // return the poll array
  delete[] pollFds;
  // return true if poll actually reported something (not just timed out)
  return numReadyFDs>0;
}




void MainLoop::startupMainLoop()
{
  hasStarted = true;
}





bool MainLoop::mainLoopCycle()
{
  // Mainloop (async) cycle
  MLMicroSeconds cycleStarted = MainLoop::now();
  while (!terminated) {
    // run timers
    MLMicroSeconds nextWake = checkTimers(maxRun);
    if (terminated) break;
    // check
    if (!checkWait()) {
      // still need to check for terminating processes
      if (nextWake>cycleStarted+waitCheckInterval) {
        nextWake = cycleStarted+waitCheckInterval;
      }
    }
    if (terminated) break;
    // limit sleeping time
    if (maxSleep!=Infinite && (nextWake==Never || nextWake>cycleStarted+maxSleep)) {
      nextWake = cycleStarted+maxSleep;
    }
    // poll I/O and/or sleep
    MLMicroSeconds pollTimeout = nextWake-MainLoop::now();
    if (nextWake!=Never && pollTimeout<=0) {
      // not sleeping at all
      handleIOPoll(0);
      // limit cycle run time
      if (cycleStarted+maxRun<MainLoop::now()) {
        return false; // run limit reached before we could sleep
      }
    }
    else {
      // nothing due before timeout
      handleIOPoll(nextWake==Never ? -1 : pollTimeout); // negative timeout means blocking forever
      return true; // we had the chance to sleep
    }
    // otherwise, continue processing
  }
  // terminated
  return true; // result does not matter any more after termination, so just assume we did sleep
}



int MainLoop::finalizeMainLoop()
{
  // run mainloop termination handlers
  for (CleanupHandlersList::iterator pos = cleanupHandlers.begin(); pos!=cleanupHandlers.end(); ++pos) {
    SimpleCB cb = *pos;
    if (cb) cb();
  }
  return exitCode;
}



int MainLoop::run()
{
  startupMainLoop();
  // run
  while (!terminated) {
    bool couldSleep = mainLoopCycle();
    if (!couldSleep) {
      // extra sleep to prevent full CPU usage
      #if MAINLOOP_STATISTICS
      timesThrottlingApplied++;
      #endif
      MainLoop::sleep(throttleSleep);
    }
  }
  return finalizeMainLoop();
}



string MainLoop::description()
{
  // get some interesting data from mainloop
  #if MAINLOOP_STATISTICS
  MLMicroSeconds statisticsPeriod = now()-statisticsStartTime;
  #endif
  return string_format(
    "Mainloop statistics:\n"
    "- installed I/O poll handlers   : %ld\n"
    "- pending child process waits   : %ld\n"
    "- pending timers right now      : %ld\n"
    "  - earliest in                 : %lld mS from now\n"
    "  - latest in                   : %lld mS from now\n"
    #if MAINLOOP_STATISTICS
    "- statistics period             : %.3f S\n"
    "- I/O poll handler runtime      : %lld mS / %d%% of period\n"
    "- wait handler runtime          : %lld mS / %d%% of period\n"
    "- thread signalhandler runtime  : %lld mS / %d%% of period\n"
    "- timer handler runtime         : %lld mS / %d%% of period\n"
    "  - max delay in execution      : %lld mS\n"
    "  - timer handlers ran too long : %ld times\n"
    "  - max timers waiting at once  : %ld\n"
    "- throttling sleep inserted     : %ld times\n"
    #endif
    ,(long)ioPollHandlers.size()
    ,(long)waitHandlers.size()
    ,(long)timers.size()
    ,(long long)(timers.size()>0 ? timers.front().executionTime-now() : 0)/MilliSecond
    ,(long long)(timers.size()>0 ? timers.back().executionTime-now() : 0)/MilliSecond
    #if MAINLOOP_STATISTICS
    ,(double)statisticsPeriod/Second
    ,ioHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * ioHandlerTime/statisticsPeriod : 0)
    ,waitHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * waitHandlerTime/statisticsPeriod : 0)
    ,threadSignalHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * threadSignalHandlerTime/statisticsPeriod : 0)
    ,timedHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * timedHandlerTime/statisticsPeriod : 0)
    ,(long long)maxTimerExecutionDelay/MilliSecond
    ,(long)timesTimersRanToLong
    ,(long)maxTimers
    ,(long)timesThrottlingApplied
    #endif
  );
}


void MainLoop::statistics_reset()
{
  #if MAINLOOP_STATISTICS
  statisticsStartTime = now();
  ioHandlerTime = 0;
  waitHandlerTime = 0;
  threadSignalHandlerTime = 0;
  timedHandlerTime = 0;
  maxTimerExecutionDelay = 0;
  timesTimersRanToLong = 0;
  timesThrottlingApplied =0;
  maxTimers = 0;
  #endif
}


// MARK: ===== execution in subthreads


ChildThreadWrapperPtr MainLoop::executeInThread(ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler)
{
  return ChildThreadWrapperPtr(new ChildThreadWrapper(*this, aThreadRoutine, aThreadSignalHandler));
}


// MARK: ===== ChildThreadWrapper


static void *thread_start_function(void *arg)
{
  // pass into method of wrapper
  return static_cast<ChildThreadWrapper *>(arg)->startFunction();
}


void *ChildThreadWrapper::startFunction()
{
  // run the routine
  threadRoutine(*this);
  // signal termination
  confirmTerminated();
  return NULL;
}



ChildThreadWrapper::ChildThreadWrapper(MainLoop &aParentThreadMainLoop, ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler) :
  parentThreadMainLoop(aParentThreadMainLoop),
  threadRoutine(aThreadRoutine),
  parentSignalHandler(aThreadSignalHandler),
  terminationPending(false),
  myMainLoopP(NULL),
  threadRunning(false)
{
  // create a signal pipe
  int pipeFdPair[2];
  if (pipe(pipeFdPair)==0) {
    // pipe could be created
    // - save FDs
    parentSignalFd = pipeFdPair[0]; // 0 is the reading end
    childSignalFd = pipeFdPair[1]; // 1 is the writing end
    // - install poll handler in the parent mainloop
    parentThreadMainLoop.registerPollHandler(parentSignalFd, POLLIN, boost::bind(&ChildThreadWrapper::signalPipeHandler, this, _2));
    // create a pthread (with default attrs for now
    threadRunning = true; // before creating it, to make sure it is set when child starts to run
    if (pthread_create(&pthread, NULL, thread_start_function, this)!=0) {
      // error, could not create thread, fake a signal callback immediately
      threadRunning = false;
      if (parentSignalHandler) {
        parentSignalHandler(*this, threadSignalFailedToStart);
      }
    }
    else {
      // thread created ok, keep wrapper object alive
      selfRef = ChildThreadWrapperPtr(this);
    }
  }
  else {
    // pipe could not be created
    if (parentSignalHandler) {
      parentSignalHandler(*this, threadSignalFailedToStart);
    }
  }
}


ChildThreadWrapper::~ChildThreadWrapper()
{
  // cancel thread
  cancel();
  // delete mainloop if any
  if (myMainLoopP) {
    delete myMainLoopP;
    myMainLoopP = NULL;
  }
}


MainLoop &ChildThreadWrapper::threadMainLoop()
{
  myMainLoopP = &MainLoop::currentMainLoop();
  return *myMainLoopP;
}


// can be called from main thread to request termination from thread routine
void ChildThreadWrapper::terminate()
{
  terminationPending = true;
  if (myMainLoopP) {
    myMainLoopP->terminate(0);
  }
}




// called from child thread when terminated
void ChildThreadWrapper::confirmTerminated()
{
  signalParentThread(threadSignalCompleted);
}




// called from child thread to send signal
void ChildThreadWrapper::signalParentThread(ThreadSignals aSignalCode)
{
  uint8_t sigByte = aSignalCode;
  write(childSignalFd, &sigByte, 1);
}


// cleanup, called from parent thread
void ChildThreadWrapper::finalizeThreadExecution()
{
  // synchronize with actual end of thread execution
  pthread_join(pthread, NULL);
  threadRunning = false;
  // unregister the handler
  MainLoop::currentMainLoop().unregisterPollHandler(parentSignalFd);
  // close the pipes
  close(childSignalFd);
  close(parentSignalFd);
}



// can be called from parent thread
void ChildThreadWrapper::cancel()
{
  if (threadRunning) {
    // cancel it
    pthread_cancel(pthread);
    // wait for cancellation to complete
    finalizeThreadExecution();
    // cancelled
    if (parentSignalHandler) {
      parentSignalHandler(*this, threadSignalCancelled);
    }
  }
}



// called on parent thread from Mainloop
bool ChildThreadWrapper::signalPipeHandler(int aPollFlags)
{
  ThreadSignals sig = threadSignalNone;
  //DBGLOG(LOG_DEBUG, "\nMAINTHREAD: signalPipeHandler with pollFlags=0x%X", aPollFlags);
  if (aPollFlags & POLLIN) {
    uint8_t sigByte;
    ssize_t res = read(parentSignalFd, &sigByte, 1); // read signal byte
    if (res==1) {
      sig = (ThreadSignals)sigByte;
    }
  }
  else if (aPollFlags & POLLHUP) {
    // HUP means thread has terminated and closed the other end of the pipe already
    // - treat like receiving a threadSignalCompleted
    sig = threadSignalCompleted;
  }
  if (sig!=threadSignalNone) {
    // check for thread terminated
    if (sig==threadSignalCompleted) {
      // finalize thread execution first
      finalizeThreadExecution();
    }
    // got signal byte, call handler
    if (parentSignalHandler) {
      ML_STAT_START_AT(parentThreadMainLoop.now());
      parentSignalHandler(*this, sig);
      ML_STAT_ADD_AT(parentThreadMainLoop.threadSignalHandlerTime, parentThreadMainLoop.now());
    }
    if (sig==threadSignalCompleted || sig==threadSignalFailedToStart || sig==threadSignalCancelled) {
      // signal indicates thread has ended (successfully or not)
      // - in case nobody keeps this object any more, it should be deleted now
      selfRef.reset();
    }
    // handled some i/O
    return true;
  }
  return false; // did not handle any I/O
}


