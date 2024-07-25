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

#include "utils.hpp"

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h> // for ssize_t, size_t etc.

using namespace p44;

// old-style C-formatted output into string object
void __printflike(3,0) p44::string_format_v(string &aStringObj, bool aAppend, const char *aFormat, va_list aArgs)
{
  const size_t bufsiz=128;
  size_t actualsize;
  char buf[bufsiz];
  int ret;

  buf[0]='\0';
  char *bufP = NULL;
  if (!aAppend) aStringObj.erase();
  // using aArgs in vsnprintf() is destructive, need a copy in
  // case we call the function a second time
  va_list args;
  va_copy(args, aArgs);
  ret = vsnprintf(buf, bufsiz, aFormat, aArgs);
  if (ret>=0) {
    actualsize = (size_t)ret;
    if (actualsize>=bufsiz) {
      // default buffer was too small, create bigger dynamic buffer
      bufP = new char[actualsize+1];
      ret = vsnprintf(bufP, actualsize+1, aFormat, args);
      if (ret>0) {
        aStringObj += bufP;
      }
      delete [] bufP;
    }
    else {
      // small default buffer was big enough, add it
      aStringObj += buf;
    }
  }
  va_end(args);
} // vStringObjPrintf


// old-style C-formatted output as string
string p44::string_format(const char *aFormat, ...)
{
  va_list args;
  va_start(args, aFormat);
  string s;
  // now make the string
  string_format_v(s, false, aFormat, args);
  va_end(args);
  return s;
} // string_format


// old-style C-formatted output appending to string
void p44::string_format_append(string &aStringToAppendTo, const char *aFormat, ...)
{
  va_list args;

  va_start(args, aFormat);
  // now make the string
  string_format_v(aStringToAppendTo, true, aFormat, args);
  va_end(args);
} // string_format_append



string p44::string_substitute(const string aString, const string aPlaceholder, const string aSubstitute, int aNumTimes)
{
  string s = aString;
  size_t i = 0;
  int n = aNumTimes;
  while (n>0 || aNumTimes==0) {
    i = s.find(aPlaceholder, i);
    if (i==string::npos) break;
    s.replace(i, aPlaceholder.size(), aSubstitute);
    i += aSubstitute.size();
    n--;
  }
  return s;
}



void p44::pathstring_make_dir(string &aPathToMakeDir)
{
  if (!aPathToMakeDir.empty() && aPathToMakeDir[aPathToMakeDir.length()-1]!='/') {
    aPathToMakeDir.append("/");
  }
}


void p44::pathstring_format_append(string &aPathToAppendTo, const char *aFormat, ...)
{
  va_list args;

  va_start(args, aFormat);
  pathstring_make_dir(aPathToAppendTo);
  // now append the path element string
  string_format_v(aPathToAppendTo, true, aFormat, args);
  va_end(args);
}



/// strftime with output to string
string p44::string_ftime(const char *aFormat, const struct tm *aTimeP)
{
  string s;
  string_ftime_append(s, aFormat, aTimeP);
  return s;
}


/// strftime appending to string
void p44::string_ftime_append(string &aStringToAppendTo, const char *aFormat, const struct tm *aTimeP)
{
  // get time if none passed
  struct tm nowtime;
  if (aTimeP==NULL) {
    time_t t = time(NULL);
    localtime_r(&t, &nowtime);
    aTimeP = &nowtime;
  }
  // format
  const size_t bufsiz=42;
  char buf[bufsiz];
  if (strftime(buf, bufsiz, aFormat, aTimeP)==0) {
    // not enough buffer
    size_t n = strlen(aFormat)*5; // heuristics, assume a %x specifier usually does not expand to more than 10 chars
    char *bufP = new char[n];
    if (strftime(bufP, n, aFormat, aTimeP)>0) {
      aStringToAppendTo += bufP;
    }
    delete [] bufP;
  }
  else {
    aStringToAppendTo += buf;
  }
}


bool p44::string_fgetline(FILE *aFile, string &aLine)
{
  if (!aFile) return false;
  const size_t bufLen = 1024;
  char buf[bufLen];
  aLine.clear();
  bool eol = false;
  while (!eol) {
    char *p = fgets(buf, bufLen-1, aFile);
    if (!p) {
      // eof or error
      if (feof(aFile)) return !aLine.empty(); // eof is ok if it occurs after having collected some data, otherwise it means: no more lines
      return false;
    }
    // something read
    size_t l = strlen(buf);
    // check for CR, LF or CRLF
    if (l>0 && buf[l-1]=='\n') {
      l--;
      eol = true;
    }
    if (l>0 && buf[l-1]=='\r') {
      l--;
      eol = true;
    }
    // collect
    aLine.append(buf,l);
  }
  return true;
}


