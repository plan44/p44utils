//
//  Copyright (c) 2017-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "p44script.hpp"

#if ENABLE_P44SCRIPT

#include "math.h"

#if ENABLE_JSON_APPLICATION && SCRIPTING_JSON_SUPPORT
  #include "application.hpp"
#endif


using namespace p44;
using namespace p44::P44Script;


// MARK: - script error

ErrorPtr ScriptError::err(ErrorCodes aErrCode, const char *aFmt, ...)
{
  Error *errP = new ScriptError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args);
  va_end(args);
  return ErrorPtr(errP);
}

// MARK: - EventSink

EventSink::~EventSink()
{
  // clear references in all sources
  clearSources();
}


void EventSink::clearSources()
{
  while (!eventSources.empty()) {
    EventSource *src = *(eventSources.begin());
    eventSources.erase(eventSources.begin());
    src->eventSinks.erase(this);
    src->sinksModified = true;
  }
}


// MARK: - EventSource

EventSource::~EventSource()
{
  // clear references in all sinks
  while (!eventSinks.empty()) {
    EventSink *sink = *(eventSinks.begin());
    eventSinks.erase(eventSinks.begin());
    sink->eventSources.erase(this);
  }
  eventSinks.clear();
  sinksModified = true;
}


void EventSource::registerForEvents(EventSink *aEventSink)
{
  if (aEventSink) {
    sinksModified = true;
    eventSinks.insert(aEventSink); // multiple registrations are possible, counted only once
    aEventSink->eventSources.insert(this);
  }
}


void EventSource::unregisterFromEvents(EventSink *aEventSink)
{
  if (aEventSink) {
    sinksModified = true;
    eventSinks.erase(aEventSink);
    aEventSink->eventSources.erase(this);
  }
}


void EventSource::sendEvent(ScriptObjPtr aEvent)
{
  if (eventSinks.empty()) return; // optimisation
  // note: duplicate notification is possible when sending event causes event sink changes and restarts
  // TODO: maybe fix this if it turns out to be a problem
  //       (should not, because entire triggering is designed to re-evaluate events after triggering)
  do {
    sinksModified = false;
    for (EventSinkSet::iterator pos=eventSinks.begin(); pos!=eventSinks.end(); ++pos) {
      (*pos)->processEvent(aEvent, *this);
      if (sinksModified) break;
    }
  } while(sinksModified);
}


void EventSource::copySinksFrom(EventSource* aOtherSource)
{
  if (!aOtherSource) return;
  for (EventSinkSet::iterator pos=aOtherSource->eventSinks.begin(); pos!=aOtherSource->eventSinks.end(); ++pos) {
    sinksModified = true;
    registerForEvents(*pos);
  }
}





// MARK: - ScriptObj

#define FOCUSLOGLOOKUP(p) \
  { string s = string_format("searching %s for '%s'", p, aName.c_str()); FOCUSLOG("%60s : requirements=0x%08x", s.c_str(), aMemberAccessFlags ); }
#define FOCUSLOGSTORE(p) \
  { string s = string_format("setting '%s' in %s", aName.c_str(), p); FOCUSLOG("%60s : value=%s", s.c_str(), ScriptObj::describe(aMember).c_str()); }


ErrorPtr ScriptObj::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE("ScriptObj")
  return ScriptError::err(ScriptError::NotCreated, "cannot assign to '%s'", aName.c_str());
}

ErrorPtr ScriptObj::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  return ScriptError::err(ScriptError::NotFound, "cannot assign at %zu", aIndex);
}


void ScriptObj::makeValid(EvaluationCB aEvaluationCB)
{
  // I am already valid - just return myself via callback
  if (aEvaluationCB) aEvaluationCB(ScriptObjPtr(this));
}


void ScriptObj::assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue)
{
  if (aEvaluationCB) aEvaluationCB(new ErrorValue(ScriptError::err(ScriptError::NotLvalue, "not assignable")));
}



string ScriptObj::typeDescription(TypeInfo aInfo)
{
  string s;
  if ((aInfo & any)==any) {
    s = "any value";
    if ((aInfo & (null|error))!=(null|error)) {
      s += " but not";
      if ((aInfo & null)==0) {
        s += " undefined";
        if ((aInfo & error)==0) s += " or";
      }
      if ((aInfo & error)==0) s += " error";
    }
  }
  else {
    // structure
    if (aInfo & array) {
      s = "array";
    }
    if (aInfo & object) {
      if (!s.empty()) s += ", ";
      s += "object";
    }
    // special
    if (aInfo & threadref) {
      if (!s.empty()) s += ", ";
      s += "thread";
    }
    if (aInfo & executable) {
      if (!s.empty()) s += ", ";
      s += "executable";
    }
    // scalar
    if (aInfo & numeric) {
      if (!s.empty()) s += ", ";
      s += "numeric";
    }
    if (aInfo & text) {
      if (!s.empty()) s += ", ";
      s += "string";
    }
    if (aInfo & json) {
      if (!s.empty()) s += ", ";
      s += "json";
    }
    // alternatives
    if (aInfo & error) {
      if (!s.empty()) s += " or ";
      s += "error";
    }
    if (aInfo & null) {
      if (!s.empty()) s += " or ";
      s += "undefined";
    }
    if (aInfo & lvalue) {
      if (!s.empty()) s += " or ";
      s += "lvalue";
    }
  }
  return s;
}


string ScriptObj::describe(ScriptObjPtr aObj)
{
  if (!aObj) return "<none>";
  string n = aObj->getIdentifier();
  if (!n.empty()) n.insert(0, " named ");
  ScriptObjPtr valObj = aObj->actualValue();
  ScriptObjPtr calcObj;
  if (valObj) calcObj = valObj->calculationValue();
  string ty = typeDescription(aObj->getTypeInfo());
  string ann = aObj->getAnnotation();
  string v;
  if (calcObj) {
    v = calcObj->stringValue();
    if (calcObj->hasType(text)) v = shellQuote(v);
  }
  else {
    v = "<no value>";
  }
  if (ann==ty || ann==v) ann = ""; else ann.insert(0, " // ");
  return string_format(
    "%s [%s%s]%s",
    v.c_str(),
    ty.c_str(),
    n.c_str(),
    ann.c_str()
  );
}


int ScriptObj::getLogLevelOffset()
{
  if (logLevelOffset==0) {
    // no own offset - inherit context's
    if (loggingContext()) return loggingContext()->getLogLevelOffset();
    return 0;
  }
  return inherited::getLogLevelOffset();
}


string ScriptObj::logContextPrefix()
{
  string prefix;
  if (loggingContext()) {
    prefix = loggingContext()->logContextPrefix();
  }
  return prefix;
}



// MARK: Generic Operators

bool ScriptObj::operator!() const
{
  return !boolValue();
}

bool ScriptObj::operator&&(const ScriptObj& aRightSide) const
{
  return boolValue() && aRightSide.boolValue();
}

bool ScriptObj::operator||(const ScriptObj& aRightSide) const
{
  return boolValue() || aRightSide.boolValue();
}


// MARK: Equality Operator (all value classes)

bool ScriptObj::operator==(const ScriptObj& aRightSide) const
{
  return
    (this==&aRightSide) || // object _instance_ identity...
    (undefined() && aRightSide.undefined()); // ..or both sides are really null/undefined
}

bool NumericValue::operator==(const ScriptObj& aRightSide) const
{
  if (aRightSide.undefined()) return false; // a number (especially: zero) is never equal with undefined
  return num==aRightSide.doubleValue();
}

bool StringValue::operator==(const ScriptObj& aRightSide) const
{
  if (aRightSide.undefined()) return false; // a string (especially: empty) is never equal with undefined
  return str==aRightSide.stringValue();
}

bool ErrorValue::operator==(const ScriptObj& aRightSide) const
{
  ErrorPtr e = aRightSide.errorValue();
  return errorValue()->isError(e->domain(), e->getErrorCode());
}


// MARK: Less-Than Operator (all value classes)

bool ScriptObj::operator<(const ScriptObj& aRightSide) const
{
  return false; // undefined comparisons are always false
}

bool NumericValue::operator<(const ScriptObj& aRightSide) const
{
  return num<aRightSide.doubleValue();
}

bool StringValue::operator<(const ScriptObj& aRightSide) const
{
  return str<aRightSide.stringValue();
}


// MARK: Derived boolean operators

bool ScriptObj::operator!=(const ScriptObj& aRightSide) const
{
  return !operator==(aRightSide);
}

bool ScriptObj::operator>=(const ScriptObj& aRightSide) const
{
  return !operator<(aRightSide);
}

bool ScriptObj::operator>(const ScriptObj& aRightSide) const
{
  return !operator<(aRightSide) && !operator==(aRightSide);
}

bool ScriptObj::operator<=(const ScriptObj& aRightSide) const
{
  return operator==(aRightSide) || operator<(aRightSide);
}



// MARK: Arithmetic Operators (all value classes)

ScriptObjPtr NumericValue::operator+(const ScriptObj& aRightSide) const
{
  return new NumericValue(num + aRightSide.doubleValue());
}


ScriptObjPtr StringValue::operator+(const ScriptObj& aRightSide) const
{
  return new StringValue(str + aRightSide.stringValue());
}


ScriptObjPtr NumericValue::operator-(const ScriptObj& aRightSide) const
{
  return new NumericValue(num - aRightSide.doubleValue());
}

ScriptObjPtr NumericValue::operator*(const ScriptObj& aRightSide) const
{
  return new NumericValue(num * aRightSide.doubleValue());
}

ScriptObjPtr NumericValue::operator/(const ScriptObj& aRightSide) const
{
  if (aRightSide.doubleValue()==0) {
    return new ErrorValue(ScriptError::DivisionByZero, "division by zero");
  }
  else {
    return new NumericValue(num / aRightSide.doubleValue());
  }
}

ScriptObjPtr NumericValue::operator%(const ScriptObj& aRightSide) const
{
  if (aRightSide.doubleValue()==0) {
    return new ErrorValue(ScriptError::DivisionByZero, "modulo by zero");
  }
  else {
    // modulo allowing float dividend and divisor, really meaning "remainder"
    double a = doubleValue();
    double b = aRightSide.doubleValue();
    int64_t q = a/b;
    return new NumericValue(a-b*q);
  }
}


// MARK: - lvalues

void ScriptLValue::makeValid(EvaluationCB aEvaluationCB)
{
  if (aEvaluationCB) {
    if (!mCurrentValue) aEvaluationCB(new ErrorValue(ScriptError::NotFound, "lvalue does not yet exist"));
    else aEvaluationCB(mCurrentValue);
  }
}


StandardLValue::StandardLValue(ScriptObjPtr aContainer, const string aMemberName, ScriptObjPtr aCurrentValue) :
  inherited(aCurrentValue),
  mContainer(aContainer),
  mMemberName(aMemberName),
  mMemberIndex(0)
{
}


StandardLValue::StandardLValue(ScriptObjPtr aContainer, size_t aMemberIndex, ScriptObjPtr aCurrentValue) :
  inherited(aCurrentValue),
  mContainer(aContainer),
  mMemberName(""),
  mMemberIndex(aMemberIndex)
{
}



void StandardLValue::assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue)
{
  if (mContainer) {
    ErrorPtr err;
    if (mMemberName.empty()) {
      err = mContainer->setMemberAtIndex(mMemberIndex, aNewValue);
    }
    else {
      err = mContainer->setMemberByName(mMemberName, aNewValue);
    }
    if (Error::notOK(err)) {
      aNewValue = new ErrorValue(err);
    }
    else {
      // if current value has event sinks, and new value is a event source, too, new value must inherit those sinks
      EventSource* oldSource = mCurrentValue ? mCurrentValue->eventSource() : NULL;
      EventSource* newSource = aNewValue ? aNewValue->eventSource() : NULL;
      if (newSource) {
        newSource->copySinksFrom(oldSource);
      }
      // previous value can now be overwritten
      mCurrentValue = aNewValue;
    }
  }
  if (aEvaluationCB) {
    aEvaluationCB(aNewValue);
  }
}


// MARK: - Special NULL values

EventPlaceholderNullValue::EventPlaceholderNullValue(string aAnnotation) :
  inherited(aAnnotation)
{
}

EventSource* EventPlaceholderNullValue::eventSource() const
{
  return static_cast<EventSource*>(const_cast<EventPlaceholderNullValue*>(this));
}


// MARK: - Error Values

ErrorValue::ErrorValue(ScriptError::ErrorCodes aErrCode, const char *aFmt, ...) :
  thrown(false)
{
  err = new ScriptError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
}


ErrorPosValue::ErrorPosValue(const SourceCursor &aCursor, ScriptError::ErrorCodes aErrCode, const char *aFmt, ...) :
  inherited(new ScriptError(aErrCode)),
  sourceCursor(aCursor)
{
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
}


// MARK: - ThreadValue

ThreadValue::ThreadValue(ScriptCodeThreadPtr aThread) : mThread(aThread)
{
}


ScriptObjPtr ThreadValue::calculationValue()
{
  if (!threadExitValue) {
    // might still be running, or is in zombie state holding final result
    if (mThread) {
      threadExitValue = mThread->finalResult();
      if (threadExitValue) {
        mThread.reset(); // release the zombie thread object itself
      }
    }
  }
  if (!threadExitValue) return new AnnotatedNullValue("still running");
  return threadExitValue;
}


void ThreadValue::abort()
{
  if (mThread) mThread->abort();
}


bool ThreadValue::running()
{
  return mThread && (mThread->finalResult()==NULL);
}



EventSource* ThreadValue::eventSource() const
{
  if (!mThread) return NULL; // no longer running -> no event source any more
  return static_cast<EventSource*>(mThread.get());
}



// MARK: Conversions

double StringValue::doubleValue() const
{
  SourceCursor cursor(stringValue());
  cursor.skipWhiteSpace();
  ScriptObjPtr n = cursor.parseNumericLiteral();
  // note: like parseInt/Float in JS we allow trailing garbage
  //   but UNLIKE JS we don't return NaN here, just 0 if there's no conversion to number
  if (n->isErr()) return 0; // otherwise we'd get error
  return n->doubleValue();
}


bool StringValue::boolValue() const
{
  // Like in JS, empty strings are false, non-empty ones are true
  return !stringValue().empty();
}



#if SCRIPTING_JSON_SUPPORT

// MARK: JsonValue + conversions

JsonObjectPtr ErrorValue::jsonValue() const
{
  JsonObjectPtr j;
  if (err) {
    j = JsonObject::newObj();
    j->add("ErrorCode", JsonObject::newInt32((int32_t)err->getErrorCode()));
    j->add("ErrorDomain", JsonObject::newString(err->getErrorDomain()));
    j->add("ErrorMessage", JsonObject::newString(err->getErrorMessage()));
  }
  return j;
}


JsonObjectPtr StringValue::jsonValue() const
{
  // old version did parse strings for json, but that's ambiguous, so
  // we just return a json string now
  return JsonObject::newString(str);
}


ScriptObjPtr JsonValue::calculationValue()
{
  if (!jsonval) return new AnnotatedNullValue("json null");
  if (jsonval->isType(json_type_boolean)) return new NumericValue(jsonval->boolValue());
  if (jsonval->isType(json_type_int)) return new NumericValue(jsonval->int64Value());
  if (jsonval->isType(json_type_double)) return new NumericValue(jsonval->doubleValue());
  if (jsonval->isType(json_type_string)) return new StringValue(jsonval->stringValue());
  return inherited::calculationValue();
}


ScriptObjPtr JsonValue::assignmentValue()
{
  // break down to standard value or copied json, unless this is a derived object such as a JSON API request that indicates to be kept as-is
  if (!hasType(keeporiginal)) {
    // avoid creating new json values for simple types
    if (jsonval && (jsonval->isType(json_type_array) || jsonval->isType(json_type_object))) {
      // must copy the contained json object
      return new JsonValue(JsonObjectPtr(new JsonObject(*jsonval)));
    }
    return calculationValue();
  }
  return inherited::assignmentValue();
}


string JsonValue::stringValue() const
{
  if (!jsonval) return ScriptObj::stringValue(); // undefined
  if (jsonval->isType(json_type_string)) return jsonval->stringValue(); // string leaf fields as strings w/o quotes!
  return jsonval->json_str(); // other types in their native json representation
}


double JsonValue::doubleValue() const
{
  if (!jsonval) return ScriptObj::doubleValue(); // undefined
  return jsonval->doubleValue();
}


bool JsonValue::boolValue() const
{
  if (!jsonval) return ScriptObj::boolValue(); // undefined
  return jsonval->boolValue();
}


TypeInfo JsonValue::getTypeInfo() const
{
  if (!jsonval || jsonval->isType(json_type_null)) return null;
  if (jsonval->isType(json_type_object)) return json+object;
  if (jsonval->isType(json_type_array)) return json+array;
  if (jsonval->isType(json_type_string)) return json+text;
  return json+numeric; // everything else is numeric
}


bool JsonValue::operator==(const ScriptObj& aRightSide) const
{
  if (inherited::operator==(aRightSide)) return true; // object identity
  if (aRightSide.undefined()) return undefined(); // both undefined is equal
  if (aRightSide.hasType(json)) {
    // compare JSON with JSON
    if (jsonval.get()==aRightSide.jsonValue().get()) return true; // json object identity (or both NULL)
    if (!jsonval || !aRightSide.jsonValue()) return false;
    if (strcmp(jsonval->c_strValue(),aRightSide.jsonValue()->c_strValue())==0) return true; // same stringified value
  }
  else {
    // compare JSON to non-JSON
    return const_cast<JsonValue *>(this)->calculationValue()->operator==(aRightSide);
  }
  return false; // everything else: not equal
}


bool JsonValue::operator<(const ScriptObj& aRightSide) const
{
  if (!aRightSide.hasType(json)) {
    // compare JSON to non-JSON
    if (aRightSide.hasType(numeric)) return doubleValue()<aRightSide.doubleValue();
    if (aRightSide.hasType(text)) return stringValue()<aRightSide.stringValue();
  }
  return false; // everything else: not orderable -> never less
}


ScriptObjPtr JsonValue::operator+(const ScriptObj& aRightSide) const
{
  JsonObjectPtr r = aRightSide.jsonValue();
  if (r && r->isType(json_type_array)) {
    // if I am an array, too -> append elements
    if (jsonval && jsonval->isType(json_type_array)) {
      JsonObjectPtr j = const_cast<JsonValue*>(this)->assignmentValue()->jsonValue();
      for (int i = 0; i<r->arrayLength(); i++) {
        j->arrayAppend(r->arrayGet(i));
      }
      return new JsonValue(j);
    }
  }
  else if (r && r->isType(json_type_object)) {
    // if I am an object, too -> merge fields
    if (jsonval && jsonval->isType(json_type_object)) {
      JsonObjectPtr j = const_cast<JsonValue*>(this)->assignmentValue()->jsonValue();
      r->resetKeyIteration();
      string k;
      JsonObjectPtr o;
      while(r->nextKeyValue(k, o)) {
        j->add(k.c_str(), o);
      }
      return new JsonValue(j);
    }
  }
  return new AnnotatedNullValue("neither array or object 'addition' (merge)");
}





const ScriptObjPtr JsonValue::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP("JsonValue");
  ScriptObjPtr m;
  if (jsonval && typeRequirementMet(json, aMemberAccessFlags, typeMask)) {
    // we cannot meet any other type requirement but json
    JsonObjectPtr j = jsonval->get(aName.c_str());
    if (j) {
      // we have that member
      m = ScriptObjPtr(new JsonValue(j));
      if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
        m = new StandardLValue(this, aName, m); // it is allowed to overwrite this value
      }
    }
    else {
      // no such member yet
      if (aMemberAccessFlags & lvalue) {
        // creation of new json object fields is generally allowed, return lvalue to create object
        m = new StandardLValue(this, aName, ScriptObjPtr()); // it is allowed to create a new value
      }
    }
  }
  return m;
}


size_t JsonValue::numIndexedMembers() const
{
  if (jsonval) return jsonval->arrayLength();
  return 0;
}


const ScriptObjPtr JsonValue::memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr m;
  if (jsonval && typeRequirementMet(json, aMemberAccessFlags, typeMask)) {
    // we cannot meet any other type requirement but json
    if (aIndex<numIndexedMembers()) {
      // we have that member
      m = ScriptObjPtr(new JsonValue(jsonval->arrayGet((int)aIndex)));
      if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
        m = new StandardLValue(this, aIndex, m); // it is allowed to overwrite this value
      }
    }
    else {
      // no such member yet
      if (aMemberAccessFlags & lvalue) {
        // creation allowed, return lvalue to create object
        m = new StandardLValue(this, aIndex, ScriptObjPtr()); // it is allowed to create a new value
      }
    }
  }
  return m;
}


ErrorPtr JsonValue::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE("JsonValue");
  if (!jsonval) {
    jsonval = JsonObject::newObj();
  }
  else if (!jsonval->isType(json_type_object)) {
    return ScriptError::err(ScriptError::Invalid, "json is not an object, cannot assign field");
  }
  if (aMember) {
    jsonval->add(aName.c_str(), aMember->jsonValue());
  }
  else {
    jsonval->del(aName.c_str());
  }
  return ErrorPtr();
}


ErrorPtr JsonValue::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  if (!jsonval) {
    jsonval = JsonObject::newArray();
  }
  else if (!jsonval->isType(json_type_array)) {
    return ScriptError::err(ScriptError::Invalid, "json is not an array, cannot set element");
  }
  if (aMember) {
    jsonval->arrayPut((int)aIndex, aMember->jsonValue());
  }
  else {
    return ScriptError::err(ScriptError::Invalid, "cannot delete from json arrays");
  }
  return ErrorPtr();
}

#endif // SCRIPTING_JSON_SUPPORT

// MARK: - StructuredObject

const ScriptObjPtr StructuredLookupObject::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP("StructuredObject");
  ScriptObjPtr m;
  LookupList::const_iterator pos = lookups.begin();
  while (pos!=lookups.end()) {
    MemberLookupPtr lookup = *pos;
    if (typeRequirementMet(lookup->containsTypes(), aMemberAccessFlags, typeMask)) {
      if ((m = lookup->memberByNameFrom(this, aName, aMemberAccessFlags))) return m;
    }
    ++pos;
  }
  return m;
}


void StructuredLookupObject::registerMemberLookup(MemberLookupPtr aMemberLookup)
{
  if (aMemberLookup) {
    // last registered lookup overrides same named objects in lookups registered before
    for (LookupList::iterator pos = lookups.begin(); pos!=lookups.end(); ++pos) {
      if (pos->get()==aMemberLookup.get()) return; // avoid registering the same lookup twice
    }
    lookups.push_front(aMemberLookup);
  }
}


// MARK: - ExecutionContext

ExecutionContext::ExecutionContext(ScriptMainContextPtr aMainContext) :
  mainContext(aMainContext),
  undefinedResult(false)
{
}


ScriptObjPtr ExecutionContext::instance() const
{
  return mainContext ? mainContext->instance() : ScriptObjPtr();
}


