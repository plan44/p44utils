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

#include "jsonobject.hpp"

#if P44_BUILD_DIGI
  // still using older json-c
  #include "json_object_private.h"
#endif

#include <sys/stat.h> // for fstat

using namespace p44;


// MARK: - constructors / destructor / assignment


// construct from raw json_object, passing ownership
JsonObject::JsonObject(struct json_object *aObjPassingOwnership) :
  mNextEntryP(NULL)
{
  mJson_obj = aObjPassingOwnership;
}


// construct empty
JsonObject::JsonObject()
{
  mJson_obj = json_object_new_object();
}


JsonObject::~JsonObject()
{
  if (mJson_obj) {
    json_object_put(mJson_obj);
    mJson_obj = NULL;
  }
}


/// copy constructor
JsonObject::JsonObject(const JsonObject& aObj) :
  mJson_obj(NULL)
{
  *this = aObj;
}

/// assignment operator
JsonObject& JsonObject::operator=(const JsonObject& aObj)
{
  if (mJson_obj) {
    json_object_put(mJson_obj);
    mJson_obj = NULL;
  }
  //json_object_deep_copy(aObj.json_obj, &json_obj, &json_c_shallow_copy_default);
  mJson_obj = json_tokener_parse(json_object_get_string(aObj.mJson_obj)); // should do "roughly the same thing"
  return *this;
}




// MARK: - read and write from files


#define MAX_JSON_BUF_SIZE 20000


static const char *nextCommentDelimiter(bool aStarting, const char* aText, ssize_t aTextLen)
{
  char c1 = aStarting ? '/' : '*';
  char c2 = aStarting ? '*' : '/';
  char c;
  while((c = *aText) && aTextLen>0) {
    if (c==c1 && *(aText+1)==c2) return aText;
    aText++;
    aTextLen--;
  }
  return NULL;
}


static const char *nextParsableSegment(const char*& aText, ssize_t& aTextLen, size_t& aSegmentLen, bool aAllowCComments, bool &aInComment)
{
  const char* seg = NULL;
  if (aAllowCComments) {
    const char* cc;
    while ((cc = nextCommentDelimiter(!aInComment, aText, aTextLen))) {
      // change of comment state
      aInComment = !aInComment;
      seg = aText;
      aText = cc+2; // continue after comment delimiter
      aTextLen -= aText-seg;
      if (aInComment) {
        // segment is from beginning to here
        aSegmentLen = (size_t)(cc-seg);
        return seg;
      }
      else {
        // out of comment, aText is new segment start, check for next comment beginning
        continue;
      }
    }
    // no comment change from here to end
    if (aInComment) {
      // rest of text is comment, nothing more to parse
      aText += aTextLen;
      aTextLen = 0;
      return NULL;
    }
  }
  // everything from here to end can be parsed
  seg = aTextLen ? aText : NULL; // must return NULL if no more text
  aSegmentLen = (size_t)aTextLen;
  aText += aTextLen;
  aTextLen = 0;
  return seg;
}


static void countLines(int &aLineCount, size_t& aLastLineLenght, const char*& aFrom, const char* aTo)
{
  while (aFrom<aTo) {
    char c;
    if ((c = *aFrom++)==0) break;
    if (c=='\n') {
      aLineCount++;
      aLastLineLenght = 0;
      continue;
    }
    aLastLineLenght++;
  }
}