bool p44::string_fgetfile(FILE *aFile, string &aData)
{
  if (!aFile) return false;
  const size_t bufLen = 1024;
  char buf[bufLen];
  aData.clear();
  while (!feof(aFile)) {
    size_t n = fread(buf, 1, bufLen-1, aFile);
    if (n>0) {
      // got data
      aData.append(buf, n);
    }
    else {
      // error or eof
      if (ferror(aFile))
        return false;
    }
  }
  return true;
}


bool p44::string_fgetfirstline(const string aFileName, string &aLine)
{
  string line;
  FILE *file = fopen(aFileName.c_str(), "r");
  bool readLine = false;
  if (file) {
    // file opened
    if (string_fgetline(file, line)) {
      aLine = trimWhiteSpace(line, true, true);
      readLine = true;
    }
    fclose(file);
  }
  return readLine;
}


const char *p44::nonNullCStr(const char *aNULLOrCStr)
{
	if (aNULLOrCStr==NULL) return "";
	return aNULLOrCStr;
}


int p44::strucmp(const char *s1, const char *s2, size_t len1, size_t len2)
{
  // allow NULL as empty strings
  if (!s1) s1 = "";
  if (!s2) s2 = "";
  // s1>s2 : 1, s1==s2 : 0, s1<s2 : -1
  size_t i;
  // calc number of chars we must compare
  size_t len = len1==0 ? len2 : (len2==0 ? len1 : (len1>len2 ? len2 : len1));
  for (i=0; (!len || i<len) && *s1 && *s2; i++) {
    // while both strings have chars and not len reached
    if (toupper(*s1)!=toupper(*s2))
      return toupper(*s1)>toupper(*s2) ? 1 : -1; // different
    // next
    s1++;
    s2++;
  }
  // equal up to end of shorter string or reached len
  // - if both reached end or len -> equal
  if ( ((len1 ? i==len1 : false) || *s1==0) && ((len2 ? i==len2 : false) || *s2==0) ) return 0;
  // - not equal, longer string is larger
  //   (if not reached end of s1 or stopped before len1, s1 is longer
  //    but note than len1 can be longer than actual length of s1, so we
  //    must check for *s1 to make sure we have really not reached end of s1)
  return (len1 ? i<len1 && *s1 : *s1) ? 1 : -1;
} // strucmp


bool p44::uequals(const char* aCString, const char *aCmp, size_t aLen1, size_t aLen2)
{
  return strucmp(aCString, aCmp, aLen1, aLen2)==0;
}

bool p44::uequals(const string& aString, const char *aCmp)
{
  return strucmp(aString.c_str(), aCmp, aString.size())==0;
}

bool p44::uequals(const string& aString, const char *aCmp, size_t aLen1, size_t aLen2)
{
  return strucmp(aString.c_str(), aCmp, aLen1, aLen2)==0;
}

bool p44::uequals(const string& aString, const string& aCmp)
{
  return strucmp(aString.c_str(), aCmp.c_str(), aString.size(), aCmp.size())==0;
}

bool p44::uequals(const string& aString, const string& aCmp, size_t aLen1)
{
  return strucmp(aString.c_str(), aCmp.c_str(), aLen1, aCmp.size())==0;
}

bool p44::uequals(const string& aString, const string& aCmp, size_t aLen1, size_t aLen2)
{
  return strucmp(aString.c_str(), aCmp.c_str(), aLen1, aLen2)==0;
}


string p44::lowerCase(const char *aString, size_t aMaxSize)
{
  string s;
  while (char c=*aString++) {
    s += (char)tolower(c);
    if (aMaxSize>0 && --aMaxSize==0) break;
  }
  return s;
}


string p44::lowerCase(const string &aString)
{
  return lowerCase(aString.c_str());
}


string p44::upperCase(const char *aString, size_t aMaxSize)
{
  string s;
  while (char c=*aString++) {
    s += (char)toupper(c);
    if (aMaxSize>0 && --aMaxSize==0) break;
  }
  return s;
}


string p44::upperCase(const string &aString)
{
  return upperCase(aString.c_str());
}


string p44::trimWhiteSpace(const string &aString, bool aLeading, bool aTrailing)
{
  size_t n = aString.length();
  size_t s = 0;
  size_t e = n;
  if (aLeading) {
    while (s<n && isspace(aString[s])) ++s;
  }
  if (aTrailing) {
    while (e>0 && isspace(aString[e-1])) --e;
  }
  return aString.substr(s,e-s);
}