ScriptingDomainPtr ExecutionContext::domain() const
{
  return mainContext ? mainContext->domain() : ScriptingDomainPtr();
}


GeoLocation* ExecutionContext::geoLocation()
{
  if (!domain()) return NULL; // no domain to fallback to
  return domain()->geoLocation(); // return domain's location
}


void ExecutionContext::clearVars()
{
  indexedVars.clear();
}


void ExecutionContext::releaseObjsFromSource(SourceContainerPtr aSource)
{
  // Note we can ignore our indexed members, as these are always temporary
  if (domain()) domain()->releaseObjsFromSource(aSource);
}


size_t ExecutionContext::numIndexedMembers() const
{
  return indexedVars.size();
}


const ScriptObjPtr ExecutionContext::memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr m;
  if (aIndex<indexedVars.size()) {
    // we have that member
    m = indexedVars[aIndex];
    if (!m->meetsRequirement(aMemberAccessFlags, typeMask)) return ScriptObjPtr();
    if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
      m = new StandardLValue(this, aIndex, m); // it is allowed to overwrite this value
    }
  }
  else {
    // no such member yet
    if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & create)) {
      // creation allowed, return lvalue to create object
      m = new StandardLValue(this, aIndex, ScriptObjPtr()); // it is allowed to create a new value
    }
  }
  return m;
}


ErrorPtr ExecutionContext::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  if (aIndex==indexedVars.size() && aMember) {
    // specially optimized case: appending
    indexedVars.push_back(aMember);
  }
  else if (aMember) {
    if (aIndex>indexedVars.size()) {
      // resize, will result in sparse array
      indexedVars.resize(aIndex+1);
    }
    indexedVars[aIndex] = aMember;
  }
  else {
    // delete member
    indexedVars.erase(indexedVars.begin()+aIndex);
  }
  return ErrorPtr();
}


ScriptObjPtr ExecutionContext::checkAndSetArgument(ScriptObjPtr aArgument, size_t aIndex, ScriptObjPtr aCallee)
{
  if (!aCallee) return new ErrorValue(ScriptError::Internal, "missing callee");
  ArgumentDescriptor info;
  bool hasInfo = aCallee->argumentInfo(aIndex, info);
  if (!hasInfo) {
    if (aArgument) {
      return new ErrorValue(ScriptError::Syntax, "too many arguments for '%s'", aCallee->getIdentifier().c_str());
    }
  }
  if (!aArgument && hasInfo) {
    // check if there SHOULD be an argument at aIndex (but we have none)
    if ((info.typeInfo & (optionalarg|multiple))==0) {
      // at aIndex is a non-optional argument expected
      return new ErrorValue(ScriptError::Syntax,
        "missing argument %zu (%s) in call to '%s'",
        aIndex+1,
        typeDescription(info.typeInfo).c_str(),
        aCallee->getIdentifier().c_str()
      );
    }
  }
  if (aArgument) {
    // not just checking for required arguments
    TypeInfo allowed = info.typeInfo;
    // now check argument we DO have
    TypeInfo argInfo = aArgument->getTypeInfo();
    if ((argInfo & allowed & typeMask) != (argInfo & typeMask)) {
      if (
        (allowed & exacttype) || // exact checking required...
        (argInfo & typeMask &~scalar)!=(allowed & typeMask &~scalar) // ...or non-scalar requirements not met
      ) {
        if (allowed & undefres) {
          // type mismatch is not an error, but just enforces undefined function result w/o executing
          undefinedResult = true;
        }
        else if (argInfo & error) {
          // getting an error for an argument that does not allow errors should forward the error as-is
          return aArgument;
        }
        else {
          return new ErrorValue(ScriptError::Syntax,
            "argument %zu in call to '%s' is %s - expected %s",
            aIndex+1,
            aCallee->getIdentifier().c_str(),
            typeDescription(argInfo).c_str(),
            typeDescription(allowed).c_str()
          );
        }
      }
    }
    // argument is fine, set it
    ErrorPtr err = setMemberAtIndex(aIndex, aArgument, info.name);
    if (Error::notOK(err)) {
      return new ErrorValue(err);
    }
  }
  return ScriptObjPtr(); // ok
}



// receives result for synchronous execution
static void syncExecDone(ScriptObjPtr* aResultStorageP, bool* aFinishedP, ScriptObjPtr aResult)
{
  *aResultStorageP = aResult;
  *aFinishedP = true;
}

ScriptObjPtr ExecutionContext::executeSynchronously(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, MLMicroSeconds aMaxRunTime)
{
  ScriptObjPtr syncResult;

  bool finished = false;
  aEvalFlags |= synchronously;
  execute(aToExecute, aEvalFlags, boost::bind(&syncExecDone, &syncResult, &finished, _1), aMaxRunTime);
  if (!finished) {
    // despite having requested synchronous execution, evaluation is not finished by now
    finished = true;
    // kill started async operations, syncResult will be set by callback
    abort(stopall, new ErrorValue(ScriptError::Internal,
      "Fatal error: synchronous Evaluation of '%s' turned out to be still async",
      aToExecute->getIdentifier().c_str()
    ));
  }
  return syncResult;
}





// MARK: - ScriptCodeContext


ScriptCodeContext::ScriptCodeContext(ScriptMainContextPtr aMainContext) :
  inherited(aMainContext)
{
}




void ScriptCodeContext::releaseObjsFromSource(SourceContainerPtr aSource)
{
  // global members
  NamedVarMap::iterator pos = namedVars.begin();
  while (pos!=namedVars.end()) {
    if (pos->second->originatesFrom(aSource)) {
      #if P44_CPP11_FEATURE
      pos = namedVars.erase(pos); // source is gone -> remove
      #else
      NamedVarMap::iterator dpos = pos++; // pre-C++ 11
      namedVars.erase(dpos); // source is gone -> remove
      #endif
    }
    else {
      ++pos;
    }
  }
  inherited::releaseObjsFromSource(aSource);
}


void ScriptCodeContext::clearFloatingGlobs()
{
  NamedVarMap::iterator pos = namedVars.begin();
  while (pos!=namedVars.end()) {
    if (pos->second->floating()) {
      #if P44_CPP11_FEATURE
      pos = namedVars.erase(pos); // source is gone -> remove
      #else
      NamedVarMap::iterator dpos = pos++; // pre-C++ 11
      namedVars.erase(dpos); // source is gone -> remove
      #endif
    }
    else {
      ++pos;
    }
  }
}



void ScriptCodeContext::clearVars()
{
  namedVars.clear();
  inherited::clearVars();
}


const ScriptObjPtr ScriptCodeContext::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP(mainContext ? "local" : (domain() ? "main" : "global vars"));
  ScriptObjPtr m;
  // 1) local variables/objects
  if ((aMemberAccessFlags & (classscope+objscope))==0) {
    NamedVarMap::const_iterator pos = namedVars.find(aName);
    if (pos!=namedVars.end()) {
      // we have that member
      m = pos->second;
      if (m->meetsRequirement(aMemberAccessFlags, typeMask)) {
        if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
          return new StandardLValue(this, aName, m); // it is allowed to overwrite this value
        }
        return m;
      }
      m.reset(); // does not meet requirements
    }
    else {
      // no such member yet
      if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & create)) {
        // creation allowed, return lvalue to create object
        return new StandardLValue(this, aName, ScriptObjPtr()); // it is allowed to create a new value
      }
    }
  }
  // 2) access to ANY members of the _instance_ itself if running in a object context
  if (instance() && (m = instance()->memberByName(aName, aMemberAccessFlags) )) return m;
  // 3) functions from the main level (but no local objects/vars of main, these must be passed into functions as arguments)
  if (mainContext && (m = mainContext->memberByName(aName, aMemberAccessFlags|classscope|constant|objscope))) return m;
  // nothing found
  // Note: do NOT call inherited, altough there is a overridden memberByName in StructuredObject, but this
  //   is NOT a default lookup for ScriptCodeContext (but explicitly used from ScriptMainContext)
  return m;
}


ErrorPtr ScriptCodeContext::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE(domain() ? "named vars" : "global vars");
  NamedVarMap::iterator pos = namedVars.find(aName);
  if (pos!=namedVars.end()) {
    // exists in local vars
    if (aMember) {
      // assign new value
      pos->second = aMember;
    }
    else {
      // delete
      namedVars.erase(pos);
    }
  }
  else if (aMember) {
    // create it, but only if we have a member (not a delete attempt)
    namedVars[aName] = aMember;
  }
  return ErrorPtr();
}


ErrorPtr ScriptCodeContext::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  ErrorPtr err = inherited::setMemberAtIndex(aIndex, aMember, aName);
  if (!aName.empty() && Error::isOK(err)) {
    err = setMemberByName(aName, aMember);
  }
  return err;
}


void ScriptCodeContext::abort(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult, ScriptCodeThreadPtr aExceptThread)
{
  if (aAbortFlags & queue) {
    // empty queue first to make sure no queued threads get started when last running thread is killed below
    while (!queuedThreads.empty()) {
      queuedThreads.back()->abort(new ErrorValue(ScriptError::Aborted, "Removed queued execution before it could start"));
      queuedThreads.pop_back();
    }
  }
  if (aAbortFlags & stoprunning) {
    ThreadList tba = threads; // copy list as original get modified while aborting
    for (ThreadList::iterator pos = tba.begin(); pos!=tba.end(); ++pos) {
      if (!aExceptThread || aExceptThread!=(*pos)) {
        (*pos)->abort(aAbortResult); // should cause threadTerminated to get called which will remove actually terminated thread from the list
      }
    }
  }
}


void ScriptCodeContext::execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, MLMicroSeconds aMaxRunTime)
{
  if (undefinedResult) {
    // just return undefined w/o even trying to execute
    undefinedResult = false;
    if (aEvaluationCB) aEvaluationCB(new AnnotatedNullValue("undefined argument caused undefined function result"));
    return;
  }
  // must be compiled code at this point
  CompiledCodePtr code = dynamic_pointer_cast<CompiledCode>(aToExecute);
  if (!code) {
    if (aEvaluationCB) aEvaluationCB(new ErrorValue(ScriptError::Internal, "Object to be run must be compiled code!"));
    return;
  }
  if ((aEvalFlags & keepvars)==0) {
    clearVars();
  }
  // code can be executed
  // - we do not run source code, only script bodies
  if (aEvalFlags & sourcecode) aEvalFlags = (aEvalFlags & ~sourcecode) | scriptbody;
  // - now run
  ScriptCodeThreadPtr thread = newThreadFrom(code, code->cursor, aEvalFlags, aEvaluationCB, aMaxRunTime);
  if (thread) {
    thread->run();
    return;
  }
  // Note: no thread at this point is ok, means that execution was queued
}


ScriptCodeThreadPtr ScriptCodeContext::newThreadFrom(CompiledCodePtr aCodeObj, SourceCursor &aFromCursor, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, MLMicroSeconds aMaxRunTime)
{
  // prepare a thread for executing now or later
  // Note: thread gets an owning Ptr back to this, so this context cannot be destructed before all
  //   threads have ended.
  ScriptCodeThreadPtr newThread = ScriptCodeThreadPtr(new ScriptCodeThread(this, aCodeObj, aFromCursor));
  MLMicroSeconds maxBlockTime = aEvalFlags&synchronously ? aMaxRunTime : domain()->getMaxBlockTime();
  newThread->prepareRun(aEvaluationCB, aEvalFlags, maxBlockTime, aMaxRunTime);
  // now check how and when to run it
  if (!threads.empty()) {
    // some threads already running
    if (aEvalFlags & stoprunning) {
      // kill all current threads (with or without queued, depending on queue set in aEvalFlags or not) first...
      abort(aEvalFlags & stopall, new ErrorValue(ScriptError::Aborted, "Aborted by another script starting"));
      // ...then start new
    }
    else if (aEvalFlags & queue) {
      // queue for later
      queuedThreads.push_back(newThread);
      return ScriptCodeThreadPtr(); // no thread to start now, but ok because it was queued
    }
    else if ((aEvalFlags & concurrently)==0) {
      // none of the multithread modes and already running: just report busy
      newThread->abort(new ErrorValue(ScriptError::Busy, "Already busy executing script"));
      return newThread; // return the thread, which will immediately terminate with "already busy" error
    }
  }
  // can start new thread now
  threads.push_back(newThread);
  return newThread;
}


void ScriptCodeContext::threadTerminated(ScriptCodeThreadPtr aThread, EvaluationFlags aThreadEvalFlags)
{
  // a thread has ended, remove it from the list
  ThreadList::iterator pos=threads.begin();
  while (pos!=threads.end()) {
    if (pos->get()==aThread.get()) {
      #if P44_CPP11_FEATURE
      pos = threads.erase(pos);
      #else
      ThreadList::iterator dpos = pos++;
      threads.erase(dpos);
      #endif
      // thread object should get disposed now, along with its SourceRef
      break;
    }
    ++pos;
  }
  if (aThreadEvalFlags & mainthread) {
    // stop all other threads in this context
    abort(stoprunning);
  }
  // check for queued executions to start now
  if (threads.empty() && !queuedThreads.empty()) {
    // get next thread from the queue
    ScriptCodeThreadPtr nextThread = queuedThreads.front();
    queuedThreads.pop_front();
    // and start it
    threads.push_back(nextThread);
    nextThread->run();
  }
}



// MARK: - ScriptMainContext

ScriptMainContext::ScriptMainContext(ScriptingDomainPtr aDomain, ScriptObjPtr aThis) :
  inherited(ScriptMainContextPtr()), // main context itself does not have a mainContext (would self-lock)
  domainObj(aDomain),
  thisObj(aThis)
{
}


const ScriptObjPtr ScriptMainContext::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP((domain() ? "main scope" : "global scope"));
  ScriptObjPtr g;
  ScriptObjPtr m;
  // member lookup during execution of a function or script body
  if ((aMemberAccessFlags & nooverride) && domain()) {
    // nooverride: first check if we have an EXISTING global (but do NOT create a global if not)
    g = domain()->memberByName(aName, aMemberAccessFlags & ~create); // global by that name already exists, might use it unless local also exists
    // still check for local...
  }
  if ((aMemberAccessFlags & (constant|(domain() ? global : none)))==0) {
    // Only if not looking only for constant members (in the sense of: not settable by scripts) or globals (which are locals when we are the domain!)
    // 1) lookup local variables/arguments in this context...
    // 2) ...and members of the instance (if any)
    if ((m = inherited::memberByName(aName, aMemberAccessFlags & (g ? ~create : ~none)))) return m; // prevent creating local when we found a global already above
    if (g) return g; // existing global return
  }
  // 3) if not excplicitly global: members from registered lookups, which might or might not be instance related (depends on the lookup)
  if ((aMemberAccessFlags & global)==0) {
    if ((m = StructuredLookupObject::memberByName(aName, aMemberAccessFlags))) return m;
  }
  // 4) lookup global members in the script domain (vars, functions, constants)
  if (domain() && (m = domain()->memberByName(aName, aMemberAccessFlags&~(classscope|constant|objscope|global)))) return m;
  // nothing found (note that inherited was queried early above, already!)
  return m;
}


// MARK: - Scripting Domain


// MARK: - Built-in member support

BuiltInLValue::BuiltInLValue(const BuiltInMemberLookupPtr aLookup, const BuiltinMemberDescriptor *aMemberDescriptor, ScriptObjPtr aThisObj, ScriptObjPtr aCurrentValue) :
  inherited(aCurrentValue),
  mLookup(aLookup),
  descriptor(aMemberDescriptor),
  mThisObj(aThisObj)
{
}


void BuiltInLValue::assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue)
{
  ScriptObjPtr m;
  if (aNewValue) {
    m = descriptor->accessor(*const_cast<BuiltInMemberLookup *>(mLookup.get()), mThisObj, aNewValue); // write access
    if (!m) m = aNewValue;
  }
  else {
    m = new ErrorValue(ScriptError::Invalid, "cannot unset built-in values");
  }
  if (aEvaluationCB) aEvaluationCB(m);
}




BuiltInMemberLookup::BuiltInMemberLookup(const BuiltinMemberDescriptor* aMemberDescriptors)
{
  // build name lookup map
  if (aMemberDescriptors) {
    while (aMemberDescriptors->name) {
      members[aMemberDescriptors->name]=aMemberDescriptors;
      aMemberDescriptors++;
    }
  }
}


ScriptObjPtr BuiltInMemberLookup::memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aMemberAccessFlags) const
{
  FOCUSLOGLOOKUP("builtin");
  // actual type requirement must match, scope requirements are irrelevant here
  ScriptObjPtr m;
  MemberMap::const_iterator pos = members.find(aName);
  if (pos!=members.end()) {
    // we have a member by that name
    TypeInfo ty = pos->second->returnTypeInfo;
    if (ty & builtinmember) {
      // is a built-in variable/object/property
      m = pos->second->accessor(*const_cast<BuiltInMemberLookup *>(this), aThisObj, ScriptObjPtr()); // read access
      if (ScriptObj::typeRequirementMet(ty, aMemberAccessFlags, typeMask)) {
        if ((ty & lvalue) && (aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
          m = new BuiltInLValue(const_cast<BuiltInMemberLookup *>(this), pos->second, aThisObj, m); // it is allowed to overwrite this value
        }
      }
    }
    else {
      // is a function, return a executable that can be function-called with arguments
      m = ScriptObjPtr(new BuiltinFunctionObj(pos->second, aThisObj, this));
    }
  }
  return m;
}


ExecutionContextPtr BuiltinFunctionObj::contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const
{
  // built-in functions get their this from the lookup they come from
  return new BuiltinFunctionContext(aMainContext, aThread);
}


bool BuiltinFunctionObj::argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const
{
  if (aIndex>=descriptor->numArgs) {
    // no argument with this index, check for open argument list
    if (descriptor->numArgs<1) return false;
    aIndex = descriptor->numArgs-1;
    if ((descriptor->arguments[aIndex].typeInfo & multiple)==0) return false;
  }
  const BuiltInArgDesc* ad = &descriptor->arguments[aIndex];
  aArgDesc.typeInfo = ad->typeInfo;
  aArgDesc.name = nonNullCStr(ad->name);
  return true;
}


BuiltinFunctionContext::BuiltinFunctionContext(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) :
  inherited(aMainContext),
  mThread(aThread),
  callSite(aThread->src.posId()) // from where in the source the context was created, which is just after the opening arg '(?
{
}


void BuiltinFunctionContext::setAbortCallback(SimpleCB aAbortCB)
{
  abortCB = aAbortCB;
}


void BuiltinFunctionContext::execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, MLMicroSeconds aMaxRunTime)
{
  if (undefinedResult) {
    // just return undefined w/o even trying to execute
    undefinedResult = false;
    if (aEvaluationCB) aEvaluationCB(new AnnotatedNullValue("undefined argument caused undefined function result"));
    return;
  }
  func = dynamic_pointer_cast<BuiltinFunctionObj>(aToExecute);
  if (!func || !func->descriptor) {
    func.reset();
    aEvaluationCB(new ErrorValue(ScriptError::Internal, "builtin function call inconsistency"));
  }
  else if ((aEvalFlags & synchronously) && (func->descriptor->returnTypeInfo & async)) {
    aEvaluationCB(new ErrorValue(ScriptError::AsyncNotAllowed, "builtin function '%s' cannot be used in synchronous evaluation", func->descriptor->name));
  }
  else {
    abortCB = NULL; // no abort callback so far, implementation must set one if it returns before finishing
    evaluationCB = aEvaluationCB;
    func->descriptor->implementation(this);
  }
}

SourceCursor::UniquePos BuiltinFunctionContext::argId(size_t aArgIndex) const
{
  if (aArgIndex<numArgs()) {
    // Note: as arguments in source occupy AT LEAST one separator, the following simplificaton
    // of just adding the argument index to the call site position is sufficient to provide a unique
    // ID for the argument at that call site
    return callSite+aArgIndex;
  }
  return NULL;
}


ScriptObjPtr BuiltinFunctionContext::arg(size_t aArgIndex)
{
  if (aArgIndex<0 || aArgIndex>=numIndexedMembers()) {
    // no such argument, return a null as the argument might be optional
    return new AnnotatedNullValue("optional function argument");
  }
  return memberAtIndex(aArgIndex);
}


void BuiltinFunctionContext::abort(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult, ScriptCodeThreadPtr aExceptThread)
{
  if (func) {
    if (abortCB) abortCB(); // stop external things the function call has started
    abortCB = NULL;
    if (!aAbortResult) aAbortResult = new ErrorValue(ScriptError::Aborted, "builtin function '%s' aborted", func->descriptor->name);
    func = NULL;
    finish(aAbortResult);
  }
}


void BuiltinFunctionContext::finish(ScriptObjPtr aResult)
{
  abortCB = NULL; // finished
  func = NULL;
  if (evaluationCB) {
    EvaluationCB cb = evaluationCB;
    evaluationCB = NULL;
    cb(aResult);
  }
}


// MARK: - SourcePos


SourcePos::SourcePos() :
  ptr(NULL),
  bol(NULL),
  eot(NULL),
  line(0)
{
}


SourcePos::SourcePos(const string &aText) :
  bot(aText.c_str()),
  ptr(aText.c_str()),
  bol(aText.c_str()),
  eot(ptr+aText.size()),
  line(0)
{
}


SourcePos::SourcePos(const SourcePos &aCursor) :
  bot(aCursor.bot),
  ptr(aCursor.ptr),
  bol(aCursor.bol),
  eot(aCursor.eot),
  line(aCursor.line)
{
}


// MARK: - SourceCursor


SourceCursor::SourceCursor(string aString, const char *aLabel) :
  source(new SourceContainer(aLabel ? aLabel : "hidden", NULL, aString)),
  pos(source->source)
{
}


SourceCursor::SourceCursor(SourceContainerPtr aContainer) :
  source(aContainer),
  pos(aContainer->source)
{
}


SourceCursor::SourceCursor(SourceContainerPtr aContainer, SourcePos aStart, SourcePos aEnd) :
  source(aContainer),
  pos(aStart)
{
  assert(pos.ptr>=source->source.c_str() && pos.eot-pos.ptr<source->source.size());
  if(aEnd.ptr>=pos.ptr && aEnd.ptr<=pos.eot) pos.eot = aEnd.ptr;
}


size_t SourceCursor::lineno() const
{
  return pos.line;
}


size_t SourceCursor::charpos() const
{
  if (!pos.ptr || !pos.bol) return 0;
  return pos.ptr-pos.bol;
}


size_t SourceCursor::textpos() const
{
  if (!pos.ptr || !pos.bot) return 0;
  return pos.ptr-pos.bot;
}


bool SourceCursor::EOT() const
{
  return !pos.ptr || pos.ptr>=pos.eot || *pos.ptr==0;
}


bool SourceCursor::valid() const
{
  return pos.ptr!=NULL;
}


char SourceCursor::c(size_t aOffset) const
{
  if (!pos.ptr || pos.ptr+aOffset>=pos.eot) return 0;
  return *(pos.ptr+aOffset);
}