JsonObjectPtr JsonObject::objFromText(const char *aJsonText, ssize_t aMaxChars, ErrorPtr *aErrorP, bool aAllowCComments, ssize_t* aParsedCharsP)
{
  JsonObjectPtr obj;
  if (aMaxChars<0) aMaxChars = (ssize_t)strlen(aJsonText);
  struct json_tokener* tokener = json_tokener_new();
  bool inComment = false;
  const char *seg;
  size_t segLen;
  int lineCnt = 0;
  size_t charOffs = 0;
  const char *beg = aJsonText; // for parsed length calculation
  const char *ll = aJsonText;
  struct json_object *o = NULL;
  JsonError::ErrorCodes jerr = json_tokener_success;
  while ((seg = nextParsableSegment(aJsonText, aMaxChars, segLen, aAllowCComments, inComment))) {
    if (aErrorP) countLines(lineCnt, charOffs, ll, seg); // count lines from beginning of text to beginning of segment
    o = json_tokener_parse_ex(tokener, seg, (int)segLen);
    if (o) {
      obj = JsonObject::newObj(o);
      goto done;
    }
    else {
      // error (or incomplete JSON, which is fine)
      jerr = json_tokener_get_error(tokener);
      if (jerr!=json_tokener_continue) {
        // real error
        goto done;
      }
      if (aErrorP) countLines(lineCnt, charOffs, ll, seg+segLen); // count lines in parsed segment
    }
  }
  // pass a null char explicitly (see json-c docs) to indicate end of JSON, so "unexpected end" errors can actually occur
  o = json_tokener_parse_ex(tokener, "", 1); // need to pass length of 0 char!
  if (o) {
    obj = JsonObject::newObj(o);
  }
  else {
    jerr = json_tokener_get_error(tokener);
  }
done:
  if (!o && aErrorP) {
    *aErrorP = ErrorPtr(new JsonError(jerr));
    countLines(lineCnt, charOffs, ll, seg+tokener->char_offset); // count lines from beginning of segment to error position
    (*aErrorP)->prefixMessage("in line %d at char %zu: ", lineCnt+1, charOffs+1);
  }
  if (aParsedCharsP) {
    *aParsedCharsP = seg+tokener->char_offset-beg;
  }
  json_tokener_free(tokener);
  return obj;
}



// factory method, create JSON object from file
JsonObjectPtr JsonObject::objFromFile(const char *aJsonFilePath, ErrorPtr *aErrorP, bool aAllowCComments)
{
  JsonObjectPtr obj;
  size_t bufSize = MAX_JSON_BUF_SIZE;
  // read file into string
  bool inComment = false;
  int fd = open(aJsonFilePath, O_RDONLY);
  if (fd>=0) {
    // opened, check buffer needs
    struct stat fs;
    fstat(fd, &fs);
    if (fs.st_size<(ssize_t)bufSize) bufSize = (size_t)fs.st_size; // don't need the entire buffer
    // decode
    struct json_tokener* tokener = json_tokener_new();
    char *jsonbuf = new char[bufSize];
    ssize_t n;
    int lineCnt = 0;
    size_t charOffs = 0;
    struct json_object *o = NULL;
    JsonError::ErrorCodes jerr = json_tokener_success;
    const char *ll = NULL;
    const char *seg = NULL;
    while((n = read(fd, jsonbuf, bufSize))>0) {
      const char *jsontext = jsonbuf;
      size_t segLen;
      ll = jsontext;
      while ((seg = nextParsableSegment(jsontext, n, segLen, aAllowCComments, inComment))) {
        if (aErrorP) countLines(lineCnt, charOffs, ll, seg); // count lines from beginning of text to beginning of segment
        o = json_tokener_parse_ex(tokener, seg, (int)segLen);
        if (o==NULL) {
          // error (or incomplete JSON, which is fine)
          jerr = json_tokener_get_error(tokener);
          if (jerr!=json_tokener_continue) {
            // real error
            goto done;
          }
        }
        else {
          // got JSON object
          obj = JsonObject::newObj(o);
          goto done;
        }
        if (aErrorP) countLines(lineCnt, charOffs, ll, seg+segLen); // count lines in parsed segment
      } // segments to parse
    } // data in file
    // pass a null char explicitly (see json-c docs) to indicate end of JSON, so "unexpected end" errors can actually occur
    o = json_tokener_parse_ex(tokener, "", 1); // need to pass length of 0 char!
    if (o) {
      obj = JsonObject::newObj(o);
    }
    else {
      jerr = json_tokener_get_error(tokener);
    }
  done:
    if (!o && aErrorP) {
      *aErrorP = ErrorPtr(new JsonError(jerr));
      if (seg) {
        countLines(lineCnt, charOffs, ll, seg+tokener->char_offset); // count lines from beginning of segment to error position
        (*aErrorP)->prefixMessage("in line %d at char %zu: ", lineCnt+1, charOffs+1);
      }
    }
    json_tokener_reset(tokener);
    delete[] jsonbuf;
    json_tokener_free(tokener);
    close(fd);
  }
  else {
    if (aErrorP) {
      *aErrorP = SysError::errNo();
      (*aErrorP)->prefixMessage("JSON reader cannot open file '%s': ", aJsonFilePath);
    }
  }
  return obj;
}