bool p44::abbreviate(string& aString, size_t aMaxChars, AbbreviationStyle aStyle)
{
  if (aString.size()>aMaxChars) {
    // needs to be abbreviated
    if (aMaxChars<=3 || aStyle==truncate) {
      aString.erase(aMaxChars);
    }
    else if (aStyle==middle_ellipsis && aMaxChars>=7) {
      aMaxChars-=3;
      size_t sz2 = aMaxChars/2;
      aString = aString.substr(0, aMaxChars-sz2) + "..." + aString.substr(aString.size()-sz2);
    }
    else {
      aString.erase(aMaxChars-3);
      aString += "...";
    }
    return true; // abbreviated
  }
  // not abbreviated
  return false;
}



string p44::singleLine(const char *aString, bool aCompactWSRuns, size_t aEllipsisAtMax)
{
  string s;
  if (aString) {
    bool wsrun = false;
    while (char c=*aString++) {
      if (c==' ' || c=='\n' || c=='\r' || c=='\t') {
        // whitespace
        if (wsrun && aCompactWSRuns) continue;
        s += ' ';
        wsrun = true;
      }
      else {
        wsrun = false;
        s += c;
      }
      if (aEllipsisAtMax!=0) {
        if (--aEllipsisAtMax<=3) {
          s += "...";
          break;
        }
      }
    }
  }
  return s;
}


string p44::shellQuote(const char *aString)
{
  string s = "'";
  while (char c=*aString++) {
    if (c=='\'') s += "'\"'\""; // must exit single quoted string to include quote
    s += c;
  }
  s += '\'';
  return s;
}


string p44::shellQuote(const string &aString)
{
  return shellQuote(aString.c_str());
}


string p44::cstringQuote(const char *aString)
{
  string s = "\"";
  while (char c=*aString++) {
    if (c=='"' || c=='\\') s += '\\'; // escape double quotes and backslashes
    else if (c=='\n') { s += "\\n"; continue; }
    else if (c=='\r') { s += "\\r"; continue; }
    else if (c=='\t') { s += "\\t"; continue; }
    else if (c<0x20) { string_format_append(s,"\\x%02x",(uint8_t)c); continue; }
    s += c;
  }
  s += '"';
  return s;
}


string p44::cstringQuote(const string &aString)
{
  return cstringQuote(aString.c_str());
}


bool p44::nextLine(const char * &aCursor, string &aLine)
{
  const char *p = aCursor;
  if (!p || *p==0) return false; // no input or end of text -> no line
  char c;
  do {
    c = *p;
    if (c==0 || c=='\n' || c=='\r') {
      // end of line or end of text
      aLine.assign(aCursor,(size_t)(p-aCursor));
      if (c) {
        // skip line end
        ++p;
        if (c=='\r' && *p=='\n') ++p; // CRLF is ok as well
      }
      // p now at end of text or beginning of next line
      aCursor = p;
      return true;
    }
    ++p;
  } while (true);
}


bool p44::nextPart(const char *&aCursor, string &aPart, char aSeparator, bool aStopAtEOL)
{
  const char *p = aCursor;
  if (!p || *p==0) return false; // no input or end of text -> no part
  char c;
  do {
    c = *p;
    if (c==0 || c==aSeparator || (aStopAtEOL && (c=='\n' || c=='\r')) ) {
      // end of part
      aPart.assign(aCursor,(size_t)(p-aCursor));
      if (c==aSeparator) p++; // skip the separator
      aCursor = p; // return start of next part or end of line/string
      return true;
    }
    ++p;
  } while (true);
}




bool p44::nextCSVField(const char * &aCursor, string &aField, char aSeparator, bool aContinueQuoted)
{
  const char *p = aCursor;
  char c;
  if (!p || *p==0) return false; // no input or end of text -> no field
  if (!aContinueQuoted) {
    // new field
    aField.clear();
    // check if it is a quoted field
    if (*p=='"') {
      aContinueQuoted = true;
      p++;
    }
  }
  bool skip = false;
  if (aContinueQuoted) {
    // (part of) quoted field
    while ((c=*p++)) {
      if (c=='"') {
        // check if followed by another quote
        if (*p=='"') {
          // decodes into a single quote
          aField += c;
          p++; // continue after second quote
        }
        else {
          // end of quoted field
          break;
        }
      }
      else {
        aField += c;
      }
    }
    if (c!='"') {
      // not properly terminated -> report special condition
      aCursor = NULL;
      return true;
    }
    skip = true;
  }
  // unquoted field or stuff to skip until next separator
  while ((c=*p)) {
    p++;
    if (
      aSeparator ?
      (c==aSeparator) :
      (c==';' || c==',' || c=='\t')
    ) {
      // is separator
      break;
    }
    else if (c=='\n' || c=='\r') {
      // newline also ends field
      break;
    }
    else if (!skip) {
      // part of value
      aField += c;
    }
  }
  // end of field
  if (c=='\r' && *p=='\n') ++p; // consume possibly trainling LF (from CRLF separated input)
  aCursor = p; // points to next field or end of input
  return true;
}




