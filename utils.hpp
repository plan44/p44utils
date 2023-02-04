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

#ifndef __p44utils__utils__
#define __p44utils__utils__

#include "p44utils_minimal.hpp"
#include "p44utils_defs.hpp"

#include <string>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#ifndef __printflike
#define __printflike(...)
#endif

using namespace std;

/// Basic utilities that DO NOT HAVE DEPENDENCIES on other p44utils classes
/// @note utilities that depends on p44::Error or other p44utils classes are in "extutils"

namespace p44 {

  /// printf-style format into string
  /// @param aFormat printf-style format string
  /// @param aStringObj string to receive formatted result
  /// @param aAppend if true, aStringObj will be appended to, otherwise contents will be replaced
  /// @param aArgs va_list of vprintf arguments
  void string_format_v(string &aStringObj, bool aAppend, const char *aFormat, va_list aArgs) __printflike(3,0);

  /// printf-style format into string
  /// @param aFormat printf-style format string
  /// @return formatted string
  string string_format(const char *aFormat, ...) __printflike(1,2);

  /// replace occurrence of one string by another
  /// @param aString string to search for placeholders
  /// @param aPlaceholder string to search for
  /// @param aSubstitute string to substitute wherever the placeholder is found
  /// @param aNumTimes how many times to substitute (default = 0 = all occurrences)
  /// @return string with placeholders substituted
  string string_substitute(const string aString, const string aPlaceholder, const string aSubstitute, int aNumTimes = 0);

  /// printf-style format appending to string
  /// @param aStringToAppendTo string to append formatted string to
  /// @param aFormat printf-style format string
  void string_format_append(string &aStringToAppendTo, const char *aFormat, ...) __printflike(2,3);

  /// printf-style format appending a path element to an existing file path
  /// @param aPathToAppendTo string to append formatted string to
  /// @param aFormat printf-style format string
  /// @note if the path is not empty, and does not yet end in a path delimiter, a path delimiter will be appended first
  void pathstring_format_append(string &aPathToAppendTo, const char *aFormat, ...) __printflike(2,3);

  /// make path a directory, i.e. make sure it ends in a path delimiter
  /// @param aPathToMakeDir path string
  void pathstring_make_dir(string &aPathToMakeDir);

  /// strftime with output to string
  /// @param aFormat strftime-style format string
  /// @param aTimeP aTime time to format, or NULL for current local time
  /// @return formatted time string
  string string_ftime(const char *aFormat, const struct tm *aTimeP = NULL) __strftimelike(1);

  /// strftime appending to string
  /// @param aStringToAppendTo string to append formatted time to
  /// @param aFormat strftime-style format string
  /// @param aTimeP aTime time to format, or NULL for current local time
  void string_ftime_append(string &aStringToAppendTo, const char *aFormat, const struct tm *aTimeP = NULL) __strftimelike(2);

  /// get next line from file into string
  /// @param aFile file open for read
  /// @param aLine string to store read line (without CRLF)
  /// @return true if line could be read, false otherwise
  bool string_fgetline(FILE *aFile, string &aLine);

  /// get entire file into string
  /// @param aFile file open for read
  /// @param aData string to store data
  /// @return true if file could be read, false otherwise
  bool string_fgetfile(FILE *aFile, string &aData);

  /// get whitespace-trimmed first line of file into string
  /// @param aFileName filename/path to read from
  /// @param aLine string to store first line, whitespace trimmed front and end
  /// @return true if file could be read, false otherwise
  bool string_fgetfirstline(const string aFileName, string &aLine);

	/// always return a valid C String, if NULL is passed, an empty string is returned
	/// @param aNULLOrCStr NULL or C-String
	/// @return the input string if it is non-NULL, or an empty string
	const char *nonNullCStr(const char *aNULLOrCStr);