size_t SourceCursor::charsleft() const
{
  return pos.ptr ? pos.eot-pos.ptr : 0;
}


bool SourceCursor::next()
{
  if (EOT()) return false;
  if (*pos.ptr=='\n') {
    pos.line++; // count line
    pos.bol = ++pos.ptr;
  }
  else {
    pos.ptr++;
  }
  return true; // could advance the pointer, does not mean there is anything here, though.
}


bool SourceCursor::advance(size_t aNumChars)
{
  while(aNumChars>0) {
    if (!next()) return false;
    --aNumChars;
  }
  return true;
}


bool SourceCursor::nextIf(char aChar)
{
  if (c()==aChar) {
    next();
    return true;
  }
  return false;
}


void SourceCursor::skipWhiteSpace()
{
  while (c()==' ' || c()=='\t' || c()=='\n' || c()=='\r') next();
}


void SourceCursor::skipNonCode()
{
  if (!pos.ptr) return;
  bool recheck;
  do {
    recheck = false;
    skipWhiteSpace();
    // also check for comments
    if (c()=='/') {
      if (c(1)=='/') {
        advance(2);
        // C++ style comment, lasts until EOT or EOL
        while (c() && c()!='\n' && c()!='\r') next();
        recheck = true;
      }
      else if (c(1)=='*') {
        // C style comment, lasts until '*/'
        advance(2);
        while (c() && c()!='*') next();
        if (c(1)=='/') {
          advance(2);
        }
        recheck = true;
      }
    }
  } while(recheck);
}


string SourceCursor::displaycode(size_t aMaxLen)
{
  return singleLine(pos.ptr, true, aMaxLen);
}


const char *SourceCursor::originLabel() const
{
  if (!source) return "<none>";
  if (!source->originLabel) return "<unlabeled>";
  return source->originLabel;
}


bool SourceCursor::parseIdentifier(string& aIdentifier, size_t* aIdentifierLenP)
{
  if (EOT()) return false;
  size_t o = 0; // offset
  if (!isalpha(c(o))) return false; // is not an identifier
  // is identifier
  o++;
  while (c(o) && (isalnum(c(o)) || c(o)=='_')) o++;
  aIdentifier.assign(pos.ptr, o);
  if (aIdentifierLenP) *aIdentifierLenP = o; // return length, keep cursor at beginning
  else pos.ptr += o; // advance
  return true;
}


bool SourceCursor::checkForIdentifier(const char *aIdentifier)
{
  if (EOT()) return false;
  size_t o = 0; // offset
  if (!isalpha(c(o))) return false; // is not an identifier
  // is identifier
  o++;
  while (c(o) && (isalnum(c(o)) || c(o)=='_')) o++;
  if (strucmp(pos.ptr, aIdentifier, o)!=0) return false; // no match
  pos.ptr += o; // advance
  return true;
}


ScriptOperator SourceCursor::parseOperator()
{
  skipNonCode();
  // check for operator
  ScriptOperator op = op_none;
  size_t o = 0; // offset
  switch (c(o++)) {
    // assignment and equality
    case ':': {
      if (c(o)!='=') goto no_op;
      o++; op = op_assign; break;
    }
    case '=': {
      if (c(o)=='=') {
        o++; op = op_equal; break;
      }
      #if SCRIPT_OPERATOR_MODE==SCRIPT_OPERATOR_MODE_C
      op = op_assign; break;
      #elif SCRIPT_OPERATOR_MODE==SCRIPT_OPERATOR_MODE_PASCAL
      op = op_equal; break;
      #else
      op = op_assignOrEq; break;
      #endif
    }
    case '*': op = op_multiply; break;
    case '/': op = op_divide; break;
    case '%': op = op_modulo; break;
    case '+': op = op_add; break;
    case '-': op = op_subtract; break;
    case '&': op = op_and; if (c(o)=='&') o++; break;
    case '|': op = op_or; if (c(o)=='|') o++; break;
    case '<': {
      if (c(o)=='=') {
        o++; op = op_leq; break;
      }
      else if (c(o)=='>') {
        o++; op = op_notequal; break;
      }
      op = op_less; break;
    }
    case '>': {
      if (c(o)=='=') {
        o++; op = op_geq; break;
      }
      op = op_greater; break;
    }
    case '!': {
      if (c(o)=='=') {
        o++; op = op_notequal; break;
      }
      op = op_not; break;
      break;
    }
    default:
    no_op:
      return op_none;
  }
  advance(o);
  skipNonCode();
  return op;
}


ScriptObjPtr SourceCursor::parseNumericLiteral()
{
  double num;
  int o;
  if (sscanf(pos.ptr, "%lf%n", &num, &o)!=1) {
    // Note: sscanf %d also handles hex!
    return new ErrorPosValue(*this, ScriptError::Syntax, "invalid number, time or date");
  }
  else {
    // o is now past consumation of sscanf
    // check for time/date literals
    // - time literals (returned in seconds) are in the form h:m or h:m:s, where all parts are allowed to be fractional
    // - month/day literals (returned in yeardays) are in the form dd.monthname or dd.mm. (mid the closing dot)
    if (c(o)) {
      if (c(o)==':') {
        // we have 'v:', could be time
        double t; int i;
        if (sscanf(pos.ptr+o+1, "%lf%n", &t, &i)!=1) {
          return new ErrorPosValue(*this, ScriptError::Syntax, "invalid time specification - use hh:mm or hh:mm:ss");
        }
        else {
          o += i+1; // past : and consumation of sscanf
          // we have v:t, take these as hours and minutes
          num = (num*60+t)*60; // in seconds
          if (c(o)==':') {
            // apparently we also have seconds
            if (sscanf(pos.ptr+o+1, "%lf%n", &t, &i)!=1) {
              return new ErrorPosValue(*this, ScriptError::Syntax, "Time specification has invalid seconds - use hh:mm:ss");
            }
            o += i+1; // past : and consumation of sscanf
            num += t; // add the seconds
          }
        }
      }
      else {
        int m = -1; int d = -1;
        if (c(o-1)=='.' && isalpha(c(o))) {
          // could be dd.monthname
          static const char * const monthNames[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
          for (m=0; m<12; m++) {
            if (strucmp(pos.ptr+o, monthNames[m], 3)==0) {
              // valid monthname following number
              // v = day, m = month-1
              m += 1;
              d = num;
              break;
            }
          }
          o += 3;
          if (d<0) {
            return new ErrorPosValue(*this, ScriptError::Syntax, "Invalid date specification - use dd.monthname");
          }
        }
        else if (c(o)=='.') {
          // must be dd.mm. (with mm. alone, sscanf would have eaten it)
          o = 0; // start over
          int l;
          if (sscanf(pos.ptr+o, "%d.%d.%n", &d, &m, &l)!=2) {
            return new ErrorPosValue(*this, ScriptError::Syntax, "Invalid date specification - use dd.mm.");
          }
          o += l;
        }
        if (d>=0) {
          struct tm loctim; MainLoop::getLocalTime(loctim);
          loctim.tm_hour = 12; loctim.tm_min = 0; loctim.tm_sec = 0; // noon - avoid miscalculations that could happen near midnight due to DST offsets
          loctim.tm_mon = m-1;
          loctim.tm_mday = d;
          mktime(&loctim);
          num = loctim.tm_yday;
        }
      }
    }
  }
  advance(o);
  return new NumericValue(num);
}


ScriptObjPtr SourceCursor::parseStringLiteral()
{
  // string literal (c-like with double quotes or php-like with single quotes and no escaping inside)
  char delimiter = c();
  if (delimiter!='"' && delimiter!='\'') {
    return new ErrorPosValue(*this, ScriptError::Syntax, "invalid string literal");
  }
  string str;
  next();
  char sc;
  while(true) {
    sc = c();
    if (sc==delimiter) {
      if (delimiter=='\'' && c(1)==delimiter) {
        // single quoted strings allow including delimiter by doubling it
        str += delimiter;
        advance(2);
        continue;
      }
      break; // end of string
    }
    if (sc==0) {
      return new ErrorPosValue(*this, ScriptError::Syntax, "unterminated string, missing %c delimiter", delimiter);
    }
    if (delimiter!='\'' && sc=='\\') {
      next();
      sc = c();
      if (sc==0) {
        return new ErrorPosValue(*this, ScriptError::Syntax, "incomplete \\-escape");
      }
      else if (sc=='n') sc='\n';
      else if (sc=='r') sc='\r';
      else if (sc=='t') sc='\t';
      else if (sc=='x') {
        unsigned int h = 0;
        next();
        if (sscanf(pos.ptr, "%02x", &h)==1) next();
        sc = (char)h;
      }
      // everything else
    }
    str += sc;
    next();
  }
  next(); // skip closing delimiter
  return new StringValue(str);
}


ScriptObjPtr SourceCursor::parseCodeLiteral()
{
  // TODO: implement
  return new ErrorPosValue(*this, ScriptError::Internal, "Code literals are not yet supported");
}



#if SCRIPTING_JSON_SUPPORT

ScriptObjPtr SourceCursor::parseJSONLiteral()
{
  if (c()!='{' && c()!='[') {
    return new ErrorPosValue(*this, ScriptError::Syntax, "invalid JSON literal");
  }
  // JSON object or array literal
  ssize_t n;
  ErrorPtr err;
  JsonObjectPtr json;
  json = JsonObject::objFromText(pos.ptr, charsleft(), &err, false, &n);
  if (Error::notOK(err)) {
    return new ErrorPosValue(*this, ScriptError::Syntax, "invalid JSON literal: %s", err->text());
  }
  advance(n);
  return new JsonValue(json);
}

#endif


// MARK: - SourceProcessor

#define FOCUSLOGSTATE FOCUSLOG( \
  "%04d %s %22s : %25s : result = %s (olderResult = %s), precedence=%d", \
  threadId(), \
  skipping ? "SKIP" : "EXEC", \
  __func__, \
  src.displaycode(25).c_str(), \
  ScriptObj::describe(result).c_str(), \
  ScriptObj::describe(olderResult).c_str(), \
  precedence \
)

int SourceProcessor::cThreadIdGen = 0;

SourceProcessor::SourceProcessor() :
  aborted(false),
  resuming(false),
  resumed(false),
  evaluationFlags(none),
  currentState(NULL),
  skipping(false),
  precedence(0),
  pendingOperation(op_none)
{
  mThreadId = cThreadIdGen++; // unique thread ID
}

void SourceProcessor::setCursor(const SourceCursor& aCursor)
{
  src = aCursor;
}


void SourceProcessor::initProcessing(EvaluationFlags aEvalFlags)
{
  evaluationFlags = aEvalFlags;
}


void SourceProcessor::setCompletedCB(EvaluationCB aCompletedCB)
{
  completedCB = aCompletedCB;
}


void SourceProcessor::start()
{
  FOCUSLOGSTATE
  stack.clear();
  // just scanning?
  skipping = (evaluationFlags&scanning)!=0;
  // scope to start in
  if (evaluationFlags & expression)
    setState(&SourceProcessor::s_expression);
  else if (evaluationFlags & scriptbody)
    setState(&SourceProcessor::s_body);
  else if (evaluationFlags & sourcecode)
    setState(&SourceProcessor::s_declarations);
  else if (evaluationFlags & block)
    setState(&SourceProcessor::s_block);
  else
    complete(new ErrorValue(ScriptError::Internal, "no processing scope defined"));
  push(&SourceProcessor::s_complete);
  result.reset();
  olderResult.reset();
  resuming = false;
  resume();
}


void SourceProcessor::resume(ScriptObjPtr aResult)
{
  // Store latest result, if any (resuming with NULL pointer does not change the result)
  if (aResult) {
    result = aResult;
  }
  // Am I getting called from a chain of calls originating from
  // myself via step() in the execution loop below?
  if (resuming) {
    // YES: avoid creating an endless call chain recursively
    resumed = true; // flag having resumed already to allow looping below
    return; // but now let chain of calls wind down to our last call (originating from step() in the loop)
  }
  // NO: this is a real re-entry
  if (aborted) {
    complete(result);
    return;
  }
  // re-start the sync execution loop
  resuming = true; // now actually start resuming
  stepLoop();
  // not resumed in the current chain of calls, resume will be called from
  // an independent call site later -> re-enable normal processing
  resuming = false;
}


void SourceProcessor::selfKeepingResume(ScriptCodeThreadPtr aThread, ScriptObjPtr aResumeResult)
{
  aThread->resume(aResumeResult);
}


void SourceProcessor::abort(ScriptObjPtr aAbortResult)
{
  FOCUSLOGSTATE
  if (aAbortResult) {
    result = aAbortResult;
  }
  aborted = true; // signal end to resume() and stepLoop()
}


void SourceProcessor::complete(ScriptObjPtr aFinalResult)
{
  FOCUSLOGSTATE
  resumed = false; // make sure stepLoop WILL exit when returning from step()
  result = aFinalResult; // set final result
  if (result && !result->isErr() && (evaluationFlags & expression)) {
    // expressions not returning an error should run to end
    src.skipNonCode();
    if (!src.EOT()) {
      result = new ErrorPosValue(src, ScriptError::Syntax, "trailing garbage");
    }
  }
  if (!result) {
    result = new AnnotatedNullValue("execution produced no result");
  }
  stack.clear(); // release all objects kept by the stack
  currentState = NULL; // dead
  if (completedCB) {
    EvaluationCB cb = completedCB;
    completedCB = NULL;
    cb(result);
  }
}


void SourceProcessor::stepLoop()
{
  do {
    // run next statemachine step
    resumed = false;
    step(); // will cause resumed to be set when resume() is called in this call's chain
    // repeat as long as we are already resumed
  } while(resumed && !aborted);
}


void SourceProcessor::step()
{
  if (!currentState) {
    complete(result);
    return;
  }
  // call the state handler
  StateHandler sh = currentState;
  (this->*sh)(); // call the handler, which will call resume() here or later
  // Info abour method pointers and their weird syntax:
  // - https://stackoverflow.com/a/1486279
  // - Also see: https://stackoverflow.com/a/6754821
}


// MARK: - source processor internal state machine

void SourceProcessor::checkAndResume()
{
  // simple result check
  if (result && result->isErr()) {
    // abort on errors
    complete(result);
    return;
  }
  resume();
}


void SourceProcessor::push(StateHandler aReturnToState, bool aPushPoppedPos)
{
  FOCUSLOG("                        push[%2lu] :                             result = %s", stack.size()+1, ScriptObj::describe(result).c_str());
  stack.push_back(StackFrame(aPushPoppedPos ? poppedPos : src.pos, skipping, aReturnToState, result, funcCallContext, precedence, pendingOperation));
}


void SourceProcessor::pop()
{
  if (stack.size()==0) {
    complete(new ErrorValue(ScriptError::Internal, "stack empty - cannot pop"));
    return;
  }
  StackFrame &s = stack.back();
  // these are just restored as before the push
  skipping = s.skipping;
  precedence = s.precedence;
  pendingOperation = s.pendingOperation;
  funcCallContext = s.funcCallContext;
  // these are restored separately, returnToState must decide what to do
  poppedPos = s.pos;
  olderResult = s.result;
  FOCUSLOG("                         pop[%2lu] :                        olderResult = %s (result = %s)", stack.size(), ScriptObj::describe(olderResult).c_str(), ScriptObj::describe(result).c_str());
  // continue here
  setState(s.returnToState);
  stack.pop_back();
}

//#error here we ruined something with lvalues - popWithResult

void SourceProcessor::popWithResult(bool aThrowErrors)
{
  FOCUSLOGSTATE;
  if (skipping || !result || result->actualValue() || result->hasType(lvalue)) {
    // no need for a validation step for loading lazy results or for empty lvalues
    popWithValidResult(aThrowErrors);
    return;
  }
  // make valid (pull value from lazy loading objects)
  setState(aThrowErrors ? &SourceProcessor::s_validResultCheck : &SourceProcessor::s_validResult);
  result->makeValid(boost::bind(&SourceProcessor::resume, this, _1));
}


void SourceProcessor::popWithValidResult(bool aThrowErrors)
{
  pop(); // get state to continue with
  if (result) {
    // try to get the actual value (in case what we have is an lvalue or similar proxy)
    ScriptObjPtr validResult = result->actualValue();
    // - replace original value only...
    if (
      validResult && (
        !result->hasType(keeporiginal|lvalue) || ( // ..if keeping not demanded and not lvalue
          currentState!=&SourceProcessor::s_exprFirstTerm && // ..or receiver is neither first term...
          currentState!=&SourceProcessor::s_funcArg && // ..nor function argument...
          currentState!=&SourceProcessor::s_assignExpression // ..nor assignment
        )
      )
    ) {
      result = validResult; // replace original result with its actualValue()
    }
    if (result->isErr() && !result->cursor()) {
      // Errors should get position as near to the creation as possible (and not
      // later when thrown and pos is no longer valid!)
      result = new ErrorPosValue(src, result->errorValue());
    }
  }
  if (aThrowErrors)
    checkAndResume();
  else {
    resume();
  }
}


bool SourceProcessor::unWindStackTo(StateHandler aPreviousState)
{
  StackList::iterator spos = stack.end();
  while (spos!=stack.begin()) {
    --spos;
    if (spos->returnToState==aPreviousState) {
      // found discard everything on top
      stack.erase(++spos, stack.end());
      // now pop the seached state
      pop();
      return true;
    }
  }
  return false;
}


bool SourceProcessor::skipUntilReaching(StateHandler aPreviousState, ScriptObjPtr aThrowValue)
{
  StackList::iterator spos = stack.end();
  while (spos!=stack.begin()) {
    --spos;
    if (spos->returnToState==aPreviousState) {
      // found requested state, make it and all entries on top skipping
      if (aThrowValue) {
        spos->result = aThrowValue;
      }
      while (spos!=stack.end()) {
        spos->skipping = true;
        ++spos;
      }
      // and also enter skip mode for current state
      skipping = true;
      return true;
    }
  }
  return false;
}


void SourceProcessor::exitWithSyntaxError(const char *aFmt, ...)
{
  ErrorPtr err = new ScriptError(ScriptError::Syntax);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  throwOrComplete(new ErrorPosValue(src, err));
}



void SourceProcessor::throwOrComplete(ErrorValuePtr aError)
{
  // thrown herewith
  result = aError;
  aError->setThrown(true);
  ErrorPtr err = aError->errorValue();
  if (err->isDomain(ScriptError::domain()) && err->getErrorCode()>=ScriptError::FatalErrors) {
    // just end the thread unconditionally
    complete(aError);
    return;
  }
  else if (!skipping) {
    if (!skipUntilReaching(&SourceProcessor::s_tryStatement, aError)) {
      complete(aError);
      return;
    }
  }
  // catch found (or skipping), continue executing there
  resume();
}


ScriptObjPtr SourceProcessor::captureCode(ScriptObjPtr aCodeContainer)
{
  CompiledCodePtr code = dynamic_pointer_cast<CompiledCode>(aCodeContainer);
  if (!code) {
    return new ErrorPosValue(src, ScriptError::Internal, "no compiled code");
  }
  else {
    if (evaluationFlags & floatingGlobs) {
      // copy from the original source
      SourceContainerPtr s = SourceContainerPtr(new SourceContainer(src, poppedPos, src.pos));
      code->setCursor(s->getCursor());
    }
    else {
      // refer to the source code part that defines the function
      code->setCursor(SourceCursor(src.source, poppedPos, src.pos));
    }
  }
  return code;
}


// MARK: Simple Terms

void SourceProcessor::s_simpleTerm()
{
  FOCUSLOGSTATE;
  // at the beginning of a simple term, result is undefined
  if (src.c()=='"' || src.c()=='\'') {
    result = src.parseStringLiteral();
    popWithValidResult();
    return;
  }
  else if (src.c()=='{') {
    // json or code block literal
    #if SCRIPTING_JSON_SUPPORT
    SourceCursor peek = src;
    peek.next();
    peek.skipNonCode();
    if (peek.c()=='"' || peek.c()=='\'' || peek.c()=='}') {
      // empty "{}" or first thing within "{" is a quoted field name: must be JSON literal
      result = src.parseJSONLiteral();
      popWithValidResult();
      return;
    }
    #endif
    // must be a code block
    result = src.parseCodeLiteral();
    popWithValidResult();
    return;
  }
  #if SCRIPTING_JSON_SUPPORT
  else if (src.c()=='[') {
    // must be JSON literal array
    result = src.parseJSONLiteral();
    popWithValidResult();
    return;
  }
  #endif
  else {
    // identifier (variable, function) or numeric literal
    if (!src.parseIdentifier(identifier)) {
      // we can get here depending on how statement delimiters are used, so should not always try to parse a numeric...
      if (!src.EOT() && src.c()!='}' && src.c()!=';') {
        // checking for statement separating chars is safe, there's no way one of these could appear at the beginning of a term
        result = src.parseNumericLiteral();
      }
      // anyway, process current result (either it's a new number or the result already set and validated earlier
      popWithValidResult();
      return;
    }
    else {
      // identifier at script scope level
      result.reset(); // lookup from script scope
      olderResult.reset(); // represents the previous level in nested lookups
      src.skipNonCode();
      if (skipping) {
        // we must always assume structured values etc.
        // Note: when skipping, we do NOT need to do complicated check for assignment.
        //   Syntactically, an assignment looks the same as a regular expression
        resumeAt(&SourceProcessor::s_member);
        return;
      }
      else {
        // if it is a plain identifier, it could be one of the built-in constants that cannot be overridden
        if (src.c()!='(' && src.c()!='.' && src.c()!='[') {
          // - check them before doing an actual member lookup
          if (uequals(identifier, "true") || uequals(identifier, "yes")) {
            result = new NumericValue(true);
            popWithResult(false);
            return;
          }
          else if (uequals(identifier, "false") || uequals(identifier, "no")) {
            result = new NumericValue(false);
            popWithResult(false);
            return;
          }
          else if (uequals(identifier, "null") || uequals(identifier, "undefined")) {
            result = new AnnotatedNullValue(identifier); // use literal as annotation
            popWithResult(false);
            return;
          }
        }
        else {
          // no need to check for assign when we already know a ./[/( follows
          assignOrAccess(false);
          return;
        }
        // need to look up the identifier, or assign it
        assignOrAccess(true);
        return;
      }
    }
  }
}

// MARK: member access

void SourceProcessor::assignOrAccess(bool aAllowAssign)
{
  // left hand term leaf member accces
  // - identifier represents the leaf member to access
  // - result represents the parent member or NULL if accessing context scope variables
  // - precedence==0 means that this could be an lvalue term
  // Note: when skipping, we do NOT need to do complicated check for assignment.
  //   Syntactically, an assignment looks the same as a regular expression
  if (!skipping) {
    if (aAllowAssign && precedence==0) {
      // COULD be an assignment
      src.skipNonCode();
      SourcePos opos = src.pos;
      ScriptOperator aop = src.parseOperator();
      if (aop==op_assign || aop==op_assignOrEq) {
        // this IS an assignment. We need to obtain an lvalue and the right hand expression to assign
        push(&SourceProcessor::s_assignExpression);
        setState(&SourceProcessor::s_validResult);
        memberByIdentifier(lvalue);
        return;
      }
      src.pos = opos; // back to before operator
    }
    // not an assignment, just request member value
    setState(&SourceProcessor::s_member);
    memberByIdentifier(src.c()=='(' ? executable : none); // will lookup member of result, or global if result is NULL
    return;
  }
  // only skipping
  setState(&SourceProcessor::s_member);
  resume();
}



