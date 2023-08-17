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

#include "mainloop.hpp"

#ifdef __APPLE__
  #include <mach/mach_time.h>
#endif
#include <unistd.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <math.h>
#ifdef ESP_PLATFORM
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "esp_system.h"
  #include "esp_timer.h"
  #include "esp_vfs.h"
#endif
#include "fdcomm.hpp"


// MARK: - MainLoop default parameters

#define MAINLOOP_DEFAULT_MAXSLEEP Infinite // if really nothing to do, we can sleep
#define MAINLOOP_DEFAULT_MAXRUN (100*MilliSecond) // noticeable reaction time
#define MAINLOOP_DEFAULT_THROTTLE_SLEEP (20*MilliSecond) // limits CPU usage to about 85%
#define MAINLOOP_DEFAULT_WAIT_CHECK_INTERVAL (100*MilliSecond) // assuming no really tight timing when using external processes
#define MAINLOOP_DEFAULT_MAX_COALESCING (1*Second) // keep timing within second precision by default

using namespace p44;


#ifdef BOOST_NO_EXCEPTIONS
// boost calls this when throwing C++ exceptions is disabled, e.g. ESP32 builds
void boost::throw_exception(std::exception const & e){
  // log and exit
  LOG(LOG_ERR, "Exception thrown -> exit");
  exit(EXIT_FAILURE);
}
#endif



// MARK: - MLTicket

MLTicket::MLTicket() :
  mTicketNo(0)
{
}


MLTicket::MLTicket(MLTicket &aTicket) : mTicketNo(0)
{
  // should not happen!
  // But if it does, we do not copy the ticket number
}


MLTicketNo MLTicket::defuse()
{
  MLTicketNo tn = mTicketNo;
  mTicketNo = 0; // cancel() or destruction will no longer end the timer
  return tn;
}



MLTicket::~MLTicket()
{
  cancel();
}


MLTicket::operator MLTicketNo() const
{
  return mTicketNo;
}


MLTicket::operator bool() const
{
  return mTicketNo!=0;
}


bool MLTicket::cancel()
{
  if (mTicketNo!=0) {
    bool cancelled = MainLoop::currentMainLoop().cancelExecutionTicket(mTicketNo);
    mTicketNo = 0;
    return cancelled;
  }
  return false; // no ticket
}


void MLTicket::executeOnceAt(TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
  MainLoop::currentMainLoop().executeTicketOnceAt(*this, aTimerCallback, aExecutionTime, aTolerance);
}


void MLTicket::executeOnce(TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
  MainLoop::currentMainLoop().executeTicketOnce(*this, aTimerCallback, aDelay, aTolerance);
}


MLTicketNo MLTicket::operator=(MLTicketNo aTicketNo)
{
  cancel();
  mTicketNo = aTicketNo;
  return mTicketNo;
}


bool MLTicket::reschedule(MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
  MLMicroSeconds executionTime = MainLoop::currentMainLoop().now()+aDelay;
  return rescheduleAt(executionTime, aTolerance);
}


bool MLTicket::rescheduleAt(MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
  if (mTicketNo==0) return false; // no ticket, no reschedule
  return MainLoop::currentMainLoop().rescheduleExecutionTicketAt(mTicketNo, aExecutionTime, aTolerance);
}




// MARK: - Time base

long long _p44_now()
{
  #if defined(__APPLE__) && __DARWIN_C_LEVEL < 199309L
  // pre-10.12 MacOS does not yet have clock_gettime
  static bool timeInfoKnown = false;
  static mach_timebase_info_data_t tb;
  if (!timeInfoKnown) {
    mach_timebase_info(&tb);
  }
  double t = mach_absolute_time();
  return t * (double)tb.numer / (double)tb.denom / 1e3; // uS
  #elif defined(ESP_PLATFORM)
  return esp_timer_get_time(); // just fits, is high precision timer in uS since boot
  #else
  // platform has clock_gettime
  struct timespec tsp;
  clock_gettime(CLOCK_MONOTONIC, &tsp);
  // return microseconds
  return ((long long)(tsp.tv_sec))*1000000ll + (long long)(tsp.tv_nsec/1000); // uS
  #endif
}


unsigned long _p44_millis()
{
  return (unsigned long)(_p44_now()/1000); // mS
}



// MARK: - MainLoop static utilities

// time reference in microseconds
MLMicroSeconds MainLoop::now()
{
  return _p44_now();
}


MLMicroSeconds MainLoop::unixtime()
{
  #if defined(__APPLE__) && __DARWIN_C_LEVEL < 199309L
  // pre-10.12 MacOS does not yet have clock_gettime
  // FIXME: Q&D approximation with seconds resolution only
  return ((MLMicroSeconds)time(NULL))*Second;
  #elif defined(ESP_PLATFORM)
  // TODO: implement getting actual time from RTC
  #warning "Fake unixtime() on ESP_PLATFORM for now"
  return 2222*365*Day+now();
  #else
  struct timespec tsp;
  clock_gettime(CLOCK_REALTIME, &tsp);
  // return unix epoch microseconds
  return ((long long)(tsp.tv_sec))*1000000ll + (long long)(tsp.tv_nsec/1000); // uS
  #endif
}



MLMicroSeconds MainLoop::mainLoopTimeToUnixTime(MLMicroSeconds aMLTime)
{
  return aMLTime-now()+unixtime();
}


MLMicroSeconds MainLoop::unixTimeToMainLoopTime(const MLMicroSeconds aUnixTime)
{
  return aUnixTime-unixtime()+now();
}


MLMicroSeconds MainLoop::timeValToMainLoopTime(struct timeval *aTimeValP)
{
  if (!aTimeValP) return Never;
  return aTimeValP->tv_sec*Second + aTimeValP->tv_usec;
}


void MainLoop::mainLoopTimeTolocalTime(MLMicroSeconds aMLTime, struct tm& aLocalTime, double* aFractionalSecondsP)
{
  MLMicroSeconds ut = mainLoopTimeToUnixTime(aMLTime);
  time_t t = ut/Second;
  if (aFractionalSecondsP) {
    *aFractionalSecondsP = (double)ut/Second-(double)t;
  }
  localtime_r(&t, &aLocalTime);
}


MLMicroSeconds MainLoop::localTimeToMainLoopTime(const struct tm& aLocalTime)
{
  time_t u = mktime((struct tm*) &aLocalTime);
  return unixTimeToMainLoopTime(u*Second);
}


void MainLoop::getLocalTime(struct tm& aLocalTime, double* aFractionalSecondsP, MLMicroSeconds aUnixTime, bool aGMT)
{
  double unixsecs = (double)(aUnixTime/Second);
  time_t t = (time_t)unixsecs;
  if (aGMT) gmtime_r(&t, &aLocalTime);
  else localtime_r(&t, &aLocalTime);
  if (aFractionalSecondsP) {
    *aFractionalSecondsP = unixsecs-floor(unixsecs);
  }
}


