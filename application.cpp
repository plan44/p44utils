//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "application.hpp"

#include "mainloop.hpp"

#include <stdlib.h> // for strtol
#include <sys/stat.h> // for umask
#include <signal.h>

#define TEMP_DIR_PATH "/tmp"

using namespace p44;

// MARK: - Application base class

static Application *sharedApplicationP = NULL;


Application *Application::sharedApplication()
{
  return sharedApplicationP;
}


bool Application::isRunning()
{
  if (sharedApplicationP) {
    return sharedApplicationP->mMainLoop.isRunning();
  }
  return false; // no app -> not running
}


bool Application::isTerminated()
{
  if (sharedApplicationP) {
    return sharedApplicationP->mMainLoop.isTerminated();
  }
  return true; // no app -> consider terminated as well
}



Application::Application(MainLoop &aMainLoop) :
  mMainLoop(aMainLoop)
{
  initializeInternal();
}


Application::Application() :
  mMainLoop(MainLoop::currentMainLoop()),
  mUserLevel(0)
{
  initializeInternal();
}


Application::~Application()
{
  sharedApplicationP = NULL;
}


void Application::initializeInternal()
{
  mResourcepath = "."; // current directory by default
  mDatapath = TEMP_DIR_PATH; // tmp by default
  // make random a bit "random" (not really, but ok for games)
  srand((unsigned)(MainLoop::now()>>32 ^ MainLoop::now()));
  // "publish" singleton
  sharedApplicationP = this;
  #ifndef ESP_PLATFORM
  // register signal handlers
  handleSignal(SIGHUP);
  handleSignal(SIGINT);
  handleSignal(SIGTERM);
  handleSignal(SIGUSR1);
  // make sure we have default SIGCHLD handling
  // - with SIGCHLD ignored, waitpid() cannot catch children's exit status!
  // - SIGCHLD ignored status is inherited via execve(), so if caller of execve
  //   does not restore SIGCHLD to SIG_DFL and execs us, we could be in SIG_IGN
  //   state now - that's why we set it now!
  signal(SIGCHLD, SIG_DFL);
  #endif
}


#ifndef ESP_PLATFORM

void Application::sigaction_handler(int aSignal, siginfo_t *aSiginfo, void *aUap)
{
  if (sharedApplicationP) {
    sharedApplicationP->signalOccurred(aSignal, aSiginfo);
  }
}


void Application::handleSignal(int aSignal)
{
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = Application::sigaction_handler;
  act.sa_flags = SA_SIGINFO;
  sigaction (aSignal, &act, NULL);
}


void Application::signalOccurred(int aSignal, siginfo_t *aSiginfo)
{
  if (aSignal==SIGUSR1) {
    // default for SIGUSR1 is showing mainloop statistics
    LOG(LOG_NOTICE, "SIGUSR1 requests %s", mMainLoop.description().c_str());
    mMainLoop.statistics_reset();
    return;
  }
  // default action for all other signals is terminating the program
  LOG(LOG_ERR, "Terminating because pid %d sent signal %d", aSiginfo->si_pid, aSignal);
  mMainLoop.terminate(EXIT_FAILURE);
}

#endif



int Application::main(int argc, char **argv)
{
	// NOP application
	return EXIT_SUCCESS;
}


void Application::initialize()
{
  // NOP in base class
}


void Application::cleanup(int aExitCode)
{
  // NOP in base class
}


int Application::run()
{
	// schedule the initialize() method as first mainloop method
	mMainLoop.executeNow(boost::bind(&Application::initialize, this));
	// run the mainloop
	int exitCode = mMainLoop.run();
  // show the statistic
  LOG(LOG_INFO, "Terminated: %s", mMainLoop.description().c_str());
  // clean up
  cleanup(exitCode);
  // done
  return exitCode;
}


void Application::terminateApp(int aExitCode)
{
  // have mainloop terminate with given exit code and exit run()
  mMainLoop.terminate(aExitCode);
}



void Application::terminateAppWith(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mMainLoop.terminate(EXIT_SUCCESS);
  }
  else {
    if (!LOGENABLED(LOG_ERR)) {
      // if even error logging is off, which is standard case for command line utilies (not daemons),
      // just output the error message to stderr, with no logging adornments
      const char *msg = aError->text();
      if (*msg) fprintf(stderr, "Error: %s\n", msg);
    }
    else {
      LOG(LOG_ERR, "Terminating because of error: %s", aError->text());
    }
    mMainLoop.terminate(EXIT_FAILURE);
  }
}