void SourceProcessor::s_member()
{
  FOCUSLOGSTATE;
  // after retrieving a potential member's value or lvalue (e.g. after identifier, subscript, function call, paranthesized subexpression)
  // - result is the member's value
  if (src.nextIf('.')) {
    // direct sub-member access
    src.skipNonCode();
    if (!src.parseIdentifier(identifier)) {
      exitWithSyntaxError("missing identifier after '.'");
      return;
    }
    // assign to this identifier or access its value (from parent object in result)
    src.skipNonCode();
    assignOrAccess(true);
    return;
  }
  else if (src.nextIf('[')) {
    // subscript access to sub-members
    src.skipNonCode();
    push(&SourceProcessor::s_subscriptArg);
    resumeAt(&SourceProcessor::s_expression);
    return;
  }
  else if (src.nextIf('(')) {
    // function call
    if (precedence==0) precedence = 1; // no longer a candidate for assignment
    src.skipNonCode();
    // - we need a function call context
    setState(&SourceProcessor::s_funcContext);
    if (!skipping) {
      newFunctionCallContext();
      return;
    }
    resume();
    return;
  }
  // Leaf value: do not error-check or validate at this level, might be lvalue
  memberEventCheck();
  popWithValidResult(false);
  return;
}


void SourceProcessor::s_subscriptArg()
{
  FOCUSLOGSTATE;
  // immediately following a subscript argument evaluation
  // - result is the subscript,
  // - olderResult is the object the subscript applies to
  src.skipNonCode();
  // determine how to proceed after accessing via subscript first...
  if (src.nextIf(']')) {
    // end of subscript processing, what we'll be looking up below is final member (of this subscript bracket, more [] or . may follow!)
    setState(&SourceProcessor::s_member);
  }
  else if (src.nextIf(',')) {
    // more subscripts to apply to the member we'll be looking up below
    src.skipNonCode();
    setState(&SourceProcessor::s_nextSubscript);
  }
  else {
    exitWithSyntaxError("missing , or ] after subscript");
    return;
  }
  if (skipping) {
    // no actual member access
    // Note: when skipping, we do NOT need to do complicated check for assignment.
    //   Syntactically, an assignment looks the same as a regular expression
    checkAndResume();
    return;
  }
  else {
    // now either get or assign the member indicated by the subscript
    TypeInfo accessFlags = none; // subscript access is always local, no scope or assignment restrictions
    ScriptObjPtr subScript = result;
    result = olderResult; // object to access member from
    if (precedence==0) {
      // COULD be an assignment
      SourcePos opos = src.pos;
      ScriptOperator aop = src.parseOperator();
      if (aop==op_assign || aop==op_assignOrEq) {
        // this IS an assignment. We need to obtain an lvalue and the right hand expression to assign
        push(&SourceProcessor::s_assignExpression);
        setState(&SourceProcessor::s_validResult);
        accessFlags |= lvalue; // we need an lvalue
      }
      else {
        // not an assignment, continue pocessing normally
        src.pos = opos; // back to before operator
      }
    }
    // now get member
    if (subScript->hasType(numeric)) {
      // array access by index
      size_t index = subScript->int64Value();
      memberByIndex(index, accessFlags);
      return;
    }
    else {
      // member access by name
      identifier = subScript->stringValue();
      memberByIdentifier(accessFlags);
      return;
    }
  }
}

void SourceProcessor::s_nextSubscript()
{
  // immediately following a subscript argument evaluation
  // - result is the object the next subscript should apply to
  push(&SourceProcessor::s_subscriptArg);
  checkAndResumeAt(&SourceProcessor::s_expression);
}


// MARK: function calls

void SourceProcessor::s_funcContext()
{
  FOCUSLOGSTATE;
  // - check for arguments
  if (src.nextIf(')')) {
    // function with no arguments
    resumeAt(&SourceProcessor::s_funcExec);
    return;
  }
  push(&SourceProcessor::s_funcArg);
  resumeAt(&SourceProcessor::s_expression);
  return;
}


void SourceProcessor::s_funcArg()
{
  FOCUSLOGSTATE;
  // immediately following a function argument evaluation
  // - result is value of the function argument
  // - olderResult is the function the argument applies to
  // - poppedpos is the beginning of the argument
  ScriptObjPtr arg = result;
  result = olderResult; // restore the function
  src.skipNonCode();
  // determine how to proceed after pushing the argument...
  if (src.nextIf(')')) {
    // end of argument processing, execute the function after pushing the final argument below
    setState(&SourceProcessor::s_funcExec);
  }
  else if (src.nextIf(',')) {
    // more arguments follow, continue evaluating them after pushing the current argument below
    src.skipNonCode();
    push(&SourceProcessor::s_funcArg);
    setState(&SourceProcessor::s_expression);
  }
  else {
    exitWithSyntaxError("missing , or ) after function argument");
    return;
  }
  // now apply the function argument
  if (skipping) {
    checkAndResume(); // just ignore the argument and continue
  }
  else {
    pushFunctionArgument(arg);
  }
  return;
}


void SourceProcessor::s_funcExec()
{
  FOCUSLOGSTATE;
  // after closing parantheis of a function call
  // - result is the function to call
  setState(&SourceProcessor::s_member); // result of the function call might be a member
  if (skipping) {
    checkAndResume(); // just NOP
  }
  else {
    executeResult(); // execute
  }
}


// MARK: Expressions

void SourceProcessor::s_assignmentExpression()
{
  FOCUSLOGSTATE;
  precedence = 0; // first lvalue can be assigned
  processExpression();
}

void SourceProcessor::s_expression()
{
  FOCUSLOGSTATE;
  precedence = 1; // first left hand term is not assignable
  processExpression();
}


void SourceProcessor::s_subExpression()
{
  FOCUSLOGSTATE;
  // no change in precedence
  processExpression();
}

void SourceProcessor::processExpression()
{
  // at start of an (sub)expression
  SourcePos epos = src.pos; // remember start of any expression, even if it's only a precedence terminated subexpression
  // - check for optional unary op
  pendingOperation = src.parseOperator(); // store for later
  if (pendingOperation!=op_none && pendingOperation!=op_subtract && pendingOperation!=op_add && pendingOperation!=op_not) {
    exitWithSyntaxError("invalid unary operator");
    return;
  }
  if (pendingOperation!=op_none && precedence==0) {
    precedence = 1; // no longer a candidate for assignment
  }
  // evaluate first (or only) term
  // - check for paranthesis term
  if (src.nextIf('(')) {
    // term is expression in paranthesis
    push(&SourceProcessor::s_groupedExpression);
    resumeAt(&SourceProcessor::s_expression);
    return;
  }
  // must be simple term
  // - a variable/constant reference
  // - a function call
  // - a literal value
  // OR:
  // - an lvalue (one that can be assigned to)
  // Note: a non-simple term is the paranthesized expression as handled above
  push(&SourceProcessor::s_exprFirstTerm);
  resumeAt(&SourceProcessor::s_simpleTerm);
}


void SourceProcessor::s_groupedExpression()
{
  FOCUSLOGSTATE;
  if (!src.nextIf(')')) {
    exitWithSyntaxError("missing ')'");
    return;
  }
  push(&SourceProcessor::s_exprFirstTerm);
  resumeAt(&SourceProcessor::s_member); // always check for submember access first (grouped expression result could be an object)
}


void SourceProcessor::s_exprFirstTerm()
{
  FOCUSLOGSTATE;
  // res now has the first term of an expression, which might need applying unary operations
  if (!skipping && result && result->defined()) {
    switch (pendingOperation) {
      case op_not : result = new NumericValue(!result->boolValue()); break;
      case op_subtract : result = new NumericValue(-result->doubleValue()); break;
      case op_add: // dummy, is NOP, allowed for clarification purposes
      default: break;
    }
  }
  resumeAt(&SourceProcessor::s_exprLeftSide);
}


void SourceProcessor::s_exprLeftSide()
{
  FOCUSLOGSTATE;
  // check binary operators
  SourcePos opos = src.pos; // position before possibly finding an operator and before skipping anything
  src.skipNonCode();
  ScriptOperator binaryop = src.parseOperator();
  int newPrecedence = binaryop & opmask_precedence;
  // end parsing here if no operator found or operator with a lower or same precedence as the passed in precedence is reached
  if (binaryop==op_none || newPrecedence<=precedence) {
    src.pos = opos; // restore position
    popWithResult(false); // receiver of expression will still get an error, no automatic throwing here!
    return;
  }
  // must parse right side of operator as subexpression
  pendingOperation = binaryop;
  push(&SourceProcessor::s_exprRightSide); // push the old precedence
  precedence = newPrecedence; // subexpression needs to exit when finding an operator weaker than this one
  resumeAt(&SourceProcessor::s_subExpression);
}


void SourceProcessor::s_assignExpression()
{
  FOCUSLOGSTATE;
  // assign an expression to the current result
  push(&SourceProcessor::s_checkAndAssignLvalue);
  resumeAt(&SourceProcessor::s_expression);
  return;
}



void SourceProcessor::s_assignOlder()
{
  FOCUSLOGSTATE;
  // - result = lvalue
  // - olderResult = value to assign
  // Note: s_assignOlder is only used from language constructs, not from normal expressions/assignments.
  //   This means the result was known before the assignment was initiated,
  //   so the to-be-assigned value can be considered checked.
  //   Also, if the value to assign to is not an lvalue, this is silently ignored (re-initialisation of globals)
  if (!skipping) {
    // assign a olderResult to the current result
    if (result && !result->hasType(lvalue)) {
      // not an lvalue, silently ignore assignment
      FOCUSLOG("   s_assignOlder: silently IGNORING assignment to non-lvalue : value=%s", ScriptObj::describe(result).c_str());
      setState(&SourceProcessor::s_result);
      resume();
      return;
    }
    ScriptObjPtr lvalue = result;
    result = olderResult;
    olderResult = lvalue;
  }
  // we don't want the result checked!
  s_assignLvalue();
}


void SourceProcessor::s_unsetMember()
{
  FOCUSLOGSTATE;
  // try to delete the current result
  if (!skipping) {
    olderResult = result;
    result.reset(); // no object means deleting
    if (!olderResult) {
      result = new AnnotatedNullValue("nothing to unset");
      s_result();
      return;
    }
  }
  s_assignLvalue();
}


void SourceProcessor::s_checkAndAssignLvalue()
{
  FOCUSLOGSTATE;
  checkAndResumeAt(&SourceProcessor::s_assignLvalue);
}

void SourceProcessor::s_assignLvalue()
{
  FOCUSLOGSTATE;
  // olderResult = lvalue
  // result = value to assign (or NULL to delete)
  setState(&SourceProcessor::s_result); // assignment expression ends here, will either result in assigned value or storage error
  if (!skipping) {
    if (result) result = result->assignmentValue(); // get a copy in case the value is mutable (i.e. copy-on-write, assignment is "writing")
    olderResult->assignLValue(boost::bind(&SourceProcessor::resume, this, _1), result);
    return;
  }
  resume();
}


void SourceProcessor::s_exprRightSide()
{
  FOCUSLOGSTATE;
  // olderResult = leftside, result = rightside
  if (!skipping) {
    // all operations involving nulls return null except equality which compares being null with not being null
    ScriptObjPtr left = olderResult->calculationValue();
    ScriptObjPtr right = result->calculationValue();
    if (pendingOperation==op_equal || pendingOperation==op_assignOrEq) {
      result = new NumericValue(*left == *right);
    }
    else if (pendingOperation==op_notequal) {
      result = new NumericValue(*left != *right);
    }
    else if (left->defined() && right->defined()) {
      // both are values -> apply the operation between leftside and rightside
      switch (pendingOperation) {
        case op_assign: {
          // unambiguous assignment operator is not allowed here (ambiguous = will be treated as comparison)
          if (!skipping) { exitWithSyntaxError("nested assigment not allowed"); return; }
          break;
        }
        case op_not: {
          exitWithSyntaxError("NOT operator not allowed here");
          return;
        }
        case op_divide:     result = *left / *right; break;
        case op_modulo:     result = *left % *right; break;
        case op_multiply:   result = *left * *right; break;
        case op_add:        result = *left + *right; break;
        case op_subtract:   result = *left - *right; break;
        // boolean result
        case op_less:       result = new NumericValue(*left <  *right); break;
        case op_greater:    result = new NumericValue(*left >  *right); break;
        case op_leq:        result = new NumericValue(*left <= *right); break;
        case op_geq:        result = new NumericValue(*left >= *right); break;
        case op_and:        result = new NumericValue(*left && *right); break;
        case op_or:         result = new NumericValue(*left || *right); break;
        default: break;
      }
    }
    else if (left->isErr()) {
      // if first is error, return that independently of what the second is
      result = left;
    }
    else if (!right->isErr()) {
      // one or both operands undefined, and none of them an error
      result = new AnnotatedNullValue("operation between undefined values");
    }
  }
  resumeAt(&SourceProcessor::s_exprLeftSide); // back to leftside, more chained operators might follow
}

// MARK: Declarations

void SourceProcessor::s_declarations()
{
  FOCUSLOGSTATE
  // skip empty statements, do not count these as start of body
  do {
    src.skipNonCode();
  } while (src.nextIf(';'));
  SourcePos declStart = src.pos;
  if (src.parseIdentifier(identifier)) {
    // could be a variable declaration
    if (uequals(identifier, "glob") || uequals(identifier, "global")) {
      // allow initialisation, even re-initialisation of global vars here!
      processVarDefs(lvalue|create|global, true, true);
      return;
    }
    if (uequals(identifier, "function")) {
      // function fname([param[,param...]]) { code }
      src.skipNonCode();
      if (!src.parseIdentifier(identifier)) {
        exitWithSyntaxError("function name expected");
        return;
      }
      CompiledCodePtr function = CompiledCodePtr(new CompiledCode(identifier));
      // optional argument list
      src.skipNonCode();
      if (src.nextIf('(')) {
        src.skipNonCode();
        if (!src.nextIf(')')) {
          do {
            src.skipNonCode();
            if (src.c()=='.' && src.c(1)=='.' && src.c(2)=='.') {
              // open argument list
              src.advance(3);
              function->pushArgumentDefinition(any|null|error|multiple, "arg");
              break;
            }
            string argName;
            if (!src.parseIdentifier(argName)) {
              exitWithSyntaxError("function argument name expected");
              return;
            }
            function->pushArgumentDefinition(any|null|error, argName);
            src.skipNonCode();
          } while(src.nextIf(','));
          if (!src.nextIf(')')) {
            exitWithSyntaxError("missing closing ')' for argument list");
            return;
          }
        }
        src.skipNonCode();
      }
      result = function;
      // now capture the code
      if (src.c()!='{') {
        exitWithSyntaxError("expected function body");
        return;
      }
      push(&SourceProcessor::s_defineFunction); // with position on the opening '{' of the function body
      skipping = true;
      src.next(); // skip the '{'
      resumeAt(&SourceProcessor::s_block);
      return;
    } // function
    if (uequals(identifier, "on")) {
      // on (triggerexpression) [toggling|changing|evaluating] { code }
      src.skipNonCode();
      if (!src.nextIf('(')) {
        exitWithSyntaxError("'(' expected");
        return;
      }
      push(&SourceProcessor::s_defineTrigger);
      skipping = true;
      resumeAt(&SourceProcessor::s_expression);
      return;
    } // handler
  } // identifier
  // nothing recognizable as declaration
  src.pos = declStart; // rewind to beginning of last statement
  setState(&SourceProcessor::s_body);
  startOfBodyCode();
}


void SourceProcessor::s_defineFunction()
{
  FOCUSLOGSTATE
  // after scanning a block containing a function body
  // - poppedPos points to the opening '{' of the body
  // - src.pos is after the closing '}' of the body
  // - olderResult is the CompiledFunction
  setState(&SourceProcessor::s_declarations); // back to declarations
  result = captureCode(olderResult);
  storeFunction();
}


void SourceProcessor::s_defineTrigger()
{
  FOCUSLOGSTATE
  // on (triggerexpression) [changing|toggling|evaluating|gettingtrue] [ stable <stabilizing time numeric literal>] [ as triggerresult ] { handlercode }
  // after scanning the trigger condition expression of a on() statement
  // - poppedPos points to the beginning of the expression
  // - src.pos should be on the ')' of the trigger expression
  if (src.c()!=')') {
    exitWithSyntaxError("')' as end of trigger expression expected");
    return;
  }
  CompiledTriggerPtr trigger = new CompiledTrigger("trigger", getCompilerMainContext());
  result = captureCode(trigger);
  src.next(); // skip ')'
  src.skipNonCode();
  // optional trigger mode
  TriggerMode mode = inactive;
  MLMicroSeconds holdOff = Never;
  bool hasid = src.parseIdentifier(identifier);
  if (hasid) {
    if (uequals(identifier, "changing")) {
      mode = onChange;
    }
    else if (uequals(identifier, "toggling")) {
      mode = onChangingBool;
    }
    else if (uequals(identifier, "evaluating")) {
      mode = onEvaluation;
    }
    else if (uequals(identifier, "gettingtrue")) {
      mode = onGettingTrue;
    }
  }
  if (mode==inactive) {
    // no explicit mode, default to onGettingTrue
    mode = onGettingTrue;
  }
  else {
    src.skipNonCode();
    hasid = src.parseIdentifier(identifier);
  }
  if (hasid) {
    if (uequals(identifier, "stable")) {
      src.skipNonCode();
      ScriptObjPtr h = src.parseNumericLiteral();
      if (h->isErr()) {
        complete(h);
        return;
      }
      holdOff = h->doubleValue()*Second;
      src.skipNonCode();
      hasid = src.parseIdentifier(identifier);
    }
  }
  if (hasid) {
    if (uequals(identifier, "as")) {
      src.skipNonCode();
      if (!src.parseIdentifier(identifier)) {
        exitWithSyntaxError("missing trigger result variable name");
        return;
      }
      trigger->mResultVarName = identifier;
    }
    else {
      exitWithSyntaxError("missing trigger mode or 'as'");
      return;
    }
  }
  trigger->setTriggerMode(mode, holdOff);
  src.skipNonCode();
  // check for beginning of handler body
  if (src.c()!='{') {
    exitWithSyntaxError("expected handler body");
    return;
  }
  push(&SourceProcessor::s_defineHandler); // with position on the opening '{' of the handler body
  skipping = true;
  src.next(); // skip the '{'
  resumeAt(&SourceProcessor::s_block);
  return;
}


void SourceProcessor::s_defineHandler()
{
  FOCUSLOGSTATE
  // after scanning a block containing a handler body
  // - poppedPos points to the opening '{' of the body
  // - src.pos is after the closing '}' of the body
  // - olderResult is the trigger, mode already set
  setState(&SourceProcessor::s_declarations); // back to declarations
  CompiledHandlerPtr handler = new CompiledHandler("handler", getCompilerMainContext());
  result = captureCode(handler); // get the code first, so we can execute it in the trigger init
  handler->installAndInitializeTrigger(olderResult);
  storeHandler();
}



// MARK: Statements

void SourceProcessor::s_noStatement()
{
  FOCUSLOGSTATE
  src.nextIf(';');
  pop();
  checkAndResume();
}


void SourceProcessor::s_oneStatement()
{
  FOCUSLOGSTATE
  setState(&SourceProcessor::s_noStatement);
  processStatement();
}


void SourceProcessor::s_block()
{
  FOCUSLOGSTATE
  processStatement();
}


void SourceProcessor::s_body()
{
  FOCUSLOGSTATE
  processStatement();
}


void SourceProcessor::processStatement()
{
  FOCUSLOG("\n========== At statement boundary : %s", src.displaycode(130).c_str());
  src.skipNonCode();
  if (src.EOT()) {
    // end of code
    if (currentState!=&SourceProcessor::s_body) {
      exitWithSyntaxError("unexpected end of code");
      return;
    }
    // complete
    complete(result);
    return;
  }
  if (src.nextIf('{')) {
    // new block starts
    push(currentState); // return to current state when block finishes
    resumeAt(&SourceProcessor::s_block); // continue as block
    return;
  }
  if (src.nextIf('}')) {
    // block ends
    if (currentState==&SourceProcessor::s_block) {
      // actually IS a block
      pop();
      checkAndResume();
      return;
    }
    exitWithSyntaxError("unexpected '}'");
    return;
  }
  if (src.nextIf(';')) {
    if (currentState==&SourceProcessor::s_oneStatement) {
      // the separator alone comprises the statement we were waiting for in s_oneStatement(), so we're done
      checkAndResume();
      return;
    }
    src.skipNonCode();
  }
  // at the beginning of a statement which is not beginning of a new block
  result.reset(); // no result to begin with at the beginning of a statement. Important for if/else, try/catch!
  // - could be language keyword, variable assignment
  SourcePos memPos = src.pos; // remember
  if (src.parseIdentifier(identifier)) {
    src.skipNonCode();
    // execution statements
    if (uequals(identifier, "if")) {
      // "if" statement
      if (!src.nextIf('(')) {
        exitWithSyntaxError("missing '(' after 'if'");
        return;
      }
      push(currentState); // return to current state when if statement finishes
      push(&SourceProcessor::s_ifCondition);
      resumeAt(&SourceProcessor::s_expression);
      return;
    }
    if (uequals(identifier, "while")) {
      // "while" statement
      if (!src.nextIf('(')) {
        exitWithSyntaxError("missing '(' after 'while'");
        return;
      }
      push(currentState); // return to current state when while finishes
      push(&SourceProcessor::s_whileCondition);
      resumeAt(&SourceProcessor::s_expression);
      return;
    }
    if (uequals(identifier, "break")) {
      if (!skipping) {
        if (!skipUntilReaching(&SourceProcessor::s_whileStatement)) {
          exitWithSyntaxError("'break' must be within 'while' statement");
          return;
        }
        checkAndResume();
        return;
      }
    }
    if (uequals(identifier, "continue")) {
      if (!skipping) {
        if (!unWindStackTo(&SourceProcessor::s_whileStatement)) {
          exitWithSyntaxError("'continue' must be within 'while' statement");
          return;
        }
        checkAndResume();
        return;
      }
    }
    if (uequals(identifier, "return")) {
      if (!src.EOT() && src.c()!=';') {
        // return with return value
        if (skipping) {
          // we must parse over the return expression properly AND then continue parsing
          push(currentState); // return to current state when return expression is parsed
          push(&SourceProcessor::s_result);
        }
        else {
          // once return expression completes, entire script completes and stack is discarded.
          // (thus, no need to push the current state, we'll not return anyway!)
          push(&SourceProcessor::s_complete);
        }
        // anyway, we need to process the return expression, skipping or not.
        checkAndResumeAt(&SourceProcessor::s_expression);
        return;
      }
      else {
        // return without return value
        if (!skipping) {
          result = new AnnotatedNullValue("return nothing");
          complete(result);
          return;
        }
        checkAndResume(); // skipping -> just ignore
        return;
      }
    }
    if (uequals(identifier, "try")) {
      push(currentState); // return to current state when statement finishes
      push(&SourceProcessor::s_tryStatement);
      resumeAt(&SourceProcessor::s_oneStatement);
      return;
    }
    if (uequals(identifier, "catch")) {
      // just check to give sensible error message
      exitWithSyntaxError("'catch' without preceeding 'try'");
      return;
    }
    if (uequals(identifier, "concurrent")) {
      // Syntax: concurrent as myThread {}
      //     or: concurrent {}
      src.skipNonCode();
      identifier.clear();
      if (src.checkForIdentifier("as")) {
        src.skipNonCode();
        if (src.parseIdentifier(identifier)) {
          // we want the thread be a variable in order to wait for it and stop it
          src.skipNonCode();
        }
      }
      if (!src.nextIf('{')) {
        exitWithSyntaxError("missing '{' to start concurrent block");
        return;
      }
      push(currentState); // return to current state when statement finishes
      setState(&SourceProcessor::s_block);
      // "fork" the thread
      if (!skipping) {
        skipping = true; // for myself: just skip the next block
        startBlockThreadAndStoreInIdentifier(); // includes resume()
        return;
      }
      checkAndResume(); // skipping, no actual fork
      return;
    }
    // Check variable definition keywords
    if (uequals(identifier, "var")) {
      processVarDefs(lvalue+create, true);
      return;
    }
    if (uequals(identifier, "glob") || uequals(identifier, "global")) {
      processVarDefs(lvalue+create+onlycreate+global, false);
      return;
    }
    if (uequals(identifier, "let")) {
      processVarDefs(lvalue, true);
      return;
    }
    if (uequals(identifier, "unset")) {
      processVarDefs(lvalue+unset, false);
      return;
    }
    // just check to give sensible error message
    if (uequals(identifier, "else")) {
      exitWithSyntaxError("'else' without preceeding 'if'");
      return;
    }
    if (uequals(identifier, "on") || uequals(identifier, "function")) {
      exitWithSyntaxError("declarations must be made before first script statement");
      return;
    }
    // identifier we've parsed above is not a keyword, rewind cursor
    src.pos = memPos;
  }
  // is an expression or possibly an assignment, also handled in expression
  push(currentState); // return to current state when expression evaluation completes
  resumeAt(&SourceProcessor::s_assignmentExpression);
  return;
}


