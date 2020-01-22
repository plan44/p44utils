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

#ifndef __p44utils__jsonobject__
#define __p44utils__jsonobject__

#include "p44utils_common.hpp"

#if P44_BUILD_DIGI
  #include "json.h"
#else
  #include <json-c/json.h>
#endif

using namespace std;

namespace p44 {



  class JsonError : public Error
  {
  public:
    // Errors
    typedef enum json_tokener_error ErrorCodes;

    static const char *domain() { return "JsonObject"; }
    virtual const char *getErrorDomain() const { return JsonError::domain(); };
    JsonError(ErrorCodes aError) : Error(ErrorCode(aError), json_tokener_error_desc(aError)) {};
  };



  class JsonObject;

  /// shared pointer for JSON object
  typedef boost::intrusive_ptr<JsonObject> JsonObjectPtr;


  /// wrapper around json-c / libjson0 object
  class JsonObject : public P44Obj
  {
    struct json_object *json_obj; ///< the json-c object

    struct lh_entry *nextEntryP; ///< iterator pointer for resetKeyIteration()/nextKeyValue()

    /// construct object as wrapper of json-c json_object.
    /// @param aObjPassingOwnership json_object, ownership is passed into this JsonObject, caller looses ownership!
    JsonObject(struct json_object *aObjPassingOwnership);

    /// construct empty object
    JsonObject();

  public:

    /// destructor, releases internally kept json_object (which is possibly owned by other objects)
    virtual ~JsonObject();

    /// factory to return smart pointer to new wrapper of a newly created json_object
    /// @param aObjPassingOwnership json_object, ownership is passed into this JsonObject, caller looses ownership!
    static JsonObjectPtr newObj(struct json_object *aObjPassingOwnership);

    /// get type
    /// @return type code
    json_type type() const;

    /// check type
    /// @param aRefType type to check for
    /// @return true if object matches given type
    bool isType(json_type aRefType) const;

    /// string representation of object.
    const char *json_c_str(int aFlags=0);
    string json_str(int aFlags=0);

    /// Note: should only be used to pass object to other json-c native APIs
    /// Note: Ownership of the jsonobject remains with this JsonObject
    /// @return pointer the embedded json-c object structure
    const struct json_object *jsoncObj() const { return json_obj; }

    /// add object for key
    void add(const char* aKey, JsonObjectPtr aObj);

    /// get object by key
    /// @param aKey key of object
    /// @return the value of the object
    /// @note to distinguish between having no such key and having the key with
    ///   a NULL object, use get(aKey,aJsonObject) instead
    JsonObjectPtr get(const char *aKey);

    /// get object by key
    /// @param aKey key of object
    /// @param aJsonObject will be set to the value of the key when return value is true
    /// @param aNonNull if set, existing keys with NULL values will return false
    /// @return true if key exists (but aJsonObject might still be empty in case of a NULL object)
    bool get(const char *aKey, JsonObjectPtr &aJsonObject, bool aNonNull = false);

    /// get object's string value by key
    /// @return NULL if key does not exists or actually has NULL value, string object otherwise
    /// @note the returned C string pointer is valid only as long as the object is not deleted
    const char *getCString(const char *aKey);

    /// delete object by key
    void del(const char *aKey);


    /// get array length
    /// @return length of array. Returns 0 for empty arrays and all non-array objects
    int arrayLength() const;

    /// append to array
    /// @param aObj object to append to the array
    void arrayAppend(JsonObjectPtr aObj);

    /// get from a specific position in the array
    /// @param aAtIndex index position to return value for
    /// @return NULL pointer if element does not exist, value otherwise
    JsonObjectPtr arrayGet(int aAtIndex);

    /// put at specific position in array
    /// @param aAtIndex index position to put value to (overwriting existing value at that position)
    /// @param aObj object to store in the array
    void arrayPut(int aAtIndex, JsonObjectPtr aObj);


    /// reset object iterator
    /// @return false if object cannot be iterated
    bool resetKeyIteration();

    /// get next child object's key and value
    /// @param aKey will be assigned the key (name) of the child object
    /// @param aValue will be assigned the child object
    /// @return false if there are no more key/values (aKey and aValue unchanged)
    bool nextKeyValue(string &aKey, JsonObjectPtr &aValue);

    /// get next child object and wrap it into a parent object.
    /// This is an alternative to get a key/value pair as a single C++ object instead of two
    /// as provided by nextKeyValue(), e.g. for iterating purposes.
    /// @return JSON object, containing a single named child, or null if no more objects
    JsonObjectPtr nextJsonObj();

    /// create new empty object
    static JsonObjectPtr newObj();

    /// create new NULL object (does not embed a real JSON-C object, just a NULL pointer)
    static JsonObjectPtr newNull();


    /// create new object from text
    static JsonObjectPtr objFromText(const char *aJsonText, ssize_t aMaxChars = -1, ErrorPtr *aErrorP = NULL, bool aAllowCComments = false);

    /// create new object from text file
    static JsonObjectPtr objFromFile(const char *aJsonFilePath, ErrorPtr *aErrorP = NULL, bool aAllowCComments = false);

    /// save object to text file
    ErrorPtr saveToFile(const char *aJsonFilePath);

    /// create new array object
    static JsonObjectPtr newArray();


    /// create new boolean object
    static JsonObjectPtr newBool(bool aBool);
    /// get boolean value
    bool boolValue() const;

    /// create int objects
    static JsonObjectPtr newInt32(int32_t aInt32);
    static JsonObjectPtr newInt64(int64_t aInt64);
    /// get int values
    int32_t int32Value() const;
    int64_t int64Value() const;

    /// create double object
    static JsonObjectPtr newDouble(double aDouble);
    /// get double value
    double doubleValue() const;

    /// create new string object
    static JsonObjectPtr newString(const char *aCStr);
    static JsonObjectPtr newString(const char *aCStr, size_t aLen);
    static JsonObjectPtr newString(const string &aString, bool aEmptyIsNull = false);
    /// get string value
    const char *c_strValue() const;
    size_t stringLength() const;
    string stringValue() const;
    string lowercaseStringValue() const;

  };

} // namespace

#endif /* defined(__p44utils__jsonobject__) */
