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

#include "consolekey.hpp"


#include <sys/select.h>
#include <termios.h>
#include <sys/ioctl.h>

using namespace p44;


// MARK: - console key

ConsoleKey::ConsoleKey(char aKeyCode, const char *aDescription, bool aInitialState)
{
  // check type of key
  initialState = aInitialState;
  canToggle = false;
  char c = tolower(aKeyCode);
  if (c>='a' && c<='z') {
    aKeyCode = c; // use lowercase only
    canToggle = true;
  }
  // store params
  keyCode = aKeyCode;
  description = aDescription;
  // display usage
  if (canToggle)
    printf("- Console input '%s' - Press '%c' to pulse, '%c' to toggle state\n", description.c_str(), keyCode, toupper(keyCode));
  else
    printf("- Console input '%s' - Press '%c' to pulse\n", description.c_str(), keyCode);
  // initial state
  state = aInitialState;
  if (state) {
    printf("  Initial state is active: 1\n");
  }
}


ConsoleKey::~ConsoleKey()
{
  keyHandlerTicket.cancel();
}


bool ConsoleKey::isSet()
{
  return state;
}


void ConsoleKey::setConsoleKeyHandler(ConsoleKeyHandlerCB aHandler)
{
  keyHandler = aHandler;
}


void ConsoleKey::setState(bool aState)
{
  keyHandlerTicket.cancel();
  state = aState;
  printf("- Console input '%s' - changed to %d\n", description.c_str(), state);
  reportState();
}


void ConsoleKey::toggle()
{
  keyHandlerTicket.cancel();
  state = !state;
  printf("- Console input '%s' - toggled to %d\n", description.c_str(), state);
  reportState();
}


void ConsoleKey::pulse()
{
  keyHandlerTicket.executeOnce(boost::bind(&ConsoleKey::pulseEnd, this), 200*MilliSecond);
  if (state==initialState) {
    state = !initialState;
    reportState();
  }
  printf("- Console input '%s' - pulsed to %d for 200mS\n", description.c_str(), state);
}


void ConsoleKey::pulseEnd()
{
  if (state!=initialState) {
    state = initialState;
    reportState();
  }
}


void ConsoleKey::reportState()
{
  if (keyHandler) {
    keyHandler(state, MainLoop::now());
  }
}



// MARK: - console key manager singleton

static ConsoleKeyManager *consoleKeyManager = NULL;


ConsoleKeyManager *ConsoleKeyManager::sharedKeyManager()
{
  if (consoleKeyManager==NULL) {
    consoleKeyManager = new ConsoleKeyManager();
  }
  return consoleKeyManager;
}


ConsoleKeyManager::ConsoleKeyManager() :
  termInitialized(false)
{
  // install polling
  keyPollTicket.executeOnce(boost::bind(&ConsoleKeyManager::consoleKeyPoll, this, _1));
}


ConsoleKeyManager::~ConsoleKeyManager()
{
}


ConsoleKeyPtr ConsoleKeyManager::newConsoleKey(char aKeyCode, const char *aDescription,  bool aInitialState)
{
  ConsoleKeyPtr newKey = ConsoleKeyPtr(new ConsoleKey(aKeyCode, aDescription, aInitialState));
  // store in map
  keyMap[newKey->keyCode] = newKey;
  // return
  return newKey;
}


void ConsoleKeyManager::setKeyPressHandler(ConsoleKeyPressCB aHandler)
{
  keyPressHandler = aHandler;
}






int ConsoleKeyManager::kbHit()
{
  static const int STDIN = 0;

  if (!termInitialized) {
    // Use termios to turn off line buffering
    termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag &= ~ICANON;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
    termInitialized = true;
  }
  // return number of bytes waiting
  int bytesWaiting;
  ioctl(STDIN, FIONREAD, &bytesWaiting);
  return bytesWaiting;
}


#define KEY_POLL_INTERVAL (50*MilliSecond)
#define KEY_POLL_TOLERANCE (20*MilliSecond)

void ConsoleKeyManager::consoleKeyPoll(MLTimer &aTimer)
{
  // process all pending console input
  while (kbHit()>0) {
    char  c = getchar();
    bool handled = false;
    if (keyPressHandler) {
      // call custom keypress handler
      handled = keyPressHandler(c);
    }
    if (!handled) {
      bool toggle = false;
      // A..Z are special "sticky" toggle variants of a..z
      if (c>='A' && c<='Z') {
        // make lowercase
        c = tolower(c);
        // toggle
        toggle = true;
      }
      // search key in map
      ConsoleKeyMap::iterator keypos = keyMap.find(c);
      if (keypos!=keyMap.end()) {
        // key is mapped
        if (toggle) {
          // toggle state
          keypos->second->toggle();
        }
        else {
          // pulse
          keypos->second->pulse();
        }
      }
    }
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, KEY_POLL_INTERVAL, KEY_POLL_TOLERANCE);
}