void SourceProcessor::processVarDefs(TypeInfo aVarFlags, bool aAllowInitializer, bool aDeclaration)
{
  src.skipNonCode();
  // one of the variable definition keywords -> an identifier must follow
  if (!src.parseIdentifier(identifier)) {
    exitWithSyntaxError("missing variable name after '%s'", identifier.c_str());
    return;
  }
  push(currentState); // return to current state when var definion statement finishes
  if (aDeclaration) skipping = false; // must enable processing now for actually assigning globals.
  src.skipNonCode();
  SourcePos memPos = src.pos;
  ScriptOperator op = src.parseOperator();
  // with initializer ?
  if (op==op_assign || op==op_assignOrEq) {
    if (!aAllowInitializer) {
      exitWithSyntaxError("no initializer allowed");
      return;
    }
    // initializing with a value
    setState(&SourceProcessor::s_assignExpression);
    memberByIdentifier(aVarFlags);
    return;
  }
  else if (op==op_none) {
    if (aVarFlags & unset) {
      // after accessing lvalue, delete it
      setState(&SourceProcessor::s_unsetMember);
      memberByIdentifier(aVarFlags, true); // lookup lvalue
      return;
    }
    else {
      // just create and initialize with null (if not already existing)
      if (aVarFlags & global) {
        result = new EventPlaceholderNullValue("uninitialized global");
      }
      else {
        result = new AnnotatedNullValue("uninitialized variable");
      }
      push(&SourceProcessor::s_assignOlder);
      setState(&SourceProcessor::s_nothrowResult);
      result.reset(); // look up on context level
      memberByIdentifier(aVarFlags); // lookup lvalue
      return;
    }
  }
  else {
    exitWithSyntaxError("assignment or end of statement expected");
    return;
  }
}



void SourceProcessor::s_ifCondition()
{
  FOCUSLOGSTATE
  // if condition is evaluated
  // - if not skipping, result is the result of the evaluation, or NULL if all of the following if/else if/else statement chain must be skipped
  // - if already skipping here, result can be anything and must be reset to propagate cutting the else chain
  if (!src.nextIf(')')) {
    exitWithSyntaxError("missing ')' after 'if' condition");
    return;
  }
  if (!skipping) {
    // a real if decision
    skipping = !result->boolValue();
    if (!skipping) result.reset(); // any executed if branch must cause skipping all following else branches
  }
  else {
    // nothing to decide any more
    result.reset();
  }
  // Note: pushed skipping AND result is needed by s_ifTrueStatement to determine further flow
  push(&SourceProcessor::s_ifTrueStatement);
  resumeAt(&SourceProcessor::s_oneStatement);
}

void SourceProcessor::s_ifTrueStatement()
{
  FOCUSLOGSTATE
  // if statement (or block of statements) is executed or skipped
  // - if olderResult is set to something, else chain must be executed, which means
  //   else-ifs must be checked or last else must be executed. Otherwise, skip everything from here on.
  // Note: "skipping" at this point is not relevant for deciding further flow
  // check for "else" following
  src.skipNonCode();
  if (src.checkForIdentifier("else")) {
    // else
    skipping = olderResult==NULL;
    src.skipNonCode();
    if (src.checkForIdentifier("if")) {
      // else if
      src.skipNonCode();
      if (!src.nextIf('(')) {
        exitWithSyntaxError("missing '(' after 'else if'");
        return;
      }
      // chained if: when preceeding "if" did execute (or would have if not already skipping),
      // rest of if/elseif...else chain will be skipped
      // Note: pushed skipping AND result is needed by s_ifCondition to determine further flow
      result = olderResult; // carry on the "entire if/elseif/else statement executing" marker
      push(&SourceProcessor::s_ifCondition);
      resumeAt(&SourceProcessor::s_expression);
      return;
    }
    else {
      // last else in chain
      resumeAt(&SourceProcessor::s_oneStatement); // run one statement, then pop
      return;
    }
  }
  else {
    // if without else
    pop(); // end if/then/else
    resume();
    return;
  }
}


void SourceProcessor::s_whileCondition()
{
  FOCUSLOGSTATE
  // while condition is evaluated
  // - result contains result of the evaluation
  // - poppedPos points to beginning of while condition
  if (!src.nextIf(')')) {
    exitWithSyntaxError("missing ')' after 'while' condition");
    return;
  }
  if (!skipping) skipping = !result->boolValue(); // set now, because following push must include "skipping" according to the decision!
  push(&SourceProcessor::s_whileStatement, true); // push poopedPos (again) = loopback position we'll need at s_whileStatement
  checkAndResumeAt(&SourceProcessor::s_oneStatement);
}

void SourceProcessor::s_whileStatement()
{
  FOCUSLOGSTATE
  // while statement (or block of statements) is executed
  // - poppedPos points to beginning of while condition
  if (skipping) {
    // skipping because condition was false or "break" set skipping in the stack with skipUntilReaching()
    pop(); // end while
    checkAndResume();
    return;
  }
  // not skipping, means we need to loop back to the condition
  src.pos = poppedPos;
  push(&SourceProcessor::s_whileCondition);
  resumeAt(&SourceProcessor::s_expression);
}


void SourceProcessor::s_tryStatement()
{
  FOCUSLOGSTATE
  // Syntax: try { statements to try } catch [as errorvariable] { statements handling error }
  // try statement is executed
  // - olderResult contains the error
  // - check for "catch" following
  src.skipNonCode();
  if (src.checkForIdentifier("catch")) {
    // if olderResult is an error, we must catch it. Otherwise skip the catch statement.
    // Note: olderResult can be the try statement's regular result at this point
    skipping = !olderResult || !olderResult->isErr();
    // catch can set the error into a local var
    src.skipNonCode();
    // run (or skip) what follows as one statement
    setState(&SourceProcessor::s_oneStatement);
    // check for error capturing variable
    if (src.checkForIdentifier("as")) {
      src.skipNonCode();
      if (!src.parseIdentifier(identifier)) {
        exitWithSyntaxError("missing error variable name after 'as'");
        return;
      }
      if (!skipping) {
        result = olderResult; // the error value
        push(currentState); // want to return here
        push(&SourceProcessor::s_assignOlder); // push the error value
        setState(&SourceProcessor::s_nothrowResult);
        result.reset(); // create error variable on scope level
        memberByIdentifier(lvalue+create);
        return;
      }
    }
    checkAndResume();
    return;
  }
  else {
    exitWithSyntaxError("missing 'catch' after 'try'");
    return;
  }
}

// MARK: Generic states

void SourceProcessor::s_result()
{
  FOCUSLOGSTATE;
  popWithResult(true);
}

void SourceProcessor::s_nothrowResult()
{
  FOCUSLOGSTATE;
  popWithResult(false);
}

void SourceProcessor::s_validResult()
{
  FOCUSLOGSTATE;
  popWithValidResult(false);
}

void SourceProcessor::s_uncheckedResult()
{
  FOCUSLOGSTATE;
  pop();
  resume();
}



void SourceProcessor::s_validResultCheck()
{
  FOCUSLOGSTATE;
  popWithValidResult(true);
}

void SourceProcessor::s_complete()
{
  FOCUSLOGSTATE;
  complete(result);
}



// MARK: source processor execution hooks

void SourceProcessor::memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError)
{
  result.reset(); // base class cannot access members
  checkAndResume();
}


void SourceProcessor::memberByIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  result.reset(); // base class cannot access members
  checkAndResume();
}


void SourceProcessor::newFunctionCallContext()
{
  result.reset(); // base class cannot execute functions
  checkAndResume();
}


void SourceProcessor::startBlockThreadAndStoreInIdentifier()
{
  /* NOP */
  checkAndResume();
}

void SourceProcessor::pushFunctionArgument(ScriptObjPtr aArgument)
{
  checkAndResume(); // NOP on the base class level
}

void SourceProcessor::storeFunction()
{
  checkAndResume(); // NOP on the base class level
}

void SourceProcessor::storeHandler()
{
  checkAndResume(); // NOP on the base class level
}

void SourceProcessor::startOfBodyCode()
{
  checkAndResume(); // NOP on the base class level
}



void SourceProcessor::executeResult()
{
  result.reset(); // base class cannot evaluate
  checkAndResume();
}


void SourceProcessor::memberEventCheck()
{
  /* NOP here */
}


// MARK: - CompiledScript, CompiledFunction, CompiledHandler


void CompiledCode::setCursor(const SourceCursor& aCursor)
{
  cursor = aCursor;
  FOCUSLOG("New code named '%s' @ 0x%p: %s", name.c_str(), this, cursor.displaycode(70).c_str());
}


CompiledCode::~CompiledCode()
{
  FOCUSLOG("Released code named '%s' @ 0x%p", name.c_str(), this);
}

ExecutionContextPtr CompiledCode::contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const
{
  // functions get executed in a private context linked to the caller's (main) context
  return new ScriptCodeContext(aMainContext);
}

void CompiledCode::pushArgumentDefinition(TypeInfo aTypeInfo, const string aArgumentName)
{
  ArgumentDescriptor arg;
  arg.typeInfo = aTypeInfo;
  arg.name = aArgumentName;
  arguments.push_back(arg);
}


bool CompiledCode::argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const
{
  size_t idx = aIndex;
  if (idx>=arguments.size()) {
    // no argument with this index, check for open argument list
    if (arguments.size()<1) return false;
    idx = arguments.size()-1;
    if ((arguments[idx].typeInfo & multiple)==0) return false;
  }
  aArgDesc = arguments[idx];
  if (aArgDesc.typeInfo & multiple) {
    aArgDesc.name = string_format("%s%zu", arguments[idx].name.c_str(), aIndex+1);
  }
  return true;
}


ExecutionContextPtr CompiledScript::contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const
{
  // compiled script bodies get their execution context assigned at compile time, just return it
  // - but maincontext passed should be the domain of our saved mainContext, so check that if aMainContext is passed
  if (aMainContext) {
    if (mainContext->domain().get()!=aMainContext.get()) {
      LOG(LOG_ERR, "internal error: script domain mismatch");
      return NULL; // mismatch, cannot use that context!
    }
  }
  return mainContext;
}


// MARK: - Trigger


CompiledTrigger::CompiledTrigger(const string aName, ScriptMainContextPtr aMainContext) :
  inherited(aName, aMainContext),
  mTriggerMode(inactive),
  mCurrentState(p44::undefined),
  mOneShotEvent(false),
  mEvalFlags(expression|synchronously),
  mNextEvaluation(Never),
  mMetAt(Never),
  mHoldOff(0)
{
}


ScriptObjPtr CompiledTrigger::initializeTrigger()
{
  // initialize it
  FOCUSLOG("\n---------- Initializing Trigger  : %s", cursor.displaycode(130).c_str());
  mReEvaluationTicket.cancel();
  mNextEvaluation = Never; // reset
  mFrozenResults.clear(); // (re)initializing trigger unfreezes all values
  clearSources(); // forget all event sources
  ExecutionContextPtr ctx = contextForCallingFrom(NULL, NULL);
  if (!ctx) return  new ErrorValue(ScriptError::Internal, "no context for trigger");
  EvaluationFlags initFlags = (mEvalFlags&~runModeMask)|initial;
  OLOG(LOG_INFO, "initial trigger evaluation: %s", cursor.displaycode(130).c_str());
  if (mEvalFlags & synchronously) {
    #if DEBUGLOGGING
    ScriptObjPtr res = ctx->executeSynchronously(this, initFlags, Infinite);
    #else
    ScriptObjPtr res = ctx->executeSynchronously(this, initFlags, 2*Second);
    #endif
    triggerDidEvaluate(initFlags, res);
    return res;
  }
  else {
    triggerEvaluation(initFlags);
    return new AnnotatedNullValue("asynchonously initializing trigger");
  }
}


void CompiledTrigger::processEvent(ScriptObjPtr aEvent, EventSource &aSource)
{
  mOneShotEvent = aEvent->hasType(oneshot);
  triggerEvaluation(triggered);
}



void CompiledTrigger::triggerEvaluation(EvaluationFlags aEvalMode)
{
  FOCUSLOG("\n---------- Evaluating Trigger    : %s", cursor.displaycode(130).c_str());
  mReEvaluationTicket.cancel();
  mNextEvaluation = Never; // reset
  ExecutionContextPtr ctx = contextForCallingFrom(NULL, NULL);
  EvaluationFlags runFlags = (aEvalMode&~runModeMask) ? aEvalMode : (mEvalFlags&~runModeMask)|aEvalMode; // use only runmode from aEvalMode if nothing else is set
  ctx->execute(ScriptObjPtr(this), runFlags, boost::bind(&CompiledTrigger::triggerDidEvaluate, this, runFlags, _1), 30*Second);
}


void CompiledTrigger::triggerDidEvaluate(EvaluationFlags aEvalMode, ScriptObjPtr aResult)
{
  OLOG(aEvalMode&initial ? LOG_INFO : LOG_DEBUG, "evaluated trigger: %s in evalmode=0x%x\n- with result: %s%s", cursor.displaycode(90).c_str(), aEvalMode, mOneShotEvent ? "(ONESHOT) " : "", ScriptObj::describe(aResult).c_str());
  bool doTrigger = false;
  Tristate newState = aResult->defined() ? (aResult->boolValue() ? p44::yes : p44::no) : p44::undefined;
  if (mTriggerMode==onEvaluation) {
    doTrigger = true;
  }
  else if (mTriggerMode==onChange) {
    doTrigger = (*aResult) != *currentResult();
  }
  else {
    // bool modes
    doTrigger = mCurrentState!=newState;
    if (mTriggerMode==onGettingTrue && doTrigger) {
      if (newState!=yes) {
        doTrigger = false; // do not trigger on getting false
        mMetAt = Never; // also reset holdoff
      }
    }
  }
  // update state
  if (mOneShotEvent) {
    // oneshot triggers do not toggle status, but must return to undefined
    mCurrentState = p44::undefined;
  }
  else {
    // Not oneshot: update state
    mCurrentState = newState;
    // check holdoff
    if (mHoldOff>0 && (aEvalMode&initial)==0) { // holdoff is only active for non-initial runs
      MLMicroSeconds now = MainLoop::now();
      // we have a hold-off
      if (doTrigger) {
        // trigger would fire now, but may not do so now -> (re)start hold-off period
        doTrigger = false; // can't trigger now
        mMetAt = now+mHoldOff;
        OLOG(LOG_INFO, "triggering conditions met, but must await holdoff period of %.2f seconds", (double)mHoldOff/Second);
        updateNextEval(mMetAt);
      }
      else if (mMetAt!=Never) {
        // not changed, but waiting for holdoff
        if (now>=mMetAt) {
          OLOG(LOG_INFO, "trigger condition has been stable for holdoff period of %.2f seconds -> fire now", (double)mHoldOff/Second);
          doTrigger = true;
          mMetAt = Never;
        }
        else {
          // not yet, silently re-schedule
          updateNextEval(mMetAt);
        }
      }
    }
  }
  mCurrentResult = aResult;
  // take unfreeze time of frozen results into account for next evaluation
  FrozenResultsMap::iterator fpos = mFrozenResults.begin();
  MLMicroSeconds now = MainLoop::now();
  while (fpos!=mFrozenResults.end()) {
    if (fpos->second.frozenUntil==Never) {
      // already detected expired -> erase
      // Note: delete only DETECTED ones, just expired ones in terms of now() MUST wait until checked in next evaluation!
      #if P44_CPP11_FEATURE
      fpos = mFrozenResults.erase(fpos);
      #else
      FrozenResultsMap::iterator dpos = fpos++;
      frozenResults.erase(dpos);
      #endif
      continue;
    }
    MLMicroSeconds frozenUntil = fpos->second.frozenUntil;
    if (frozenUntil<now) {
      // unfreeze time is in the past (should not!)
      OLOG(LOG_WARNING, "unfreeze time is in the past -> re-run in 30 sec: %s", cursor.displaycode(70).c_str());
      frozenUntil = now+30*Second; // re-evaluate in 30 seconds to make sure it does not completely stall
    }
    updateNextEval(frozenUntil);
    fpos++;
  }
  // treat static trigger like oneshot (i.e. with no persistent current state)
  if (mNextEvaluation==Never && !hasSources()) {
    // Warn if trigger is unlikely to ever fire (note: still might make sense, e.g. as evaluator reset)
    if ((aEvalMode&initial)!=0) {
      OLOG(LOG_WARNING, "probably trigger will not work as intended (no timers nor events): %s", cursor.displaycode(70).c_str());
    }
    mCurrentState = p44::undefined;
  }
  // schedule next timed evaluation if one is needed
  scheduleNextEval();
  // callback (always, even when initializing)
  if (doTrigger && mTriggerCB) {
    FOCUSLOG("\n---------- FIRING Trigger        : result = %s", ScriptObj::describe(aResult).c_str());
    OLOG(LOG_INFO, "trigger fires with result = %s", ScriptObj::describe(aResult).c_str());
    mTriggerCB(aResult);
  }
}


void CompiledTrigger::scheduleNextEval()
{
  if (mNextEvaluation!=Never) {
    OLOG(LOG_INFO, "Trigger re-evaluation scheduled for %s: '%s'", MainLoop::string_mltime(mNextEvaluation, 3).c_str(), cursor.displaycode(70).c_str());
    mReEvaluationTicket.executeOnceAt(
      boost::bind(&CompiledTrigger::triggerEvaluation, this, timed),
      mNextEvaluation
    );
    mNextEvaluation = Never; // prevent re-triggering without calling updateNextEval()
  }
}


void CompiledTrigger::scheduleEvalNotLaterThan(const MLMicroSeconds aLatestEval)
{
  if (updateNextEval(aLatestEval)) {
    scheduleNextEval();
  }
}


bool CompiledTrigger::updateNextEval(const MLMicroSeconds aLatestEval)
{
  if (aLatestEval==Never || aLatestEval==Infinite) return false; // no next evaluation needed, no need to update
  if (mNextEvaluation==Never || aLatestEval<mNextEvaluation) {
    // new time is more recent than previous, update
    mNextEvaluation = aLatestEval;
    return true;
  }
  return false;
}


bool CompiledTrigger::updateNextEval(const struct tm& aLatestEvalTm)
{
  MLMicroSeconds latestEval = MainLoop::localTimeToMainLoopTime(aLatestEvalTm);
  return updateNextEval(latestEval);
}


CompiledTrigger::FrozenResult* CompiledTrigger::getFrozen(ScriptObjPtr &aResult, SourceCursor::UniquePos aFreezeId)
{
  FrozenResultsMap::iterator frozenVal = mFrozenResults.find(aFreezeId);
  FrozenResult* frozenResultP = NULL;
  if (frozenVal!=mFrozenResults.end()) {
    frozenResultP = &(frozenVal->second);
    // there is a frozen result for this position in the expression
    OLOG(LOG_DEBUG, "- frozen result (%s) for actual result (%s) for freezeId 0x%p exists - will expire %s",
      frozenResultP->frozenResult->stringValue().c_str(),
      aResult->stringValue().c_str(),
      aFreezeId,
      frozenResultP->frozen() ? MainLoop::string_mltime(frozenResultP->frozenUntil, 3).c_str() : "NOW"
    );
    aResult = frozenVal->second.frozenResult;
    if (!frozenResultP->frozen()) frozenVal->second.frozenUntil = Never; // mark expired
  }
  return frozenResultP;
}


bool CompiledTrigger::FrozenResult::frozen()
{
  return frozenUntil==Infinite || (frozenUntil!=Never && frozenUntil>MainLoop::now());
}


CompiledTrigger::FrozenResult* CompiledTrigger::newFreeze(FrozenResult* aExistingFreeze, ScriptObjPtr aNewResult, SourceCursor::UniquePos aFreezeId, MLMicroSeconds aFreezeUntil, bool aUpdate)
{
  if (!aExistingFreeze) {
    // nothing frozen yet, freeze it now
    FrozenResult newFreeze;
    newFreeze.frozenResult = aNewResult;
    newFreeze.frozenUntil = aFreezeUntil;
    mFrozenResults[aFreezeId] = newFreeze;
    OLOG(LOG_DEBUG, "- new result (%s) frozen for freezeId 0x%p until %s",
      aNewResult->stringValue().c_str(),
      aFreezeId,
      MainLoop::string_mltime(newFreeze.frozenUntil, 3).c_str()
    );
    return &mFrozenResults[aFreezeId];
  }
  else if (!aExistingFreeze->frozen() || aUpdate || aFreezeUntil==Never) {
    OLOG(LOG_DEBUG, "- existing freeze updated to value %s and to expire %s",
      aNewResult->stringValue().c_str(),
      aFreezeUntil==Never ? "IMMEDIATELY" : MainLoop::string_mltime(aFreezeUntil, 3).c_str()
    );
    aExistingFreeze->frozenResult = aNewResult;
    aExistingFreeze->frozenUntil = aFreezeUntil;
  }
  else {
    OLOG(LOG_DEBUG, "- no freeze created/updated");
  }
  return aExistingFreeze;
}