string MainLoop::string_mltime(MLMicroSeconds aTime, int aFractionals)
{
  if (aTime==Infinite) return "Infinite";
  if (aTime==Never) return "Never";
  return string_fmltime("%Y-%m-%d %H:%M:%S", aTime, aFractionals);
}


string MainLoop::string_fmltime(const char *aFormat, MLMicroSeconds aTime, int aFractionals)
{
  struct tm tim;
  string ts;
  if (aFractionals==0) {
    mainLoopTimeTolocalTime(aTime, tim);
    string_ftime_append(ts, aFormat, &tim);
  }
  else {
    double fracSecs;
    mainLoopTimeTolocalTime(aTime, tim, &fracSecs);
    string_ftime_append(ts, aFormat, &tim);
    int f = (int)(fracSecs*pow(10, aFractionals));
    string_format_append(ts, ".%0*d", aFractionals, f);
  }
  return ts;
}


void MainLoop::sleep(MLMicroSeconds aSleepTime)
{
  #ifdef ESP_PLATFORM
  vTaskDelay(aSleepTime/MilliSecond/portTICK_PERIOD_MS);
  #else
  // Linux/MacOS has nanosleep in nanoseconds
  timespec sleeptime;
  sleeptime.tv_sec=aSleepTime/Second;
  sleeptime.tv_nsec=(long)((aSleepTime % Second)*1000ll); // nS = 1000 uS
  nanosleep(&sleeptime,NULL);
  #endif
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


// MARK: - ExecError

ErrorPtr ExecError::exitStatus(int aExitStatus, const char *aContextMessage)
{
  if (aExitStatus==0)
    return ErrorPtr(); // empty, no error
  return Error::err_cstr<ExecError>(aExitStatus, aContextMessage);
}


// MARK: - MainLoop ESP32 specific utilities

#ifdef ESP_PLATFORM

#define P44UTILS_MAINLOOP_EVFS_PATH "/p44EvFs"

static bool evFsInstalled = false;
static bool foreignTaskTimersWaiting = false;

static int evFs_open(const char *path, int flags, int mode)
{
  // is a singleton, local FD is always 0
  DBGFOCUSLOG("evFs_open");
  return 0; // FD
}

static int evFs_close(int fd)
{
  // nop
  DBGFOCUSLOG("evFs_close");
  return 0; // ok
}

static SemaphoreHandle_t evFsSemaphore = NULL;


esp_err_t evFs_start_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, esp_vfs_select_sem_t sem, void **end_select_args)
{
  // remember the semaphore we need to trigger
  DBGFOCUSLOG("evFs_start_select");
  // from esp_vfs.h: type of "sem" is SemaphoreHandle_t when true, defined by socket driver otherwise
  assert(sem.is_sem_local);
  evFsSemaphore = (SemaphoreHandle_t)sem.sem;
  if (foreignTaskTimersWaiting) {
    // new timers were waiting before select has started -> immediately terminate select()
    foreignTaskTimersWaiting = false;
    DBGFOCUSLOG("evFs_start_select with foreignTaskTimersPending immediately frees evFsSemaphore");
    xSemaphoreGive(evFsSemaphore);
  }
  *end_select_args = NULL;
  return ESP_OK;
}


static esp_err_t evFs_end_select(void *end_select_args)
{
  DBGFOCUSLOG("evFs_end_select");
  evFsSemaphore = NULL; // forget semaphore again
  return ESP_OK;
}


static esp_vfs_t evFs = {
  .flags = ESP_VFS_FLAG_DEFAULT,
  .open = &evFs_open,
  .close = &evFs_close,
  .start_select = &evFs_start_select,
  .end_select = &evFs_end_select,
};


#endif // ESP_PLATFORM



// MARK: - MainLoop

#if MAINLOOP_LIBEV_BASED

namespace p44 {
  void libev_io_poll_handler(EV_P_ struct ev_io *i, int revents); // declaration to silence warning
  void libev_sleep_timer_done(EV_P_ struct ev_timer *t, int revents); // declaration to silence warning
}



void p44::libev_sleep_timer_done(EV_P_ struct ev_timer *t, int revents)
{
  MainLoop* mlP = static_cast<MainLoop*>(t->data);
  ev_timer_stop(mlP->mLibEvLoopP, t);

  // NOP, just needed to exit IO polling
  DBGFOCUSLOG(LOG_DEBUG, "libev IO polling timeout");
}

#endif // MAINLOOP_LIBEV_BASED

#if MAINLOOP_LIBEV_BASED
static bool gDefaultMainloopInUse = false;
#endif


MainLoop::MainLoop() :
  mTimersChanged(false),
  mTicketNo(0),
  mStartedAt(Never),
  mTerminated(false),
  mExitCode(EXIT_SUCCESS)
{
  #ifdef ESP_PLATFORM
  FOCUSLOG("mainloop: ESP32 specific initialisation of mainloop@%p", this);
  // capture the task handle
  mTaskHandle = xTaskGetCurrentTaskHandle();
  // create locked semaphore to protect from untimely calls to executeNowFromForeignTask
  mTimersLock = xSemaphoreCreateBinary();
  evFsFD = -1;
  if (!evFsInstalled) {
    DBGFOCUSLOG("- installing pseudo VFS");
    // create a pseudo VFS driver for the only purpose to be able to post a event that interrupts a select()
    ErrorPtr err = EspError::err(esp_vfs_register(P44UTILS_MAINLOOP_EVFS_PATH, &evFs, NULL));
    if (Error::notOK(err)) {
      FOCUSLOG("mainloop: esp_vfs_register failed: %s", err->text());
    }
    evFsInstalled = true;
    // now open a FD we can later use in handleIoPoll with select()
    DBGFOCUSLOG("- opening select trigger event pseudo file");
    evFsFD = open(P44UTILS_MAINLOOP_EVFS_PATH, O_RDONLY);
    if (evFsFD<0) {
      FOCUSLOG("mainloop: cannot open " P44UTILS_MAINLOOP_EVFS_PATH);
    }
    DBGFOCUSLOG("- select trigger event pseudo file, FD=%d", evFsFD);
  }
  #elif MAINLOOP_LIBEV_BASED
  if (gDefaultMainloopInUse) {
    // this must be a subthread's mainloop, must create a new libev mainloop for it
    mLibEvLoopP = ev_loop_new();
  }
  else {
    // this is the main main loop
    gDefaultMainloopInUse = true; // all further mainloops must create a new ev_loop
    mLibEvLoopP = EV_DEFAULT;
  }
  // init timer we need when we allow libev to "sleep"
  ev_timer_init(&mLibEvTimer, &libev_sleep_timer_done, 1, 0);
  mLibEvTimer.data = this;
  #endif
  // default configuration
  mMaxSleep = MAINLOOP_DEFAULT_MAXSLEEP;
  mMaxRun = MAINLOOP_DEFAULT_MAXRUN;
  mThrottleSleep = MAINLOOP_DEFAULT_THROTTLE_SLEEP;
  mWaitCheckInterval = MAINLOOP_DEFAULT_WAIT_CHECK_INTERVAL;
  mMaxCoalescing = MAINLOOP_DEFAULT_MAX_COALESCING;
  #if MAINLOOP_STATISTICS
  statistics_reset();
  #endif
}


