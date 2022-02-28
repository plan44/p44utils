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
    struct json_object *mJson_obj; ///< the json-c object

    struct lh_entry *mNextEntryP; ///< iterator pointer for resetKeyIteration()/nextKeyValue()

    /// construct object as wrapper of json-c json_object.
    /// @param aObjPassingOwnership json_object, ownership is passed into this JsonObject, caller looses ownership!
    JsonObject(struct json_object *aObjPassingOwnership);

    /// construct empty object
    JsonObject();

  public:

    /// destructor, releases internally kept json_object (which is possibly owned by other objects)
    virtual ~JsonObject();

    /// copy constructor
    JsonObject(const JsonObject& aObj);

    /// assignment operator
    JsonObject& operator=(const JsonObject& aObj);

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

    /// return JSON string representation
    /// @param aFlags formatting options, see JSON_C_TO_STRING_PRETTY and other constants
    /// @return JSON C string representation (valid as long as object exists)
    const char *json_c_str(int aFlags=0);

    /// return JSON string representation
    /// @param aFlags formatting options, see JSON_C_TO_STRING_PRETTY and other constants
    /// @return JSON string representation of object.
    string json_str(int aFlags=0);

    /// Convenience method: return JSON string representation of passed object, which may be NULL
    /// @param aFlags formatting options, see JSON_C_TO_STRING_PRETTY and other constants
    /// @return JSON C string representation of json aJsonObj, or "<none>" if aJsonObj is NULL
    static const char* text(JsonObjectPtr aJsonObj, int aFlags=0);

    /// Note: should only be used to pass object to other json-c native APIs
    /// Note: Ownership of the jsonobject remains with this JsonObject
    /// @return pointer the embedded json-c object structure
    const struct json_object *jsoncObj() const { return mJson_obj; }

    /// add object for key
    /// @param aKey name of key
    /// @param aObj object to store in field named aKey
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
    /// @param aKey key of field to delete
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

    #if !REDUCED_FOOTPRINT
    /// delete element(s) at specific position in array
    /// @param aAtIndex index position of first element to delete
    /// @param aNumElements number of elements to delete
    void arrayDel(int aAtIndex, int aNumElements = 1);
    #endif // REDUCED_FOOTPRINT

    /// reset object iterator
    /// @return false if object cannot be iterated
    bool resetKeyIteration();

    /// get next child object's key and value
    /// @param aKey will be assigned the key (name) of the child object
    /// @param aValue will be assigned the child object
    /// @return false if there are no more key/values (aKey and aValue unchanged)
    bool nextKeyValue(string &aKey, JsonObjectPtr &aValue);

    /// get number of keys in this object
    /// @return number of keys. Returns 0 for empty objects and all non-objects
    int numKeys();

    /// get key/value by index
    /// @param aIndex index into object (internal, non-predictable order)
    /// @param aKey will be assigned the key (name) of the child object
    /// @param aValueP if not NULL, will be assigned the child object
    /// @return false if there is no object with the specified aIndex
    bool keyValueByIndex(int aIndex, string &aKey, JsonObjectPtr* aValueP = NULL);

    /// @return new empty object
    static JsonObjectPtr newObj();

    /// @return create new NULL object (does not embed a real JSON-C object, just a NULL pointer)
    static JsonObjectPtr newNull();


    /// create new object from text
    /// @param aJsonText text to be read as JSON
    /// @param aMaxChars max chars to read from aJsonText, or -1 to read entire C string (until NUL terminator is found)
    /// @param aErrorP where to store parsing error objects, or NULL if no error return is needed
    /// @param aAllowCComments if set, C-Style comments /* */ are allowed withing JSON text (not conformant to JSON specs)
    /// @param aParsedCharsP where to store the number of chars parsed from aJsonText, or NULL if not needed
    /// @return new object or NULL if aJsonText parsing was not succesful
    static JsonObjectPtr objFromText(const char *aJsonText, ssize_t aMaxChars = -1, ErrorPtr *aErrorP = NULL, bool aAllowCComments = false, ssize_t* aParsedCharsP = NULL);

    /// create new object from text file
    /// @param aJsonFilePath path of file to read as JSON
    /// @param aErrorP where to store parsing error objects, or NULL if no error return is needed
    /// @param aAllowCComments if set, C-Style comments /* */ are allowed withing JSON text (not conformant to JSON specs)
    /// @return new object or NULL if aJsonText parsing was not succesful
    static JsonObjectPtr objFromFile(const char *aJsonFilePath, ErrorPtr *aErrorP = NULL, bool aAllowCComments = false);

    /// save object to text file
    /// @param aJsonFilePath path of file to (over)write with text representation of this JSON object
    /// @param aFlags formatting options, see JSON_C_TO_STRING_PRETTY and other constants
    /// @return ok or error
    ErrorPtr saveToFile(const char *aJsonFilePath, int aFlags = 0);

    /// @return new array object
    static JsonObjectPtr newArray();


    /// @return new boolean JSON object
    static JsonObjectPtr newBool(bool aBool);

    /// @return boolean value
    bool boolValue() const;

    /// create int JSON object
    /// @param aInt32 32 bit integer number
    static JsonObjectPtr newInt32(int32_t aInt32);

    /// create int JSON object
    /// @param aInt64 64 bit integer number
    static JsonObjectPtr newInt64(int64_t aInt64);

    /// @return int32 value
    int32_t int32Value() const;

    /// @return int64 value
    int64_t int64Value() const;

    /// @return double JSON object
    /// @param aDouble double float number
    static JsonObjectPtr newDouble(double aDouble);

    /// @return double value
    double doubleValue() const;

    /// Create new JSON string object or no object from C string
    /// @param aCStr C string to make JSON string of, can be NULL to create no object
    /// @return new JSON string object or NULL if aCStr is NULL
    static JsonObjectPtr newString(const char *aCStr);

    /// Create new JSON string object or no object from part of C string
    /// @param aCStr C string to take characters from for JSON string, can be NULL to create no object
    /// @param aLen max number of characters to take from aCStr
    /// @return new JSON string object or NULL if aCStr is NULL
    static JsonObjectPtr newString(const char *aCStr, size_t aLen);

    /// @return new JSON string object
    /// @param aString string to make JSON string of
    /// @param aEmptyIsNull if set, and aString is empty, NULL is returned
    /// @return new JSON string object or NULL if aString is empty and aEmptyIsNull is set
    static JsonObjectPtr newString(const string &aString, bool aEmptyIsNull = false);

    /// @return value of object as string
    const char* c_strValue() const;

    /// @return length of value of object as string
    size_t stringLength() const;

    /// @return value of object as string
    string stringValue() const;

    /// @return value of object as lowercase string
    string lowercaseStringValue() const;

  };

} // namespace

#endif /* defined(__p44utils__jsonobject__) */