bool CompiledTrigger::unfreeze(SourceCursor::UniquePos aFreezeId)
{
  FrozenResultsMap::iterator frozenVal = mFrozenResults.find(aFreezeId);
  if (frozenVal!=mFrozenResults.end()) {
    mFrozenResults.erase(frozenVal);
    return true;
  }
  return false;
}



// MARK: - CompiledHandler

void CompiledHandler::installAndInitializeTrigger(ScriptObjPtr aTrigger)
{
  trigger = dynamic_pointer_cast<CompiledTrigger>(aTrigger);
  // link trigger with my handler action
  if (trigger) {
    trigger->setTriggerCB(boost::bind(&CompiledHandler::triggered, this, _1));
    trigger->setTriggerEvalFlags(expression|synchronously|concurrently); // need to be concurrent because handler might run in same shared context as trigger does
    trigger->initializeTrigger();
  }
}


void CompiledHandler::triggered(ScriptObjPtr aTriggerResult)
{
  // execute the handler script now
  if (mainContext) {
    SPLOG(mainContext->domain(), LOG_INFO, "%s triggered: '%s' with result = %s", name.c_str(), cursor.displaycode(50).c_str(), ScriptObj::describe(aTriggerResult).c_str());
    ExecutionContextPtr ctx = contextForCallingFrom(mainContext->domain(), NULL);
    if (ctx) {
      if (!trigger->mResultVarName.empty()) {
        ctx->setMemberByName(trigger->mResultVarName, aTriggerResult);
      }
      ctx->execute(this, scriptbody|keepvars|concurrently, boost::bind(&CompiledHandler::actionExecuted, this, _1));
      return;
    }
  }
  SPLOG(mainContext->domain(), LOG_ERR, "%s action cannot execute - no context", name.c_str());
}


void CompiledHandler::actionExecuted(ScriptObjPtr aActionResult)
{
  SPLOG(mainContext->domain(), LOG_INFO, "%s executed: result =  %s", name.c_str(), ScriptObj::describe(aActionResult).c_str());
}


// MARK: - ScriptCompiler


static void flagSetter(bool* aFlag) { *aFlag = true; }

ScriptObjPtr ScriptCompiler::compile(SourceContainerPtr aSource, CompiledCodePtr aIntoCodeObj, EvaluationFlags aParsingMode, ScriptMainContextPtr aMainContext)
{
  if (!aSource) return new ErrorValue(ScriptError::Internal, "No source code");
  // set up starting point
  if ((aParsingMode & (sourcecode|checking))==0) {
    // Shortcut for non-checked expression and scriptbody: no need to "compile"
    bodyRef = aSource->getCursor();
  }
  else {
    // could contain declarations, must scan these now
    setCursor(aSource->getCursor());
    aParsingMode = (aParsingMode & ~runModeMask) | scanning | (aParsingMode&checking); // compiling only, with optional checking
    initProcessing(aParsingMode);
    bool completed = false;
    setCompletedCB(boost::bind(&flagSetter,&completed));
    compileForContext = aMainContext; // set for compiling other scriptlets (triggers, handlers) into the same context
    start();
    compileForContext.reset(); // release
    if (!completed) {
      // the compiler must complete synchronously!
      return new ErrorValue(ScriptError::Internal, "Fatal: compiler execution not synchronous!");
    }
    if (result && result->isErr()) {
      return result;
    }
  }
  if (aIntoCodeObj) {
    aIntoCodeObj->setCursor(bodyRef);
  }
  return aIntoCodeObj;
}


void ScriptCompiler::startOfBodyCode()
{
  bodyRef = src; // rest of source code is body
  if ((evaluationFlags&checking)==0) {
    complete(new AnnotatedNullValue("compiled"));
    return;
  }
  // we want a full syntax scan, continue skipping
  resume();
}


void ScriptCompiler::storeFunction()
{
  if (!result->isErr()) {
    // functions are always global
    ErrorPtr err = domain->setMemberByName(result->getIdentifier(), result);
    if (Error::notOK(err)) {
      result = new ErrorPosValue(src, err);
    }
  }
  checkAndResume();
}


void ScriptCompiler::memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError)
{
  // global members are available at compile time
  if (skipping) {
    result.reset();
    resume();
  }
  // non-skipping compilation means evaluating global var initialisation
  result = domain->memberByName(identifier, aMemberAccessFlags);
  if (!result) {
    result = new ErrorPosValue(src, ScriptError::Syntax, "'%s' cannot be accessed in declarations", identifier.c_str());
  }
  checkAndResume();
}



void ScriptCompiler::storeHandler()
{
  if (!result->isErr()) {
    // handlers are always global
    result = domain->registerHandler(result);
  }
  checkAndResume();
}



// MARK: - SourceContainer

SourceContainer::SourceContainer(const char *aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource) :
  originLabel(aOriginLabel),
  loggingContextP(aLoggingContextP),
  source(aSource),
  mFloating(false)
{
}


SourceContainer::SourceContainer(const SourceCursor &aCodeFrom, const SourcePos &aStartPos, const SourcePos &aEndPos) :
  originLabel("copied"),
  loggingContextP(aCodeFrom.source->loggingContextP),
  mFloating(true) // copied source is floating
{
  source.assign(aStartPos.ptr, aEndPos.ptr-aStartPos.ptr);
}


SourceCursor SourceContainer::getCursor()
{
  return SourceCursor(this);
}



// MARK: - ScriptSource

ScriptSource::ScriptSource(EvaluationFlags aDefaultFlags, const char* aOriginLabel, P44LoggingObj* aLoggingContextP) :
  defaultFlags(aDefaultFlags),
  originLabel(aOriginLabel),
  loggingContextP(aLoggingContextP)
{
}

ScriptSource::~ScriptSource()
{
  setSource(""); // force removal of global objects depending on this
}


void ScriptSource::setDomain(ScriptingDomainPtr aDomain)
{
  scriptingDomain = aDomain;
};


ScriptingDomainPtr ScriptSource::domain()
{
  if (!scriptingDomain) {
    // none assigned so far, assign default
    scriptingDomain = ScriptingDomainPtr(&StandardScriptingDomain::sharedDomain());
  }
  return scriptingDomain;
}


void ScriptSource::setSharedMainContext(ScriptMainContextPtr aSharedMainContext)
{
  // cached executable gets invalid when setting new context
  if (sharedMainContext!=aSharedMainContext) {
    if (cachedExecutable) {
      cachedExecutable.reset(); // release cached executable (will release SourceCursor holding our source)
    }
    sharedMainContext = aSharedMainContext; // use this particular context for executing scripts
  }
}



bool ScriptSource::setSource(const string aSource, EvaluationFlags aEvaluationFlags)
{
  if (aEvaluationFlags==inherit || defaultFlags==aEvaluationFlags || aEvaluationFlags==inherit) {
    // same flags, check source
    if (sourceContainer && sourceContainer->source == aSource) {
      return false; // no change at all -> NOP
    }
  }
  // changed, invalidate everything related to the previous code
  if (aEvaluationFlags!=inherit) defaultFlags = aEvaluationFlags;
  if (cachedExecutable) {
    cachedExecutable.reset(); // release cached executable (will release SourceCursor holding our source)
  }
  if (sourceContainer && scriptingDomain) {
    scriptingDomain->releaseObjsFromSource(sourceContainer); // release all global objects from this source
    sourceContainer.reset(); // release it myself
  }
  // create new source container
  if (!aSource.empty()) {
    sourceContainer = SourceContainerPtr(new SourceContainer(originLabel, loggingContextP, aSource));
  }
  return true; // source has changed
}


string ScriptSource::getSource() const
{
  return sourceContainer ? sourceContainer->source : "";
}


bool ScriptSource::empty() const
{
  return sourceContainer ? sourceContainer->source.empty() : true;
}


bool ScriptSource::refersTo(const SourceCursor& aCursor)
{
  return aCursor.refersTo(sourceContainer);
}


ScriptObjPtr ScriptSource::getExecutable()
{
  if (sourceContainer) {
    if (!cachedExecutable) {
      // need to compile
      ScriptCompiler compiler(domain());
      ScriptMainContextPtr mctx = sharedMainContext; // use shared context if one is set
      if (!mctx) {
        // default to independent execution in a non-object context (no instance pointer)
        mctx = domain()->newContext();
      }
      CompiledCodePtr code;
      if (defaultFlags & anonymousfunction) {
        code = new CompiledCode("anonymous");
      }
      else if (defaultFlags & (triggered|timed|initial)) {
        code = new CompiledTrigger("trigger", mctx);
      }
      else {
        code = new CompiledScript("script", mctx);
      }
      cachedExecutable = compiler.compile(sourceContainer, code, defaultFlags, mctx);
    }
    return cachedExecutable;
  }
  return new ErrorValue(ScriptError::Internal, "no source -> no executable");
}


ScriptObjPtr ScriptSource::syntaxcheck()
{
  EvaluationFlags checkFlags = (defaultFlags&~runModeMask)|scanning|checking;
  ScriptCompiler compiler(domain());
  ScriptMainContextPtr mctx = sharedMainContext; // use shared context if one is set
  if (!mctx) {
    // default to independent execution in a non-object context (no instance pointer)
    mctx = domain()->newContext();
  }
  return compiler.compile(sourceContainer, CompiledCodePtr(), checkFlags, mctx);
}


ScriptObjPtr ScriptSource::run(EvaluationFlags aRunFlags, EvaluationCB aEvaluationCB, MLMicroSeconds aMaxRunTime)
{
  EvaluationFlags flags = defaultFlags; // default to compile flags
  if (aRunFlags & runModeMask) {
    // runmode set in run flags -> use it
    flags = (flags&~runModeMask) | (aRunFlags&runModeMask);
  }
  if (aRunFlags & scopeMask) {
    // scope set in run flags -> use it
    flags = (flags&~scopeMask) | (aRunFlags&scopeMask);
  }
  // add in execution modifiers
  flags |= (aRunFlags&execModifierMask);
  ScriptObjPtr code = getExecutable();
  ScriptObjPtr result;
  // get the context to run it
  if (code) {
    if (code->hasType(executable)) {
      ExecutionContextPtr ctx = code->contextForCallingFrom(domain(), NULL);
      if (ctx) {
        if (flags & synchronously) {
          result = ctx->executeSynchronously(code, flags, aMaxRunTime);
        }
        else {
          ctx->execute(code, flags, aEvaluationCB, aMaxRunTime);
          return result; // null, callback will deliver result
        }
      }
      else {
        // cannot evaluate due to missing context
        result = new ErrorValue(ScriptError::Internal, "No context to execute code");
      }
    }
    else {
      result = code;
    }
  }
  if (!code) {
    result = new AnnotatedNullValue("no source code");
  }
  if (aEvaluationCB) aEvaluationCB(result);
  return result;
}


// MARK: - TriggerSource

bool TriggerSource::setTriggerSource(const string aSource, bool aAutoInit)
{
  bool changed = setSource(aSource); // actual run mode is set when run, but a trigger related mode must be set to generate a CompiledTrigger object
  if (changed && aAutoInit) {
    compileAndInit();
  }
  return changed;
}


bool TriggerSource::setTriggerHoldoff(MLMicroSeconds aHoldOffTime, bool aAutoInit)
{
  if (aHoldOffTime!=mHoldOffTime) {
    mHoldOffTime = aHoldOffTime;
    if (aAutoInit) {
      compileAndInit();
    }
    return true;
  }
  return false;
}


ScriptObjPtr TriggerSource::compileAndInit()
{
  CompiledTriggerPtr trigger = dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (!trigger) return  new ErrorValue(ScriptError::Internal, "is not a trigger");
  trigger->setTriggerMode(mTriggerMode, mHoldOffTime);
  trigger->setTriggerCB(mTriggerCB);
  trigger->setTriggerEvalFlags(defaultFlags);
  return trigger->initializeTrigger();
}


bool TriggerSource::evaluate(EvaluationFlags aRunMode)
{
  CompiledTriggerPtr trigger = dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (trigger) {
    if (!trigger->isActive()) {
      compileAndInit();
    }
    else {
      trigger->triggerEvaluation(aRunMode&runModeMask);
    }
    return true;
  }
  return false;
}


void TriggerSource::nextEvaluationNotLaterThan(MLMicroSeconds aLatestEval)
{
  CompiledTriggerPtr trigger = dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (trigger) {
    trigger->scheduleEvalNotLaterThan(aLatestEval);
  }
}



// MARK: - ScriptingDomain

ScriptMainContextPtr ScriptingDomain::newContext(ScriptObjPtr aInstanceObj)
{
  return new ScriptMainContext(this, aInstanceObj);
}


void ScriptingDomain::releaseObjsFromSource(SourceContainerPtr aSource)
{
  // handlers
  HandlerList::iterator pos = handlers.begin();
  while (pos!=handlers.end()) {
    if ((*pos)->originatesFrom(aSource)) {
      pos = handlers.erase(pos); // source is gone -> remove
    }
    else {
      ++pos;
    }
  }
  inherited::releaseObjsFromSource(aSource);
}


void ScriptingDomain::clearFloatingGlobs()
{
  HandlerList::iterator pos = handlers.begin();
  while (pos!=handlers.end()) {
    if ((*pos)->floating()) {
      pos = handlers.erase(pos); // source is gone -> remove
    }
    else {
      ++pos;
    }
  }
  inherited::clearFloatingGlobs();
}



ScriptObjPtr ScriptingDomain::registerHandler(ScriptObjPtr aHandler)
{
  CompiledHandlerPtr handler = dynamic_pointer_cast<CompiledHandler>(aHandler);
  if (!handler) {
    return new ErrorValue(ScriptError::Internal, "is not a handler");
  }
  handlers.push_back(handler);
  return handler;
}


// MARK: - ScriptCodeThread

ScriptCodeThread::ScriptCodeThread(ScriptCodeContextPtr aOwner, CompiledCodePtr aCode, const SourceCursor& aStartCursor) :
  mOwner(aOwner),
  codeObj(aCode),
  maxBlockTime(0),
  maxRunTime(Infinite),
  runningSince(Never)
{
  setCursor(aStartCursor);
  FOCUSLOG("\n%04x START        thread created : %s", (uint32_t)((intptr_t)static_cast<SourceProcessor *>(this)) & 0xFFFF, src.displaycode(130).c_str());
}

ScriptCodeThread::~ScriptCodeThread()
{
  FOCUSLOG("\n%04x END          thread deleted : %s", (uint32_t)((intptr_t)static_cast<SourceProcessor *>(this)) & 0xFFFF, src.displaycode(130).c_str());
}



P44LoggingObj* ScriptCodeThread::loggingContext()
{
  return codeObj && codeObj->loggingContext() ? codeObj->loggingContext() : NULL;
}


int ScriptCodeThread::getLogLevelOffset()
{
  if (logLevelOffset==0) {
    // no own offset - inherit context's
    if (loggingContext()) return loggingContext()->getLogLevelOffset();
    return 0;
  }
  return P44LoggingObj::getLogLevelOffset();
}


string ScriptCodeThread::logContextPrefix()
{
  string prefix;
  if (loggingContext()) {
    prefix = loggingContext()->logContextPrefix();
  }
  return prefix;
}






void ScriptCodeThread::prepareRun(
  EvaluationCB aTerminationCB,
  EvaluationFlags aEvalFlags,
  MLMicroSeconds aMaxBlockTime,
  MLMicroSeconds aMaxRunTime
)
{
  setCompletedCB(aTerminationCB);
  initProcessing(aEvalFlags);
  maxBlockTime = aMaxBlockTime;
  maxRunTime = aMaxRunTime;
}


void ScriptCodeThread::run()
{
  runningSince = MainLoop::now();
  OLOG(LOG_DEBUG,
    "starting %04d at (%s:%zu,%zu):  %s",
    threadId(),
    src.originLabel(), src.lineno(), src.charpos(),
    src.displaycode(90).c_str()
  );
  start();
}


void ScriptCodeThread::abort(ScriptObjPtr aAbortResult)
{
  // Note: calling abort must execute the callback passed to this thread when starting it
  inherited::abort(aAbortResult); // set the result
  if (childContext) {
    // having a child context means that a function (built-in or scripted) is executing
    childContext->abort(stopall, aAbortResult); // will call resume() via the callback of the thread we've started the child context for
  }
  else {
    complete(aAbortResult); // complete now, will eventually invoke completion callback
  }
}


ScriptObjPtr ScriptCodeThread::finalResult()
{
  if (currentState==NULL) return result; // exit value of the thread
  return ScriptObjPtr(); // still running
}



void ScriptCodeThread::complete(ScriptObjPtr aFinalResult)
{
  autoResumeTicket.cancel();
  inherited::complete(aFinalResult);
  OLOG(LOG_DEBUG,
    "complete %04d at (%s:%zu,%zu):  %s\n- with result: %s",
    threadId(),
    src.originLabel(), src.lineno(), src.charpos(),
    src.displaycode(90).c_str(),
    ScriptObj::describe(result).c_str()
  );
  sendEvent(result); // send the final result as event to registered EventSinks
  mOwner->threadTerminated(this, evaluationFlags);
}


void ScriptCodeThread::stepLoop()
{
  MLMicroSeconds loopingSince = MainLoop::now();
  do {
    MLMicroSeconds now = MainLoop::now();
    // check for abort
    // Check maximum execution time
    if (maxRunTime!=Infinite && now-runningSince>maxRunTime) {
      // Note: not calling abort as we are WITHIN the call chain
      complete(new ErrorPosValue(src, ScriptError::Timeout, "Aborted because of overall execution limit"));
      return;
    }
    else if (maxBlockTime!=Infinite && now-loopingSince>maxBlockTime) {
      // time expired
      if (evaluationFlags & synchronously) {
        // Note: not calling abort as we are WITHIN the call chain
        complete(new ErrorPosValue(src, ScriptError::Timeout, "Aborted because of synchronous execution limit"));
        return;
      }
      // in an async script, just give mainloop time to do other things for a while (but do not change result)
      autoResumeTicket.executeOnce(boost::bind(&selfKeepingResume, this, ScriptObjPtr()), 2*maxBlockTime);
      return;
    }
    // run next statemachine step
    resumed = false; // start of a new
    step(); // will cause resumed to be set when resume() is called in this call's chain
    // repeat as long as we are already resumed
  } while(resumed && !aborted);
}


void ScriptCodeThread::checkAndResume()
{
  ErrorValuePtr e = dynamic_pointer_cast<ErrorValue>(result);
  if (e) {
    if (!e->wasThrown()) {
      // need to throw, adding pos if not yet included
      OLOG(LOG_DEBUG, "   error at: %s\nwith result: %s", src.displaycode(90).c_str(), ScriptObj::describe(e).c_str());
      throwOrComplete(e);
      return;
    }
    // already thrown (and equipped with pos), just propagate as result
    result = e;
  }
  resume();
}


void ScriptCodeThread::memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError)
{
  if (result) {
    // look up member of the result itself
    result = result->memberByName(identifier, aMemberAccessFlags);
  }
  else {
    // context level
    result = mOwner->memberByName(identifier, aMemberAccessFlags);
    if (!result) {
      // on context level, if nothing else was found, check overrideable convenience constants
      static const char * const weekdayNames[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
      if (identifier.size()==3) {
        // Optimisation, all weekdays have 3 chars
        for (int w=0; w<7; w++) {
          if (uequals(identifier, weekdayNames[w])) {
            result = new NumericValue(w);
            break;
          }
        }
      }
    }
  }
  if (!result && !aNoNotFoundError) {
    // not having a result here (not even a not-yet-created lvalue) means the member
    // does not exist and cannot/must not be created
    result = new ErrorPosValue(src, ScriptError::NotFound , "'%s' unknown here", identifier.c_str());
  }
  resume();
}


void ScriptCodeThread::memberByIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  if (result) {
    // look up member of the result itself
    result = result->memberAtIndex(aIndex, aMemberAccessFlags);
  }
  if (!result) {
    // not having a result here (not even a not-yet-created lvalue) means the member
    // does not exist and cannot/must not be created
    result = new ErrorPosValue(src, ScriptError::NotFound , "array element %d unknown here", aIndex);
  }
  // no indexed members at the context level!
  resume();
}


void ScriptCodeThread::newFunctionCallContext()
{
  if (result) {
    funcCallContext = result->contextForCallingFrom(mOwner->scriptmain(), this);
  }
  if (!funcCallContext) {
    result = new ErrorPosValue(src, ScriptError::NotCallable, "not a function");
  }
  checkAndResume();
}


void ScriptCodeThread::startBlockThreadAndStoreInIdentifier()
{
  ScriptCodeThreadPtr thread = mOwner->newThreadFrom(codeObj, src, concurrently|block, NULL);
  if (thread) {
    if (!identifier.empty()) {
      push(currentState); // skipping==true is pushed (as we're already skipping the concurrent block in the main thread)
      skipping = false; // ...but we need it off to store the thread var
      result = new ThreadValue(thread);
      push(&SourceProcessor::s_assignOlder);
      thread->run();
      result.reset();
      setState(&SourceProcessor::s_uncheckedResult);
      memberByIdentifier(lvalue+create+nooverride);
      return;
    }
    else {
      thread->run();
      checkAndResume();
    }
  }
  checkAndResume();
}


/// apply the specified argument to the current result
void ScriptCodeThread::pushFunctionArgument(ScriptObjPtr aArgument)
{
  // apply the specified argument to the current function call context
  if (funcCallContext) {
    ScriptObjPtr errVal = funcCallContext->checkAndSetArgument(aArgument, funcCallContext->numIndexedMembers(), result);
    if (errVal) result = errVal;
  }
  checkAndResume();
}


// evaluate the current result and replace it with the output from the evaluation (e.g. function call)
void ScriptCodeThread::executeResult()
{
  if (funcCallContext && result) {
    // check for missing arguments after those we have
    ScriptObjPtr errVal = funcCallContext->checkAndSetArgument(ScriptObjPtr(), funcCallContext->numIndexedMembers(), result);
    if (errVal) {
      result = errVal;
      checkAndResume();
    }
    else {
      childContext = funcCallContext; // as long as this executes, the function context becomes the child context of this thread
      // NO LONGER: execute function as main thread, which means all subthreads it might spawn will be killed when function itself completes
      // Note: must have keepvars because these are the arguments!
      funcCallContext->execute(result, evaluationFlags|keepvars/*|mainthread*/, boost::bind(&ScriptCodeThread::executedResult, this, _1));
    }
    // function call completion will call resume
    return;
  }
  result = new ErrorPosValue(src, ScriptError::Internal, "cannot execute object");
  checkAndResume();
}