// MARK: timer setup

// private implementation
MLTicketNo MainLoop::executeOnce(TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
	MLMicroSeconds executionTime = now()+aDelay;
	return executeOnceAt(aTimerCallback, executionTime, aTolerance);
}


// private implementation
MLTicketNo MainLoop::executeOnceAt(TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
	MLTimer tmr;
  tmr.mReinsert = false;
  tmr.mTicketNo = ++mTicketNo;
  tmr.mExecutionTime = aExecutionTime;
  tmr.mTolerance = aTolerance;
	tmr.mCallback = aTimerCallback;
  scheduleTimer(tmr);
  return tmr.mTicketNo;
}


void MainLoop::executeTicketOnceAt(MLTicket &aTicket, TimerCB aTimerCallback, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
  aTicket.cancel();
  aTicket = (MLTicketNo)executeOnceAt(aTimerCallback, aExecutionTime, aTolerance);
}


void MainLoop::executeTicketOnce(MLTicket &aTicket, TimerCB aTimerCallback, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
  aTicket.cancel();
  aTicket = executeOnce(aTimerCallback, aDelay, aTolerance);
}


void MainLoop::executeNow(TimerCB aTimerCallback)
{
  executeOnce(aTimerCallback, 0, 0);
}


#ifdef ESP_PLATFORM

void MainLoop::executeNowFromForeignTask(TimerCB aTimerCallback)
{
  // - safely insert in timer queue
  DBGFOCUSLOG("executeNowFromForeignTask: evFsFD=%d in mainloop@%p", evFsFD, this);
  xSemaphoreTake(mTimersLock, portMAX_DELAY);
  DBGFOCUSLOG("- executeNowFromForeignTask lock taken in mainloop@%p", this);
  executeNow(aTimerCallback);
  if (evFsFD>=0) {
    // signal select to exit immediately when not yet running
    foreignTaskTimersWaiting = true;
    // make select() exit if already running
    if (evFsSemaphore) {
      // we are currently in select, release it
      DBGFOCUSLOG("- executeNowFromForeignTask frees evFsSemaphore");
      xSemaphoreGive(evFsSemaphore);
    }
  }
  xSemaphoreGive(mTimersLock);
}

#endif



void MainLoop::scheduleTimer(MLTimer &aTimer)
{
  #if MAINLOOP_STATISTICS
  size_t n = mTimers.size()+1;
  if (n>mMaxTimers) mMaxTimers = n;
  #endif
	// insert in queue before first item that has a higher execution time
	TimerList::iterator pos = mTimers.begin();
  // optimization: if no timers, just append
  if (pos!=mTimers.end()) {
    // optimization: if new timer is later than all others, just append
    if (aTimer.mExecutionTime<mTimers.back().mExecutionTime) {
      // is somewhere between current timers, need to find position
      do {
        if (pos->mExecutionTime>aTimer.mExecutionTime) {
          mTimers.insert(pos, aTimer);
          mTimersChanged = true;
          return;
        }
        ++pos;
      } while (pos!=mTimers.end());
    }
  }
  // none executes later than this one, just append
  mTimersChanged = true; // when processing timers now, the list must be re-checked! Processing iterator might be at end of list already!
  mTimers.push_back(aTimer);
}



void MainLoop::cancelExecutionTicket(MLTicket &aTicket)
{
  aTicket.cancel();
}


// private implementation
bool MainLoop::cancelExecutionTicket(MLTicketNo aTicketNo)
{
  if (aTicketNo==0) return false; // no ticket, NOP
  for (TimerList::iterator pos = mTimers.begin(); pos!=mTimers.end(); ++pos) {
		if (pos->mTicketNo==aTicketNo) {
			pos = mTimers.erase(pos);
      mTimersChanged = true;
      return true; // ticket found and cancelled
		}
	}
  return false; // no such ticket
}


bool MainLoop::rescheduleExecutionTicket(MLTicketNo aTicketNo, MLMicroSeconds aDelay, MLMicroSeconds aTolerance)
{
	MLMicroSeconds executionTime = now()+aDelay;
	return rescheduleExecutionTicketAt(aTicketNo, executionTime, aTolerance);
}