bool p44::keyAndValue(const string &aInput, string &aKey, string &aValue, char aSeparator)
{
  size_t i = aInput.find(aSeparator);
  if (i==string::npos) return false; // not a key: value line
  // get key, trim whitespace
  aKey = trimWhiteSpace(aInput.substr(0,i), true, true);
  // get value, trim leading whitespace
  aValue = trimWhiteSpace(aInput.substr(i+1,string::npos), true, false);
  return aKey.length()>0; // valid key/value only if key is not zero length
}


size_t p44::pickTagContents(const string &aInput, const string aTag, string &aContents, size_t aStart)
{
  // find the tag
  size_t t = aInput.find("<"+aTag, aStart);
  if (t==string::npos) return 0;
  // find end of tag start
  size_t te = aInput.find(">",t+1+aTag.size());
  if (te==string::npos) return 0;
  if (te>=1 && aInput[te-1]=='/') {
    // self-terminating: no contents
    aContents.clear();
    return true;
  }
  te++; // content starts here
  // find tag end
  size_t e = aInput.find("</"+aTag+">", te);
  if (e==string::npos) return 0;
  // now assign contents
  aContents = aInput.substr(te,e-te);
  trimWhiteSpace(aContents, true, true);
  return e+2+aTag.size()+1;
}




// split URL into protocol, hostname, document name and auth-info (user, password)
void p44::splitURL(const char *aURI,string *aProtocol,string *aHost,string *aDoc,string *aUser, string *aPasswd)
{
  const char *p = aURI;
  const char *q,*r;

  if (!p) return; // safeguard
  // extract protocol
  q=strchr(p,':');
  if (q) {
    // protocol found
    if (aProtocol) aProtocol->assign(p,(size_t)(q-p));
    p=q+1; // past colon
    while (*p=='/') p++; // past trailing slashes
    // if protocol specified, check for auth info
    q=strchr(p,'@');
    if (q) {
      r=strchr(p,':');
      if (r && r<q) {
        // user and password specified
        if (aUser) aUser->assign(p,(size_t)(r-p));
        if (aPasswd) aPasswd->assign(r+1,(size_t)(q-r-1));
      }
      else {
        // only user, no password
        if (aUser) aUser->assign(p,(size_t)(q-p));
        if (aPasswd) aPasswd->erase();
      }
      p=q+1; // past "@"
    }
    else {
      // no auth found
      if (aUser) aUser->erase();
      if (aPasswd) aPasswd->erase();
    }
  }
  else {
    // no protocol found
    if (aProtocol) aProtocol->erase();
    // no protocol, no auth
    if (aUser) aUser->erase();
    if (aPasswd) aPasswd->erase();
  }
  // separate hostname and document
  // - assume path
  q=strchr(p,'/');
  // - if no path, check if there is a CGI param directly after the host name
  if (!q) {
    q=strchr(p,'?');
    // in case of no docpath, but CGI, put '?' into docname
    r=q;
  }
  else {
    // has docpath beginning with slash
    r=q; // include the slash
  }
  if (q) {
    // document exists
    if (aDoc) {
      aDoc->erase();
      if (*q=='?') (*aDoc)+='/'; // if doc starts with CGI, we are at root
      aDoc->append(r); // till end of string
    }
    if (aHost) aHost->assign(p,(size_t)(q-p)); // assign host (all up to / or ?)
  }
  else {
    if (aDoc) aDoc->erase(); // empty document name
    if (aHost) aHost->assign(p); // entire string is host
  }
} // splitURL



void p44::splitHost(const char *aHostSpec, string *aHostName, uint16_t *aPortNumber)
{
  if (!aHostSpec) return; // safeguard
  const char *q = aHostSpec;
  if (*q=='[') {
    // IPv6 address specification, advance to closing ] to skip colons that are part of the IPv6
    q = strchr(q+1,']');
  }
  if (q) {
    q=strchr(q,':');
  }
  if (q) {
    // there is a port specification
    uint16_t port;
    if (sscanf(q+1,"%hd", &port)==1) {
      if (aPortNumber) *aPortNumber = port;
    }
    if (aHostName) aHostName->assign(aHostSpec,(size_t)(q-aHostSpec));
  }
  else {
    if (aHostName) aHostName->assign(aHostSpec);
  }
}


