//
//  Copyright (c) 2013-2015 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__utils__
#define __p44utils__utils__

#include <string>
#include <stdarg.h>
#include <stdint.h>

#ifndef __printflike
#define __printflike(...)
#endif

using namespace std;

namespace p44 {

  /// tristate type
  typedef enum {
    yes = 1,
    no = 0,
    undefined = -1
  } Tristate;


  /// printf-style format into std::string
  /// @param aFormat printf-style format string
  /// @return formatted string
  void string_format_v(std::string &aStringObj, bool aAppend, const char *aFormat, va_list aArgs);

  /// printf-style format into std::string
  /// @param aFormat printf-style format string
  /// @return formatted string
  std::string string_format(const char *aFormat, ...) __printflike(1,2);

  /// printf-style format appending to std::string
  /// @param aStringToAppendTo std::string to append format to
  /// @param aFormat printf-style format string
  void string_format_append(std::string &aStringToAppendTo, const char *aFormat, ...) __printflike(2,3);

  /// get next line from file into string
  /// @param aFile file open for read
  /// @param aLine string to store read line (without CRLF)
  /// @return true if line could be read, false otherwise
  bool string_fgetline(FILE *aFile, string &aLine);

	/// always return a valid C String, if NULL is passed, an empty string is returned
	/// @param aNULLOrCStr NULL or C-String
	/// @return the input string if it is non-NULL, or an empty string
	const char *nonNullCStr(const char *aNULLOrCStr);

  /// return simple (non locale aware) ASCII lowercase version of string
  /// @param aString a string
  /// @return lowercase (char by char tolower())
  string lowerCase(const char *aString);
  string lowerCase(const string &aString);

  /// return string quoted such that it works as a single shell argument
  /// @param aString a string
  /// @return quoted string
  string shellQuote(const char *aString);
  string shellQuote(const string &aString);

  /// return string with trailimg and/or leading spaces removed
  /// @param aString a string
  /// @param aLeading if set, remove leading spaces
  /// @param aTrailing if set, remove trailing spaces
  /// @return trimmed string
  string trimWhiteSpace(const string &aString, bool aLeading=true, bool aTrailing=true);

  /// return next line from buffer
  /// @param aCursor at entry, must point to the beginning of a line. At exit, will point
  ///   to the beginning of the next line or the terminating NUL
  /// @param aLine the contents of the line, without any CR or LF chars
  /// @return true if a line could be extracted, false if end of text
  bool nextLine(const char * &aCursor, string &aLine);

  /// return next TSV or CSV field from line
  /// @param aCursor at entry, must point to the beginning of a CSV field (beginning of line or
  ///   result of a previous nextCSVField() call.
  ///   After return, this is updated to either
  ///   - point to the beginning of the next field or the end of input
  ///   - NULL (but still returning true) if at end of incomplete quoted field (no closing quote found). This
  ///     condition can be used to support newlines embedded in quoted field values, by getting next line and using aContinueQuoted
  /// @param aField the unescaped content of the field
  /// @param aSeparator the separator. If 0 (default), any unquoted ",", ";" or TAB is considered a separator.
  /// @param aContinueQuoted if set, aCursor must be within a quoted field, and aField will be added to to complete it
  /// @return true if a CSV field could be extracted, false if no more fields found
  bool nextCSVField(const char * &aCursor, string &aField, char aSeparator=0, bool aContinueQuoted=false);


  /// return next part from a separated string
  /// @param aCursor at entry, must point to the beginning of a string
  ///   After return, this is updated to point to next char following aSeparator or to end-of-string
  /// @param aPart the part found between aCursor and the next aSeparator (or end of string)
  /// @param aSeparator the separator char
  /// @param aStopAtEOL if set, reaching EOL (CR or LF) also ends part
  /// @return true if a part could be extracted, false if no more parts found
  bool nextPart(const char *&aCursor, string &aPart, char aSeparator, bool aStopAtEOL=false);

  /// extract key and value from "KEY: VALUE" type header string
  /// @param aInput the contents of the line to extract key/value from
  /// @param aKey the key (everything before the first aSeparator, stripped from surrounding whitespace)
  /// @param aValue the value (everything after the first non-whitespace after the first aSeparator)
  /// @param aSeparator the separating character, defaults to colon ':'
  /// @return true if key/value could be extracted
  bool keyAndValue(const string &aInput, string &aKey, string &aValue, char aSeparator=':');

  /// split URL into its parts
  /// @param aURI a URI in the [protocol:][//][user[:password]@]host[/doc] format
  /// @param aProtocol if not NULL, returns the protocol (e.g. "http"), empty string if none
  /// @param aHost if not NULL, returns the host (hostname only or host:port)
  /// @param aDoc if not NULL, returns the document part (path and possibly query), empty string if none
  /// @param aUser if not NULL, returns user, empty string if none
  /// @param aPasswd if not NULL, returns password, empty string if none
  void splitURL(const char *aURI, string *aProtocol, string *aHost, string *aDoc, string *aUser=NULL, string *aPasswd=NULL);

  /// split host specification into hostname and port
  /// @param aHostSpec a host specification in the host[:port] format
  /// @param aHostName if not NULL, returns the host name/IP address, empty string if none
  /// @param aPortNumber if not NULL, returns the port number. Is left untouched if no port number is specified
  ///   (such that variable passed can be initialized with the default port to use beforehand)
  void splitHost(const char *aHostSpec, string *aHostName, uint16_t *aPortNumber);

  /// Verify or calculate GTIN check digit
  /// @param aGtin full GTIN, including possibly inaccurate check digit
  /// @return value to add to aGtin to make it a valid GTIN (-9..9)
  /// @note
  /// - if you want to check the GTIN, check for a result==0
  /// - if you want to calculate the check digit, supply aGTIN with last digit 0
  /// - if you want to make a GTIN valid, just add the result to aGtin
  int gtinCheckDigit(uint64_t aGtin);

  /// hex string to binary string conversion
  /// @param aHexString string in hex notation
  /// @return binary string
  string hexToBinaryString(const char *aHexString);

  /// hex string to binary string conversion
  /// @param aBinaryString binary string
  /// @return hex string
  string binaryToHexString(const string &aBinaryString);

} // namespace p44

#endif /* defined(__p44utils__utils__) */
