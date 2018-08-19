
p44utils
========

[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=luz&url=https://github.com/plan44/p44utils&title=p44utils&language=&tags=github&category=software) 

"p44utils" is a set of free (opensource, GPLv3) C++ utility classes and functions for creating single-threaded, mainloop event based applications, mainly targeted at linux deamons for automation.

"p44utils" have been developed as part of the [vdcd project](https://github.com/plan44/vdcd) (a digitalSTROM virtual device container daemon), but are of more generic use, and thus have been separated from the vdcd repository (and made vdcd and other tools use p44utils as a submodule)

"p44utils" makes very light use of boost (intrusive\_ptr, bind), and has some classes that use civetweb, sqlitepp, rpi_ws281x, json-c.

Usage
-----
p44utils are meant to be included as .cpp and .hpp files into a project (usually as a git submodule) and compiled together with the project's other sources.
Examples using p44utils: [p44vdc](https://github.com/plan44/p44vdc)/[vdcd](https://github.com/plan44/vdcd), [pixelboardd](https://github.com/plan44/pixelboardd), [p44wiperd](https://github.com/plan44/p44wiperd), [lethd](https://github.com/plan44/lethd).

License
-------

p44utils are licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).

Some of the p44utils make use of third party project's code (civetweb, sqlitepp, rpi_ws281x), which is under less strict licenses such as MIT or BSD. This code is included in the "thirdparty" subfolder. Please see the LICENSE files or license header comments in the individual projects included in that folder.


Features
--------

- common base class implementing a reference counted memory management model via boost intrusive\_ptr (more efficient and easier to use than shared\_ptr)
- Mainloop as central event dispatcher, supports timers, I/O based events, subthread events, subprocess events.
- Application base class implementing command line parsing, option handling and usage message formatting
- logging with loglevels, efficiently avoiding disabled log levels to waste CPU time
- support for using unix file descriptors with the I/O based mainloop events
- helper classes for serial line communication
- helper class for socket communication
- helper class for socket based generic JSON and JSON RPC 2 protocols
- wrapper class for json-c JSON objects
- helper class for implementing persistent storage of parameters for object trees with automatic schema updating
- support for a simple http client (mainly targeted at automation APIs)
- support for JSON based http APIs
- wrappers to abstract various sources of digital and analog inputs (such as GPIO, I2C and SPI peripherals) into easy to use input or output objects, including debouncing for inputs and blinking sequences for indicator outputs.
- helper class for serial data controlled RGB and RGBW LED chains (WS281x, SK6812; APA102 planned)
- other stuff :-)

(c) 2013-2018 by Lukas Zeller / [plan44.ch](http://www.plan44.ch/opensource)