int p44::gtinCheckDigit(uint64_t aGtin)
{
  // 64bit in decimal has max 20 digits (largest known to me GTIN-like number, the SSCC, has 17 digits)
  // mod10 algorithm is:
  // - sum of digits*3 at odd digit positions (least significant=rightmost=1=odd) + sum of digits at even positions.
  // - check digit is the value to add to sum to get an even multiple of 10
  int sum = 0;
  int oldcheck = (int)(aGtin % 10); // current check digit as found in aGtin
  for (int i=0; i<20; i++) {
    aGtin /= 10;
    int dig = (int)(aGtin % 10);
    sum += ((i&1)==0 ? 3 : 1) * dig;
  }
  int newcheck = sum%10;
  if (newcheck>0) newcheck = 10-newcheck; // difference to next multiple of 10 is checksum
  return newcheck-oldcheck;
}


string p44::hexToBinaryString(const char *aHexString, bool aSpacesAllowed, size_t aMaxBytes)
{
  string bs;
  uint8_t b = 0;
  bool firstNibble = true;
  char c;
  while (aMaxBytes==0 || bs.size()<aMaxBytes) {
    c = *aHexString++;
    if (c==0 || c=='-' || c==':' || (aSpacesAllowed && c==' ')) {
      if (aSpacesAllowed && !firstNibble) {
        // in space-allowed mode, a separator (space or dash) means the byte is complete, even if it has only one digit
        bs.append((char *)&b,1);
        firstNibble = true;
      }
      if (c==0) break; // done
      continue; // skip delimiter
    }
    c = (char) (toupper(c)-'0');
    if (c>9) c = (char) (c-'A'+'9'+1);
    if ((uint8_t)c>0xF)
      break; // invalid char, done
    if (firstNibble) {
      b = (uint8_t)c;
      firstNibble = false;
    }
    else {
      b = (uint8_t)((b<<4) | c);
      bs.append((char *)&b,1);
      firstNibble = true;
    }
  }
  return bs;
}


string p44::binaryToHexString(const string &aBinaryString, char aSeparator)
{
  return dataToHexString(aBinaryString.c_str(), aBinaryString.size());
}


string p44::dataToHexString(const void* aDataP, size_t aDataSize, char aSeparator)
{
  string s;
  for (size_t i=0; i<aDataSize; i++) {
    if (aSeparator && i!=0) s += aSeparator;
    string_format_append(s, "%02X", ((uint8_t*)aDataP)[i]);
  }
  return s;
}




string p44::macAddressToString(uint64_t aMacAddress, char aSeparator)
{
  string b;
  for (int i=0; i<6; ++i) {
    b += (char)((aMacAddress>>((5-i)*8)) & 0xFF);
  }
  return binaryToHexString(b, aSeparator);
}


uint64_t p44::stringToMacAddress(const char *aMacString, bool aSpacesAllowed)
{
  uint64_t mac = 0;
  string b = hexToBinaryString(aMacString, aSpacesAllowed, 6);
  if (b.size()==6) {
    for (size_t i=0; i<6; ++i) {
      mac <<= 8;
      mac += (uint8_t)b[i];
    }
  }
  return mac;
}

string p44::ipv4ToString(uint32_t aIPv4Address)
{
  return string_format("%d.%d.%d.%d",
    (aIPv4Address>>24) & 0xFF,
    (aIPv4Address>>16) & 0xFF,
    (aIPv4Address>>8) & 0xFF,
    aIPv4Address & 0xFF
  );
}



uint32_t p44::stringToIpv4(const char *aIPv4String)
{
  short ib[4];
  if (sscanf(aIPv4String, "%hd.%hd.%hd.%hd", &ib[0], &ib[1], &ib[2], &ib[3])==4) {
    return ((uint32_t)ib[0]<<24) | ((uint32_t)ib[1]<<16) | ((uint32_t)ib[2]<<8) | (uint32_t)ib[3];
  }
  return 0; // failed
}


double p44::cyclic(double aValue, double aMin, double aMax)
{
  aValue = aValue-aMin; // make null based
  double r = aMax-aMin; // wrap range
  if (aValue>=r) aValue -= int(aValue/r)*r;
  else if (aValue<0) aValue += (int(-aValue/r)+1)*r;
  return aValue+aMin;
}


double p44::limited(double aValue, double aMin, double aMax)
{
  if (aValue<aMin) aValue = aMin;
  else if (aValue>aMax) aValue = aMax;
  return aValue;
}