bool MainLoop::rescheduleExecutionTicketAt(MLTicketNo aTicketNo, MLMicroSeconds aExecutionTime, MLMicroSeconds aTolerance)
{
  if (aTicketNo==0) return false; // no ticket, no reschedule
  for (TimerList::iterator pos = mTimers.begin(); pos!=mTimers.end(); ++pos) {
		if (pos->mTicketNo==aTicketNo) {
      MLTimer h = *pos;
      // remove from queue
			pos = mTimers.erase(pos);
      // reschedule
      h.mExecutionTime = aExecutionTime;
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
  aTimer.mTolerance = aTolerance;
  if (aSkip==absolute) {
    if (aInterval<now+aTimer.mTolerance) {
      skipped = 1; // signal we skipped some time
      aTimer.mExecutionTime = now; // ASAP
    }
    else {
      aTimer.mExecutionTime = aInterval; // aInterval is absolute time to fire timer next
    }
    aTimer.mReinsert = true;
    return skipped;
  }
  else if (aSkip==from_now_if_late) {
    aTimer.mExecutionTime += aInterval;
    if (aTimer.mExecutionTime+aTimer.mTolerance < now) {
      // too late (even taking allowed tolerance into account)
      aTimer.mExecutionTime = now+aInterval;
      skipped = 1; // signal we skipped some time
    }
    // we're not yet too late to let this timer run within its tolerance -> re-insert it
    aTimer.mReinsert = true;
    return skipped;
  }
  else if (aSkip==from_now) {
    // unconditionally relative to now
    aTimer.mExecutionTime = now+aInterval;
    aTimer.mReinsert = true;
    return skipped;
  }
  else {
    do {
      aTimer.mExecutionTime += aInterval;
      if (aTimer.mExecutionTime >= now) {
        // success
        aTimer.mReinsert = true;
        return skipped;
      }
      skipped++;
    } while (skipped<=aSkip);
    // could not advance the timer enough
    return -1; // signal failure to retrigger within the specified limits
  }
}


// MARK: subprocesses

#ifndef ESP_PLATFORM

void MainLoop::waitForPid(WaitCB aCallback, pid_t aPid)
{
  LOG(LOG_DEBUG, "waitForPid: requested wait for pid=%d", aPid);
  if (aCallback) {
    // install new callback
    WaitHandler h;
    h.callback = aCallback;
    h.pid = aPid;
    mWaitHandlers[aPid] = h;
  }
  else {
    WaitHandlerMap::iterator pos = mWaitHandlers.find(aPid);
    if (pos!=mWaitHandlers.end()) {
      // remove it from list
      mWaitHandlers.erase(pos);
    }
  }
}


extern char **environ;


pid_t MainLoop::fork_and_execve(ExecCB aCallback, const char *aPath, char *const aArgv[], char *const aEnvp[], bool aPipeBackStdOut, int* aPipeBackFdP, int aStdErrFd, int aStdInFd)
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
      if (aStdErrFd>=0) {
        if (aStdErrFd==0) aStdErrFd = open("/dev/null", O_WRONLY);
        dup2(aStdErrFd, STDERR_FILENO); // replace STDERR by provided fd
        close(aStdErrFd);
      }
      if (aStdInFd>=0) {
        if (aStdInFd==0) aStdErrFd = open("/dev/null", O_RDONLY);
        dup2(aStdInFd, STDIN_FILENO); // replace STDIN by provided fd
        close(aStdInFd);
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
        if (aPipeBackFdP) {
          // caller wants to handle the pipe end, return file descriptor
          *aPipeBackFdP = answerPipe[0];
        }
        else {
          // collect output in a string
          ans = FdStringCollectorPtr(new FdStringCollector(MainLoop::currentMainLoop()));
          ans->setFd(answerPipe[0]);
        }
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


pid_t MainLoop::fork_and_system(ExecCB aCallback, const char *aCommandLine, bool aPipeBackStdOut, int* aPipeBackFdP, int aStdErrFd, int aStdInFd)
{
  char * args[4];
  args[0] = (char *)"sh";
  args[1] = (char *)"-c";
  args[2] = (char *)aCommandLine;
  args[3] = NULL;
  return fork_and_execve(aCallback, "/bin/sh", args, NULL, aPipeBackStdOut, aPipeBackFdP, aStdErrFd, aStdInFd);
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
  LOG(LOG_DEBUG, "childAnswerCollected: error = %s", Error::text(aError));
  // close my end of the pipe
  aAnswerCollector->stopMonitoringAndClose();
  // now get answer
  string answer = aAnswerCollector->mCollectedData;
  LOG(LOG_DEBUG, "- Answer = %s", answer.c_str());
  // call back directly
  aCallback(aError, answer);
}

#endif // !ESP_PLATFORM


// MARK: mainloop core


void MainLoop::registerCleanupHandler(SimpleCB aCleanupHandler)
{
  cleanupHandlers.push_back(aCleanupHandler);
}



void MainLoop::terminate(int aExitCode)
{
  mExitCode = aExitCode;
  mTerminated = true;
}


MLMicroSeconds MainLoop::checkTimers(MLMicroSeconds aTimeout)
{
  ML_STAT_START
  MLMicroSeconds nextTimer = Never;
  MLMicroSeconds runUntilMax = MainLoop::now() + aTimeout;
  do {
    nextTimer = Never;
    TimerList::iterator pos = mTimers.begin();
    mTimersChanged = false; // detect changes happening from callbacks
    // Note: it is essential to check timersChanged in the while loop condition, because when the loop
    //   is actually taken, the runningTimer object can go out of scope and cause a
    //   chain of destruction which in turn can cause timer changes AFTER the timer callback
    //   has already returned.
    while (!mTimersChanged && pos!=mTimers.end()) {
      nextTimer = pos->mExecutionTime;
      MLMicroSeconds now = MainLoop::now();
      // check for executing next timer
      MLMicroSeconds tl = pos->mTolerance;
      if (tl>mMaxCoalescing) tl=mMaxCoalescing;
      if (nextTimer-tl>now) {
        // next timer not ready to run
        goto done;
      } else if (now>runUntilMax) {
        // we are running too long already
        #if MAINLOOP_STATISTICS
        mTimesTimersRanToLong++;
        #endif
        goto done;
      }
      else {
        // earliest allowed execution time for this timer is reached, execute it
        if (mTerminated) {
          nextTimer = Never; // no more timers to run if terminated
          goto done;
        }
        #if MAINLOOP_STATISTICS
        // update max delay from intented execution time
        MLMicroSeconds late = now-nextTimer-pos->mTolerance;
        if (late>mMaxTimerExecutionDelay) mMaxTimerExecutionDelay = late;
        #endif
        // run this timer
        MLTimer runningTimer = *pos; // copy the timer object
        runningTimer.mReinsert = false; // not re-inserting by default
        pos = mTimers.erase(pos); // remove timer from queue
        runningTimer.mCallback(runningTimer, now); // call handler
        if (runningTimer.mReinsert) {
          // retriggering requested, do it now
          scheduleTimer(runningTimer);
        }
      }
    }
    // we get here when list was processed
  } while (mTimersChanged);
done:
  ML_STAT_ADD(mTimedHandlerTime);
  return nextTimer; // report to caller when we need to be called again to meet next timer
}


#ifndef ESP_PLATFORM

bool MainLoop::checkWait()
{
  if (mWaitHandlers.size()>0) {
    // check for process signal
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid>0) {
      LOG(LOG_DEBUG, "checkWait: child pid=%d reports exit status %d", pid, status);
      // process has status
      WaitHandlerMap::iterator pos = mWaitHandlers.find(pid);
      if (pos!=mWaitHandlers.end()) {
        // we have a callback
        WaitCB cb = pos->second.callback; // get it
        // remove it from list
        mWaitHandlers.erase(pos);
        // call back
        ML_STAT_START
        LOG(LOG_DEBUG, "- calling wait handler for pid=%d now with status=%d", pid, status);
        cb(pid, status);
        ML_STAT_ADD(mWaitHandlerTime);
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
        WaitHandlerMap oldHandlers = mWaitHandlers; // copy
        mWaitHandlers.clear(); // remove all handlers from real list, as new handlers might be added in handlers we'll call now
        ML_STAT_START
        for (WaitHandlerMap::iterator pos = oldHandlers.begin(); pos!=oldHandlers.end(); pos++) {
          WaitCB cb = pos->second.callback; // get callback
          LOG(LOG_DEBUG, "- calling wait handler for pid=%d now WITH FAKE STATUS 0", pos->second.pid);
          cb(pos->second.pid, 0); // fake status
        }
        ML_STAT_ADD(mWaitHandlerTime);
      }
      else {
        LOG(LOG_DEBUG, "checkWait: waitpid returns error %s", strerror(e));
      }
    }
  }
  return true; // all checked
}

#endif


// MARK: - IO event handling

#if MAINLOOP_LIBEV_BASED

static inline int pollToEv(int aPollFlags)
{
  // POLLIN, POLLOUT -> EV_READ, EV_WRITE
  int events = 0;
  if (aPollFlags & POLLIN) events |= EV_READ;
  if (aPollFlags & POLLOUT) events |= EV_WRITE;
  return events;
}


static inline int evToPoll(int aLibEvEvents)
{
  int pollFlags = 0;
  if (aLibEvEvents & EV_READ) pollFlags |= POLLIN;
  if (aLibEvEvents & EV_WRITE) pollFlags |= POLLOUT;
  return pollFlags;
}

#ifndef __APPLE__

static inline int pollToEpoll(int aPollFlags)
{
  // POLLIN, POLLOUT, POLLPRI -> EPOLLIN, EPOLLOUT, EPOLLPRI
  int events = 0;
  if (aPollFlags & POLLIN) events |= EPOLLIN;
  if (aPollFlags & POLLOUT) events |= EPOLLOUT;
  if (aPollFlags & POLLPRI) events |= EPOLLPRI;
  return events;
}


static inline int epollToPoll(int aEpollFlags)
{
  // EPOLL... -> POLL...
  int events = 0;
  if (aEpollFlags & EPOLLIN) events |= POLLIN;
  if (aEpollFlags & EPOLLOUT) events |= POLLOUT;
  if (aEpollFlags & EPOLLERR) events |= POLLERR;
  if (aEpollFlags & EPOLLHUP) events |= POLLHUP;
  return events;
}

#endif



void p44::libev_io_poll_handler(EV_P_ struct ev_io *i, int revents)
{
  MainLoop::IOPollHandler *h = (MainLoop::IOPollHandler*)((char *)i-offsetof(MainLoop::IOPollHandler, mIoWatcher));
  #ifndef __APPLE__
  if (h->mEpolledFd>=0) {
    // this is an epoll FD firing, get the actual event
    struct epoll_event ev;
    if (epoll_wait(i->fd, &ev, 1, 0)!=1) return; // no event -> no op
    // return the event flags reported by epoll
    h->mPollHandler(h->mEpolledFd, epollToPoll(ev.events));
    return;
  }
  #endif
  // directly return the flags reported by libev
  h->mPollHandler(i->fd, evToPoll(revents));
}


MainLoop::IOPollHandler::IOPollHandler()
  #ifndef __APPLE__
  : mEpolledFd(-1)
  #endif
{
  ev_io_init(&mIoWatcher, libev_io_poll_handler, 0, 0);
  mIoWatcher.data = NULL; // not yet installed
}

void MainLoop::IOPollHandler::deactivate()
{
  if (mIoWatcher.data) {
    // only if data is set, the handler is actually installed and must remove the watcher from libev
    MainLoop* mlP = static_cast<MainLoop*>(mIoWatcher.data);
    ev_io_stop(mlP->mLibEvLoopP, &mIoWatcher);
    #ifndef __APPLE__
    // - also only for installed handlers, there might be an extra epoll FD
    if (mEpolledFd>=0) {
      close(mIoWatcher.fd); // close the extra epoll FD
      mEpolledFd=-1;
    }
    #endif
    mIoWatcher.data = NULL; // disconnect to make sure
  }
}

MainLoop::IOPollHandler::~IOPollHandler()
{
  deactivate();
}

MainLoop::IOPollHandler& MainLoop::IOPollHandler::operator= (MainLoop::IOPollHandler& aReplacing)
{
  deactivate();
  mIoWatcher = aReplacing.mIoWatcher;
  mPollHandler = aReplacing.mPollHandler;
  #ifndef __APPLE__
  mEpolledFd = aReplacing.mEpolledFd;
  #endif
  return *this;
}


#endif // MAINLOOP_LIBEV_BASED


void MainLoop::registerPollHandler(int aFD, int aPollFlags, IOPollCB aPollEventHandler)
{
  if (aPollEventHandler.empty()) {
    unregisterPollHandler(aFD); // no handler means unregistering handler
  }
  else {
    // register new handler
    #if MAINLOOP_LIBEV_BASED
    IOPollHandler h;
    h.mPollHandler = aPollEventHandler;
    if (aPollFlags & ~(POLLIN|POLLOUT)) {
      #ifdef __APPLE__
      LOG(LOG_ERR,"registerPollHandler: Requested Linux-only flags which libev cannot handle");
      aPollFlags |= POLLIN; // default to POLLIN
      #else
      // requesting options libev cannot handle natively, in particular: POLLPRI for GPIO edge detection
      // - we need an epoll file descriptor as a proxy
      int epfd = epoll_create1(EPOLL_CLOEXEC);
      if (epfd>=0) {
        // - set it up to watch the requested flags on the original FD
        struct epoll_event ev;
        ev.events = pollToEpoll(aPollFlags);
        aPollFlags = POLLIN; // when the epoll Fd detects event, it will signal POLLIN, so let libev watch for this
        ev.data.ptr = NULL; // not needed, because we'll have the libev context
        epoll_ctl(epfd, EPOLL_CTL_ADD, aFD, &ev);
        h.mEpolledFd = aFD; // save original
        aFD = epfd; // let libev watch the epoll FD
      }
      #endif
    }
    // Note: just assigning to map
    mIoPollHandlers[aFD] = h; // copies h
    ev_io* w = &(mIoPollHandlers[aFD].mIoWatcher);
    w->data = this; // only now the watcher is considered active (and stopped when destructed)
    ev_io_set(w, aFD, pollToEv(aPollFlags));
    ev_io_start(mLibEvLoopP, w);
    #else
    IOPollHandler h;
    h.monitoredFD = aFD;
    h.pollFlags = aPollFlags;
    h.pollHandler = aPollEventHandler;
    mIoPollHandlers[aFD] = h;
    #endif
  }
}

// older libev do not yet have ev_io_modify
#ifndef ev_io_modify
#define ev_io_modify(ev,events_) do { (ev)->events = ((ev)->events & EV__IOFDSET) | (events_); } while (0)
#endif

void MainLoop::changePollFlags(int aFD, int aSetPollFlags, int aClearPollFlags)
{
  IOPollHandlerMap::iterator pos = mIoPollHandlers.find(aFD);
  if (pos!=mIoPollHandlers.end()) {
    // found fd to set flags for
    #if MAINLOOP_LIBEV_BASED
    int f = evToPoll(pos->second.mIoWatcher.events);
    #else
    int f = pos->second.pollFlags;
    #endif
    if (aClearPollFlags>=0) {
      // read modify write
      // - clear specified flags
      f &= ~aClearPollFlags;
      f |= aSetPollFlags;
    }
    else {
      // just set
      f = aSetPollFlags;
    }
    #if MAINLOOP_LIBEV_BASED
    // TODO: for now, changing flags beyond POLLIN and POLLOUT is not supported with libev
    assert((f & ~(POLLIN|POLLOUT))==0);
    // Note: it is *essential* not to call ev_io_modify() before stopping the IOWatcher first!
    //   Otherwise, it seems that (at least when the modification removes EV_WRITE from
    //   a socket FD from a callback of the same watcher) libev can get into a state
    //   where ev_poll() always immediately exits with no event callback called.
    // - stop
    ev_io_stop(mLibEvLoopP, &pos->second.mIoWatcher);
    // - modify flags
    ev_io_modify(&pos->second.mIoWatcher, pollToEv(f));
    // - restart
    ev_io_start(mLibEvLoopP, &pos->second.mIoWatcher);
    #else
    pos->second.pollFlags = f;
    #endif
  }
}



void MainLoop::unregisterPollHandler(int aFD)
{
  mIoPollHandlers.erase(aFD);
}



void MainLoop::handleIOPoll(MLMicroSeconds aTimeout)
{
  #ifdef ESP_PLATFORM
  // use select(), more modern poll() is not available
  fd_set readfs; // file descriptor set for read
  fd_set writefs; // file descriptor set for write
  fd_set errorfs; // file descriptor set for errors
  // Create bitmap for select call
  int numFDsToTest = 0; // number of file descriptors to test (max+1 of all used FDs)
  IOPollHandlerMap::iterator pos = ioPollHandlers.begin();
  if (pos!=ioPollHandlers.end() || evFsFD>=0) {
    FD_ZERO(&readfs);
    FD_ZERO(&writefs);
    FD_ZERO(&errorfs);
    // collect FDs
    while (pos!=ioPollHandlers.end()) {
      IOPollHandler h = pos->second;
      numFDsToTest = MAX(h.monitoredFD+1, numFDsToTest);
      if (h.pollFlags & POLLIN) FD_SET(h.monitoredFD, &readfs);
      if (h.pollFlags & POLLOUT) FD_SET(h.monitoredFD, &writefs);
      if (h.pollFlags & (POLLERR+POLLHUP)) FD_SET(h.monitoredFD, &errorfs);
      ++pos;
    }
    if (evFsFD>=0) {
      // Only on application's first mainloop: always add the special evFsFD which allows other tasks to trigger exiting select
      numFDsToTest = MAX(evFsFD+1, numFDsToTest);
      FD_SET(evFsFD, &errorfs); // evFs does not check flags, anyway, only exits select() when something happens
    }
  }
  // block until input becomes available or timeout
  // Note: while we wait or poll for external events is the ONLY time while we allow external task to queue timer events
  DBGFOCUSLOG("opening mTimersLock");
  xSemaphoreGive(mTimersLock); // give others the opportunity to insert timer events
  int numReadyFDs = 0;
  if (numFDsToTest>0) {
    // actual FDs to test
    struct timeval tv;
    tv.tv_sec = aTimeout / 1000000;
    tv.tv_usec = aTimeout % 1000000;
    numReadyFDs = select(numFDsToTest, &readfs, &writefs, &errorfs, aTimeout!=Infinite ? &tv : NULL);
    DBGFOCUSLOG("select returns %d", numReadyFDs);
  }
  else {
    // nothing to test, just await timeout
    static const MLMicroSeconds tickInterval = portTICK_PERIOD_MS*MilliSecond;
    if (aTimeout>=tickInterval) {
      // only sleep if it's actually more than a tick, rounded down (better getting control back too soon than too late!)
      // Note: usleep would waste rest of time in a busy loop, so we'd rather spend it looping around here than there ;-)
      vTaskDelay(aTimeout/tickInterval);
    }
  }
  DBGFOCUSLOG("closing mTimersLock");
  xSemaphoreTake(mTimersLock, portMAX_DELAY); // running again, lock timer list
  DBGFOCUSLOG("closed mTimersLock");
  foreignTaskTimersWaiting = false; // not waiting, timers will be checked ASAP
  if (numReadyFDs>0) {
    // check the descriptor sets and call handlers when needed
    for (int i = 0; i<numFDsToTest; i++) {
      if (i==evFsFD) {
        if (FD_ISSET(i, &errorfs)) {
          DBGFOCUSLOG("evFsFD triggered");
        }
        continue; // there is no ioPollHandler for this, just ignore (only purpose is to exit select())
      }
      int pollflags = 0;
      if (FD_ISSET(i, &readfs)) pollflags |= POLLIN;
      if (FD_ISSET(i, &writefs)) pollflags |= POLLOUT;
      if (FD_ISSET(i, &errorfs)) pollflags |= POLLERR;
      if (pollflags!=0) {
        ML_STAT_START
        // an event has occurred for this FD
        // - get handler, note that it might have been deleted in the meantime
        IOPollHandler h = ioPollHandlers[i];
        // - call handler
        h.pollHandler(h.monitoredFD, pollflags);
        ML_STAT_ADD(ioHandlerTime);
      }
    }
  }
  #elif MAINLOOP_LIBEV_BASED
  // use libev
  if (aTimeout==0) {
    // just check
    ev_run(mLibEvLoopP, EVRUN_NOWAIT);
  }
  else if (aTimeout>0) {
    // pass control for specified time
    ev_timer_set(&mLibEvTimer, (double)aTimeout/Second, 0.);
    ev_timer_start(mLibEvLoopP, &mLibEvTimer);
    ev_run(mLibEvLoopP, EVRUN_ONCE);
    ev_timer_stop(mLibEvLoopP, &mLibEvTimer);
  }
  else {
    // no timers, just FDs -> run until one event has occurred (which can mean
    //   a handler might have added a new p44 timer, which must be taken into acount
    //   before passing control to libev mainloop again)
    if (!ev_run(mLibEvLoopP, EVRUN_ONCE)) {
      // libev thinks there is nothing to possibly generate an event any more.
      // However: as we manage the timers ourselves, it could be that in the run that has just occurred,
      // a libev callbacks has caused scheduling a new p44 mainloop timer! So check that before declaring the app dead.
      if (mTimers.empty()) {
        // still no timers
        LOG(LOG_WARNING, "Probably dead - no candidates any more for generating mainloop events (except signals)");
        // keep loop running, because a signal could revive it (unlikely, ugly but still).
        sleep(10*Second);
      }
    }
  }
  #else
  // use poll() - create poll structure
  struct pollfd *pollFds = NULL;
  size_t maxFDsToTest = mIoPollHandlers.size();
  if (maxFDsToTest>0) {
    // allocate pollfd array (max, in case some are disabled, we'll need less)
    pollFds = new struct pollfd[maxFDsToTest];
  }
  // fill poll structure
  IOPollHandlerMap::iterator pos = mIoPollHandlers.begin();
  size_t numFDsToTest = 0;
  // collect FDs
  while (pos!=mIoPollHandlers.end()) {
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
    // actual FDs to test. Note: while in Linux timeout<0 means block forever, ONLY exactly -1 means block in macOS!
    numReadyFDs = poll(pollFds, (int)numFDsToTest, aTimeout==Infinite ? -1 : (int)(aTimeout/MilliSecond));
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
        IOPollHandlerMap::iterator pos = mIoPollHandlers.find(pollfdP->fd);
        if (pos!=mIoPollHandlers.end()) {
          // - there is a handler, call it
          pos->second.pollHandler(pollfdP->fd, pollfdP->revents);
        }
        ML_STAT_ADD(mIoHandlerTime);
      }
    }
  }
  // return the poll array
  delete[] pollFds;
  #endif
}




