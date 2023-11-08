
p44utils
========

*[[if you want to support p44utils development, please consider to sponsor plan44]](https://github.com/sponsors/plan44)* 

*p44utils* is a set of free (opensource, GPLv3) C++ utility classes and functions for creating single-threaded, mainloop event based applications, mainly targeted at linux daemons for automation.

*p44utils* have been developed as part of the [vdcd project](https://github.com/plan44/vdcd) (a Digital Strom virtual device container daemon), but are of more generic use, and thus have been separated from the vdcd repository (and made vdcd and other tools use p44utils as a submodule)

*p44utils* makes very light use of boost (intrusive\_ptr, bind), and has some classes that use civetweb, sqlitepp, rpi_ws281x, json-c, uwsc, libmodbus, lvgl.

Usage
-----
*p44utils* are meant to be included as .cpp and .hpp files into a project (usually as a git submodule) and compiled together with the project's other sources.

Some aspects can be configured via the `p44utils_config.hpp` header file, which should be placed somewhere in the project's header search path. There is a template file name `p44utils_config_TEMPLATE.hpp` which can be copied into the project and renamed to `p44utils_config.hpp`.

Examples using p44utils: [p44vdc](https://github.com/plan44/p44vdc)/[vdcd](https://github.com/plan44/vdcd), [pixelboardd](https://github.com/plan44/pixelboardd), [p44wiperd](https://github.com/plan44/p44wiperd), [p44featured](https://github.com/plan44/p44featured).

License
-------

p44utils are licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).

Some of the p44utils make use of third party project's code (e.g. civetweb, sqlitepp, rpi_ws281x), which is under less strict licenses such as MIT or BSD. This code is included in the "thirdparty" subfolder. Please see the LICENSE files or license header comments in the individual projects included in that folder.


Features
--------

- common base class implementing a reference counted memory management model via boost intrusive\_ptr (more efficient and easier to use than shared\_ptr).
- Mainloop as central event dispatcher, supports timers, I/O based events, subthread events, subprocess events (standalone or based on/cooperating with libev)
- Application base class implementing command line parsing, option handling and usage message formatting.
- logging with loglevels, efficiently avoiding disabled log levels to waste CPU time.
- support for using unix file descriptors with the I/O based mainloop events.
- a full-featured script language *p44script*, designed for exposing to end users for allowing very flexible but still simple to use customisation. See [p44-techdocs](https://plan44.ch/p44-techdocs/en/script_ref/) for details.
- helper classes for serial line communication.
- helper class for socket communication.
- helper class for socket based generic JSON and JSON RPC 2 protocols.
- wrapper class for json-c JSON objects.
- helper class for implementing persistent storage of parameters for object trees with automatic schema updating.
- support for a simple http client (mainly targeted at automation APIs).
- support for websocket client via libuwsc.
- support for JSON based http APIs.
- wrappers to abstract various sources of digital and analog inputs (such as GPIO, I2C and SPI peripherals) into easy to use input or output objects, including debouncing for inputs and blinking sequences for indicator outputs.
- helper class for serial data controlled RGB and RGBW LED chains (WS281x, SK6812 etc.), and arranging multiple chains to form a display surface that can be used with [p44lrgraphics](https://github.com/plan44/p44lrgraphics).
- value animator class which provides all functionality to change a numeric property over a given time with a specific curve, for example properties of [p44lrgraphics](https://github.com/plan44/p44lrgraphics) views or a PWM output.
- support for using [lvgl](https://lvgl.io) embedded graphics library in p44utils based applications, including a JSON-based UI configuration mechanism.
- support for bidirectional DC motor control including current supervision and end contacts.
- support for modbus client and server via TCP or RS485.
- utils: simple utility functions that DO NOT depend on other p44utils classes.
- extutils: simple utility functions that depend on other p44utils classes.
- other stuff :-)

Supporting p44utils
-------------------

1. use it!
2. support development via [github sponsors](https://github.com/sponsors/plan44) or [flattr](https://flattr.com/@luz)
3. Discuss it in the [plan44 community forum](https://forum.plan44.ch/t/opensource-c-vdcd).
3. contribute patches, report issues and suggest new functionality [on github](https://github.com/plan44/p44utils) or in the [forum](https://forum.plan44.ch/t/opensource-c-vdcd).
5. Buy plan44.ch [products](https://plan44.ch/automation/products.php) - sales revenue is paying the time for contributing to opensource projects :-)


(c) 2013-2022 by Lukas Zeller / [plan44.ch](http://www.plan44.ch/opensource)