string Application::resourcePath(const string aResource)
{
  if (aResource.empty())
    return mResourcepath; // just return resource path
  if (aResource[0]=='/')
    return aResource; // argument is absolute path, use it as-is
  // relative to resource directory
  return mResourcepath + "/" + aResource;
}


void Application::setResourcePath(const char *aResourcePath)
{
  mResourcepath = aResourcePath;
  if (mResourcepath.size()>1 && mResourcepath[mResourcepath.size()-1]=='/') {
    mResourcepath.erase(mResourcepath.size()-1);
  }
}


string Application::dataPath(const string aDataFile)
{
  if (aDataFile.empty())
    return mDatapath; // just return data path
  if (aDataFile[0]=='/')
    return aDataFile; // argument is absolute path, use it as-is
  // relative to data directory
  return mDatapath + "/" + aDataFile;
}


void Application::setDataPath(const char *aDataPath)
{
  mDatapath = aDataPath;
  if (mDatapath.size()>1 && mDatapath[mDatapath.size()-1]=='/') {
    mDatapath.erase(mDatapath.size()-1);
  }
}


string Application::tempPath(const string aTempFile)
{
  if (aTempFile.empty())
    return TEMP_DIR_PATH; // just return temp path
  if (aTempFile[0]=='/')
    return aTempFile; // argument is absolute path, use it as-is
  // relative to temp directory
  return TEMP_DIR_PATH "/" + aTempFile;
}


#if ENABLE_JSON_APPLICATION

JsonObjectPtr Application::jsonObjOrResource(const string &aText, ErrorPtr *aErrorP, const string aPrefix)
{
  JsonObjectPtr obj;
  if (!aText.empty() && aText[0]=='{') {
    // parse JSON
    obj = JsonObject::objFromText(aText.c_str(), -1, aErrorP, true);
  }
  else {
    // pass as a simple string, will try to load resource file
    obj = jsonObjOrResource(JsonObject::newString(aText), aErrorP, aPrefix);
  }
  return obj;
}


JsonObjectPtr Application::jsonResource(string aResourceName, ErrorPtr *aErrorP, const string aPrefix)
{
  JsonObjectPtr r;
  ErrorPtr err;
  if (aResourceName.substr(0,2)=="./")
    aResourceName.erase(0,2);
  else
    aResourceName.insert(0, aPrefix);
  string fn = Application::sharedApplication()->resourcePath(aResourceName);
  r = JsonObject::objFromFile(fn.c_str(), &err, true);
  if (aErrorP) *aErrorP = err;
  return r;
}


JsonObjectPtr Application::jsonObjOrResource(JsonObjectPtr aConfig, ErrorPtr *aErrorP, const string aPrefix)
{
  ErrorPtr err;
  if (aConfig) {
    if (aConfig->isType(json_type_string)) {
      // could be resource
      string resname = aConfig->stringValue();
      if (resname.substr(resname.size()-5)==".json") {
        aConfig = jsonResource(resname, &err, aPrefix);
      }
    }
  }
  else {
    err = TextError::err("missing JSON or filename");
  }
  if (aErrorP) *aErrorP = err;
  return aConfig;
}

#endif // ENABLE_JSON_APPLICATION



string Application::version() const
{
  #if defined(P44_APPLICATION_VERSION)
  return P44_APPLICATION_VERSION; // specific version number
  #elif defined(PACKAGE_VERSION)
  return PACKAGE_VERSION; // automake package version number
  #else
  return "unknown_version"; // none known
  #endif
}


void Application::daemonize()
{
  pid_t pid, sid;

  /* already a daemon */
  if ( getppid() == 1 ) return;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* At this point we are executing as the child process */

  /* Change the file mode mask */
  umask(0);

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  /* Change the current working directory.  This prevents the current
	 directory from being locked; hence not being able to remove it. */
  if ((chdir("/")) < 0) {
    exit(EXIT_FAILURE);
  }

  /* Redirect standard files to /dev/null */
  freopen( "/dev/null", "r", stdin);
  freopen( "/dev/null", "w", stdout);
  freopen( "/dev/null", "w", stderr);
}



#ifndef ESP_PLATFORM

// MARK: ===== CmdLineApp command line application