void ScriptCodeThread::executedResult(ScriptObjPtr aResult)
{
  if (!aResult) {
    aResult = new AnnotatedNullValue("no return value");
  }
  childContext.reset(); // release the child context
  resume(aResult);
}


void ScriptCodeThread::memberEventCheck()
{
  // check for event sources in member
  if (!skipping && (evaluationFlags&initial)) {
    // initial run of trigger -> register trigger itself as event sink
    EventSource* eventSource = result->eventSource();
    if (eventSource) {
      // register the code object (the trigger) as event sink with the source
      EventSink* triggerEventSink = dynamic_cast<EventSink*>(codeObj.get());
      if (triggerEventSink) {
        eventSource->registerForEvents(triggerEventSink);
      }
    }
  }
}



// MARK: - Built-in Standard functions

namespace BuiltinFunctions {

// TODO: change all function implementations in other files
// Here's a BBEdit find & replace sequence to prepare:

// ===== FIND:
//  (else *)?if \(aFunc=="([^"]*)".*\n */?/? ?(.*)
// ===== REPLACE:
//  //#DESC  { "\2", any, \2_args, \&\2_func },
//  // \3
//  static const ArgumentDescriptor \2_args[] = { { any } };
//  static void \2_func(BuiltinFunctionContextPtr f)
//  {


// ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
static const BuiltInArgDesc ifvalid_args[] = { { any|error|null }, { any|error|null } };
static const size_t ifvalid_numargs = sizeof(ifvalid_args)/sizeof(BuiltInArgDesc);
static void ifvalid_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->arg(0)->hasType(value) ? f->arg(0) : f->arg(1));
}

// isvalid(a)      if a is a valid value, return true, otherwise return false
static const BuiltInArgDesc isvalid_args[] = { { any|error|null } };
static const size_t isvalid_numargs = sizeof(isvalid_args)/sizeof(BuiltInArgDesc);
static void isvalid_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->hasType(value)));
}


// if (c, a, b)    if c evaluates to true, return a, otherwise b
static const BuiltInArgDesc if_args[] = { { value|null }, { any|null }, { any|null } };
static const size_t if_numargs = sizeof(if_args)/sizeof(BuiltInArgDesc);
static void if_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->arg(0)->boolValue() ? f->arg(1) : f->arg(2));
}

// abs (a)         absolute value of a
static const BuiltInArgDesc abs_args[] = { { scalar|undefres } };
static const size_t abs_numargs = sizeof(abs_args)/sizeof(BuiltInArgDesc);
static void abs_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(fabs(f->arg(0)->doubleValue())));
}


// int (a)         integer value of a
static const BuiltInArgDesc int_args[] = { { scalar|undefres } };
static const size_t int_numargs = sizeof(int_args)/sizeof(BuiltInArgDesc);
static void int_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(int(f->arg(0)->int64Value())));
}


// frac (a)         fractional value of a
static const BuiltInArgDesc frac_args[] = { { scalar|undefres } };
static const size_t frac_numargs = sizeof(frac_args)/sizeof(BuiltInArgDesc);
static void frac_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->doubleValue()-f->arg(0)->int64Value())); // result retains sign
}


// round (a)       round value to integer
// round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
static const BuiltInArgDesc round_args[] = { { scalar|undefres }, { numeric|optionalarg } };
static const size_t round_numargs = sizeof(round_args)/sizeof(BuiltInArgDesc);
static void round_func(BuiltinFunctionContextPtr f)
{
  double precision = 1;
  if (f->arg(1)->defined()) {
    precision = f->arg(1)->doubleValue();
  }
  f->finish(new NumericValue(round(f->arg(0)->doubleValue()/precision)*precision));
}


// random (a,b)     random value from a up to and including b
static const BuiltInArgDesc random_args[] = { { numeric }, { numeric } };
static const size_t random_numargs = sizeof(random_args)/sizeof(BuiltInArgDesc);
static void random_func(BuiltinFunctionContextPtr f)
{
  // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
  f->finish(new NumericValue(f->arg(0)->doubleValue() + (double)rand()*(f->arg(1)->doubleValue()-f->arg(0)->doubleValue())/((double)RAND_MAX)));
}


// min (a, b)    return the smaller value of a and b
static const BuiltInArgDesc min_args[] = { { scalar|undefres }, { value|undefres } };
static const size_t min_numargs = sizeof(min_args)/sizeof(BuiltInArgDesc);
static void min_func(BuiltinFunctionContextPtr f)
{
  if (f->argval(0)<f->argval(1)) f->finish(f->arg(0));
  else f->finish(f->arg(1));
}


// max (a, b)    return the bigger value of a and b
static const BuiltInArgDesc max_args[] = { { scalar|undefres }, { value|undefres } };
static const size_t max_numargs = sizeof(max_args)/sizeof(BuiltInArgDesc);
static void max_func(BuiltinFunctionContextPtr f)
{
  if (f->argval(0)>f->argval(1)) f->finish(f->arg(0));
  else f->finish(f->arg(1));
}


// limited (x, a, b)    return min(max(x,a),b), i.e. x limited to values between and including a and b
static const BuiltInArgDesc limited_args[] = { { scalar|undefres }, { numeric }, { numeric } };
static const size_t limited_numargs = sizeof(limited_args)/sizeof(BuiltInArgDesc);
static void limited_func(BuiltinFunctionContextPtr f)
{
  ScriptObj &a = f->argval(0);
  if (a<f->argval(1)) f->finish(f->arg(1));
  else if (a>f->argval(2)) f->finish(f->arg(2));
  else f->finish(f->arg(0));
}


// cyclic (x, a, b)    return x with wraparound into range a..b (not including b because it means the same thing as a)
static const BuiltInArgDesc cyclic_args[] = { { scalar|undefres }, { numeric }, { numeric } };
static const size_t cyclic_numargs = sizeof(cyclic_args)/sizeof(BuiltInArgDesc);
static void cyclic_func(BuiltinFunctionContextPtr f)
{
  double o = f->arg(1)->doubleValue();
  double x0 = f->arg(0)->doubleValue()-o; // make null based
  double r = f->arg(2)->doubleValue()-o; // wrap range
  if (x0>=r) x0 -= int(x0/r)*r;
  else if (x0<0) x0 += (int(-x0/r)+1)*r;
  f->finish(new NumericValue(x0+o));
}


// string(anything)
static const BuiltInArgDesc string_args[] = { { any|error|null } };
static const size_t string_numargs = sizeof(string_args)/sizeof(BuiltInArgDesc);
static void string_func(BuiltinFunctionContextPtr f)
{
  if (f->arg(0)->undefined())
    f->finish(new StringValue("undefined")); // make it visible
  else
    f->finish(new StringValue(f->arg(0)->stringValue())); // force convert to string, including nulls and errors
}


// describe(anything)
static const BuiltInArgDesc describe_args[] = { { any|error|null } };
static const size_t describe_numargs = sizeof(string_args)/sizeof(BuiltInArgDesc);
static void describe_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(ScriptObj::describe(f->arg(0))));
}



// number(anything)
static const BuiltInArgDesc number_args[] = { { any|error|null } };
static const size_t number_numargs = sizeof(number_args)/sizeof(BuiltInArgDesc);
static void number_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->doubleValue())); // force convert to numeric
}

#if SCRIPTING_JSON_SUPPORT

// json(string)     parse json from string
static const BuiltInArgDesc json_args[] = { { text }, { numeric|optionalarg } };
static const size_t json_numargs = sizeof(json_args)/sizeof(BuiltInArgDesc);
static void json_func(BuiltinFunctionContextPtr f)
{
  string jstr = f->arg(0)->stringValue();
  ErrorPtr err;
  JsonObjectPtr j = JsonObject::objFromText(jstr.c_str(), jstr.size(), &err, f->arg(1)->boolValue());
  if (Error::isOK(err))
    f->finish(new JsonValue(j));
  else
    f->finish(new ErrorValue(err));
}


#if ENABLE_JSON_APPLICATION

static const BuiltInArgDesc jsonresource_args[] = { { text+undefres } };
static const size_t jsonresource_numargs = sizeof(jsonresource_args)/sizeof(BuiltInArgDesc);
static void jsonresource_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err;
  JsonObjectPtr j = Application::jsonResource(f->arg(0)->stringValue(), &err);
  if (Error::isOK(err))
    f->finish(new JsonValue(j));
  else
    f->finish(new ErrorValue(err));
}
#endif // ENABLE_JSON_APPLICATION
#endif // SCRIPTING_JSON_SUPPORT


// lastarg(expr, expr, exprlast)
static const BuiltInArgDesc lastarg_args[] = { { any|null|multiple, "side-effect" } };
static const size_t lastarg_numargs = sizeof(lastarg_args)/sizeof(BuiltInArgDesc);
static void lastarg_func(BuiltinFunctionContextPtr f)
{
  // (for executing side effects of non-last arg evaluation, before returning the last arg)
  if (f->numArgs()==0) f->finish(); // no arguments -> null
  else f->finish(f->arg(f->numArgs()-1)); // value of last argument
}


// strlen(string)
static const BuiltInArgDesc strlen_args[] = { { text|undefres } };
static const size_t strlen_numargs = sizeof(strlen_args)/sizeof(BuiltInArgDesc);
static void strlen_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((double)f->arg(0)->stringValue().size())); // length of string
}

// elements(array)
static const BuiltInArgDesc elements_args[] = { { any|undefres } };
static const size_t elements_numargs = sizeof(elements_args)/sizeof(BuiltInArgDesc);
static void elements_func(BuiltinFunctionContextPtr f)
{
  if (f->arg(0)->hasType(json)) {
    f->finish(new NumericValue(f->arg(0)->jsonValue()->arrayLength()));
    return;
  }
  f->finish(new AnnotatedNullValue("not an array"));
}


// substr(string, from)
// substr(string, from, count)
// substr(string, -fromend, count)
static const BuiltInArgDesc substr_args[] = { { text|undefres }, { numeric }, { numeric|optionalarg } };
static const size_t substr_numargs = sizeof(substr_args)/sizeof(BuiltInArgDesc);
static void substr_func(BuiltinFunctionContextPtr f)
{
  string s = f->arg(0)->stringValue();
  ssize_t start = f->arg(1)->intValue();
  if (start<0) start = s.size()+start;
  if (start>s.size()) start = s.size();
  size_t count = string::npos; // to the end
  if (f->arg(2)->defined()) {
    count = f->arg(2)->intValue();
  }
  f->finish(new StringValue(s.substr(start, count)));
}


// find(haystack, needle)
// find(haystack, needle, from)
static const BuiltInArgDesc find_args[] = { { text|undefres }, { text }, { numeric|optionalarg }  };
static const size_t find_numargs = sizeof(find_args)/sizeof(BuiltInArgDesc);
static void find_func(BuiltinFunctionContextPtr f)
{
  string haystack = f->arg(0)->stringValue(); // haystack can be anything, including invalid
  string needle = f->arg(1)->stringValue();
  size_t start = 0;
  if (f->arg(2)->defined()) {
    start = f->arg(2)->intValue();
    if (start>haystack.size()) start = haystack.size();
  }
  size_t p = haystack.find(needle, start);
  if (p!=string::npos)
    f->finish(new NumericValue((double)p));
  else
    f->finish(new AnnotatedNullValue("no such substring")); // not found
}


// format(formatstring, value [, value...])
// only % + - 0..9 . d, x, and f supported
static const BuiltInArgDesc format_args[] = { { text }, { any|null|error|multiple } };
static const size_t format_numargs = sizeof(format_args)/sizeof(BuiltInArgDesc);
static void format_func(BuiltinFunctionContextPtr f)
{
  string fmt = f->arg(0)->stringValue();
  string res;
  const char* p = fmt.c_str();
  size_t ai = 1;
  while (*p) {
    const char *e = strchr(p, '%');
    if (!e) e = fmt.c_str()+fmt.size();
    if (e>p) res.append(p, e-p);
    p=e;
    if (*p) {
      // decode format
      p++;
      if (*p=='%') {
        res += *p++; // copy single % to output
      }
      else {
        // real format
        e=p;
        char c = *e++;
        while (c && (isdigit(c)||(c=='.')||(c=='+')||(c=='-'))) c = *e++; // skip field length specs
        if (f->arg(ai)->undefined()) {
          // whatever undefined value, show annotation
          string_format_append(res, "<%s>", f->arg(ai++)->getAnnotation().c_str());
        }
        else if (c=='d'||c=='u'||c=='x'||c=='X') {
          // integer formatting
          string nfmt(p-1,e-p);
          nfmt.append("ll"); nfmt.append(1, c); // make it a longlong in all cases
          string_format_append(res, nfmt.c_str(), f->arg(ai++)->int64Value());
        }
        else if (c=='e'||c=='E'||c=='g'||c=='G'||c=='f') {
          // double formatting
          string nfmt(p-1,e-p+1);
          string_format_append(res, nfmt.c_str(), f->arg(ai++)->doubleValue());
        }
        else if (c=='s') {
          // string formatting
          string nfmt(p-1,e-p+1);
          string_format_append(res, nfmt.c_str(), f->arg(ai++)->stringValue().c_str());
        }
        else {
          f->finish(new ErrorValue(ScriptError::Syntax, "invalid format string, only basic %%duxXeEgGfs specs allowed"));
          return;
        }
        p=e;
      }
    }
  }
  f->finish(new StringValue(res));
}


// formattime([time] [formatstring]])
static const BuiltInArgDesc formattime_args[] = { { numeric|text|optionalarg } , { text|optionalarg } };
static const size_t formattime_numargs = sizeof(formattime_args)/sizeof(BuiltInArgDesc);
static void formattime_func(BuiltinFunctionContextPtr f)
{
  MLMicroSeconds t;
  size_t ai = 0;
  if (f->arg(ai)->hasType(numeric)) {
    t = f->arg(ai)->doubleValue()*Second;
    ai++;
  }
  else {
    t = MainLoop::unixtime();
  }
  struct tm disptim;
  string fmt;
  if (f->numArgs()>ai) {
    fmt = f->arg(ai)->stringValue();
  }
  else if (t>Day) fmt = "%Y-%m-%d %H:%M:%S";
  else fmt = "%H:%M:%S";
  MainLoop::getLocalTime(disptim, NULL, t, t<Day);
  f->finish(new StringValue(string_ftime(fmt.c_str(), &disptim)));
}


// throw(value)       - throw a expression user error with the string value of value as errormessage
static const BuiltInArgDesc throw_args[] = { { any|error } };
static const size_t throw_numargs = sizeof(throw_args)/sizeof(BuiltInArgDesc);
static void throw_func(BuiltinFunctionContextPtr f)
{
  // throw(errvalue)    - (re-)throw with the error of the value passed
  ScriptObjPtr throwVal;
  if (f->arg(0)->isErr())
    throwVal = f->arg(0);
  else
    throwVal = new ErrorValue(ScriptError::User, "%s", f->arg(0)->stringValue().c_str());
  f->finish(throwVal);
}


// error(value)       - create a user error value with the string value of value as errormessage, in all cases, even if value is already an error
static const BuiltInArgDesc error_args[] = { { any|error|null } };
static const size_t error_numargs = sizeof(error_args)/sizeof(BuiltInArgDesc);
static void error_func(BuiltinFunctionContextPtr f)
{
  ErrorValuePtr e = new ErrorValue(Error::err<ScriptError>(ScriptError::User, "%s", f->arg(0)->stringValue().c_str()));
  e->setThrown(true); // mark it caught already, so it can be passed as a regular value without throwing
  f->finish(e);
}


// errordomain(errvalue)
static const BuiltInArgDesc errordomain_args[] = { { error|undefres } };
static const size_t errordomain_numargs = sizeof(errordomain_args)/sizeof(BuiltInArgDesc);
static void errordomain_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("not error")); // no error, no domain
  f->finish(new StringValue(err->getErrorDomain()));
}


// errorcode(errvalue)
static const BuiltInArgDesc errorcode_args[] = { { error|undefres } };
static const size_t errorcode_numargs = sizeof(errorcode_args)/sizeof(BuiltInArgDesc);
static void errorcode_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("not error")); // no error, no code
  f->finish(new NumericValue((double)err->getErrorCode()));
}


// errormessage(value)
static const BuiltInArgDesc errormessage_args[] = { { error|undefres } };
static const size_t errormessage_numargs = sizeof(errormessage_args)/sizeof(BuiltInArgDesc);
static void errormessage_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("not error")); // no error, no message
  f->finish(new StringValue(err->getErrorMessage()));
}


// eval(string, [args...])    have string executed as script code, with access to optional args as arg0, arg1, argN...
static const BuiltInArgDesc eval_args[] = { { text|executable }, { any|null|error|multiple } };
static const size_t eval_numargs = sizeof(eval_args)/sizeof(BuiltInArgDesc);
static void eval_func(BuiltinFunctionContextPtr f)
{
  ScriptObjPtr evalcode;
  if (f->arg(0)->hasType(executable)) {
    evalcode = f->arg(0);
  }
  else {
    // need to compile string first
    ScriptSource src(
      scriptbody|anonymousfunction,
      "eval function",
      f->instance() ? f->instance()->loggingContext() : NULL
    );
    src.setDomain(f->domain());
    src.setSource(f->arg(0)->stringValue());
    evalcode = src.getExecutable();
  }
  if (evalcode->hasType(executable)) {
    // get the context to run it as an anonymous function from the current main context
    ExecutionContextPtr ctx = evalcode->contextForCallingFrom(f->scriptmain(), f->thread());
    if (ctx) {
      // pass args, if any
      for (size_t i = 1; i<f->numArgs(); i++) {
        ctx->setMemberAtIndex(i-1, f->arg(i), string_format("arg%zu", i));
      }
      // evaluate, end all threads when main thread ends
      // Note: must have keepvars because these are the arguments!
      ctx->execute(evalcode, scriptbody|mainthread|keepvars, boost::bind(&BuiltinFunctionContext::finish, f, _1));
      return;
    }
  }
  f->finish(evalcode); // return object itself, is an error or cannot be executed
}


// await(event [, event...] [,timeout])    wait for an event (or one of serveral)
class AwaitEventSink : public EventSink
{
  BuiltinFunctionContextPtr f;
public:
  MLTicket timeoutTicket;
  AwaitEventSink(BuiltinFunctionContextPtr aF) : f(aF) {};
  virtual void processEvent(ScriptObjPtr aEvent, EventSource &aSource) P44_OVERRIDE
  {
    // unwind stack before actually responding (to avoid changing containers this event originates from)
    MainLoop::currentMainLoop().executeNow(boost::bind(&AwaitEventSink::finishWait, this, aEvent));
  }
  void finishWait(ScriptObjPtr aEvent)
  {
    f->finish(aEvent);
    f->setAbortCallback(NULL);
    delete this;
  }
  void timeout()
  {
    f->finish(new AnnotatedNullValue("await timeout"));
    f->setAbortCallback(NULL);
    delete this;
  }
};

static void await_abort(AwaitEventSink* aAwaitEventSink)
{
  delete aAwaitEventSink;
}

static const BuiltInArgDesc await_args[] = { { any|null }, { any|null|optionalarg|multiple } };
static const size_t await_numargs = sizeof(await_args)/sizeof(BuiltInArgDesc);
static void await_func(BuiltinFunctionContextPtr f)
{
  int ai=0;
  AwaitEventSink* awaitEventSink = new AwaitEventSink(f); // temporary object that will receive one of the events or the timeout
  MLMicroSeconds to = Infinite;
  do {
    ScriptObjPtr cv = f->arg(ai)->calculationValue(); // e.g. threadVars detect stopped thread only when being asked for calculation value first
    EventSource* ev = f->arg(ai)->eventSource(); // ...but ask original value for event source (calculation value is not an event itself)
    if (!ev) {
      // must be last arg and numeric to be timeout, otherwise error
      if (ai==f->numArgs()-1 && f->arg(ai)->hasType(numeric)) {
        to = f->arg(ai)->doubleValue()*Second;
        break;
      }
      // not an event source -> just immediately return the value itself
      delete awaitEventSink;
      f->finish(f->arg(ai));
      return;
    }
    ev->registerForEvents(awaitEventSink); // register each of the event sources for getting events
    ai++;
  } while(ai<f->numArgs());
  if (to!=Infinite) {
    awaitEventSink->timeoutTicket.executeOnce(boost::bind(&AwaitEventSink::timeout, awaitEventSink), to);
  }
  f->setAbortCallback(boost::bind(&await_abort, awaitEventSink));
  return;
}


// abort(thread)    abort specified thread
// abort()          abort all subthreads
static const BuiltInArgDesc abort_args[] = { { threadref|exacttype|optionalarg } };
static const size_t abort_numargs = sizeof(abort_args)/sizeof(BuiltInArgDesc);
static void abort_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==1) {
    // single thread represented by arg0
    ThreadValue *t = dynamic_cast<ThreadValue *>(f->arg(0).get());
    if (t && t->running()) {
      // still running
      t->abort();
    }
  }
  else {
    // all subthreads
    f->thread()->abortOthers(stopall);
  }
  f->finish();
}


// undeclare()    undeclare functions and handlers - only works in embeddedGlobs threads
static void undeclare_func(BuiltinFunctionContextPtr f)
{
  if ((f->evalFlags() & floatingGlobs)==0) {
    f->finish(new ErrorValue(ScriptError::Invalid, "undeclare() can only be used in interactive sessions"));
    return;
  }
  f->thread()->owner()->domain()->clearFloatingGlobs();
  f->finish();
}



// log (logmessage)
// log (loglevel, logmessage)
static const BuiltInArgDesc log_args[] = { { value }, { value|optionalarg } };
static const size_t log_numargs = sizeof(log_args)/sizeof(BuiltInArgDesc);
static void log_func(BuiltinFunctionContextPtr f)
{
  int loglevel = LOG_NOTICE;
  size_t ai = 0;
  if (f->numArgs()>1) {
    loglevel = f->arg(ai)->intValue();
    ai++;
  }
  LOG(loglevel, "Script log: %s", f->arg(ai)->stringValue().c_str());
  f->finish(f->arg(ai)); // also return the message logged
}


// loglevel()
// loglevel(newlevel)
static const BuiltInArgDesc loglevel_args[] = { { numeric|optionalarg } };
static const size_t loglevel_numargs = sizeof(loglevel_args)/sizeof(BuiltInArgDesc);
static void loglevel_func(BuiltinFunctionContextPtr f)
{
  int oldLevel = LOGLEVEL;
  if (f->numArgs()>0) {
    int newLevel = f->arg(0)->intValue();
    if (newLevel>=0 && newLevel<=7) {
      SETLOGLEVEL(newLevel);
      LOG(newLevel, "\n\n========== script changed log level from %d to %d ===============", oldLevel, newLevel);
    }
  }
  f->finish(new NumericValue(oldLevel));
}