void MainLoop::startupMainLoop(bool aRestart)
{
  if (aRestart) mTerminated = false;
  mStartedAt = MainLoop::now();
}





bool MainLoop::mainLoopCycle()
{
  // Mainloop (async) cycle
  MLMicroSeconds cycleStarted = MainLoop::now();
  while (!mTerminated) {
    // run timers
    MLMicroSeconds nextWake = checkTimers(mMaxRun);
    if (mTerminated) break;
    #ifndef ESP_PLATFORM
    // check
    if (!checkWait()) {
      // still need to check for terminating processes
      if (nextWake>cycleStarted+mWaitCheckInterval) {
        nextWake = cycleStarted+mWaitCheckInterval;
      }
    }
    #endif // !ESP_PLATFORM
    if (mTerminated) break;
    // limit sleeping time
    if (mMaxSleep!=Infinite && (nextWake==Never || nextWake>cycleStarted+mMaxSleep)) {
      nextWake = cycleStarted+mMaxSleep;
    }
    // poll I/O and/or sleep
    MLMicroSeconds pollTimeout = nextWake-MainLoop::now();
    if (nextWake!=Never && pollTimeout<=0) {
      // not sleeping at all
      handleIOPoll(0);
      // limit cycle run time
      if (cycleStarted+mMaxRun<MainLoop::now()) {
        return false; // run limit reached before we could sleep
      }
    }
    else {
      // nothing due before timeout
      handleIOPoll(nextWake==Never ? Infinite : pollTimeout);
      return true; // we had the chance to sleep
    }
    // otherwise, continue processing
  }
  // terminated
  return true; // result does not matter any more after termination, so just assume we did sleep
}