/// constructor
CmdLineApp::CmdLineApp(MainLoop &aMainLoop) :
  inherited(aMainLoop),
  optionDescriptors(NULL)
{
}

/// destructor
CmdLineApp::~CmdLineApp()
{
}


CmdLineApp *CmdLineApp::sharedCmdLineApp()
{
  return dynamic_cast<CmdLineApp *>(Application::sharedApplication());
}


void CmdLineApp::setCommandDescriptors(const char *aSynopsis, const CmdLineOptionDescriptor *aOptionDescriptors)
{
  optionDescriptors = aOptionDescriptors;
  synopsis = aSynopsis ? aSynopsis : "Usage: %1$s";
}


#define MAX_INDENT 40
#define MAX_LINELEN 100

void CmdLineApp::showUsage()
{
  // print synopsis
  fprintf(stderr, synopsis.c_str(), invocationName.c_str());
  // print options
  int numDocumentedOptions = 0;
  // - calculate indent
  ssize_t indent = 0;
  const CmdLineOptionDescriptor *optionDescP = optionDescriptors;
  bool anyShortOpts = false;
  while (optionDescP && (optionDescP->longOptionName!=NULL || optionDescP->shortOptionChar!='\x00')) {
    const char *desc = optionDescP->optionDescription;
    if (desc) {
      // documented option
      numDocumentedOptions++;
      if (optionDescP->shortOptionChar) {
        anyShortOpts = true;
      }
      size_t n = 0;
      if (optionDescP->longOptionName) {
        n += strlen(optionDescP->longOptionName)+2; // "--XXXXX"
      }
      if (optionDescP->withArgument) {
        const char *p = strchr(desc, ';');
        if (p) {
          n += 3 + (p-desc); // add room for argument description
        }
      }
      if (n>indent) indent = n; // new max
    }
    optionDescP++;
  }
  if (anyShortOpts) indent += 4; // "-X, " prefix
  indent += 2 + 2; // two at beginning, two at end
  if (indent>MAX_INDENT) indent = MAX_INDENT;
  // - print options
  if (numDocumentedOptions>0) {
    fprintf(stderr, "Options:\n");
    optionDescP = optionDescriptors;
    while (optionDescP && (optionDescP->longOptionName!=NULL || optionDescP->shortOptionChar!='\x00')) {
      //  fprintf(stderr, "\n");
      const char *desc = optionDescP->optionDescription;
      if (desc) {
        ssize_t remaining = indent;
        fprintf(stderr, "  "); // start indent
        remaining -= 2;
        if (anyShortOpts) {
          // short names exist, print them for those options that have them
          if (optionDescP->shortOptionChar)
            fprintf(stderr, "-%c", optionDescP->shortOptionChar);
          else
            fprintf(stderr, "  ");
          remaining -= 2;
          if (optionDescP->longOptionName) {
            // long option follows, fill up
            if (optionDescP->shortOptionChar)
              fprintf(stderr, ", ");
            else
              fprintf(stderr, "  ");
            remaining -= 2;
          }
        }
        // long name
        if (optionDescP->longOptionName) {
          fprintf(stderr, "--%s", optionDescP->longOptionName);
          remaining -= strlen(optionDescP->longOptionName)+2;
        }
        // argument
        if (optionDescP->withArgument) {
          const char *p = strchr(desc, ';');
          if (p) {
            size_t n = (p-desc);
            string argDesc(desc,n);
            fprintf(stderr, " <%s>", argDesc.c_str());
            remaining -= argDesc.length()+3;
            desc += n+1; // desc starts after semicolon
          }
        }
        // complete first line indent
        if (remaining>0) {
          while (remaining-- > 0) fprintf(stderr, " ");
        }
        else {
          fprintf(stderr, "  "); // just two spaces
          remaining -= 2; // count these
        }
        // print option description, properly indented and word-wrapped
        // Note: remaining is 0 or negative in case arguments reached beyond indent
        if (desc) {
          ssize_t listindent = 0;
          while (*desc) {
            ssize_t ll = MAX_LINELEN-indent+remaining;
            remaining = 0;
            ssize_t l = 0;
            ssize_t lastWs = -1;
            // scan for list indent
            if (*desc=='-') {
              // next non-space is list indent
              while (desc[++listindent]==' ');
            }
            // scan for end of text, last space or line end
            const char *e = desc;
            while (*e) {
              if (*e==' ') lastWs = l;
              else if (*e=='\n') {
                // explicit line break
                listindent = 0;
                break;
              }
              // check line lenght
              l++;
              if (l>=ll) {
                // line gets too long, break at previous space
                if (lastWs>0) {
                  // reposition end
                  e = desc+lastWs;
                }
                break;
              }
              // next
              e++;
            }
            // e now points to either LF, or breaking space, or NUL (end of text)
            // - output chars between desc and e
            while (desc<e) fprintf(stderr, "%c", *desc++);
            // - if not end of text, insert line break and new indent
            if (*desc) {
              // there is a next line
              fprintf(stderr, "\n");
              // indent
              ssize_t r = indent+listindent;
              while (r-- > 0) fprintf(stderr, " ");
              desc++; // skip the LF or space that caused the line end
            }
          }
        }
        // end of option, next line
        fprintf(stderr, "\n");
      }
      // next option
      optionDescP++;
    }
  } // if any options to show
  fprintf(stderr, "\n");
}