  /// case insensitive strncmp with separate size for both strings
  /// @param s1 first string, if NULL, it is considered empty string
  /// @param s2 second string, if NULL, it is considered empty string
  /// @param len1 length of first string, or 0 if first string is zero terminated.
  /// @param len2 length of second string, or 0 if second string is zero terminated.
  /// @return same as with standard strncasecmp(), but on returns 0 only if strings are equal AND of same length
  /// @note used like strucmp(varstr, "literal", varstrsz) to compare a partial string sequence to a literal
  int strucmp(const char *s1, const char *s2, size_t len1=0, size_t len2=0);

  /// case insensitive comparison functor for std containers, based on strucmp
  struct lessStrucmp : public std::binary_function<string, string, bool> {
    bool operator()(const string &lhs, const string &rhs) const {
      return strucmp(lhs.c_str(), rhs.c_str()) < 0 ;
    }
  };

  /// convenience for case insensitive equaltest
  bool uequals(const string& aString, const char *aCmp);
  bool uequals(const string& aString, const string& aCmp);

  /// return simple (non locale aware) ASCII lowercase version of string
  /// @param aStringP a C string pinter
  /// @param aMaxSize max number of chars to read fom aStringP
  /// @return lowercase (char by char tolower())
  string lowerCase(const char *aStringP, size_t aMaxSize = 0);

  /// return simple (non locale aware) ASCII lowercase version of string
  /// @param aString string pinter
  /// @return lowercase (char by char tolower())
  string lowerCase(const string &aString);

  /// return simple (non locale aware) ASCII uppercase version of string
  /// @param aStringP a C string pinter
  /// @param aMaxSize max number of chars to read fom aStringP
  /// @return uppercase (char by char tolower())
  string upperCase(const char *aStringP, size_t aMaxSize = 0);

  /// return simple (non locale aware) ASCII uppercase version of string
  /// @param aString string pinter
  /// @return uppercase (char by char toupper())
  string upperCase(const string &aString);

  /// return string quoted such that it safely works as a single shell argument with no variable expansion
  /// @param aString a string
  /// @return (single) quoted shell argument string, if original contains single quotes these are represented as '"'"'
  string shellQuote(const char *aString);
  string shellQuote(const string &aString);

  /// return string quoted such that it works as a C string literal
  /// @param aString a string
  /// @return (double) quoted C string
  string cstringQuote(const char *aString);
  string cstringQuote(const string &aString);


  /// return string with trailimg and/or leading spaces removed
  /// @param aString a string
  /// @param aLeading if set, remove leading spaces
  /// @param aTrailing if set, remove trailing spaces
  /// @return trimmed string
  string trimWhiteSpace(const string &aString, bool aLeading = true, bool aTrailing = true);

  /// return string as single line (LF, CR, TAB converted to spaces)
  /// @param aString a string
  /// @param aCompactWSRuns if true, runs of consecutive spaces/LFs/CRs/TABs will be compacted to a single space
  /// @param aEllipsisAtMax if >0, output string will not be longer than specified value, if input string is longer, it will made end with ellipsis ("...")
  string singleLine(const char *aString, bool aCompactWSRuns = true, size_t aEllipsisAtMax = 0);

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
  bool nextCSVField(const char * &aCursor, string &aField, char aSeparator = 0, bool aContinueQuoted = false);


  /// return next part from a separated string
  /// @param aCursor at entry, must point to the beginning of a string
  ///   After return, this is updated to point to next char following aSeparator or to end-of-string
  /// @param aPart the part found between aCursor and the next aSeparator (or end of string)
  /// @param aSeparator the separator char
  /// @param aStopAtEOL if set, reaching EOL (CR or LF) also ends part
  /// @return true if a part could be extracted, false if no more parts found
  bool nextPart(const char *&aCursor, string &aPart, char aSeparator, bool aStopAtEOL = false);