int MainLoop::finalizeMainLoop()
{
  // clear all runtim handlers to release all possibly retained objects
  mTimers.clear();
  mWaitHandlers.clear();
  mIoPollHandlers.clear();
  // run mainloop termination handlers
  for (CleanupHandlersList::iterator pos = cleanupHandlers.begin(); pos!=cleanupHandlers.end(); ++pos) {
    SimpleCB cb = *pos;
    if (cb) cb();
  }
  return mExitCode;
}



int MainLoop::run(bool aRestart)
{
  startupMainLoop(aRestart);
  // run
  while (!mTerminated) {
    bool couldSleep = mainLoopCycle();
    if (!couldSleep) {
      // extra sleep to prevent full CPU usage
      #if MAINLOOP_STATISTICS
      mTimesThrottlingApplied++;
      #endif
      MainLoop::sleep(mThrottleSleep);
    }
  }
  return finalizeMainLoop();
}


#if MAINLOOP_LIBEV_BASED

struct ev_loop* MainLoop::libevLoop()
{
  return mLibEvLoopP;
}


#if DEBUG
// FIXME: maybe remove later
void logP44ClockRefs(long long aForeignClockRef)
{
  long long ref_now = aForeignClockRef;
  long long ml_now = MainLoop::now() / MilliSecond;
  long long libev_now = (long long)(ev_now(MainLoop::currentMainLoop().libevLoop()) * 1000);
  long long libev_time = (long long)(ev_time() * 1000);
  LOG(LOG_NOTICE,
    "‼️ NOW[mS]: reference=%lld, mainloop=%lld (%+lld), ev_now=%lld (%+lld), ev_time=%lld (%+lld), ev_now-ev_time=%+lld",
      ref_now,
    ml_now,
    ml_now-ref_now,
    libev_now,
    libev_now-ref_now,
    libev_time,
    libev_time-ref_now,
    libev_now-libev_time
  );
}
#endif // DEBUG