bool CmdLineApp::parseCommandLine(int aArgc, char **aArgv)
{
  if (aArgc>0) {
    invocationName = aArgv[0];
    int rawArgIndex=1;
    while(rawArgIndex<aArgc) {
      const char *argP = aArgv[rawArgIndex];
      if (*argP=='-') {
        // option argument
        argP++;
        bool longOpt = false;
        string optName;
        string optArg;
        bool optArgFound = false;
        if (*argP=='-') {
          // long option
          longOpt = true;
          optName = argP+1;
        }
        else {
          // short option
          optName = argP;
          if (optName.length()>1 && optName[1]!='=') {
            // option argument follows directly after single char option
            optArgFound = true; // is non-empty by definition
            optArg = optName.substr(1,string::npos);
            optName.erase(1,string::npos);
          }
        }
        // search for option argument directly following option separated by equal sign
        string::size_type n = optName.find("=");
        if (n!=string::npos) {
          optArgFound = true; // explicit specification, counts as option argument even if empty string
          optArg = optName.substr(n+1,string::npos);
          optName.erase(n,string::npos);
        }
        // search for option descriptor
        const CmdLineOptionDescriptor *optionDescP = optionDescriptors;
        bool optionFound = false;
        while (optionDescP && (optionDescP->longOptionName!=NULL || optionDescP->shortOptionChar!='\x00')) {
          // not yet end of descriptor list
          if (
            (longOpt && optName==optionDescP->longOptionName) ||
            (!longOpt && optName[0]==optionDescP->shortOptionChar)
          ) {
            // option match found
            if (!optionDescP->withArgument) {
              // option without argument
              if (optArgFound) {
                fprintf(stderr, "Option '%s' does not expect an argument\n", optName.c_str());
                showUsage();
                terminateApp(EXIT_FAILURE);
                return false; // terminated
              }
            }
            else {
              // option with argument
              if (!optArgFound) {
                // check for next arg as option arg
                if (rawArgIndex<aArgc-1) {
                  // there is a next argument, use it as option argument
                  optArgFound = true;
                  optArg = aArgv[++rawArgIndex];
                }
              }
              if (!optArgFound) {
                fprintf(stderr, "Option '%s' requires an argument\n", optName.c_str());
                showUsage();
                terminateApp(EXIT_FAILURE);
                return false; // terminated
              }
            }
            // now have option processed by subclass
            if (processOption(*optionDescP, optArg.c_str())) {
              // option processed, check if it has terminated the app
              if (isTerminated()) return false;
            }
            else {
              // not processed, store instead
              if (optionDescP->longOptionName)
                optName = optionDescP->longOptionName;
              else
                optName[0] = optionDescP->shortOptionChar;
              // save in map
              options[optName] = optArg;
            }
            optionFound = true;
            break;
          }
          // next in list
          optionDescP++;
        }
        if (!optionFound) {
          fprintf(stderr, "Unknown Option '%s'\n", optName.c_str());
          showUsage();
          terminateApp(EXIT_FAILURE);
          return false;  // terminated
        }
      }
      else {
        // non-option argument
        // - have argument processed by subclass
        if (!processArgument(argP)) {
          // not processed, store instead
          arguments.push_back(argP);
        }
      }
      // next argument
      rawArgIndex++;
    }
  }
  return true; // parsed, not terminated
}