  /// extract key and value from "KEY: VALUE" type header string
  /// @param aInput the contents of the line to extract key/value from
  /// @param aKey the key (everything before the first aSeparator, stripped from surrounding whitespace)
  /// @param aValue the value (everything after the first non-whitespace after the first aSeparator)
  /// @param aSeparator the separating character, defaults to colon ':'
  /// @return true if key/value could be extracted
  bool keyAndValue(const string &aInput, string &aKey, string &aValue, char aSeparator = ':');

  /// pick contents of a (properly terminated) HTML or XML tag
  /// @param aInput the contents of the XML/HTML text to extract the tag contents from
  /// @param aTag the name of the XML/HTML tag to extract the tag contents from
  /// @param aContents will be assigned contents (everything between start and end)
  /// @param aStart where to start looking for tag in aInput
  /// @return 0 if tag not found, position of next char behind closing tag when tag was found
  /// @note primitive picking only, does not follow hierarchy so nested tags of same name dont work
  ///   also does not process entities etc.
  size_t pickTagContents(const string &aInput, const string aTag, string &aContents, size_t aStart = 0);

  /// split URL into its parts
  /// @param aURI a URI in the [protocol:][//][user[:password]@]host[/doc] format
  /// @param aProtocol if not NULL, returns the protocol (e.g. "http"), empty string if none
  /// @param aHost if not NULL, returns the host (hostname only or host:port)
  /// @param aDoc if not NULL, returns the document part (path and possibly query), empty string if none
  /// @param aUser if not NULL, returns user, empty string if none
  /// @param aPasswd if not NULL, returns password, empty string if none
  void splitURL(const char *aURI, string *aProtocol, string *aHost, string *aDoc, string *aUser = NULL, string *aPasswd = NULL);

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
  /// @param aHexString bytes string in hex notation, byte-separating dashes and colons allowed
  /// @param aSpacesAllowed if true, spaces may be used between bytes, and single digits can be used for bytes with value 0..F
  /// @param aMaxBytes if>0, this is the max number of output bytes produced reading from aHexString
  /// @return binary string
  string hexToBinaryString(const char *aHexString, bool aSpacesAllowed = false, size_t aMaxBytes = 0);

  /// hex string to binary string conversion
  /// @param aBinaryString binary string
  /// @param aSeparator character to be used to separate hex bytes, 0 for no separator
  /// @return hex string
  string binaryToHexString(const string &aBinaryString, char aSeparator = 0);

  /// data buffer to binary string conversion
  /// @param aDataP data pointer
  /// @param aDataSize number of bytes
  /// @param aSeparator character to be used to separate hex bytes, 0 for no separator
  /// @return hex string
  string dataToHexString(const void* aDataP, size_t aDataSize, char aSeparator = 0);

  /// mac address to string
  /// @param aMacAddress MAC address as 48bit integer number
  /// @param aSeparator character to be used to separate MAC bytes, 0 for no separator
  /// @return MAC address as string
  string macAddressToString(uint64_t aMacAddress, char aSeparator = 0);

  /// mac address to string
  /// @param aMacString MAC address as string, with bytes either unseparated two hex digits each, or separated by dash or colon
  /// @return MAC address as 48bit integer number, or 0 if input string is invalid
  uint64_t stringToMacAddress(const char *aMacString, bool aSpacesAllowed = false);

  /// mac address to string
  /// @param aIPv4Address IPv4 address as 32bit integer number
  /// @return IPv4 address as string
  string ipv4ToString(uint32_t aIPv4Address);

  /// @param aIPv4String IPv4 address in x.x.x.x notation
  /// @return IPv4 address as 32bit integer number, or 0 if input string is invalid
  uint32_t stringToIpv4(const char *aIPv4String);

  /// @return aValue, wrapped around at aMax to aMin
  double cyclic(double aValue, double aMin, double aMax);

  /// @return aValue, limited to be between and including aMin and aMax
  double limited(double aValue, double aMin, double aMax);

} // namespace p44

#endif /* defined(__p44utils__utils__) */