// logleveloffset()
// logleveloffset(newoffset)
static const BuiltInArgDesc logleveloffset_args[] = { { numeric|optionalarg } };
static const size_t logleveloffset_numargs = sizeof(logleveloffset_args)/sizeof(BuiltInArgDesc);
static void logleveloffset_func(BuiltinFunctionContextPtr f)
{
  int oldOffset = f->getLogLevelOffset();
  if (f->numArgs()>0) {
    int newOffset = f->arg(0)->intValue();
    f->setLogLevelOffset(newOffset);
  }
  f->finish(new NumericValue(oldOffset));
}


// is_weekday(w,w,w,...)
static const BuiltInArgDesc is_weekday_args[] = { { numeric|multiple } };
static const size_t is_weekday_numargs = sizeof(is_weekday_args)/sizeof(BuiltInArgDesc);
static void is_weekday_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; MainLoop::getLocalTime(loctim);
  // check if any of the weekdays match
  int weekday = loctim.tm_wday; // 0..6, 0=sunday
  SourceCursor::UniquePos freezeId = f->argId(0); // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
  bool isday = false;
  for (int i = 0; i<f->numArgs(); i++) {
    int w = (int)f->arg(i)->doubleValue();
    if (w==7) w=0; // treat both 0 and 7 as sunday
    if (w==weekday) {
      // today is one of the days listed
      isday = true;
      break;
    }
  }
  ScriptObjPtr newRes = new NumericValue(isday);
  // freeze until next check: next day 0:00:00
  loctim.tm_mday++;
  loctim.tm_hour = 0;
  loctim.tm_min = 0;
  loctim.tm_sec = 0;
  ScriptObjPtr res = newRes;
  if (CompiledTrigger* trigger = f->trigger()) {
    CompiledTrigger::FrozenResult* frozenP = trigger->getFrozen(res, freezeId);
    trigger->newFreeze(frozenP, newRes, freezeId, MainLoop::localTimeToMainLoopTime(loctim));
  }
  f->finish(res); // freeze time over, use actual, newly calculated result
}


#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function

// common implementation for after_time() and is_time()
static void timeCheckFunc(bool aIsTime, BuiltinFunctionContextPtr f)
{
  struct tm loctim; MainLoop::getLocalTime(loctim);
  int newSecs;
  SourceCursor::UniquePos freezeId = f->argId(0);
  if (f->numArgs()==2) {
    // TODO: get rid of legacy syntax later
    // legacy time spec in hours and minutes
    newSecs = (f->arg(0)->intValue() * 60 + f->arg(1)->intValue()) * 60;
  }
  else {
    // specification in seconds, usually using time literal
    newSecs = f->arg(0)->intValue();
  }
  ScriptObjPtr secs = new NumericValue(newSecs);
  int daySecs = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec;
  CompiledTrigger* trigger = f->trigger();
  CompiledTrigger::FrozenResult* frozenP = NULL;
  if (trigger) frozenP = trigger->getFrozen(secs, freezeId);
  bool met = daySecs>=secs->intValue();
  // next check at specified time, today if not yet met, tomorrow if already met for today
  loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = secs->intValue();
  FOCUSLOG("is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim), 3).c_str());
  bool res = met;
  // limit to a few secs around target if it's "is_time"
  if (aIsTime && met && daySecs<secs->intValue()+IS_TIME_TOLERANCE_SECONDS) {
    // freeze again for a bit
    if (trigger) trigger->newFreeze(frozenP, secs, freezeId, MainLoop::localTimeToMainLoopTime(loctim)+IS_TIME_TOLERANCE_SECONDS*Second);
  }
  else {
    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = newSecs;
    if (met) {
      loctim.tm_mday++; // already met today, check again at midnight tomorrow (to make sure result gets false before it gets true again!)
      loctim.tm_sec = 0; // midnight
      if (aIsTime) res = false;
    }
    if (trigger) trigger->newFreeze(frozenP, new NumericValue(newSecs), freezeId, MainLoop::localTimeToMainLoopTime(loctim));
  }
  f->finish(new NumericValue(res));
}


// after_time(time)
static const BuiltInArgDesc after_time_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t after_time_numargs = sizeof(after_time_args)/sizeof(BuiltInArgDesc);
static void after_time_func(BuiltinFunctionContextPtr f)
{
  timeCheckFunc(false, f);
}

// is_time(time)
static const BuiltInArgDesc is_time_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t is_time_numargs = sizeof(is_time_args)/sizeof(BuiltInArgDesc);
static void is_time_func(BuiltinFunctionContextPtr f)
{
  timeCheckFunc(true, f);
}

#define MIN_RETRIGGER_SECONDS 10 ///< how soon testlater() is allowed to re-trigger


// initial()  returns true if this is a "initial" run of a trigger, meaning after startup or expression changes
static void initial_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((f->evalFlags()&initial)!=0));
}

// testlater(seconds, timedtest [, retrigger])
// return "invalid" now, re-evaluate after given seconds and return value of test then.
// If retrigger is true then, the timer will be re-scheduled
static const BuiltInArgDesc testlater_args[] = { { numeric }, { numeric }, { numeric|optionalarg } };
static const size_t testlater_numargs = sizeof(testlater_args)/sizeof(BuiltInArgDesc);
static void testlater_func(BuiltinFunctionContextPtr f)
{
  CompiledTrigger* trigger = f->trigger();
  if (!trigger) {
    f->finish(new ErrorValue(ScriptError::Invalid, "testlater() can only be used in triggers"));
    return;
  }
  bool retrigger = f->arg(2)->boolValue();
  double s = f->arg(0)->doubleValue();
  if (retrigger && s<MIN_RETRIGGER_SECONDS) {
    // prevent too frequent re-triggering that could eat up too much cpu
    LOG(LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", s, (double)MIN_RETRIGGER_SECONDS);
    s = MIN_RETRIGGER_SECONDS;
  }
  ScriptObjPtr secs = new NumericValue(s);
  ScriptObjPtr currentSecs = secs;
  SourceCursor::UniquePos  freezeId = f->argId(0);
  CompiledTrigger::FrozenResult* frozenP = trigger->getFrozen(currentSecs, freezeId);
  bool evalNow = frozenP && !frozenP->frozen();
  if ((f->evalFlags()&timed)==0) {
    if ((f->evalFlags()&initial)==0 || retrigger) {
      // evaluating non-timed, non-initial (or retriggering) means "not yet ready" and must start or extend freeze period
      trigger->newFreeze(frozenP, secs, freezeId, MainLoop::now()+s*Second, true);
    }
    evalNow = false; // never evaluate on non-timed run
  }
  else {
    // evaluating timed after frozen period means "now is later" and if retrigger is set, must start a new freeze
    if (frozenP && retrigger) {
      trigger->newFreeze(frozenP, secs, freezeId, MainLoop::now()+secs->doubleValue()*Second);
    }
  }
  if (evalNow) {
    // evaluation runs because freeze is over, return test result
    f->finish(f->arg(1));
  }
  else {
    // still frozen, return undefined
    f->finish(new AnnotatedNullValue("testlater() not yet ready"));
  }
}

#define MIN_EVERY_SECONDS 0.5 ///< how fast every() can go

// every(interval [, syncoffset])
// returns true once every interval
// Note: first true is returned at first evaluation or, if syncoffset is set,
//   at next integer number of intervals calculated from beginning of the day + syncoffset
static const BuiltInArgDesc every_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t every_numargs = sizeof(every_args)/sizeof(BuiltInArgDesc);
static void every_func(BuiltinFunctionContextPtr f)
{
  CompiledTrigger* trigger = f->trigger();
  if (!trigger) {
    f->finish(new ErrorValue(ScriptError::Invalid, "every() can only be used in triggers"));
    return;
  }
  double syncoffset = -1;
  if (f->numArgs()>=2) {
    syncoffset = f->arg(1)->doubleValue();
  }
  double s = f->arg(0)->doubleValue();
  if (s<MIN_EVERY_SECONDS) {
    // prevent too frequent re-triggering that could eat up too much cpu
    LOG(LOG_WARNING, "every() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", s, (double)MIN_EVERY_SECONDS);
    s = MIN_EVERY_SECONDS;
  }
  ScriptObjPtr secs = new NumericValue(s);
  ScriptObjPtr currentSecs = secs;
  SourceCursor::UniquePos freezeId = f->argId(0);
  CompiledTrigger::FrozenResult* frozenP = trigger->getFrozen(currentSecs, freezeId);
  bool triggered = frozenP && !frozenP->frozen();
  if (triggered || (f->evalFlags()&initial)!=0) {
    // setup new interval
    double interval = s;
    if (syncoffset<0) {
      // no sync
      // - interval starts from now
      trigger->newFreeze(frozenP, secs, freezeId, MainLoop::now()+s*Second, true);
      triggered = true; // fire even in initial evaluation
    }
    else {
      // synchronize with real time
      double fracSecs;
      struct tm loctim; MainLoop::getLocalTime(loctim, &fracSecs);
      double secondOfDay = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs; // second of day right now
      double untilNext = syncoffset+(floor((secondOfDay-syncoffset)/interval)+1)*interval - secondOfDay; // time to next repetition
      trigger->newFreeze(frozenP, secs, freezeId, MainLoop::now()+untilNext*Second, true);
    }
    // also cause a immediate re-evaluation as every() is an instant that immediately goes away
    trigger->updateNextEval(MainLoop::now());
  }
  f->finish(new NumericValue(triggered));
}


static const BuiltInArgDesc between_dates_args[] = { { numeric }, { numeric } };
static const size_t between_dates_numargs = sizeof(between_dates_args)/sizeof(BuiltInArgDesc);
static void between_dates_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; MainLoop::getLocalTime(loctim);
  int smaller = (int)(f->arg(0)->doubleValue());
  int larger = (int)(f->arg(1)->doubleValue());
  int currentYday = loctim.tm_yday;
  loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = 0;
  loctim.tm_mon = 0;
  bool lastBeforeFirst = smaller>larger;
  if (lastBeforeFirst) swap(larger, smaller);
  if (currentYday<smaller) loctim.tm_mday = 1+smaller;
  else if (currentYday<=larger) loctim.tm_mday = 1+larger;
  else { loctim.tm_mday = smaller; loctim.tm_year += 1; } // check one day too early, to make sure no day is skipped in a leap year to non leap year transition
  CompiledTrigger* trigger = f->trigger();
  if (trigger) trigger->updateNextEval(loctim);
  f->finish(new NumericValue((currentYday>=smaller && currentYday<=larger)!=lastBeforeFirst));
}


// helper for geolocation dependent functions, returns annotated NULL when no location is set
static bool checkGeoLocation(BuiltinFunctionContextPtr f)
{
  if (!f->geoLocation()) {
    f->finish(new AnnotatedNullValue("no geolocation information available"));
    return false;
  }
  return true;
}

// sunrise()
static void sunrise_func(BuiltinFunctionContextPtr f)
{
  if (checkGeoLocation(f)) {
    f->finish(new NumericValue(sunrise(time(NULL), *(f->geoLocation()), false)*3600));
  }
}


// dawn()
static void dawn_func(BuiltinFunctionContextPtr f)
{
  if (checkGeoLocation(f)) {
    f->finish(new NumericValue(sunrise(time(NULL), *(f->geoLocation()), true)*3600));
  }
}


// sunset()
static void sunset_func(BuiltinFunctionContextPtr f)
{
  if (checkGeoLocation(f)) {
    f->finish(new NumericValue(sunset(time(NULL), *(f->geoLocation()), false)*3600));
  }
}


// dusk()
static void dusk_func(BuiltinFunctionContextPtr f)
{
  if (checkGeoLocation(f)) {
    f->finish(new NumericValue(sunset(time(NULL), *(f->geoLocation()), true)*3600));
  }
}


// epochtime()
static void epochtime_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((double)MainLoop::unixtime()/Second)); // epoch time in seconds
}


// epochdays()
static void epochdays_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((double)MainLoop::unixtime()/Day)); // epoch time in days with fractional time
}


// TODO: convert into single function returning a structured time object

// helper macro for getting time
#define prepTime \
  MLMicroSeconds t; \
  if (f->arg(0)->defined()) { \
    t = f->arg(0)->doubleValue()*Second; \
  } \
  else { \
    t = MainLoop::unixtime(); \
  } \
  double fracSecs; \
  struct tm loctim; \
  MainLoop::getLocalTime(loctim, &fracSecs, t, t<=Day);

// common argument descriptor for all time funcs
static const BuiltInArgDesc timegetter_args[] = { { numeric|optionalarg } };
static const size_t timegetter_numargs = sizeof(timegetter_args)/sizeof(BuiltInArgDesc);

// timeofday([epochtime])
static void timeofday_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs));
}


// hour([epochtime])
static void hour_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_hour));
}


// minute([epochtime])
static void minute_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_min));
}


// second([epochtime])
static void second_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_sec));
}


// year([epochtime])
static void year_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_year+1900));
}


// month([epochtime])
static void month_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_mon+1));
}


// day([epochtime])
static void day_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_mday));
}


// weekday([epochtime])
static void weekday_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_wday));
}


// yearday([epochtime])
static void yearday_func(BuiltinFunctionContextPtr f)
{
  prepTime
  f->finish(new NumericValue(loctim.tm_yday));
}


// delay(seconds)
static void delay_abort(TicketObjPtr aTicket)
{
  aTicket->ticket.cancel();
}
static const BuiltInArgDesc delay_args[] = { { numeric } };
static const size_t delay_numargs = sizeof(delay_args)/sizeof(BuiltInArgDesc);
static void delay_func(BuiltinFunctionContextPtr f)
{
  MLMicroSeconds delay = f->arg(0)->doubleValue()*Second;
  TicketObjPtr delayTicket = TicketObjPtr(new TicketObj);
  delayTicket->ticket.executeOnce(boost::bind(&BuiltinFunctionContext::finish, f, new AnnotatedNullValue("delayed")), delay);
  f->setAbortCallback(boost::bind(&delay_abort, delayTicket));
}


// The standard function descriptor table
static const BuiltinMemberDescriptor standardFunctions[] = {
  { "ifvalid", executable|any, ifvalid_numargs, ifvalid_args, &ifvalid_func },
  { "isvalid", executable|numeric, isvalid_numargs, isvalid_args, &isvalid_func },
  { "if", executable|any, if_numargs, if_args, &if_func },
  { "abs", executable|numeric|null, abs_numargs, abs_args, &abs_func },
  { "int", executable|numeric|null, int_numargs, int_args, &int_func },
  { "frac", executable|numeric|null, frac_numargs, frac_args, &frac_func },
  { "round", executable|numeric|null, round_numargs, round_args, &round_func },
  { "random", executable|numeric, random_numargs, random_args, &random_func },
  { "min", executable|numeric|null, min_numargs, min_args, &min_func },
  { "max", executable|numeric|null, max_numargs, max_args, &max_func },
  { "limited", executable|numeric|null, limited_numargs, limited_args, &limited_func },
  { "cyclic", executable|numeric|null, cyclic_numargs, cyclic_args, &cyclic_func },
  { "string", executable|text, string_numargs, string_args, &string_func },
  { "number", executable|numeric, number_numargs, number_args, &number_func },
  { "describe", executable|text, describe_numargs, describe_args, &describe_func },
  { "json", executable|json, json_numargs, json_args, &json_func },
  #if ENABLE_JSON_APPLICATION
  { "jsonresource", executable|json|error, jsonresource_numargs, jsonresource_args, &jsonresource_func },
  #endif
  { "elements", executable|numeric|null, elements_numargs, elements_args, &elements_func },
  { "lastarg", executable|any, lastarg_numargs, lastarg_args, &lastarg_func },
  { "strlen", executable|numeric|null, strlen_numargs, strlen_args, &strlen_func },
  { "substr", executable|text|null, substr_numargs, substr_args, &substr_func },
  { "find", executable|numeric|null, find_numargs, find_args, &find_func },
  { "format", executable|text, format_numargs, format_args, &format_func },
  { "formattime", executable|text, formattime_numargs, formattime_args, &formattime_func },
  { "throw", executable|any, throw_numargs, throw_args, &throw_func },
  { "error", executable|error, error_numargs, error_args, &error_func },
  { "errordomain", executable|text|null, errordomain_numargs, errordomain_args, &errordomain_func },
  { "errorcode", executable|numeric|null, errorcode_numargs, errorcode_args, &errorcode_func },
  { "errormessage", executable|text|null, errormessage_numargs, errormessage_args, &errormessage_func },
  { "abort", executable|null, abort_numargs, abort_args, &abort_func },
  { "undeclare", executable|null, 0, NULL, &undeclare_func },
  { "log", executable|text, log_numargs, log_args, &log_func },
  { "loglevel", executable|numeric, loglevel_numargs, loglevel_args, &loglevel_func },
  { "logleveloffset", executable|numeric, logleveloffset_numargs, logleveloffset_args, &logleveloffset_func },
  { "is_weekday", executable|any, is_weekday_numargs, is_weekday_args, &is_weekday_func },
  { "after_time", executable|numeric, after_time_numargs, after_time_args, &after_time_func },
  { "is_time", executable|numeric, is_time_numargs, is_time_args, &is_time_func },
  { "initial", executable|numeric, 0, NULL, &initial_func },
  { "testlater", executable|numeric, testlater_numargs, testlater_args, &testlater_func },
  { "every", executable|numeric, every_numargs, every_args, &every_func },
  { "between_dates", executable|numeric, between_dates_numargs, between_dates_args, &between_dates_func },
  { "sunrise", executable|numeric|null, 0, NULL, &sunrise_func },
  { "dawn", executable|numeric|null, 0, NULL, &dawn_func },
  { "sunset", executable|numeric|null, 0, NULL, &sunset_func },
  { "dusk", executable|numeric|null, 0, NULL, &dusk_func },
  { "epochtime", executable|any, 0, NULL, &epochtime_func },
  { "epochdays", executable|any, 0, NULL, &epochdays_func },
  { "timeofday", executable|numeric, timegetter_numargs, timegetter_args, &timeofday_func },
  { "hour", executable|numeric, timegetter_numargs, timegetter_args, &hour_func },
  { "minute", executable|numeric, timegetter_numargs, timegetter_args, &minute_func },
  { "second", executable|numeric, timegetter_numargs, timegetter_args, &second_func },
  { "year", executable|numeric, timegetter_numargs, timegetter_args, &year_func },
  { "month", executable|numeric, timegetter_numargs, timegetter_args, &month_func },
  { "day", executable|numeric, timegetter_numargs, timegetter_args, &day_func },
  { "weekday", executable|numeric, timegetter_numargs, timegetter_args, &weekday_func },
  { "yearday", executable|numeric, timegetter_numargs, timegetter_args, &yearday_func },
  // Async
  { "await", executable|async|any, await_numargs, await_args, &await_func },
  { "delay", executable|async|null, delay_numargs, delay_args, &delay_func },
  { "eval", executable|async|any, eval_numargs, eval_args, &eval_func },
  { NULL } // terminator
};

} // BuiltinFunctions

// MARK: - Standard Scripting Domain

static ScriptingDomainPtr standardScriptingDomain;

ScriptingDomain& StandardScriptingDomain::sharedDomain()
{
  if (!standardScriptingDomain) {
    standardScriptingDomain = new StandardScriptingDomain();
    // the standard scripting domains has the standard functions
    standardScriptingDomain->registerMemberLookup(new BuiltInMemberLookup(BuiltinFunctions::standardFunctions));
  }
  return *standardScriptingDomain.get();
};



#if SIMPLE_REPL_APP

// MARK: - Simple REPL (Read Execute Print Loop) App
#include "fdcomm.hpp"
#include "httpcomm.hpp"
#include "socketcomm.hpp"

class SimpleREPLApp : public CmdLineApp
{
  typedef CmdLineApp inherited;

  ScriptSource source;
  ScriptMainContextPtr replContext;
  FdCommPtr input;

public:

  SimpleREPLApp() :
    source(sourcecode|regular|keepvars|concurrently|floatingGlobs, "REPL")
  {
  }

  virtual int main(int argc, char **argv)
  {
    const char *usageText =
      "Usage: %1$s [options]\n";
    const CmdLineOptionDescriptor options[] = {
      CMDLINE_APPLICATION_LOGOPTIONS,
      CMDLINE_APPLICATION_STDOPTIONS,
      { 0, NULL } // list terminator
    };
    // parse the command line, exits when syntax errors occur
    setCommandDescriptors(usageText, options);
    parseCommandLine(argc, argv);
    processStandardLogOptions(false);
    // app now ready to run (or cleanup when already terminated)
    return run();
  }

  virtual void initialize()
  {
    // add some capabilities
    #if ENABLE_HTTP_SCRIPT_FUNCS
    source.domain()->registerMemberLookup(new HttpLookup);
    #endif
    #if ENABLE_SOCKET_SCRIPT_FUNCS
    source.domain()->registerMemberLookup(new SocketLookup);
    #endif
    // get context
    replContext = source.domain()->newContext();
    source.setSharedMainContext(replContext);
    printf("p44Script REPL - type 'quit' to leave\n\n");
    input = FdCommPtr(new FdComm);
    R();
    input->setFd(0); // stdin
    input->makeNonBlocking();
    input->setReceiveHandler(boost::bind(&SimpleREPLApp::E, this, _1), '\n');
  }


  void R()
  {
    printf("p44Script: ");
  }


  void E(ErrorPtr err)
  {
    string cmd;
    if(Error::notOK(err)) {
      printf("\nI/O error: %s\n", err->text());
      terminateApp(EXIT_FAILURE);
      return;
    }
    if (input->receiveDelimitedString(cmd)) {
      cmd = trimWhiteSpace(cmd);
      if (uequals(cmd, "quit")) {
        printf("\nquitting p44Script REPL - bye!\n");
        terminateApp(EXIT_SUCCESS);
        return;
      }
      source.setSource(cmd);
      source.run(inherit, boost::bind(&SimpleREPLApp::PL, this, _1));
    }
  }

  void PL(ScriptObjPtr aResult)
  {
    if (aResult) {
      SourceCursor *cursorP = aResult->cursor();
      if (cursorP) {
        const char* p = cursorP->linetext();
        string line;
        nextLine(p, line);
        if (!source.refersTo(*cursorP)) {
          printf("     code: %s\n", line.c_str());
        }
        if (cursorP->lineno()>0) {
          printf(" line %3lu: %s\n", cursorP->lineno()+1, line.c_str());
        }
        string errInd;
        errInd.append(cursorP->charpos(), '-');
        errInd += '^';
        printf("       at: %s\n", errInd.c_str());
      }
      printf("   result: %s [%s]\n\n", aResult->stringValue().c_str(), aResult->getAnnotation().c_str());
    }
    else {
      printf("   result: <none>\n\n");
    }
    R();
  }
};


int main(int argc, char **argv)
{
  // prevent debug output before application.main scans command line
  SETLOGLEVEL(LOG_NOTICE);
  SETERRLEVEL(0, false);
  // create app with current mainloop
  static SimpleREPLApp application;
  // pass control
  return application.main(argc, argv);
}


#endif // SIMPLE_REPL_APP

#endif // ENABLE_P44SCRIPT