#endif // MAINLOOP_LIBEV_BASED


string MainLoop::description()
{
  // get some interesting data from mainloop
  #if MAINLOOP_STATISTICS
  MLMicroSeconds statisticsPeriod = now()-mStatisticsStartTime;
  #endif
  return string_format(
    "Mainloop statistics:\n"
    "- installed I/O poll handlers   : %ld\n"
    "- pending child process waits   : %ld\n"
    "- pending timers right now      : %ld\n"
    "  - earliest                    : %s - %lld mS from now\n"
    "  - latest                      : %s - %lld mS from now\n"
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
    #if MAINLOOP_LIBEV_BASED
    "- pending libev watchers        : %d %s\n"
    #endif
    ,(long)mIoPollHandlers.size()
    ,(long)mWaitHandlers.size()
    ,(long)mTimers.size()
    ,mTimers.size()>0 ? string_mltime(mTimers.front().mExecutionTime).c_str() : "none" ,(long long)(mTimers.size()>0 ? mTimers.front().mExecutionTime-now() : 0)/MilliSecond
    ,mTimers.size()>0 ? string_mltime(mTimers.back().mExecutionTime).c_str() : "none" ,(long long)(mTimers.size()>0 ? mTimers.back().mExecutionTime-now() : 0)/MilliSecond
    #if MAINLOOP_STATISTICS
    ,(double)statisticsPeriod/Second
    ,mIoHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * mIoHandlerTime/statisticsPeriod : 0)
    ,mWaitHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * mWaitHandlerTime/statisticsPeriod : 0)
    ,mThreadSignalHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * mThreadSignalHandlerTime/statisticsPeriod : 0)
    ,mTimedHandlerTime/MilliSecond ,(int)(statisticsPeriod>0 ? 100ll * mTimedHandlerTime/statisticsPeriod : 0)
    ,(long long)mMaxTimerExecutionDelay/MilliSecond
    ,(long)mTimesTimersRanToLong
    ,(long)mMaxTimers
    ,(long)mTimesThrottlingApplied
    #endif
    #if MAINLOOP_LIBEV_BASED
    ,(mLibEvLoopP ? ev_pending_count(mLibEvLoopP) : 0)
    ,(mLibEvLoopP ? "" : "(NOT IN USE)")
    #endif
  );
}