bool CmdLineApp::processOption(const CmdLineOptionDescriptor &aOptionDescriptor, const char *aOptionValue)
{
  // directly process "help" option (long name must be "help", short name can be anything but usually is 'h')
  if (!aOptionDescriptor.withArgument && strucmp(aOptionDescriptor.longOptionName,"help")==0) {
    showUsage();
    terminateApp(EXIT_SUCCESS);
  }
  else if (!aOptionDescriptor.withArgument && strucmp(aOptionDescriptor.longOptionName,"version")==0) {
    fprintf(stdout, "%s\n", version().c_str());
    terminateApp(EXIT_SUCCESS);
  }
  else if (aOptionDescriptor.withArgument && strucmp(aOptionDescriptor.longOptionName,"resourcepath")==0) {
    setResourcePath(aOptionValue);
  }
  else if (aOptionDescriptor.withArgument && strucmp(aOptionDescriptor.longOptionName,"datapath")==0) {
    setDataPath(aOptionValue);
  }
  else if (aOptionDescriptor.withArgument && strucmp(aOptionDescriptor.longOptionName,"userlevel")==0) {
    mUserLevel = atoi(aOptionValue);
  }
  else {
    return false; // not processed
  }
  return true; // option already processed
}


const char *CmdLineApp::getInvocationName()
{
  return invocationName.c_str();
}


void CmdLineApp::resetCommandLine()
{
  invocationName.clear();
  synopsis.clear();
  options.clear();
  arguments.clear();
}


const char *CmdLineApp::getOption(const char *aOptionName, const char *aDefaultValue)
{
  const char *opt = aDefaultValue;
  OptionsMap::iterator pos = options.find(aOptionName);
  if (pos!=options.end()) {
    opt = pos->second.c_str();
  }
  return opt;
}


bool CmdLineApp::getIntOption(const char *aOptionName, int &aInteger)
{
  const char *opt = getOption(aOptionName);
  if (opt) {
    char *e = NULL;
    long i = strtol(opt, &e, 0);
    if (e && *e==0) {
      aInteger = (int)i;
      return true;
    }
  }
  return false; // no such option
}


bool CmdLineApp::getStringOption(const char *aOptionName, const char *&aCString)
{
  const char *opt = getOption(aOptionName);
  if (opt) {
    aCString = opt;
    return true;
  }
  return false; // no such option
}


bool CmdLineApp::getStringOption(const char *aOptionName, string &aString)
{
  const char *opt = getOption(aOptionName);
  if (opt) {
    aString = opt;
    return true;
  }
  return false; // no such option
}



size_t CmdLineApp::numOptions()
{
  return options.size();
}


const char *CmdLineApp::getArgument(size_t aArgumentIndex)
{
  if (aArgumentIndex>=arguments.size()) return NULL;
  return arguments[aArgumentIndex].c_str();
}


bool CmdLineApp::getStringArgument(size_t aArgumentIndex, string &aArg)
{
  const char *a = getArgument(aArgumentIndex);
  if (!a) return false;
  aArg = a;
  return true;
}


bool CmdLineApp::getIntArgument(size_t aArgumentIndex, int &aInteger)
{
  const char *a = getArgument(aArgumentIndex);
  if (!a || *a==0) return false;
  char *e = NULL;
  long i = strtol(a, &e, 0);
  if (e && *e==0) {
    aInteger = (int)i;
    return true;
  }
  return false;
}





size_t CmdLineApp::numArguments()
{
  return arguments.size();
}


void CmdLineApp::processStandardLogOptions(bool aForDaemon, int aDefaultErrLevel)
{
  // determines the mode
  SETDAEMONMODE(aForDaemon);
  if (DAEMONMODE) {
    int loglevel = LOG_NOTICE; // moderate logging by default
    getIntOption("loglevel", loglevel);
    SETLOGLEVEL(loglevel);
    int errLevel = aDefaultErrLevel;
    getIntOption("errlevel", errLevel);
    bool dontLogErrors = false;
    if (getOption("dontlogerrors")) dontLogErrors = true;
    SETERRLEVEL(errLevel, !dontLogErrors); // errors and more serious go to stderr, all log goes to stdout
  }
  else {
    int loglevel = LOG_CRIT; // almost no logging by default
    getIntOption("loglevel", loglevel);
    SETLOGLEVEL(loglevel);
  }
  SETDELTATIME(getOption("deltatstamps"));
}

#endif // !ESP_PLATFORM