ErrorPtr JsonObject::saveToFile(const char *aJsonFilePath, int aFlags)
{
  int fd = open(aJsonFilePath, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
  if (fd<0) {
    return SysError::errNo("Cannot open file to save JSON: ");
  }
  else {
    const char *jsontext = json_c_str(aFlags);
    if (write(fd, jsontext, strlen(jsontext))<0) {
      close(fd);
      return SysError::errNo("Error writing JSON: ");
    }
    // success
    close(fd);
  }
  return ErrorPtr();
}


// MARK: - type


json_type JsonObject::type() const
{
  return json_object_get_type(mJson_obj);
}


bool JsonObject::isType(json_type aRefType) const
{
  return json_object_is_type(mJson_obj, aRefType);
}



// MARK: - conversion to string

const char *JsonObject::json_c_str(int aFlags)
{
  return json_object_to_json_string_ext(mJson_obj, aFlags);
}


string JsonObject::json_str(int aFlags)
{
  return string(json_c_str(aFlags));
}


const char* JsonObject::text(JsonObjectPtr aJsonObj, int aFlags)
{
  if (aJsonObj) return aJsonObj->json_c_str(aFlags);
  else return "<none>";
}



// MARK: - add, get and delete by key

void JsonObject::add(const char* aKey, JsonObjectPtr aObj)
{
  // json_object_object_add assumes caller relinquishing ownership,
  // so we must compensate this by retaining (getting) the object
  // as the object still belongs to us
  // Except if a NULL (no object) is passed
  json_object_object_add(mJson_obj, aKey, aObj ? json_object_get(aObj->mJson_obj) : NULL);
}


bool JsonObject::get(const char *aKey, JsonObjectPtr &aJsonObject, bool aNonNull)
{
  json_object *weakObjRef = NULL;
  if (json_object_object_get_ex(mJson_obj, aKey, &weakObjRef)) {
    // found object, but can be the NULL object (which will return no JsonObjectPtr
    if (weakObjRef==NULL) {
      if (aNonNull) return false; // we don't want a NULL value returned
      aJsonObject = JsonObjectPtr(); // no object
    }
    else {
      // - claim ownership as json_object_object_get_ex does not do that automatically
      json_object_get(weakObjRef);
      // - create wrapper
      aJsonObject = newObj(weakObjRef);
    }
    return true; // key exists, but returned object might still be NULL
  }
  return false; // key does not exist, aJsonObject unchanged
}



JsonObjectPtr JsonObject::get(const char *aKey)
{
  JsonObjectPtr p;
  get(aKey, p);
  return p;
}


const char *JsonObject::getCString(const char *aKey)
{
  JsonObjectPtr p = get(aKey);
  if (p)
    return p->c_strValue();
  return NULL;
}



void JsonObject::del(const char *aKey)
{
  json_object_object_del(mJson_obj, aKey);
}


// MARK: - arrays


int JsonObject::arrayLength() const
{
  if (type()!=json_type_array)
    return 0; // normal objects don't have a length
  else
    return (int)json_object_array_length(mJson_obj);
}


void JsonObject::arrayAppend(JsonObjectPtr aObj)
{
  if (type()==json_type_array) {
    // - claim ownership as json_object_array_add does not do that automatically
    json_object_get(aObj->mJson_obj);
    json_object_array_add(mJson_obj, aObj->mJson_obj);
  }
}


JsonObjectPtr JsonObject::arrayGet(int aAtIndex)
{
  JsonObjectPtr p;
  json_object *weakObjRef = json_object_array_get_idx(mJson_obj, (size_t)aAtIndex);
  if (weakObjRef) {
    // found object
    // - claim ownership as json_object_array_get_idx does not do that automatically
    json_object_get(weakObjRef);
    // - return wrapper
    p = newObj(weakObjRef);
  }
  return p;
}


void JsonObject::arrayPut(int aAtIndex, JsonObjectPtr aObj)
{
  if (type()==json_type_array) {
    // - claim ownership as json_object_array_put_idx does not do that automatically
    json_object_get(aObj->mJson_obj);
    json_object_array_put_idx(mJson_obj, (size_t)aAtIndex, aObj->mJson_obj);
  }
}


#if !REDUCED_FOOTPRINT
void JsonObject::arrayDel(int aAtIndex, int aNumElements)
{
  if (type()==json_type_array) {
    #if HAVE_JSONC_VERSION_013
    // JSON-C v0.13 onwards does have json_object_array_del_idx()
    json_object_array_del_idx(mJson_obj, aAtIndex, aNumElements);
    #else
    // for JSON-C before 0.13, we need to emulate array element deletion by coyping all but to-be-deleted elements
    struct json_object *newarray = json_object_new_array();
    int n = arrayLength();
    for (int i=0; i<n; i++) {
      if (i<aAtIndex || i>=aAtIndex+aNumElements) {
        json_object *weakObjRef = json_object_array_get_idx(mJson_obj, (size_t)i);
        if (weakObjRef) {
          // - claim ownership as neither json_object_array_get_idx nor json_object_array_add does not do that automatically
          //   (and we *need* to own the object because deleting the old array will put all of its contained objects)
          json_object_get(weakObjRef);
          json_object_array_add(newarray, weakObjRef);
        }
      }
    }
    json_object_put(mJson_obj); // forget the old array
    mJson_obj = newarray; // replace it with the new array
    #endif // !HAVE_JSONC_VERSION_013
  }
}
#endif // !REDUCED_FOOTPRINT


// MARK: - object key/value iteration


bool JsonObject::resetKeyIteration()
{
  if (isType(json_type_object)) {
    mNextEntryP = json_object_get_object(mJson_obj)->head;
    return true; // can be iterated (but might still have zero key/values)
  }
  return false; // cannot be iterated
}


// helper for nextKeyValue and keyValueByIndex
static void keyValueFromEntry(struct lh_entry *aEntryP, string* aKeyP, JsonObjectPtr* aValueP)
{
  // get key
  if (aKeyP) *aKeyP = (char*)aEntryP->k;
  // get value
  if (aValueP) {
    json_object *weakObjRef = (struct json_object*)aEntryP->v;
    if (weakObjRef) {
      // claim ownership
      json_object_get(weakObjRef);
      // - return wrapper
      *aValueP = JsonObject::newObj(weakObjRef);
    }
    else {
      *aValueP = JsonObjectPtr(); // NULL
    }
  }
}


bool JsonObject::nextKeyValue(string &aKey, JsonObjectPtr &aValue)
{
  if (mNextEntryP) {
    keyValueFromEntry(mNextEntryP, &aKey, &aValue);
    // advance to next
    mNextEntryP = mNextEntryP->next;
    return true;
  }
  // no more entries
  return false;
}


int JsonObject::numKeys()
{
  int nk = 0;
  if (isType(json_type_object)) {
    struct lh_entry *eP = json_object_get_object(mJson_obj)->head; ///< iterator pointer
    while(eP) {
      nk++;
      eP = eP->next;
    }
  }
  return nk;
}


bool JsonObject::keyValueByIndex(int aIndex, string &aKey, JsonObjectPtr* aValueP)
{
  if (isType(json_type_object)) {
    int i = 0;
    struct lh_entry *eP = json_object_get_object(mJson_obj)->head; ///< iterator pointer
    while(eP) {
      if (i==aIndex) {
        keyValueFromEntry(eP, &aKey, aValueP);
        return true;
      }
      // next
      i++;
      eP = eP->next;
    }
  }
  // no such object
  return false;
}



// MARK: - factories and value getters

// private wrapper factory from newly created json_object (ownership passed in)
JsonObjectPtr JsonObject::newObj(struct json_object *aObjPassingOwnership)
{
  return JsonObjectPtr(new JsonObject(aObjPassingOwnership));
}



JsonObjectPtr JsonObject::newObj()
{
  return JsonObjectPtr(new JsonObject());
}


JsonObjectPtr JsonObject::newNull()
{
  // create wrapper with no embedded object (as a plain C NULL pointer represents NULL in JSON-C)
  return JsonObjectPtr(new JsonObject(NULL));
}


JsonObjectPtr JsonObject::newArray()
{
  return JsonObjectPtr(new JsonObject(json_object_new_array()));
}




JsonObjectPtr JsonObject::newBool(bool aBool)
{
  return newObj(json_object_new_boolean(aBool));
}

bool JsonObject::boolValue() const
{
  // TODO: remove once bugfix in json-c is common
  // workaround for bug in json_object_get_boolean() returning false for arrays and objects
  // Bug was reported on github: https://github.com/json-c/json-c/issues/658
  // PR for fixing the bug: https://github.com/json-c/json-c/pull/659
  json_type t = json_object_get_type(mJson_obj);
  if (t==json_type_object || t==json_type_array) return true;
  // end of bugfix
  return json_object_get_boolean(mJson_obj);
}


JsonObjectPtr JsonObject::newInt32(int32_t aInt32)
{
  return newObj(json_object_new_int(aInt32));
}

JsonObjectPtr JsonObject::newInt64(int64_t aInt64)
{
  return newObj(json_object_new_int64(aInt64));
}

int32_t JsonObject::int32Value() const
{
  if (isType(json_type_string)) {
    // check for hex
    const char *xstr = c_strValue();
    if (strncmp(xstr, "0x", 2)==0) {
      return (int32_t)strtol(xstr, NULL, 0);
    }
  }
  return json_object_get_int(mJson_obj);
}

int64_t JsonObject::int64Value() const
{
  if (isType(json_type_string)) {
    // check for hex
    const char *xstr = c_strValue();
    if (strncmp(xstr, "0x", 2)==0) {
      return (int64_t)strtoll(xstr, NULL, 0);
    }
  }
  return json_object_get_int64(mJson_obj);
}


JsonObjectPtr JsonObject::newDouble(double aDouble)
{
  return newObj(json_object_new_double(aDouble));
}

double JsonObject::doubleValue() const
{
  return json_object_get_double(mJson_obj);
}



JsonObjectPtr JsonObject::newString(const char *aCStr)
{
  if (!aCStr) return JsonObjectPtr();
  return newObj(json_object_new_string(aCStr));
}

JsonObjectPtr JsonObject::newString(const char *aCStr, size_t aLen)
{
  if (!aCStr) return JsonObjectPtr();
  return newObj(json_object_new_string_len(aCStr, (int)aLen));
}

JsonObjectPtr JsonObject::newString(const string &aString, bool aEmptyIsNull)
{
  if (aEmptyIsNull & aString.empty()) return JsonObjectPtr();
  return JsonObject::newString(aString.c_str());
}

const char *JsonObject::c_strValue() const
{
  return json_object_get_string(mJson_obj);
}

size_t JsonObject::stringLength() const
{
  return (size_t)json_object_get_string_len(mJson_obj);
}

string JsonObject::stringValue() const
{
  return string(c_strValue());
}

string JsonObject::lowercaseStringValue() const
{
  return lowerCase(c_strValue());
}