void MainLoop::statistics_reset()
{
  #if MAINLOOP_STATISTICS
  mStatisticsStartTime = now();
  mIoHandlerTime = 0;
  mWaitHandlerTime = 0;
  mThreadSignalHandlerTime = 0;
  mTimedHandlerTime = 0;
  mMaxTimerExecutionDelay = 0;
  mTimesTimersRanToLong = 0;
  mTimesThrottlingApplied =0;
  mMaxTimers = 0;
  #endif
}


// MARK: - execution in subthreads


ChildThreadWrapperPtr MainLoop::executeInThread(ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler)
{
  return ChildThreadWrapperPtr(new ChildThreadWrapper(*this, aThreadRoutine, aThreadSignalHandler));
}


// MARK: - ChildThreadWrapper


static void *thread_start_function(void *arg)
{
  // pass into method of wrapper
  return static_cast<ChildThreadWrapper *>(arg)->startFunction();
}


void *ChildThreadWrapper::startFunction()
{
  // run the routine
  mThreadRoutine(*this);
  // signal termination
  confirmTerminated();
  return NULL;
}



ChildThreadWrapper::ChildThreadWrapper(MainLoop &aParentThreadMainLoop, ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler) :
  mThreadRunning(false),
  mParentThreadMainLoop(aParentThreadMainLoop),
  mChildSignalFd(-1),
  mParentSignalFd(-1),
  mParentSignalHandler(aThreadSignalHandler),
  mThreadRoutine(aThreadRoutine),
  mTerminationPending(false),
  mMyMainLoopP(NULL)
{
  // create a signal pipe
  int pipeFdPair[2];
  if (pipe(pipeFdPair)==0) {
    // pipe could be created
    // - save FDs
    mParentSignalFd = pipeFdPair[0]; // 0 is the reading end
    mChildSignalFd = pipeFdPair[1]; // 1 is the writing end
    // - install poll handler in the parent mainloop
    mParentThreadMainLoop.registerPollHandler(mParentSignalFd, POLLIN, boost::bind(&ChildThreadWrapper::signalPipeHandler, this, _2));
    // create a pthread (with default attrs for now
    mThreadRunning = true; // before creating it, to make sure it is set when child starts to run
    if (pthread_create(&mPthread, NULL, thread_start_function, this)!=0) {
      // error, could not create thread, fake a signal callback immediately
      mThreadRunning = false;
      if (mParentSignalHandler) {
        mParentSignalHandler(*this, threadSignalFailedToStart);
      }
    }
    else {
      // thread created ok, keep wrapper object alive
      mSelfRef = ChildThreadWrapperPtr(this);
    }
  }
  else {
    // pipe could not be created
    if (mParentSignalHandler) {
      mParentSignalHandler(*this, threadSignalFailedToStart);
    }
  }
}


ChildThreadWrapper::~ChildThreadWrapper()
{
  // cancel thread
  cancel();
  // delete mainloop if any
  if (mMyMainLoopP) {
    delete mMyMainLoopP;
    mMyMainLoopP = NULL;
  }
}


MainLoop &ChildThreadWrapper::threadMainLoop()
{
  mMyMainLoopP = &MainLoop::currentMainLoop();
  return *mMyMainLoopP;
}


// can be called from main thread to request termination from thread routine
void ChildThreadWrapper::terminate()
{
  mTerminationPending = true;
  if (mMyMainLoopP) {
    mMyMainLoopP->terminate(0);
  }
}




// called from child thread when terminated
void ChildThreadWrapper::confirmTerminated()
{
  signalParentThread(threadSignalCompleted);
}


void ChildThreadWrapper::disconnect()
{
  mParentSignalHandler = NoOP;
}


// called from child thread to send signal
void ChildThreadWrapper::signalParentThread(ThreadSignals aSignalCode)
{
  uint8_t sigByte = aSignalCode;
  write(mChildSignalFd, &sigByte, 1);
}


// cleanup, called from parent thread
void ChildThreadWrapper::finalizeThreadExecution()
{
  // synchronize with actual end of thread execution
  pthread_join(mPthread, NULL);
  mThreadRunning = false;
  // unregister the handler
  MainLoop::currentMainLoop().unregisterPollHandler(mParentSignalFd);
  // close the pipes
  close(mChildSignalFd);
  close(mParentSignalFd);
}



// can be called from parent thread
void ChildThreadWrapper::cancel()
{
  if (mThreadRunning) {
    // cancel it
    pthread_cancel(mPthread);
    // wait for cancellation to complete
    finalizeThreadExecution();
    // cancelled
    if (mParentSignalHandler) {
      ML_STAT_START_AT(mParentThreadMainLoop.now());
      mParentSignalHandler(*this, threadSignalCancelled);
      ML_STAT_ADD_AT(mParentThreadMainLoop.mThreadSignalHandlerTime, mParentThreadMainLoop.now());
    }
    // thread has ended now, object must not retain itself beyond this point
    mSelfRef.reset();
  }
}



// called on parent thread from Mainloop
bool ChildThreadWrapper::signalPipeHandler(int aPollFlags)
{
  ThreadSignals sig = threadSignalNone;
  //DBGLOG(LOG_DEBUG, "\nMAINTHREAD: signalPipeHandler with pollFlags=0x%X", aPollFlags);
  if (aPollFlags & POLLIN) {
    uint8_t sigByte;
    ssize_t res = read(mParentSignalFd, &sigByte, 1); // read signal byte
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
    if (mParentSignalHandler) {
      ML_STAT_START_AT(mParentThreadMainLoop.now());
      mParentSignalHandler(*this, sig);
      ML_STAT_ADD_AT(mParentThreadMainLoop.mThreadSignalHandlerTime, mParentThreadMainLoop.now());
    }
    if (sig==threadSignalCompleted || sig==threadSignalFailedToStart || sig==threadSignalCancelled) {
      // signal indicates thread has ended (successfully or not)
      // - in case nobody keeps this object any more, it should be deleted now
      mSelfRef.reset();
    }
    // handled some i/O
    return true;
  }
  return false; // did not handle any I/O
}


