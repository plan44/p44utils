//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#if ENABLE_JSON_APPLICATION && SCRIPTING_JSON_SUPPORT || ENABLE_APPLICATION_SUPPORT
  #include "application.hpp"
  #include <sys/stat.h> // for mkdir
  #include <stdio.h>
#endif
#ifndef ALWAYS_ALLOW_SYSTEM_FUNC
  #define ALWAYS_ALLOW_SYSTEM_FUNC 0
#endif
#ifndef ALWAYS_ALLOW_ALL_FILES
  #define ALWAYS_ALLOW_ALL_FILES 0
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
  while (!mEventSources.empty()) {
    EventSource *src = *(mEventSources.begin());
    mEventSources.erase(mEventSources.begin());
    src->mEventSinks.erase(this);
    src->mSinksModified = true;
  }
}


// MARK: - EventHandler

void EventHandler::setHandler(EventHandlingCB aEventHandlingCB)
{
  mEventHandlingCB = aEventHandlingCB;
}


void EventHandler::processEvent(ScriptObjPtr aEvent, EventSource &aSource, intptr_t aRegId)
{
  if (mEventHandlingCB) {
    mEventHandlingCB(aEvent, aSource, aRegId);
  }
}


// MARK: - EventSource

EventSource::~EventSource()
{
  // clear references in all sinks
  while (!mEventSinks.empty()) {
    EventSink *sink = mEventSinks.begin()->first;
    mEventSinks.erase(mEventSinks.begin());
    sink->mEventSources.erase(this);
  }
  mEventSinks.clear();
  mSinksModified = true;
}


void EventSource::registerForEvents(EventSink* aEventSink, intptr_t aRegId)
{
  if (aEventSink) {
    registerForEvents(*aEventSink, aRegId);
  }
}


void EventSource::registerForEvents(EventSink& aEventSink, intptr_t aRegId)
{
  mSinksModified = true;
  mEventSinks[&aEventSink] = aRegId; // multiple registrations are possible, counted only once, only last aRegId stored
  aEventSink.mEventSources.insert(this);
}


void EventSource::unregisterFromEvents(EventSink *aEventSink)
{
  if (aEventSink) {
    unregisterFromEvents(*aEventSink);
  }
}


void EventSource::unregisterFromEvents(EventSink& aEventSink)
{
  mSinksModified = true;
  mEventSinks.erase(&aEventSink);
  aEventSink.mEventSources.erase(this);
}



void EventSource::sendEvent(ScriptObjPtr aEvent)
{
  if (mEventSinks.empty()) return; // optimisation
  // note: duplicate notification is possible when sending event causes event sink changes and restarts
  // TODO: maybe fix this if it turns out to be a problem
  //       (should not, because entire triggering is designed to re-evaluate events after triggering)
  do {
    mSinksModified = false;
    for (EventSinkMap::iterator pos=mEventSinks.begin(); pos!=mEventSinks.end(); ++pos) {
      pos->first->processEvent(aEvent, *this, pos->second);
      if (mSinksModified) break;
    }
  } while(mSinksModified);
}


void EventSource::copySinksFrom(EventSource* aOtherSource)
{
  if (!aOtherSource) return;
  for (EventSinkMap::iterator pos=aOtherSource->mEventSinks.begin(); pos!=aOtherSource->mEventSinks.end(); ++pos) {
    mSinksModified = true;
    registerForEvents(pos->first, pos->second);
  }
}





// MARK: - ScriptObj

#if FOCUSLOGGING
  #define FOCUSLOGCLEAR(p) \
    if (FOCUSLOGENABLED) { string s = string_format("CLEARING %s@%pX", p, this); FOCUSLOG("%60s", s.c_str() ); }
  #define FOCUSLOGCALLER(p) \
    if (FOCUSLOGENABLED) { string s = string_format("calling@%pX for %s", this, p); FOCUSLOG("%60s : calling...", s.c_str() ); }
  #define FOCUSLOGLOOKUP(p) \
    if (FOCUSLOGENABLED) { string s = string_format("searching %s@%pX for '%s'", p, this, aName.c_str()); FOCUSLOG("%60s : requirements=0x%08x", s.c_str(), aMemberAccessFlags ); }
  #define FOCUSLOGSTORE(p) \
    if (FOCUSLOGENABLED) { string s = string_format("setting '%s' in %s@%pX", aName.c_str(), p, this); FOCUSLOG("%60s : value = %s", s.c_str(), ScriptObj::describe(aMember).c_str()); }
#else
  #define FOCUSLOGCLEAR(p)
  #define FOCUSLOGCALLER(p)
  #define FOCUSLOGLOOKUP(p)
  #define FOCUSLOGSTORE(p)
#endif // FOCUSLOGGING


ErrorPtr ScriptObj::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE("ScriptObj")
  return ScriptError::err(ScriptError::NotCreated, "cannot assign to '%s'", aName.c_str());
}

ErrorPtr ScriptObj::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  return ScriptError::err(ScriptError::NotFound, "cannot assign at %zu", aIndex);
}

ValueIteratorPtr ScriptObj::newIterator()
{
  // by default, iterate by index
  return new IndexedValueIterator(this);
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
    if (calcObj->hasType(text)) v = cstringQuote(v);
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
  if (undefined()) return inherited::operator==(aRightSide); // derived numerics might be null
  if (aRightSide.undefined()) return false; // a number (especially: zero) is never equal with undefined
  return doubleValue()==aRightSide.doubleValue();
}

bool StringValue::operator==(const ScriptObj& aRightSide) const
{
  if (undefined()) return inherited::operator==(aRightSide); // derived strings might be null
  if (aRightSide.undefined()) return false; // a string (especially: empty) is never equal with undefined
  return stringValue()==aRightSide.stringValue();
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
  if (undefined()) return inherited::operator<(aRightSide); // derived numerics might be null
  return doubleValue()<aRightSide.doubleValue();
}

bool StringValue::operator<(const ScriptObj& aRightSide) const
{
  if (undefined()) return inherited::operator<(aRightSide); // derived strings might be null
  return stringValue()<aRightSide.stringValue();
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
  return new NumericValue(doubleValue() + aRightSide.doubleValue());
}


ScriptObjPtr StringValue::operator+(const ScriptObj& aRightSide) const
{
  return new StringValue(stringValue() + aRightSide.stringValue());
}


ScriptObjPtr NumericValue::operator-(const ScriptObj& aRightSide) const
{
  return new NumericValue(doubleValue() - aRightSide.doubleValue());
}

ScriptObjPtr NumericValue::operator*(const ScriptObj& aRightSide) const
{
  return new NumericValue(doubleValue() * aRightSide.doubleValue());
}

ScriptObjPtr NumericValue::operator/(const ScriptObj& aRightSide) const
{
  if (aRightSide.doubleValue()==0) {
    return new ErrorValue(ScriptError::DivisionByZero, "division by zero");
  }
  else {
    return new NumericValue(doubleValue() / aRightSide.doubleValue());
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


// MARK: - iterator

IndexedValueIterator::IndexedValueIterator(ScriptObjPtr aObj) :
  mIteratedObj(aObj),
  mCurrentIndex(0)
{
}

void IndexedValueIterator::reset()
{
  mCurrentIndex = 0;
}

void IndexedValueIterator::next()
{
  if (validIndex()) mCurrentIndex += 1;
}

bool IndexedValueIterator::validIndex()
{
  return mCurrentIndex<mIteratedObj->numIndexedMembers();
}


void IndexedValueIterator::obtainKey(EvaluationCB aEvaluationCB, bool aNumericPreferred)
{
  if (!validIndex()) {
    aEvaluationCB(nullptr);
  }
  else {
    aEvaluationCB(new NumericValue(mCurrentIndex));
  }
}


void IndexedValueIterator::obtainValue(EvaluationCB aEvaluationCB, TypeInfo aMemberAccessFlags)
{
  if (!validIndex()) {
    aEvaluationCB(nullptr);
  }
  else {
    aEvaluationCB(mIteratedObj->memberAtIndex(mCurrentIndex, aMemberAccessFlags));
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
      // If current value is a placeholder with event sinks, and new value is a event source, too
      // the new value must inherit those sinks. The ONLY application is that a global on() handlers might
      // get declared (at compile time) watching a declared global (which is created as EventPlaceholderNullValue)
      // but will only at script run time get the actual value to watch, e.g. a socket or similar.
      // This is a use case of early p44script days, when non-global, run-time-defined handlers did not yet
      // exist - so declaring a global and then a handler on it and THEN then running code that assigns a socket
      // to that global was the ONLY way.
      // Before 2022-10-16 this was not limited to EventPlaceholderNullValue, which caused unwanted
      // accumulation of event sinks and really hard-to-explain outcomes.
      if (dynamic_cast<EventPlaceholderNullValue*>(mCurrentValue.get())) {
        EventSource* oldSource = mCurrentValue->eventSource();
        EventSource* newSource = aNewValue ? aNewValue->eventSource() : NULL;
        if (newSource) {
          newSource->copySinksFrom(oldSource);
        }
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


OneShotEventNullValue::OneShotEventNullValue(EventSource *aEventSource, string aAnnotation) :
  inherited(aAnnotation),
  mEventSource(aEventSource)
{
}


EventSource* OneShotEventNullValue::eventSource() const
{
  return mEventSource;
}



// MARK: - Error Values

ErrorValue::ErrorValue(ScriptError::ErrorCodes aErrCode, const char *aFmt, ...) :
  mCaught(false)
{
  mErr = new ScriptError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  mErr->setFormattedMessage(aFmt, args);
  va_end(args);
}


ErrorValue::ErrorValue(ScriptObjPtr aErrVal)
{
  ErrorValue* eP = dynamic_cast<ErrorValue *>(aErrVal.get());
  if (eP) {
    mErr = eP->mErr;
    mCaught = eP->mCaught;
  }
  else {
    mErr = Error::ok();
  }
}


ScriptObjPtr ErrorValue::trueOrError(ErrorPtr aError)
{
  // return a ErrorValue if aError is set and not OK, a true value otherwise
  if (Error::isOK(aError)) {
    return new BoolValue(true);
  }
  else {
    return new ErrorValue(aError);
  }
}



ErrorPosValue::ErrorPosValue(const SourceCursor &aCursor, ErrorPtr aError) :
  inherited(aError),
  mSourceCursor(aCursor)
{
}


ErrorPosValue::ErrorPosValue(const SourceCursor &aCursor, ScriptObjPtr aErrValue) :
  inherited(aErrValue),
  mSourceCursor(aCursor)
{
}



ErrorPosValue::ErrorPosValue(const SourceCursor &aCursor, ScriptError::ErrorCodes aErrCode, const char *aFmt, ...) :
  inherited(new ScriptError(aErrCode)),
  mSourceCursor(aCursor)
{
  va_list args;
  va_start(args, aFmt);
  mErr->setFormattedMessage(aFmt, args);
  va_end(args);
}


string ErrorPosValue::stringValue() const
{
  return string_format(
    "(%s:%zu,%zu): %s",
    mSourceCursor.originLabel(),
    mSourceCursor.lineno()+1,
    mSourceCursor.charpos()+1,
    Error::text(mErr)
  );
}


#if P44SCRIPT_FULL_SUPPORT
// MARK: - ThreadValue

ThreadValue::ThreadValue(ScriptCodeThreadPtr aThread) : mThread(aThread)
{
}


ScriptObjPtr ThreadValue::calculationValue()
{
  if (!mThreadExitValue) {
    // might still be running, or is in zombie state holding final result
    if (mThread) {
      mThreadExitValue = mThread->finalResult();
      if (mThreadExitValue) {
        mThread.reset(); // release the zombie thread object itself
      }
    }
  }
  if (!mThreadExitValue) return new AnnotatedNullValue("still running");
  return mThreadExitValue;
}


void ThreadValue::abort(ScriptObjPtr aAbortResult)
{
  if (mThread) mThread->abort(aAbortResult);
}


bool ThreadValue::running()
{
  return mThread && (mThread->finalResult()==NULL);
}


TypeInfo ThreadValue::getTypeInfo() const
{
  return threadref|keeporiginal|(!mThread ? nowait : 0);
}


EventSource* ThreadValue::eventSource() const
{
  return static_cast<EventSource*>(mThread.get());
}

#endif // P44SCRIPT_FULL_SUPPORT


// MARK: - Conversions

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

// MARK: - JsonRepresentedValue + conversions

JsonObjectPtr ErrorValue::jsonValue() const
{
  JsonObjectPtr j;
  if (mErr) {
    j = JsonObject::newObj();
    j->add("ErrorCode", JsonObject::newInt32((int32_t)mErr->getErrorCode()));
    j->add("ErrorDomain", JsonObject::newString(mErr->getErrorDomain()));
    j->add("ErrorMessage", JsonObject::newString(mErr->getErrorMessage()));
  }
  return j;
}


JsonObjectPtr StringValue::jsonValue() const
{
  // old version did parse strings for json, but that's ambiguous, so
  // we just return a json string now
  return JsonObject::newString(stringValue());
}


ScriptObjPtr JsonRepresentedValue::calculationValue()
{
  if (!jsonValue()) return new AnnotatedNullValue("json null");
  if (jsonValue()->isType(json_type_boolean)) return new BoolValue(jsonValue()->boolValue());
  if (jsonValue()->isType(json_type_int)) return new NumericValue(jsonValue()->int64Value());
  if (jsonValue()->isType(json_type_double)) return new NumericValue(jsonValue()->doubleValue());
  if (jsonValue()->isType(json_type_string)) return new StringValue(jsonValue()->stringValue());
  return inherited::calculationValue();
}


string JsonRepresentedValue::stringValue() const
{
  if (!jsonValue()) return ScriptObj::stringValue(); // undefined
  if (jsonValue()->isType(json_type_string)) return jsonValue()->stringValue(); // string leaf fields as strings w/o quotes!
  return jsonValue()->json_str(); // other types in their native json representation
}


double JsonRepresentedValue::doubleValue() const
{
  if (!jsonValue()) return ScriptObj::doubleValue(); // undefined
  return jsonValue()->doubleValue();
}


bool JsonRepresentedValue::boolValue() const
{
  if (!jsonValue()) return ScriptObj::boolValue(); // undefined
  return jsonValue()->boolValue();
}


bool JsonRepresentedValue::operator==(const ScriptObj& aRightSide) const
{
  if (inherited::operator==(aRightSide)) return true; // object identity
  if (aRightSide.undefined()) return undefined(); // both undefined is equal
  if (aRightSide.hasType(json)) {
    // compare JSON with JSON
    if (jsonValue().get()==aRightSide.jsonValue().get()) return true; // json object identity (or both NULL)
    if (!jsonValue() || !aRightSide.jsonValue()) return false;
    if (strcmp(jsonValue()->c_strValue(),aRightSide.jsonValue()->c_strValue())==0) return true; // same stringified value
  }
  else {
    // compare JSON to non-JSON
    return const_cast<JsonRepresentedValue *>(this)->calculationValue()->operator==(aRightSide);
  }
  return false; // everything else: not equal
}


bool JsonRepresentedValue::operator<(const ScriptObj& aRightSide) const
{
  if (!aRightSide.hasType(json)) {
    // compare JSON to non-JSON
    if (aRightSide.hasType(numeric)) return doubleValue()<aRightSide.doubleValue();
    if (aRightSide.hasType(text)) return stringValue()<aRightSide.stringValue();
  }
  return false; // everything else: not orderable -> never less
}


ScriptObjPtr JsonRepresentedValue::operator+(const ScriptObj& aRightSide) const
{
  JsonObjectPtr r = aRightSide.jsonValue();
  if (r && r->isType(json_type_array)) {
    // if I am an array, too -> append elements
    if (jsonValue() && jsonValue()->isType(json_type_array)) {
      JsonObjectPtr j = const_cast<JsonRepresentedValue*>(this)->assignmentValue()->jsonValue();
      for (int i = 0; i<r->arrayLength(); i++) {
        j->arrayAppend(r->arrayGet(i));
      }
      return new JsonValue(j);
    }
  }
  else if (r && r->isType(json_type_object)) {
    // if I am an object, too -> merge fields
    if (jsonValue() && jsonValue()->isType(json_type_object)) {
      JsonObjectPtr j = const_cast<JsonRepresentedValue*>(this)->assignmentValue()->jsonValue();
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


const ScriptObjPtr JsonRepresentedValue::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP("JsonRepresentedValue");
  ScriptObjPtr m;
  if (jsonValue() && typeRequirementMet(json, aMemberAccessFlags, typeMask)) {
    // we cannot meet any other type requirement but json
    JsonObjectPtr j = jsonValue()->get(aName.c_str());
    if (j) {
      // we have that member
      m = ScriptObjPtr(new JsonValue(j));
      if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
        m = new StandardLValue(this, aName, m); // it is allowed to overwrite this value
      }
    }
    else {
      // no such member yet
      if ((aMemberAccessFlags & (lvalue|create))==(lvalue|create)) {
        // return lvalue to create object
        m = new StandardLValue(this, aName, ScriptObjPtr()); // it is allowed to create a new value
      }
    }
  }
  return m;
}


size_t JsonRepresentedValue::numIndexedMembers() const
{
  JsonObjectPtr j = jsonValue();
  if (j) {
    if (j->isType(json_type_object)) return j->numKeys(); // objects can be accessed as arrays to get the keys
    return j->arrayLength();
  }
  return 0;
}


const ScriptObjPtr JsonRepresentedValue::memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr m;
  JsonObjectPtr j = jsonValue();
  if (j && typeRequirementMet(json, aMemberAccessFlags, typeMask)) {
    // we cannot meet any other type requirement but json
    if (aIndex<numIndexedMembers()) {
      // we have that member
      if (j->isType(json_type_object)) {
        // special case of accessing object's key as an array, read-only
        string key;
        if (j->keyValueByIndex((int)aIndex, key, NULL)) {
          m = ScriptObjPtr(new StringValue(key));
        }
      }
      else {
        // must be array, elements can be written to
        m = ScriptObjPtr(new JsonValue(j->arrayGet((int)aIndex)));
        if ((aMemberAccessFlags & lvalue) && (aMemberAccessFlags & onlycreate)==0) {
          m = new StandardLValue(this, aIndex, m); // it is allowed to overwrite this value
        }
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


ValueIteratorPtr JsonRepresentedValue::newIterator()
{
  return new JsonValueIterator(this);
}


JsonValueIterator::JsonValueIterator(ScriptObjPtr aObj) :
  inherited(aObj)
{
}


void JsonValueIterator::obtainKey(EvaluationCB aEvaluationCB, bool aNumericPreferred)
{
  if (validIndex() && !aNumericPreferred) {
    // obtain the field name of the currently indexed object field
    JsonObjectPtr j = mIteratedObj->jsonValue();
    if (j && j->isType(json_type_object)) {
      string key;
      if (j->keyValueByIndex((int)mCurrentIndex, key, NULL)) {
        aEvaluationCB(new StringValue(key));
        return;
      }
    }
  }
  inherited::obtainKey(aEvaluationCB, aNumericPreferred);
}


void JsonValueIterator::obtainValue(EvaluationCB aEvaluationCB, TypeInfo aMemberAccessFlags)
{
  if (validIndex()) {
    // obtain the field contents of the currently indexed object field
    JsonObjectPtr j = mIteratedObj->jsonValue();
    if (j && j->isType(json_type_object)) {
      JsonObjectPtr val;
      string key;
      if (j->keyValueByIndex((int)mCurrentIndex, key, &val)) {
        aEvaluationCB(new JsonValue(val));
        return;
      }
    }
  }
  inherited::obtainValue(aEvaluationCB, aMemberAccessFlags);
}


// MARK: - JsonValue


TypeInfo JsonValue::getTypeInfo() const
{
  JsonObject* j = jsonValue().get();
  if (!j || j->isType(json_type_null)) return null;
  if (j->isType(json_type_object)) return json+object;
  if (j->isType(json_type_array)) return json+array;
  if (j->isType(json_type_string)) return json+text;
  return json+numeric; // everything else is numeric
}


ScriptObjPtr JsonValue::assignmentValue()
{
  // break down to standard value or copied json, unless this is a derived object such as a JSON API request that indicates to be kept as-is
  if (!hasType(keeporiginal)) {
    // avoid creating new json values for simple types
    if (jsonValue() && (jsonValue()->isType(json_type_array) || jsonValue()->isType(json_type_object))) {
      // must copy the contained json object
      return new JsonValue(JsonObjectPtr(new JsonObject(*jsonValue())));
    }
    return calculationValue();
  }
  return inherited::assignmentValue();
}


ErrorPtr JsonValue::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE("JsonValue");
  if (!mJsonval) {
    mJsonval = JsonObject::newObj();
  }
  else if (!mJsonval->isType(json_type_object)) {
    return ScriptError::err(ScriptError::Invalid, "json is not an object, cannot assign field");
  }
  if (aMember) {
    mJsonval->add(aName.c_str(), aMember->jsonValue());
  }
  else {
    mJsonval->del(aName.c_str());
  }
  return ErrorPtr();
}


ErrorPtr JsonValue::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  if (!mJsonval) {
    mJsonval = JsonObject::newArray();
  }
  else if (!mJsonval->isType(json_type_array)) {
    return ScriptError::err(ScriptError::Invalid, "json is not an array, cannot set element");
  }
  if (aMember) {
    mJsonval->arrayPut((int)aIndex, aMember->jsonValue());
  }
  else {
    mJsonval->arrayDel((int)aIndex, 1);
  }
  return ErrorPtr();
}

#endif // SCRIPTING_JSON_SUPPORT


// MARK: - SimpleVarContainer

void SimpleVarContainer::clearVars()
{
  FOCUSLOGCLEAR("SimpleVarContainer");
  while (!mNamedVars.empty()) {
    mNamedVars.begin()->second->deactivate();
    mNamedVars.erase(mNamedVars.begin());
  }
  mNamedVars.clear();
}

void SimpleVarContainer::releaseObjsFromSource(SourceContainerPtr aSource)
{
  NamedVarMap::iterator pos = mNamedVars.begin();
  while (pos!=mNamedVars.end()) {
    if (pos->second->originatesFrom(aSource)) {
      pos->second->deactivate(); // pre-deletion, breaks retain cycles
      #if P44_CPP11_FEATURE
      pos = mNamedVars.erase(pos); // source is gone -> remove
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


void SimpleVarContainer::clearFloating()
{
  NamedVarMap::iterator pos = mNamedVars.begin();
  while (pos!=mNamedVars.end()) {
    if (pos->second->floating()) {
      pos->second->deactivate(); // pre-deletion, breaks retain cycles
      #if P44_CPP11_FEATURE
      pos = mNamedVars.erase(pos); // source is gone -> remove
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



const ScriptObjPtr SimpleVarContainer::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP("SimpleVarContainer");
  ScriptObjPtr m;
  NamedVarMap::const_iterator pos = mNamedVars.find(aName);
  if (pos!=mNamedVars.end()) {
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
  // nothing found
  return m;
}


ErrorPtr SimpleVarContainer::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE("SimpleVarContainer");
  NamedVarMap::iterator pos = mNamedVars.find(aName);
  if (pos!=mNamedVars.end()) {
    // exists in local vars
    if (aMember) {
      // assign new value
      pos->second = aMember;
    }
    else {
      // delete
      // - deactivate first to break retain cycles
      pos->second->deactivate();
      // - now release from container
      mNamedVars.erase(pos);
    }
  }
  else if (aMember) {
    // create it, but only if we have a member (not a delete attempt)
    mNamedVars[aName] = aMember;
  }
  return ErrorPtr();
}


#if SCRIPTING_JSON_SUPPORT

JsonObjectPtr SimpleVarContainer::jsonValue() const
{
  JsonObjectPtr obj = JsonObject::newObj();
  for(NamedVarMap::const_iterator pos = mNamedVars.begin(); pos!=mNamedVars.end(); ++pos) {
    obj->add(pos->first.c_str(), pos->second->jsonValue());
  }
  return obj;
}

#endif // SCRIPTING_JSON_SUPPORT




// MARK: - StructuredLookupObject

const ScriptObjPtr StructuredLookupObject::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP("StructuredLookupObject");
  ScriptObjPtr m;
  LookupList::const_iterator pos = mLookups.begin();
  while (pos!=mLookups.end()) {
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
    for (LookupList::iterator pos = mLookups.begin(); pos!=mLookups.end(); ++pos) {
      if (pos->get()==aMemberLookup.get()) return; // avoid registering the same lookup twice
    }
    mLookups.push_front(aMemberLookup);
  }
}


void StructuredLookupObject::registerSharedLookup(BuiltInMemberLookup*& aSingletonLookupP, const struct BuiltinMemberDescriptor* aMemberDescriptors)
{
  if (aSingletonLookupP==NULL) {
    aSingletonLookupP = new BuiltInMemberLookup(aMemberDescriptors);
    aSingletonLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(aSingletonLookupP);
}


void StructuredLookupObject::registerMember(const string aName, ScriptObjPtr aMember)
{
  if (!mSingleMembers) {
    // add a lookup for single members
    mSingleMembers = MemberLookupPtr(new PredefinedMemberLookup);
    mLookups.push_front(mSingleMembers);
  }
  mSingleMembers->registerMember(aName, aMember);
}

#if SCRIPTING_JSON_SUPPORT

JsonObjectPtr StructuredLookupObject::jsonValue() const
{
  JsonObjectPtr obj = JsonObject::newObj();
  for(LookupList::const_iterator pos = mLookups.begin(); pos!=mLookups.end(); ++pos) {
    (*pos)->addJsonValues(obj);
  }
  return obj;
}


JsonObjectPtr StructuredLookupObject::builtinsInfo()
{
  JsonObjectPtr hl = JsonObject::newObj();
  ScriptObjPtr m;
  for (LookupList::const_iterator pos = mLookups.begin(); pos!=mLookups.end(); pos++) {
    MemberLookupPtr lookup = *pos;
    lookup->addJsonValues(hl);
  }
  return hl;
}



#endif // SCRIPTING_JSON_SUPPORT



// MARK: - PredefinedMemberLookup

ScriptObjPtr PredefinedMemberLookup::memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements) const
{
  NamedVarMap::const_iterator pos = mMembers.find(aName);
  if (pos!=mMembers.end()) return pos->second;
  return ScriptObjPtr();
}


void PredefinedMemberLookup::registerMember(const string aName, ScriptObjPtr aMember)
{
  mMembers[aName] = aMember;
}


#if SCRIPTING_JSON_SUPPORT

void PredefinedMemberLookup::addJsonValues(JsonObjectPtr &aObj) const
{
  for(NamedVarMap::const_iterator pos = mMembers.begin(); pos!=mMembers.end(); ++pos) {
    aObj->add(pos->first.c_str(), pos->second->jsonValue());
  }
}

#endif // SCRIPTING_JSON_SUPPORT




// MARK: - ExecutionContext

ExecutionContext::ExecutionContext(ScriptMainContextPtr aMainContext) :
  mMainContext(aMainContext),
  mUndefinedResult(false)
{
}


ScriptObjPtr ExecutionContext::instance() const
{
  return mMainContext ? mMainContext->instance() : ScriptObjPtr();
}


ScriptingDomainPtr ExecutionContext::domain() const
{
  return mMainContext ? mMainContext->domain() : ScriptingDomainPtr();
}


GeoLocation* ExecutionContext::geoLocation()
{
  if (!domain()) return NULL; // no domain to fallback to
  return domain()->geoLocation(); // return domain's location
}


void ExecutionContext::clearVars()
{
  FOCUSLOGCLEAR("indexed Variables");
  mIndexedVars.clear();
}


void ExecutionContext::releaseObjsFromSource(SourceContainerPtr aSource)
{
  // Note we can ignore our indexed members, as these are always temporary
  if (domain()) domain()->releaseObjsFromSource(aSource);
}


size_t ExecutionContext::numIndexedMembers() const
{
  return mIndexedVars.size();
}


const ScriptObjPtr ExecutionContext::memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr m;
  if (aIndex<mIndexedVars.size()) {
    // we have that member
    m = mIndexedVars[aIndex];
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
  if (aIndex==mIndexedVars.size() && aMember) {
    // specially optimized case: appending
    mIndexedVars.push_back(aMember);
  }
  else if (aMember) {
    if (aIndex>mIndexedVars.size()) {
      // resize, will result in sparse array
      mIndexedVars.resize(aIndex+1);
    }
    mIndexedVars[aIndex] = aMember;
  }
  else {
    // delete member
    mIndexedVars.erase(mIndexedVars.begin()+aIndex);
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
    // check JSON-agnostic, because any type can have the additional json type, when it comes out of a json value
    if ((argInfo & allowed & jsonagnosticMask) != (argInfo & jsonagnosticMask)) {
      // arg has type bits set that are not allowed
      if (
        (allowed & exacttype) || // exact checking required...
        (argInfo & jsonagnosticMask &~scalar)!=(allowed & jsonagnosticMask &~scalar) // ...or non-scalar requirements not met
      ) {
        // argument type mismatch
        if (allowed & undefres) {
          // type mismatch is not an error, but just enforces undefined function result w/o executing
          mUndefinedResult = true;
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

ScriptObjPtr ExecutionContext::executeSynchronously(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, ScriptObjPtr aThreadLocals, MLMicroSeconds aMaxRunTime)
{
  ScriptObjPtr syncResult;

  bool finished = false;
  aEvalFlags |= synchronously;
  execute(aToExecute, aEvalFlags, boost::bind(&syncExecDone, &syncResult, &finished, _1), nullptr, aThreadLocals, aMaxRunTime);
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
  mLocalVars.isMemberVariable();
}




void ScriptCodeContext::releaseObjsFromSource(SourceContainerPtr aSource)
{
  // also release any objects linked to that source
  mLocalVars.releaseObjsFromSource(aSource);
  inherited::releaseObjsFromSource(aSource);
}


bool ScriptCodeContext::isExecutingSource(SourceContainerPtr aSource)
{
  for (ThreadList::iterator pos = mThreads.begin(); pos!=mThreads.end(); ++pos) {
    if ((*pos)->isExecutingSource(aSource)) return true;
  }
  return false;
}



void ScriptCodeContext::clearFloating()
{
  mLocalVars.clearFloating();
}



void ScriptCodeContext::clearVars()
{
  FOCUSLOGCLEAR(mMainContext ? "local" : (domain() ? "main" : "global"));
  mLocalVars.clearVars();
  inherited::clearVars();
}


const ScriptObjPtr ScriptCodeContext::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP(mMainContext ? "local" : (domain() ? "main" : "global"));
  ScriptObjPtr c;
  ScriptObjPtr m;
  // 0) first check if we have EXISTING var in main/global
  if ((aMemberAccessFlags & nooverride) && mMainContext) {
    // nooverride: first check if we have an EXISTING main/global (but do NOT create a global if not)
    FOCUSLOGCALLER("existing main/globals");
    c = mMainContext->memberByName(aName, aMemberAccessFlags & ~create); // might return main/global by that name that already exists
    // still check for local...
  }
  // 1) local variables/objects
  if ((aMemberAccessFlags & (classscope+objscope))==0) {
    FOCUSLOGCALLER("read context locals");
    if ((m = mLocalVars.memberByName(aName, aMemberAccessFlags & (c ? ~create : ~none)))) return m; // use local when it already exists
    if (c) return c; // if main/global by that name exists, return it
    if (aMemberAccessFlags & create) {
      FOCUSLOGCALLER("create context locals");
      if ((m = mLocalVars.memberByName(aName, aMemberAccessFlags))) return m; // allow creating locally
    }
  }
  // 2) access to ANY members of the _instance_ itself if running in a object context
  FOCUSLOGCALLER("existing instance members");
  if (instance() && (m = instance()->memberByName(aName, aMemberAccessFlags) )) return m;
  // 3) main level (if not already checked above)
  if ((aMemberAccessFlags & nooverride)==0 && mMainContext) {
    FOCUSLOGCALLER("main/globals");
    if (mMainContext && (m = mMainContext->memberByName(aName, aMemberAccessFlags))) return m;
  }
  // nothing found
  // Note: do NOT call inherited, altough there is a overridden memberByName in StructuredObject, but this
  //   is NOT a default lookup for ScriptCodeContext (but explicitly used from ScriptMainContext)
  return m;
}


ErrorPtr ScriptCodeContext::setMemberByName(const string aName, const ScriptObjPtr aMember)
{
  FOCUSLOGSTORE(domain() ? "named vars" : "global vars");
  return mLocalVars.setMemberByName(aName, aMember);
}


ErrorPtr ScriptCodeContext::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  ErrorPtr err = inherited::setMemberAtIndex(aIndex, aMember, aName);
  if (!aName.empty() && Error::isOK(err)) {
    err = setMemberByName(aName, aMember);
  }
  return err;
}


bool ScriptCodeContext::abort(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult, ScriptCodeThreadPtr aExceptThread)
{
  bool anyAborted = false;
  if (aAbortFlags & queue) {
    // empty queue first to make sure no queued threads get started when last running thread is killed below
    while (!mQueuedThreads.empty()) {
      mQueuedThreads.back()->abort(new ErrorValue(ScriptError::Aborted, "Removed queued execution before it could start"));
      mQueuedThreads.pop_back();
    }
  }
  if (aAbortFlags & stoprunning) {
    ThreadList tba = mThreads; // copy list as original get modified while aborting
    for (ThreadList::iterator pos = tba.begin(); pos!=tba.end(); ++pos) {
      if (!aExceptThread || aExceptThread!=(*pos)) {
        anyAborted = true;
        (*pos)->abort(aAbortResult); // should cause threadTerminated to get called which will remove actually terminated thread from the list
      }
    }
  }
  return anyAborted;
}


bool ScriptCodeContext::abortThreadsRunningSource(SourceContainerPtr aSource, ScriptObjPtr aError)
{
  bool anyAborted = false;
  ThreadList tba = mThreads; // copy list as original get modified while aborting
  for (ThreadList::iterator pos = tba.begin(); pos!=tba.end(); ++pos) {
    if ((*pos)->isExecutingSource(aSource)) {
      anyAborted = true;
      (*pos)->abort(aError); // should cause threadTerminated to get called which will remove actually terminated thread from the list
    }
  }
  return anyAborted;
}


#if SCRIPTING_JSON_SUPPORT

JsonObjectPtr ScriptCodeContext::jsonValue() const
{
  return mLocalVars.jsonValue();
}

#endif // SCRIPTING_JSON_SUPPORT


#if P44SCRIPT_DEBUGGING_SUPPORT

/// @param aCodeObj the object to check for
/// @return true if aCodeObj already has a paused thread in this context
bool ScriptCodeContext::hasThreadPausedIn(CompiledCodePtr aCodeObj)
{
  for (ThreadList::iterator pos = mThreads.begin(); pos!=mThreads.end(); ++pos) {
    if ((*pos)->pauseReason()>unpause && (*pos)->mCodeObj==aCodeObj) {
      return true;
    }
  }
  return false;
}

#endif // P44SCRIPT_DEBUGGING_SUPPORT



void ScriptCodeContext::execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainedFromThread, ScriptObjPtr aThreadLocals, MLMicroSeconds aMaxRunTime)
{
  if (mUndefinedResult) {
    // just return undefined w/o even trying to execute
    mUndefinedResult = false;
    if (aEvaluationCB) aEvaluationCB(new AnnotatedNullValue("undefined argument caused undefined function result"));
    return;
  }
  // must be compiled code at this point
  CompiledCodePtr code = boost::dynamic_pointer_cast<CompiledCode>(aToExecute);
  if (!code) {
    if (aEvaluationCB) aEvaluationCB(new ErrorValue(ScriptError::Internal, "Object to be run must be compiled code!"));
    return;
  }
  #if P44SCRIPT_DEBUGGING_SUPPORT
  // if debugging is enabled, make sure no paused thread is already running this same code
  if (domain()->defaultPausingMode()>nopause && hasThreadPausedIn(code)) {
    OLOG(LOG_WARNING, "'%s' is already executing in paused thread -> SUPPRESSED starting again in new thread", code->getIdentifier().c_str());
    return;
  }
  #endif // P44SCRIPT_DEBUGGING_SUPPORT
  if ((aEvalFlags & keepvars)==0) {
    clearVars();
  }
  // code can be executed
  #if P44SCRIPT_FULL_SUPPORT
  // - we do not run source code, only script bodies
  if (aEvalFlags & sourcecode) aEvalFlags = (aEvalFlags & ~sourcecode) | scriptbody;
  #endif // P44SCRIPT_FULL_SUPPORT
  // - now run
  ScriptCodeThreadPtr thread = newThreadFrom(code, code->mCursor, aEvalFlags, aEvaluationCB, aChainedFromThread, aThreadLocals, aMaxRunTime);
  if (thread) {
    thread->run();
    return;
  }
  // Note: no thread at this point is ok, means that execution was queued
}


ScriptCodeThreadPtr ScriptCodeContext::newThreadFrom(CompiledCodePtr aCodeObj, SourceCursor &aFromCursor, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainedFromThread, ScriptObjPtr aThreadLocals, MLMicroSeconds aMaxRunTime)
{
  // prepare a thread for executing now or later
  // Note: thread gets an owning Ptr back to this, so this context cannot be destructed before all
  //   threads have ended.
  ScriptCodeThreadPtr newThread = ScriptCodeThreadPtr(new ScriptCodeThread(this, aCodeObj, aFromCursor, aThreadLocals, aChainedFromThread));
  MLMicroSeconds maxBlockTime = aEvalFlags&synchronously ? aMaxRunTime : domain()->getMaxBlockTime();
  newThread->prepareRun(aEvaluationCB, aEvalFlags, maxBlockTime, aMaxRunTime);
  // now check how and when to run it
  if (!mThreads.empty()) {
    // some threads already running
    if (aEvalFlags & stoprunning) {
      // kill all current threads (with or without queued, depending on queue set in aEvalFlags or not) first...
      abort(aEvalFlags & stopall, new ErrorValue(ScriptError::Aborted, "Aborted by another script starting"));
      // ...then start new
    }
    else if (aEvalFlags & queue) {
      if (aEvalFlags & concurrently) {
        // queue+concurrently means queue if another queued thread is already running, otherwise just run
        for (ThreadList::iterator pos=mThreads.begin(); pos!=mThreads.end(); ++pos) {
          if (((*pos)->mEvaluationFlags & queue)!=0) {
            // at least one thread marked queued is running, must queue this one
            mQueuedThreads.push_back(newThread);
            return ScriptCodeThreadPtr(); // no thread to start now, but ok because it was queued
          }
        }
        // ...can start now, because no other non-concurrent thread is running
      }
      else {
        // just queue for later
        mQueuedThreads.push_back(newThread);
        return ScriptCodeThreadPtr(); // no thread to start now, but ok because it was queued
      }
    }
    else if ((aEvalFlags & concurrently)==0) {
      // none of the multithread modes and already running: just report busy
      newThread->abort(new ErrorValue(ScriptError::Busy, "Already busy executing script"));
      return newThread; // return the thread, which will immediately terminate with "already busy" error
    }
  }
  // can start new thread now
  mThreads.push_back(newThread);
  return newThread;
}


void ScriptCodeContext::threadTerminated(ScriptCodeThreadPtr aThread, EvaluationFlags aThreadEvalFlags)
{
  // a thread has ended, remove it from the list
  ThreadList::iterator pos=mThreads.begin();
  bool anyFromQueue = false;
  while (pos!=mThreads.end()) {
    if (pos->get()==aThread.get()) {
      #if P44_CPP11_FEATURE
      pos = mThreads.erase(pos);
      #else
      ThreadList::iterator dpos = pos++;
      threads.erase(dpos);
      #endif
      // thread object should get disposed now, along with its SourceRef
      if (anyFromQueue) break; // optimization: no need to continue loop
      continue;
    }
    if ((*pos)->mEvaluationFlags & queue) {
      anyFromQueue = true;
    }
    ++pos;
  }
  if (aThreadEvalFlags & mainthread) {
    // stop all other threads in this context
    abort(stoprunning);
  }
  // check for queued executions to start now
  if (!anyFromQueue && !mQueuedThreads.empty() ) {
    // check next thread from the queue
    ScriptCodeThreadPtr nextThread = mQueuedThreads.front();
    // next queued thread may run concurrently with not-from-queue threads if it is also marked concurrently
    if (mThreads.empty() || (nextThread->mEvaluationFlags & concurrently)!=0) {
      mQueuedThreads.pop_front();
      // and start it
      mThreads.push_back(nextThread);
      nextThread->run();
    }
  }
}



// MARK: - ScriptMainContext

ScriptMainContext::ScriptMainContext(ScriptingDomainPtr aDomain, ScriptObjPtr aThis) :
  inherited(ScriptMainContextPtr()), // main context itself does not have a mainContext (would self-lock)
  mDomainObj(aDomain),
  mThisObj(aThis)
{
}


#if P44SCRIPT_FULL_SUPPORT


void ScriptMainContext::releaseObjsFromSource(SourceContainerPtr aSource)
{
  // handlers
  HandlerList::iterator pos = mHandlers.begin();
  while (pos!=mHandlers.end()) {
    if ((*pos)->originatesFrom(aSource)) {
      (*pos)->deactivate(); // pre-deletion, breaks retain cycles
      pos = mHandlers.erase(pos); // source is gone -> remove
    }
    else {
      ++pos;
    }
  }
  inherited::releaseObjsFromSource(aSource);
}


ScriptObjPtr ScriptMainContext::registerHandler(ScriptObjPtr aHandler)
{
  CompiledHandlerPtr handler = boost::dynamic_pointer_cast<CompiledHandler>(aHandler);
  if (!handler) {
    return new ErrorValue(ScriptError::Internal, "is not a handler");
  }
  // prevent duplicating handlers
  // - when source is changed, all handlers are erased before
  // - but re-compiling without changes or re-running a on()-statement can make it getting registered twice
  // - this can be detected by comparing source start locations
  for (HandlerList::iterator pos = mHandlers.begin(); pos!=mHandlers.end(); pos++) {
    if ((*pos)->codeFromSameSourceAs(*handler)) {
      // replace this handler by the new one
      CompiledHandlerPtr h = handler;
      OLOG(LOG_INFO, "Replacing handler at %s:%zu,%zu ...", h->mCursor.originLabel(), h->mCursor.lineno()+1, h->mCursor.charpos()+1);
      pos->swap(h);
      // deactivate and kill the previous instance
      h->deactivate();
      h.reset();
      // return the active instance
      return handler;
    }
  }
  // install the handler
  mHandlers.push_back(handler);
  return handler;
}


JsonObjectPtr ScriptMainContext::handlersInfo()
{
  JsonObjectPtr hl = JsonObject::newArray();
  for (HandlerList::iterator pos = mHandlers.begin(); pos!=mHandlers.end(); pos++) {
    CompiledHandlerPtr h = *pos;
    JsonObjectPtr hi = JsonObject::newObj();
    hi->add("name", JsonObject::newString(h->mName));
    hi->add("origin", JsonObject::newString(h->mCursor.originLabel()));
    P44LoggingObj *l = h->mCursor.mSourceContainer->loggingContext();
    if (l) hi->add("logcontext", JsonObject::newString(l->logContextPrefix()));
    hi->add("line", JsonObject::newInt64(h->mCursor.lineno()+1));
    hi->add("char", JsonObject::newInt64(h->mCursor.charpos()+1));
    hi->add("posid", JsonObject::newInt64((intptr_t)h->mCursor.mPos.posId()));
    hl->arrayAppend(hi);
  }
  return hl;
}




void ScriptMainContext::clearVars()
{
  HandlerList::iterator pos = mHandlers.begin();
  while (pos!=mHandlers.end()) {
    (*pos)->deactivate(); // pre-deletion, breaks retain cycles
    pos = mHandlers.erase(pos); // source is gone -> remove
  }
  inherited::clearVars();
}


void ScriptMainContext::clearFloating()
{
  #if P44SCRIPT_FULL_SUPPORT
  HandlerList::iterator pos = mHandlers.begin();
  while (pos!=mHandlers.end()) {
    if ((*pos)->floating()) {
      pos = mHandlers.erase(pos); // source is gone -> remove
    }
    else {
      ++pos;
    }
  }
  #endif // P44SCRIPT_FULL_SUPPORT
  inherited::clearFloating();
}



#endif // P44SCRIPT_FULL_SUPPORT





const ScriptObjPtr ScriptMainContext::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  FOCUSLOGLOOKUP((domain() ? "main scope" : "global scope"));
  ScriptObjPtr g;
  ScriptObjPtr m;
  // member lookup during execution of a function or script body
  if ((aMemberAccessFlags & nooverride) && domain()) {
    // nooverride: first check if we have an EXISTING global (but do NOT create a global if not)
    FOCUSLOGCALLER("existing globals");
    g = domain()->memberByName(aName, aMemberAccessFlags & ~create); // might return main/global by that name that already exists
    // still check for local...
  }
  if ((aMemberAccessFlags & (constant|(domain() ? global : none)))==0) {
    // Only if not looking only for constant members (in the sense of: not settable by scripts) or globals (which are locals when we are the domain!)
    // 1) lookup local variables/arguments in this context...
    // 2) ...and members of the instance (if any)
    FOCUSLOGCALLER("inherited's variables");
    if ((m = inherited::memberByName(aName, aMemberAccessFlags & (g ? ~create : ~none)))) return m; // use local when it already exists
    if (g) return g; // if global by that name exists, return it
  }
  // 3) if not excplicitly global: members from registered lookups, which might or might not be instance related (depends on the lookup)
  if ((aMemberAccessFlags & global)==0) {
    FOCUSLOGCALLER("local members");
    if ((m = StructuredLookupObject::memberByName(aName, aMemberAccessFlags))) return m;
  }
  // 4) lookup global members in the script domain (vars, functions, constants)
  FOCUSLOGCALLER("globals");
  if (domain() && (m = domain()->memberByName(aName, aMemberAccessFlags&~(classscope|constant|objscope|global)))) return m;
  // nothing found (note that inherited was queried early above, already!)
  return m;
}


// MARK: - Scripting Domain


// MARK: - Built-in member support

BuiltInLValue::BuiltInLValue(const BuiltInMemberLookupPtr aLookup, const BuiltinMemberDescriptor *aMemberDescriptor, ScriptObjPtr aThisObj, ScriptObjPtr aCurrentValue) :
  inherited(aCurrentValue),
  mLookup(aLookup),
  mDescriptor(aMemberDescriptor),
  mThisObj(aThisObj)
{
}


void BuiltInLValue::assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue)
{
  ScriptObjPtr m;
  if (aNewValue) {
    m = mDescriptor->accessor(*const_cast<BuiltInMemberLookup *>(mLookup.get()), mThisObj, aNewValue); // write access
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
      mMembers[aMemberDescriptors->name]=aMemberDescriptors;
      aMemberDescriptors++;
    }
  }
}


ScriptObjPtr BuiltInMemberLookup::memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aMemberAccessFlags) const
{
  FOCUSLOGLOOKUP("builtin");
  // actual type requirement must match, scope requirements are irrelevant here
  ScriptObjPtr m;
  MemberMap::const_iterator pos = mMembers.find(aName);
  if (pos!=mMembers.end()) {
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


#if SCRIPTING_JSON_SUPPORT

void BuiltInMemberLookup::addJsonValues(JsonObjectPtr &aObj) const
{
  for(MemberMap::const_iterator pos = mMembers.begin(); pos!=mMembers.end(); ++pos) {
    // FIXME: as long as we use JSON here, there's no way for now to add non-values like functions, so just return NULL
    //   Only once we have P44Value hierarchy with iterators, we can do better
    const BuiltinMemberDescriptor* m = pos->second;
    if (m->returnTypeInfo & executable) {
      // function
      aObj->add(pos->first.c_str(), JsonObject::newString(string_format("built-in function with %zu arguments", m->numArgs)));
    }
    else {
      // member
      aObj->add(pos->first.c_str(), JsonObject::newString("built-in object field"));
    }
  }
}

#endif // SCRIPTING_JSON_SUPPORT



ExecutionContextPtr BuiltinFunctionObj::contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const
{
  // built-in functions get their "this" from the lookup they come from
  return new BuiltinFunctionContext(aMainContext, aThread);
}


bool BuiltinFunctionObj::argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const
{
  if (aIndex>=mDescriptor->numArgs) {
    // no argument with this index, check for open argument list
    if (mDescriptor->numArgs<1) return false;
    aIndex = mDescriptor->numArgs-1;
    if ((mDescriptor->arguments[aIndex].typeInfo & multiple)==0) return false;
  }
  const BuiltInArgDesc* ad = &mDescriptor->arguments[aIndex];
  aArgDesc.typeInfo = ad->typeInfo;
  aArgDesc.name = nonNullCStr(ad->name);
  return true;
}


BuiltinFunctionContext::BuiltinFunctionContext(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) :
  inherited(aMainContext),
  mThread(aThread),
  mCallSite(aThread->mSrc.mPos.posId()) // from where in the source the context was created, which is just after the opening arg '(?
{
}


void BuiltinFunctionContext::setAbortCallback(SimpleCB aAbortCB)
{
  mAbortCB = aAbortCB;
}


void BuiltinFunctionContext::execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainedFromThread, ScriptObjPtr aThreadLocals, MLMicroSeconds aMaxRunTime)
{
  if (mUndefinedResult) {
    // just return undefined w/o even trying to execute
    mUndefinedResult = false;
    if (aEvaluationCB) aEvaluationCB(new AnnotatedNullValue("undefined argument caused undefined function result"));
    return;
  }
  mFunc = boost::dynamic_pointer_cast<BuiltinFunctionObj>(aToExecute);
  if (!mFunc || !mFunc->mDescriptor) {
    mFunc.reset();
    aEvaluationCB(new ErrorValue(ScriptError::Internal, "builtin function call inconsistency"));
  }
  else if ((aEvalFlags & synchronously) && (mFunc->mDescriptor->returnTypeInfo & async)) {
    aEvaluationCB(new ErrorValue(ScriptError::AsyncNotAllowed,
      "builtin function '%s' cannot be used in synchronous evaluation",
      mFunc->mDescriptor->name
    ));
  }
  else {
    mAbortCB = NoOP; // no abort callback so far, implementation must set one if it returns before finishing
    mEvaluationCB = aEvaluationCB;
    mFunc->mDescriptor->implementation(this);
  }
}

SourcePos::UniquePos BuiltinFunctionContext::argId(size_t aArgIndex) const
{
  if (aArgIndex<numArgs()) {
    // Note: as arguments in source occupy AT LEAST one separator, the following simplificaton
    // of just adding the argument index to the call site position is sufficient to provide a unique
    // ID for the argument at that call site
    return mCallSite+aArgIndex;
  }
  return NULL;
}


ScriptObjPtr BuiltinFunctionContext::arg(size_t aArgIndex)
{
  if (aArgIndex>=numIndexedMembers()) {
    // no such argument, return a null as the argument might be optional
    return new AnnotatedNullValue("optional function argument");
  }
  return memberAtIndex(aArgIndex);
}


bool BuiltinFunctionContext::abort(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult, ScriptCodeThreadPtr aExceptThread)
{
  if (mFunc) {
    if (mAbortCB) mAbortCB(); // stop external things the function call has started
    mAbortCB = NoOP;
    if (!aAbortResult) aAbortResult = new ErrorValue(ScriptError::Aborted, "builtin function '%s' aborted", mFunc->mDescriptor->name);
    mFunc = NULL;
    finish(aAbortResult);
    return true;
  }
  return false;
}


void BuiltinFunctionContext::finish(ScriptObjPtr aResult)
{
  mAbortCB = NoOP; // finished
  mFunc = NULL;
  if (mEvaluationCB) {
    EvaluationCB cb = mEvaluationCB;
    mEvaluationCB = NoOP;
    cb(aResult);
  }
}


// MARK: - SourcePos


SourcePos::SourcePos() :
  mPtr(NULL),
  mBol(NULL),
  mEot(NULL),
  mLine(0)
{
}


SourcePos::SourcePos(const string &aText) :
  mBot(aText.c_str()),
  mPtr(aText.c_str()),
  mBol(aText.c_str()),
  mEot(mPtr+aText.size()),
  mLine(0)
{
}


SourcePos::SourcePos(const SourcePos &aCursor) :
  mBot(aCursor.mBot),
  mPtr(aCursor.mPtr),
  mBol(aCursor.mBol),
  mEot(aCursor.mEot),
  mLine(aCursor.mLine)
{
}


// MARK: - SourceCursor


SourceCursor::SourceCursor(string aString, const char *aLabel) :
  mSourceContainer(new SourceContainer(aLabel ? aLabel : "hidden", NULL, aString)),
  mPos(mSourceContainer->mSource)
{
}


SourceCursor::SourceCursor(SourceContainerPtr aContainer) :
  mSourceContainer(aContainer),
  mPos(aContainer->mSource)
{
}


SourceCursor::SourceCursor(SourceContainerPtr aContainer, SourcePos aStart, SourcePos aEnd) :
  mSourceContainer(aContainer),
  mPos(aStart)
{
  assert(mPos.mPtr>=mSourceContainer->mSource.c_str() && mPos.mEot-mPos.mPtr<mSourceContainer->mSource.size());
  if(aEnd.mPtr>=mPos.mPtr && aEnd.mPtr<=mPos.mEot) mPos.mEot = aEnd.mPtr;
}


size_t SourceCursor::lineno() const
{
  return mPos.mLine;
}


size_t SourceCursor::charpos() const
{
  if (!mPos.mPtr || !mPos.mBol) return 0;
  return mPos.mPtr-mPos.mBol;
}


size_t SourceCursor::textpos() const
{
  if (!mPos.mPtr || !mPos.mBot) return 0;
  return mPos.mPtr-mPos.mBot;
}


bool SourceCursor::EOT() const
{
  return !mPos.mPtr || mPos.mPtr>=mPos.mEot || *mPos.mPtr==0;
}


bool SourceCursor::valid() const
{
  return mPos.mPtr!=NULL;
}


char SourceCursor::c(size_t aOffset) const
{
  if (!mPos.mPtr || mPos.mPtr+aOffset>=mPos.mEot) return 0;
  return *(mPos.mPtr+aOffset);
}


size_t SourceCursor::charsleft() const
{
  return mPos.mPtr ? mPos.mEot-mPos.mPtr : 0;
}


bool SourceCursor::next()
{
  if (EOT()) return false;
  if (*mPos.mPtr=='\n') {
    mPos.mLine++; // count line
    mPos.mBol = ++mPos.mPtr;
  }
  else {
    mPos.mPtr++;
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
  if (!mPos.mPtr) return;
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
        while(c()) {
          while (c() && c()!='*') next();
          if (c(1)=='/') {
            advance(2);
            break;
          }
          next();
        }
        recheck = true;
      }
    }
  } while(recheck);
}


string SourceCursor::displaycode(size_t aMaxLen) const
{
  return singleLine(mPos.mPtr, true, aMaxLen);
}


const char *SourceCursor::originLabel() const
{
  if (!mSourceContainer) return "<none>";
  if (!mSourceContainer->mOriginLabel) return "<unlabeled>";
  return mSourceContainer->mOriginLabel;
}


bool SourceCursor::parseIdentifier(string& aIdentifier, size_t* aIdentifierLenP)
{
  if (EOT()) return false;
  size_t o = 0; // offset
  if (!isalpha(c(o))) return false; // is not an identifier
  // is identifier
  o++;
  while (c(o) && (isalnum(c(o)) || c(o)=='_')) o++;
  aIdentifier.assign(mPos.mPtr, o);
  if (aIdentifierLenP) *aIdentifierLenP = o; // return length, keep cursor at beginning
  else mPos.mPtr += o; // advance
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
  if (strucmp(mPos.mPtr, aIdentifier, o)!=0) return false; // no match
  mPos.mPtr += o; // advance
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
  if (sscanf(mPos.mPtr, "%lf%n", &num, &o)!=1) {
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
        if (sscanf(mPos.mPtr+o+1, "%lf%n", &t, &i)!=1) {
          return new ErrorPosValue(*this, ScriptError::Syntax, "invalid time specification - use hh:mm or hh:mm:ss");
        }
        else {
          o += i+1; // past : and consumation of sscanf
          // we have v:t, take these as hours and minutes
          num = (num*60+t)*60; // in seconds
          if (c(o)==':') {
            // apparently we also have seconds
            if (sscanf(mPos.mPtr+o+1, "%lf%n", &t, &i)!=1) {
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
            if (strucmp(mPos.mPtr+o, monthNames[m], 3)==0) {
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
          if (sscanf(mPos.mPtr+o, "%d.%d.%n", &d, &m, &l)!=2) {
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
        if (sscanf(mPos.mPtr, "%02x", &h)==1) next();
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


#if P44SCRIPT_DEBUGGING_SUPPORT

bool SourceCursor::onBreakPoint() const
{
  return mSourceContainer->breakPointAt(mPos.posId())!=nullptr;
}

#endif // P44SCRIPT_DEBUGGING_SUPPORT


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
  json = JsonObject::objFromText(mPos.mPtr, charsleft(), &err, false, &n);
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
  mSkipping ? "SKIP" : "EXEC", \
  __func__, \
  mSrc.displaycode(25).c_str(), \
  ScriptObj::describe(mResult).c_str(), \
  ScriptObj::describe(mOlderResult).c_str(), \
  mPrecedence \
)

int SourceProcessor::cThreadIdGen = 0;

SourceProcessor::SourceProcessor() :
  mAborted(false),
  mResuming(false),
  mResumed(false),
  mEvaluationFlags(none),
  mCurrentState(NULL),
  mSkipping(false),
  mPrecedence(0),
  mPendingOperation(op_none)
{
  mThreadId = cThreadIdGen++; // unique thread ID
}


P44LoggingObj* SourceProcessor::loggingContext()
{
  return (mSrc.mSourceContainer ? mSrc.mSourceContainer->loggingContext() : NULL);
}


void SourceProcessor::setCursor(const SourceCursor& aCursor)
{
  mSrc = aCursor;
}


const SourceCursor& SourceProcessor::cursor() const
{
  return mSrc;
}



void SourceProcessor::initProcessing(EvaluationFlags aEvalFlags)
{
  mEvaluationFlags = aEvalFlags;
}


void SourceProcessor::setCompletedCB(EvaluationCB aCompletedCB)
{
  mCompletedCB = aCompletedCB;
}


const ScriptObjPtr SourceProcessor::currentResult() const
{
  return mResult;
}



void SourceProcessor::start()
{
  FOCUSLOGSTATE
  mStack.clear();
  // just scanning?
  mSkipping = (mEvaluationFlags&scanning)!=0;
  // scope to start in
  if (mEvaluationFlags & expression)
    setState(&SourceProcessor::s_expression);
  #if P44SCRIPT_FULL_SUPPORT
  else if (mEvaluationFlags & scriptbody)
    setState(&SourceProcessor::s_body);
  else if (mEvaluationFlags & sourcecode)
    setState(&SourceProcessor::s_declarations);
  else if (mEvaluationFlags & block)
    setState(&SourceProcessor::s_block);
  #endif // P44SCRIPT_FULL_SUPPORT
  else
    complete(new ErrorValue(ScriptError::Internal, "no processing scope defined"));
  push(&SourceProcessor::s_complete);
  mResult.reset();
  mOlderResult.reset();
  mResuming = false;
  resume();
}


void SourceProcessor::resume()
{
  // Am I getting called from a chain of calls originating from
  // myself via step() in the execution loop below?
  if (mResuming) {
    // YES: avoid creating an endless call chain recursively
    mResumed = true; // flag having resumed already to allow looping below
    return; // but now let chain of calls wind down to our last call (originating from step() in the loop)
  }
  // NO: this is a real re-entry
  if (mAborted) {
    complete(mResult);
    return;
  }
  // re-start the sync execution loop
  mResuming = true; // now actually start resuming
  stepLoop();
  // not resumed in the current chain of calls, resume will be called from
  // an independent call site later -> re-enable normal processing,
  // OR when debugging/singlestepping, by continuing execution from the debugger
  mResuming = false;
}


void SourceProcessor::resume(ScriptObjPtr aResult)
{
  // Store latest result, if any (resuming with NULL pointer does not change the result)
  if (aResult) {
    mResult = aResult;
  }
  resume();
}


void SourceProcessor::resumeAllowingNull(ScriptObjPtr aResultOrNull)
{
  mResult = aResultOrNull;
  resume();
}


void SourceProcessor::selfKeepingResume(ScriptCodeThreadPtr aThread, ScriptObjPtr aResumeResult)
{
  aThread->resume(aResumeResult);
}


void SourceProcessor::abort(ScriptObjPtr aAbortResult)
{
  FOCUSLOGSTATE
  if (aAbortResult) {
    mResult = aAbortResult;
  }
  mAborted = true; // signal end to resume() and stepLoop()
}


void SourceProcessor::complete(ScriptObjPtr aFinalResult)
{
  FOCUSLOGSTATE
  mResumed = false; // make sure stepLoop WILL exit when returning from step()
  mResult = aFinalResult; // set final result
  if (mResult && !mResult->isErr() && (mEvaluationFlags & expression)) {
    // expressions not returning an error should run to end
    mSrc.skipNonCode();
    if (!mSrc.EOT()) {
      mResult = new ErrorPosValue(mSrc, ScriptError::Syntax, "trailing garbage");
    }
  }
  if (!mResult) {
    mResult = new AnnotatedNullValue("execution produced no result");
  }
  mStack.clear(); // release all objects kept by the stack
  mCurrentState = NULL; // dead
  if (mCompletedCB) {
    EvaluationCB cb = mCompletedCB;
    mCompletedCB = NoOP;
    cb(mResult);
  }
}


void SourceProcessor::stepLoop()
{
  do {
    // run next statemachine step
    mResumed = false;
    step(); // will cause resumed to be set when resume() is called in this call's chain
    // repeat as long as we are already resumed
  } while(mResumed && !mAborted);
}


void SourceProcessor::step()
{
  if (!mCurrentState) {
    complete(mResult);
    return;
  }
  // call the state handler
  StateHandler sh = mCurrentState;
  (this->*sh)(); // call the handler, which will call resume() here or later
  // Info abour method pointers and their weird syntax:
  // - https://stackoverflow.com/a/1486279
  // - Also see: https://stackoverflow.com/a/6754821
}


// MARK: - source processor internal state machine

void SourceProcessor::checkAndResume()
{
  // simple result check
  if (mResult && mResult->isErr()) {
    // abort on errors
    complete(mResult);
    return;
  }
  resume();
}


void SourceProcessor::push(StateHandler aReturnToState, bool aPushPoppedPos)
{
  FOCUSLOG("                        push[%2lu] :                             result = %s", mStack.size()+1, ScriptObj::describe(mResult).c_str());
  mStack.push_back(StackFrame(aPushPoppedPos ? mPoppedPos : mSrc.mPos, mSkipping, aReturnToState, mResult, mFuncCallContext, mLoopController, mPrecedence, mPendingOperation));
}


void SourceProcessor::pop()
{
  if (mStack.size()==0) {
    complete(new ErrorValue(ScriptError::Internal, "stack empty - cannot pop"));
    return;
  }
  StackFrame &s = mStack.back();
  // these are just restored as before the push
  mSkipping = s.mSkipping;
  mPrecedence = s.mPrecedence;
  mPendingOperation = s.mPendingOperation;
  mFuncCallContext = s.mFuncCallContext;
  mLoopController = s.mLoopController;
  // these are restored separately, returnToState must decide what to do
  mPoppedPos = s.mPos;
  mOlderResult = s.mResult;
  FOCUSLOG("                         pop[%2lu] :                        olderResult = %s (result = %s)", mStack.size(), ScriptObj::describe(mOlderResult).c_str(), ScriptObj::describe(mResult).c_str());
  // continue here
  setState(s.mReturnToState);
  mStack.pop_back();
}


void SourceProcessor::popWithResult(bool aThrowErrors)
{
  FOCUSLOGSTATE;
  if (mSkipping || !mResult || mResult->actualValue() || mResult->hasType(lvalue)) {
    // no need for a validation step for loading lazy results or for empty lvalues
    popWithValidResult(aThrowErrors);
    return;
  }
  // make valid (pull value from lazy loading objects)
  setState(aThrowErrors ? &SourceProcessor::s_validResultCheck : &SourceProcessor::s_validResult);
  mResult->makeValid(boost::bind(&SourceProcessor::resume, this, _1));
}


void SourceProcessor::popWithValidResult(bool aThrowErrors)
{
  pop(); // get state to continue with
  if (mResult) {
    // try to get the actual value (in case what we have is an lvalue or similar proxy)
    ScriptObjPtr validResult = mResult->actualValue();
    // - replace original value only...
    if (
      validResult && (
        !mResult->hasType(keeporiginal|lvalue) || ( // ..if keeping not demanded and not lvalue
          mCurrentState!=&SourceProcessor::s_exprFirstTerm && // ..or receiver is neither first term...
          mCurrentState!=&SourceProcessor::s_funcArg && // ..nor function argument...
          mCurrentState!=&SourceProcessor::s_assignExpression // ..nor assignment
        )
      )
    ) {
      mResult = validResult; // replace original result with its actualValue()
    }
    if (mResult->isErr() && !mResult->cursor()) {
      // Errors should get position as near to the creation as possible (and not
      // later when thrown and pos is no longer valid!)
      mResult = new ErrorPosValue(mSrc, mResult);
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
  StackList::iterator spos = mStack.end();
  while (spos!=mStack.begin()) {
    --spos;
    if (spos->mReturnToState==aPreviousState) {
      // found discard everything on top
      mStack.erase(++spos, mStack.end());
      // now pop the seached state
      pop();
      return true;
    }
  }
  return false;
}


bool SourceProcessor::skipUntilReaching(StateHandler aPreviousState, ScriptObjPtr aThrowValue)
{
  StackList::iterator spos = mStack.end();
  while (spos!=mStack.begin()) {
    --spos;
    if (spos->mReturnToState==aPreviousState) {
      // found requested state, make it and all entries on top skipping
      if (aThrowValue) {
        spos->mResult = aThrowValue;
      }
      while (spos!=mStack.end()) {
        spos->mSkipping = true;
        ++spos;
      }
      // and also enter skip mode for current state
      mSkipping = true;
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
  throwOrComplete(new ErrorPosValue(mSrc, err));
}



void SourceProcessor::throwOrComplete(ErrorValuePtr aError)
{
  mResult = aError;
  ErrorPtr err = aError->errorValue();
  if (err->isDomain(ScriptError::domain()) && err->getErrorCode()>=ScriptError::FatalErrors) {
    // just end the thread unconditionally
    complete(aError);
    return;
  }
  else if (!mSkipping) {
    #if P44SCRIPT_FULL_SUPPORT
    if (!skipUntilReaching(&SourceProcessor::s_tryStatement, aError))
    #endif
    {
      complete(aError);
      return;
    }
  }
  // catch found (or skipping), continue executing there
  aError->setCaught(true); // error must not throw any more (caught or skipping)
  resume();
}


ScriptObjPtr SourceProcessor::captureCode(ScriptObjPtr aCodeContainer)
{
  CompiledCodePtr code = boost::dynamic_pointer_cast<CompiledCode>(aCodeContainer);
  if (!code) {
    return new ErrorPosValue(mSrc, ScriptError::Internal, "no compiled code");
  }
  else {
    if (mEvaluationFlags & ephemeralSource) {
      // copy from the original source
      SourceContainerPtr s = SourceContainerPtr(new SourceContainer(mSrc, mPoppedPos, mSrc.mPos));
      code->setCursor(s->getCursor());
    }
    else {
      // refer to the source code part that defines the function
      code->setCursor(SourceCursor(mSrc.mSourceContainer, mPoppedPos, mSrc.mPos));
    }
  }
  return code;
}


// MARK: Simple Terms

void SourceProcessor::s_simpleTerm()
{
  FOCUSLOGSTATE;
  // at the beginning of a simple term, result is undefined
  if (mSrc.c()=='"' || mSrc.c()=='\'') {
    mResult = mSrc.parseStringLiteral();
    popWithValidResult();
    return;
  }
  else if (mSrc.c()=='{') {
    // json or code block literal
    #if SCRIPTING_JSON_SUPPORT
    SourceCursor peek = mSrc;
    peek.next();
    peek.skipNonCode();
    if (peek.c()=='"' || peek.c()=='\'' || peek.c()=='}') {
      // empty "{}" or first thing within "{" is a quoted field name: must be JSON literal
      mResult = mSrc.parseJSONLiteral();
      popWithValidResult();
      return;
    }
    #endif
    // must be a code block
    mResult = mSrc.parseCodeLiteral();
    popWithValidResult();
    return;
  }
  #if SCRIPTING_JSON_SUPPORT
  else if (mSrc.c()=='[') {
    // must be JSON literal array
    mResult = mSrc.parseJSONLiteral();
    popWithValidResult();
    return;
  }
  #endif
  else {
    // identifier (variable, function) or numeric literal
    if (!mSrc.parseIdentifier(mIdentifier)) {
      // we can get here depending on how statement delimiters are used, so should not always try to parse a numeric...
      if (!mSrc.EOT() && mSrc.c()!='}' && mSrc.c()!=';') {
        // checking for statement separating chars is safe, there's no way one of these could appear at the beginning of a term
        mResult = mSrc.parseNumericLiteral();
      }
      // anyway, process current result (either it's a new number or the result already set and validated earlier
      popWithValidResult();
      return;
    }
    else {
      // identifier at script scope level
      mResult.reset(); // lookup from script scope
      mOlderResult.reset(); // represents the previous level in nested lookups
      mSrc.skipNonCode();
      if (mSkipping) {
        // we must always assume structured values etc.
        // Note: when skipping, we do NOT need to do complicated check for assignment.
        //   Syntactically, an assignment looks the same as a regular expression
        resumeAt(&SourceProcessor::s_member);
        return;
      }
      else {
        // if it is a plain identifier, it could be one of the built-in constants that cannot be overridden
        if (mSrc.c()!='(' && mSrc.c()!='.' && mSrc.c()!='[') {
          // - check them before doing an actual member lookup
          if (uequals(mIdentifier, "true") || uequals(mIdentifier, "yes")) {
            mResult = new BoolValue(true);
            popWithResult(false);
            return;
          }
          else if (uequals(mIdentifier, "false") || uequals(mIdentifier, "no")) {
            mResult = new BoolValue(false);
            popWithResult(false);
            return;
          }
          else if (uequals(mIdentifier, "null") || uequals(mIdentifier, "undefined")) {
            mResult = new AnnotatedNullValue(mIdentifier); // use literal as annotation
            popWithResult(false);
            return;
          }
        }
        else {
          // no need to check for assign when we already know a ./[/( follows
          assignOrAccess(none);
          return;
        }
        // need to look up the identifier, or assign it
        assignOrAccess(lvalue);
        return;
      }
    }
  }
}

// MARK: member access

void SourceProcessor::assignOrAccess(TypeInfo aAccessFlags)
{
  // left hand term leaf member accces
  // - identifier represents the leaf member to access
  // - result represents the parent member or NULL if accessing context scope variables
  // - precedence==0 means that this could be an lvalue term
  // Note: when skipping, we do NOT need to do complicated check for assignment.
  //   Syntactically, an assignment looks the same as a regular expression
  if (!mSkipping) {
    if (mPendingOperation==op_delete) {
      // COULD be deleting the member
      mSrc.skipNonCode();
      if (mSrc.c()!='.' && mSrc.c()!='[' && mSrc.c()!='(') {
        // this is the leaf member to be deleted. We need to obtain an lvalue and unset it
        setState(&SourceProcessor::s_unsetMember);
        memberByIdentifier(lvalue);
        return;
      }
    }
    else if ((aAccessFlags & lvalue) && mPrecedence==0) {
      // COULD be an assignment
      mSrc.skipNonCode();
      SourcePos opos = mSrc.mPos;
      ScriptOperator aop = mSrc.parseOperator();
      if (aop==op_assign || aop==op_assignOrEq) {
        // this IS an assignment. We need to obtain an lvalue and the right hand expression to assign
        push(&SourceProcessor::s_assignExpression);
        setState(&SourceProcessor::s_validResult);
        memberByIdentifier(aAccessFlags);
        return;
      }
      mSrc.mPos = opos; // back to before operator
    }
    // not an assignment, just request member value
    setState(&SourceProcessor::s_member);
    memberByIdentifier(mSrc.c()=='(' ? executable : none); // will lookup member of result, or global if result is NULL
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
  if (mSrc.nextIf('.')) {
    // direct sub-member access
    mSrc.skipNonCode();
    if (!mSrc.parseIdentifier(mIdentifier)) {
      exitWithSyntaxError("missing identifier after '.'");
      return;
    }
    // assign to this identifier or access its value (from parent object in result)
    mSrc.skipNonCode();
    assignOrAccess(lvalue|create); // try creating submembers
    return;
  }
  else if (mSrc.nextIf('[')) {
    // subscript access to sub-members
    mSrc.skipNonCode();
    if (mSrc.nextIf(']')) {
      // nothing in the subscript bracket
      mOlderResult = mResult; // the object the subscript applies to
      mResult.reset(); // NO subscript (not a NULL subscript)
      process_subscript(create);
      return;
    }
    push(&SourceProcessor::s_subscriptArg);
    resumeAt(&SourceProcessor::s_expression);
    return;
  }
  else if (mSrc.nextIf('(')) {
    // function call
    // Note: results of function calls remain candiates for assignments (e.g. `globalvars().subfield = 42`)
    mSrc.skipNonCode();
    // - we need a function call context
    setState(&SourceProcessor::s_funcContext);
    if (!mSkipping) {
      newFunctionCallContext();
      return;
    }
    resume();
    return;
  }
  // Leaf value
  memberEventCheck(); // detect and possibly register event sources
  popWithValidResult(false); // do not error-check or validate at this level, might be lvalue
  return;
}


void SourceProcessor::s_subscriptArg()
{
  FOCUSLOGSTATE;
  // immediately following a subscript argument evaluation
  // - result is the subscript,
  // - olderResult is the object the subscript applies to
  mSrc.skipNonCode();
  // determine how to proceed after accessing via subscript first...
  TypeInfo accessFlags = none; // subscript access is always local, no scope or assignment restrictions
  if (mSrc.nextIf(']')) {
    // end of subscript processing, what we'll be looking up below is final member (of this subscript bracket, more [] or . may follow!)
    setState(&SourceProcessor::s_member);
    // subscript members can generally be created
    accessFlags |= create;
  }
  else if (mSrc.nextIf(',')) {
    // more subscripts to apply to the member we'll be looking up below
    mSrc.skipNonCode();
    setState(&SourceProcessor::s_nextSubscript);
  }
  else {
    exitWithSyntaxError("missing , or ] after subscript");
    return;
  }
  process_subscript(accessFlags);
}


void SourceProcessor::process_subscript(TypeInfo aAccessFlags)
{
  if (mSkipping) {
    // no actual member access
    // Note: when skipping, we do NOT need to do complicated check for assignment.
    //   Syntactically, an assignment looks the same as a regular expression
    checkAndResume();
    return;
  }
  else {
    // now either get or assign the member indicated by the subscript
    ScriptObjPtr subscript = mResult;
    mResult = mOlderResult; // object to access member from
    if (mPendingOperation==op_delete) {
      // COULD be deleting the member
      mSrc.skipNonCode();
      if (mSrc.c()!='.' && mSrc.c()!='[') {
        // this is the leaf member to be deleted. We need to obtain an lvalue and unset it
        setState(&SourceProcessor::s_unsetMember);
        aAccessFlags |= lvalue; // we need an lvalue
      }
    }
    else if (mPrecedence==0) {
      // COULD be an assignment
      SourcePos opos = mSrc.mPos;
      ScriptOperator aop = mSrc.parseOperator();
      if (aop==op_assign || aop==op_assignOrEq) {
        // this IS an assignment. We need to obtain an lvalue and the right hand expression to assign
        push(&SourceProcessor::s_assignExpression);
        setState(&SourceProcessor::s_validResult);
        aAccessFlags |= lvalue; // we need an lvalue
      }
      else {
        // not an assignment, continue pocessing normally
        mSrc.mPos = opos; // back to before operator
      }
    }
    // now get member
    if (!subscript) {
      // no subscript specified (means appending to array)
      size_t nextIndex = mResult->numIndexedMembers(); // index of next to-be-added array element
      memberByIndex(nextIndex, aAccessFlags);
      return;
    }
    else if (subscript->hasType(numeric)) {
      // array access by index
      size_t index = subscript->int64Value();
      memberByIndex(index, aAccessFlags);
      return;
    }
    else {
      // member access by name
      mIdentifier = subscript->stringValue();
      memberByIdentifier(aAccessFlags);
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
  if (mSrc.nextIf(')')) {
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
  ScriptObjPtr arg = mResult;
  mResult = mOlderResult; // restore the function
  mSrc.skipNonCode();
  // determine how to proceed after pushing the argument...
  if (mSrc.nextIf(')')) {
    // end of argument processing, execute the function after pushing the final argument below
    setState(&SourceProcessor::s_funcExec);
  }
  else if (mSrc.nextIf(',')) {
    // more arguments follow, continue evaluating them after pushing the current argument below
    mSrc.skipNonCode();
    push(&SourceProcessor::s_funcArg);
    setState(&SourceProcessor::s_expression);
  }
  else {
    exitWithSyntaxError("missing , or ) after function argument");
    return;
  }
  // now apply the function argument
  if (mSkipping) {
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
  if (mSkipping) {
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
  mPrecedence = 0; // first lvalue can be assigned
  processExpression();
}

void SourceProcessor::s_expression()
{
  FOCUSLOGSTATE;
  mPrecedence = 1; // first left hand term is not assignable
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
  // - check for optional unary op
  mPendingOperation = mSrc.parseOperator(); // store for later
  if (mPendingOperation!=op_none && mPendingOperation!=op_subtract && mPendingOperation!=op_add && mPendingOperation!=op_not) {
    exitWithSyntaxError("invalid unary operator");
    return;
  }
  if (mPendingOperation!=op_none && mPrecedence==0) {
    mPrecedence = 1; // no longer a candidate for assignment
  }
  // evaluate first (or only) term
  // - check for paranthesis term
  if (mSrc.nextIf('(')) {
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
  if (!mSrc.nextIf(')')) {
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
  if (!mSkipping && mResult && mResult->defined()) {
    switch (mPendingOperation) {
      case op_not : mResult = new BoolValue(!mResult->boolValue()); break;
      case op_subtract : mResult = new NumericValue(-mResult->doubleValue()); break;
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
  SourcePos opos = mSrc.mPos; // position before possibly finding an operator and before skipping anything
  mSrc.skipNonCode();
  ScriptOperator binaryop = mSrc.parseOperator();
  int newPrecedence = binaryop & opmask_precedence;
  // end parsing here if no operator found or operator with a lower or same precedence as the passed in precedence is reached
  if (binaryop==op_none || newPrecedence<=mPrecedence) {
    mSrc.mPos = opos; // restore position
    popWithResult(false); // receiver of expression will still get an error, no automatic throwing here!
    return;
  }
  // must parse right side of operator as subexpression
  mPendingOperation = binaryop;
  push(&SourceProcessor::s_exprRightSide); // push the old precedence
  mPrecedence = newPrecedence; // subexpression needs to exit when finding an operator weaker than this one
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
  if (!mSkipping) {
    // assign a olderResult to the current result
    if (mResult && !mResult->hasType(lvalue)) {
      // not an lvalue, silently ignore assignment
      FOCUSLOG("   s_assignOlder: silently IGNORING assignment to non-lvalue : value=%s", ScriptObj::describe(mResult).c_str());
      setState(&SourceProcessor::s_result);
      resume();
      return;
    }
    ScriptObjPtr lvalue = mResult;
    mResult = mOlderResult;
    mOlderResult = lvalue;
  }
  // we don't want the result checked!
  s_assignLvalue();
}


void SourceProcessor::s_unsetMember()
{
  FOCUSLOGSTATE;
  // try to delete the current result
  if (!mSkipping) {
    mOlderResult = mResult;
    mResult.reset(); // no object means deleting
    if (!mOlderResult || !mOlderResult->hasType(lvalue)) {
      mResult = new AnnotatedNullValue("nothing to unset");
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
  if (!mSkipping) {
    if (mResult) mResult = mResult->assignmentValue(); // get a copy in case the value is mutable (i.e. copy-on-write, assignment is "writing")
    mOlderResult->assignLValue(boost::bind(&SourceProcessor::resume, this, _1), mResult);
    return;
  }
  resume();
}


void SourceProcessor::s_exprRightSide()
{
  FOCUSLOGSTATE;
  // olderResult = leftside, result = rightside
  if (!mSkipping) {
    // all operations involving nulls return null except equality which compares being null with not being null
    ScriptObjPtr left = mOlderResult->calculationValue();
    ScriptObjPtr right = mResult->calculationValue();
    if (mPendingOperation==op_equal || mPendingOperation==op_assignOrEq) {
      mResult = new BoolValue(*left == *right);
    }
    else if (mPendingOperation==op_notequal) {
      mResult = new BoolValue(*left != *right);
    }
    else if (left->defined() && right->defined()) {
      // both are values -> apply the operation between leftside and rightside
      switch (mPendingOperation) {
        case op_assign: {
          // unambiguous assignment operator is not allowed here (ambiguous = will be treated as comparison)
          if (!mSkipping) { exitWithSyntaxError("nested assigment not allowed"); return; }
          break;
        }
        case op_not: {
          exitWithSyntaxError("NOT operator not allowed here");
          return;
        }
        case op_divide:     mResult = *left / *right; break;
        case op_modulo:     mResult = *left % *right; break;
        case op_multiply:   mResult = *left * *right; break;
        case op_add:        mResult = *left + *right; break;
        case op_subtract:   mResult = *left - *right; break;
        // boolean result
        case op_less:       mResult = new BoolValue(*left <  *right); break;
        case op_greater:    mResult = new BoolValue(*left >  *right); break;
        case op_leq:        mResult = new BoolValue(*left <= *right); break;
        case op_geq:        mResult = new BoolValue(*left >= *right); break;
        case op_and:        mResult = new BoolValue(*left && *right); break;
        case op_or:         mResult = new BoolValue(*left || *right); break;
        default: break;
      }
    }
    else if (left->isErr()) {
      // if first is error, return that independently of what the second is
      mResult = left;
    }
    else if (!right->isErr()) {
      // one or both operands undefined, and none of them an error
      mResult = new AnnotatedNullValue("operation between undefined values");
    }
  }
  resumeAt(&SourceProcessor::s_exprLeftSide); // back to leftside, more chained operators might follow
}

#if P44SCRIPT_FULL_SUPPORT

// MARK: Declarations

void SourceProcessor::s_declarations()
{
  FOCUSLOGSTATE
  // skip empty statements, do not count these as start of body
  do {
    mSrc.skipNonCode();
  } while (mSrc.nextIf(';'));
  SourcePos declStart = mSrc.mPos;
  if (mSrc.parseIdentifier(mIdentifier)) {
    // explicitly or implicitly global declarations
    bool globvardef = false;
    bool globfuncdef = false;
    if (uequals(mIdentifier, "global")) {
      mSrc.skipNonCode();
      if (mSrc.checkForIdentifier("function")) {
        globfuncdef = true;
      }
      else {
        // must be variable declaration
        globvardef = true;
      }
    }
    if (globvardef || uequals(mIdentifier, "glob")) {
      // allow initialisation, even re-initialisation of global vars here!
      processVarDefs(lvalue|create|global, true, true);
      return;
    }
    if (globfuncdef || uequals(mIdentifier, "function")) {
      // Note: functions can be in declaration part (global) OR in running script code (if explicitly declared local)
      processFunction();
      return;
    } // function
    if (uequals(mIdentifier, "on")) {
      // Note: on handlers can be in declaration part OR in running script code
      processOnHandler();
      return;
    } // handler
  } // identifier
  // nothing recognizable as declaration
  mSrc.mPos = declStart; // rewind to beginning of last statement
  setState(&SourceProcessor::s_body);
  startOfBodyCode();
}


void SourceProcessor::processFunction()
{
  // function fname([param[,param...]]) { code }
  push(mCurrentState); // return to current state when function definition completes
  mSrc.skipNonCode();
  if (!mSrc.parseIdentifier(mIdentifier)) {
    exitWithSyntaxError("function name expected");
    return;
  }
  CompiledCodePtr function = CompiledCodePtr(new CompiledCode(mIdentifier));
  // optional argument list
  mSrc.skipNonCode();
  if (mSrc.nextIf('(')) {
    mSrc.skipNonCode();
    if (!mSrc.nextIf(')')) {
      do {
        mSrc.skipNonCode();
        if (mSrc.c()=='.' && mSrc.c(1)=='.' && mSrc.c(2)=='.') {
          // open argument list
          mSrc.advance(3);
          function->pushArgumentDefinition(any|null|error|multiple, "arg");
          break;
        }
        string argName;
        if (!mSrc.parseIdentifier(argName)) {
          exitWithSyntaxError("function argument name expected");
          return;
        }
        function->pushArgumentDefinition(any|null|error, argName);
        mSrc.skipNonCode();
      } while(mSrc.nextIf(','));
      if (!mSrc.nextIf(')')) {
        exitWithSyntaxError("missing closing ')' for argument list");
        return;
      }
    }
    mSrc.skipNonCode();
  }
  mResult = function;
  // now capture the code
  if (mSrc.c()!='{') {
    exitWithSyntaxError("expected function body");
    return;
  }
  push(&SourceProcessor::s_defineFunction); // with position on the opening '{' of the function body
  mSkipping = true;
  mSrc.next(); // skip the '{'
  resumeAt(&SourceProcessor::s_block);
}


void SourceProcessor::s_defineFunction()
{
  FOCUSLOGSTATE
  // after scanning a block containing a function body
  // - mPoppedPos points to the opening '{' of the body
  // - src.pos is after the closing '}' of the body
  // - olderResult is the CompiledFunction
  if (!compiling() || declaring()) {
    setState(&SourceProcessor::s_declarations); // back to declarations
    mResult = captureCode(mOlderResult);
    storeFunction();
  }
  else {
    checkAndResume();
  }
  // back to where we were before
  pop();
}


void SourceProcessor::processOnHandler()
{
  // on (triggerexpression) [toggling|changing|evaluating] { code }
  push(mCurrentState); // return to current state when handler definition completes
  mSrc.skipNonCode();
  if (!mSrc.nextIf('(')) {
    exitWithSyntaxError("'(' expected");
    return;
  }
  push(&SourceProcessor::s_defineTrigger);
  mSkipping = true;
  resumeAt(&SourceProcessor::s_expression);
}


void SourceProcessor::s_defineTrigger()
{
  FOCUSLOGSTATE
  // on (triggerexpression) [changing|toggling|evaluating|gettingtrue] [ stable <stabilizing time numeric literal>] [ as triggerresult ] { handlercode }
  // after scanning the trigger condition expression of a on() statement
  // - mPoppedPos points to the beginning of the expression
  // - src.pos should be on the ')' of the trigger expression
  if (mSrc.c()!=')') {
    exitWithSyntaxError("')' as end of trigger expression expected");
    return;
  }
  CompiledTriggerPtr trigger;
  if (!compiling() || declaring()) {
    trigger = new CompiledTrigger("trigger", getTriggerAndHandlerMainContext());
    mResult = captureCode(trigger);
  }
  mSrc.next(); // skip ')'
  mSrc.skipNonCode();
  // optional trigger mode
  TriggerMode mode = inactive;
  MLMicroSeconds holdOff = Never;
  bool hasid = mSrc.parseIdentifier(mIdentifier);
  if (hasid) {
    if (uequals(mIdentifier, "changing")) {
      mode = onChange;
    }
    else if (uequals(mIdentifier, "toggling")) {
      mode = onChangingBool;
    }
    else if (uequals(mIdentifier, "evaluating")) {
      mode = onEvaluation;
    }
    else if (uequals(mIdentifier, "gettingtrue")) {
      mode = onGettingTrue;
    }
  }
  if (mode==inactive) {
    // no explicit mode, default to onGettingTrue
    mode = onGettingTrue;
  }
  else {
    mSrc.skipNonCode();
    hasid = mSrc.parseIdentifier(mIdentifier);
  }
  if (hasid) {
    if (uequals(mIdentifier, "stable")) {
      mSrc.skipNonCode();
      ScriptObjPtr h = mSrc.parseNumericLiteral();
      if (h->isErr()) {
        complete(h);
        return;
      }
      holdOff = h->doubleValue()*Second;
      mSrc.skipNonCode();
      hasid = mSrc.parseIdentifier(mIdentifier);
    }
  }
  if (hasid) {
    if (uequals(mIdentifier, "as")) {
      mSrc.skipNonCode();
      if (!mSrc.parseIdentifier(mIdentifier)) {
        exitWithSyntaxError("missing trigger result variable name");
        return;
      }
      if (trigger) trigger->mResultVarName = mIdentifier;
    }
    else {
      exitWithSyntaxError("missing trigger mode or 'as'");
      return;
    }
  }
  if (trigger) trigger->setTriggerMode(mode, holdOff);
  mSrc.skipNonCode();
  // check for beginning of handler body
  if (mSrc.c()!='{') {
    exitWithSyntaxError("expected handler body");
    return;
  }
  push(&SourceProcessor::s_defineHandler); // with position on the opening '{' of the handler body
  mSkipping = true;
  mSrc.next(); // skip the '{'
  resumeAt(&SourceProcessor::s_block);
  return;
}


void SourceProcessor::s_defineHandler()
{
  FOCUSLOGSTATE
  // after scanning a block containing a handler body
  // - mPoppedPos points to the opening '{' of the body
  // - src.pos is after the closing '}' of the body
  // - olderResult is the trigger, mode already set
  if (!compiling() || declaring()) {
    CompiledHandlerPtr handler = new CompiledHandler("handler", getTriggerAndHandlerMainContext());
    mResult = captureCode(handler); // get the code first, so we can execute it in the trigger init
    handler->installAndInitializeTrigger(mOlderResult);
    storeHandler();
  }
  else {
    checkAndResume();
  }
  // back to where we were before
  pop();
}

// MARK: Statements

void SourceProcessor::s_noStatement()
{
  FOCUSLOGSTATE
  mSrc.nextIf(';');
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
  FOCUSLOG("\n========== At statement boundary : %s", mSrc.displaycode(130).c_str());
  mSrc.skipNonCode();
  if (mSrc.EOT()) {
    // end of code
    if (mCurrentState!=&SourceProcessor::s_body) {
      exitWithSyntaxError("unexpected end of code");
      return;
    }
    // complete
    complete(mResult);
    return;
  }
  // beginning of a new statement
  #if P44SCRIPT_DEBUGGING_SUPPORT
  if (pauseCheck(step_over)) {
    // single stepping statements, thread is paused, debugger needs to call resume() to continue
    return;
  }
  #endif
  if (mSrc.nextIf('{')) {
    // new block starts
    push(mCurrentState); // return to current state when block finishes
    resumeAt(&SourceProcessor::s_block); // continue as block
    return;
  }
  if (mSrc.nextIf('}')) {
    // block ends
    if (mCurrentState==&SourceProcessor::s_block) {
      // actually IS a block
      pop();
      checkAndResume();
      return;
    }
    exitWithSyntaxError("unexpected '}'");
    return;
  }
  if (mSrc.nextIf(';')) {
    if (mCurrentState==&SourceProcessor::s_oneStatement) {
      // the separator alone comprises the statement we were waiting for in s_oneStatement(), so we're done
      checkAndResume();
      return;
    }
    mSrc.skipNonCode();
  }
  // at the beginning of a statement which is not beginning of a new block
  mResult.reset(); // no result to begin with at the beginning of a statement. Important for if/else, try/catch!
  // - could be language keyword, variable assignment
  SourcePos memPos = mSrc.mPos; // remember
  if (mSrc.parseIdentifier(mIdentifier)) {
    mSrc.skipNonCode();
    // execution statements
    if (uequals(mIdentifier, "if")) {
      // "if" statement
      if (!mSrc.nextIf('(')) {
        exitWithSyntaxError("missing '(' after 'if'");
        return;
      }
      push(mCurrentState); // return to current state when if statement finishes
      push(&SourceProcessor::s_ifCondition);
      resumeAt(&SourceProcessor::s_expression);
      return;
    }
    if (uequals(mIdentifier, "foreach")) {
      // Syntax: foreach container as member {}
      //     or: foreach container as key,member {}
      push(mCurrentState); // return to current state when foreach finishes
      push(&SourceProcessor::s_foreachTarget);
      resumeAt(&SourceProcessor::s_expression);
      return;
    }
    if (uequals(mIdentifier, "for")) {
      // TODO: implement using a fully scripted iterator
      exitWithSyntaxError("for not yet implemented");
    }
    if (uequals(mIdentifier, "while")) {
      // "while" statement
      // TODO: refactor to use check-only scripted iterator
      if (!mSrc.nextIf('(')) {
        exitWithSyntaxError("missing '(' after 'while'");
        return;
      }
      push(mCurrentState); // return to current state when while finishes
      push(&SourceProcessor::s_whileCondition);
      resumeAt(&SourceProcessor::s_expression);
      return;
    }
    if (uequals(mIdentifier, "break")) {
      if (!mSkipping) {
        if (!skipUntilReaching(&SourceProcessor::s_whileStatement)) {
          exitWithSyntaxError("'break' must be within 'while', 'for' or 'foreach' statement");
          return;
        }
        checkAndResume();
        return;
      }
    }
    if (uequals(mIdentifier, "continue")) {
      if (!mSkipping) {
        if (!unWindStackTo(&SourceProcessor::s_whileStatement)) {
          exitWithSyntaxError("'continue' must be within 'while', 'for' or 'foreach' statement");
          return;
        }
        checkAndResume();
        return;
      }
    }
    if (uequals(mIdentifier, "return")) {
      if (!mSrc.EOT() && mSrc.c()!=';') {
        // return with return value
        if (mSkipping) {
          // we must parse over the return expression properly AND then continue parsing
          push(mCurrentState); // return to current state when return expression is parsed
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
        if (!mSkipping) {
          mResult = new AnnotatedNullValue("return nothing");
          complete(mResult);
          return;
        }
        checkAndResume(); // skipping -> just ignore
        return;
      }
    }
    if (uequals(mIdentifier, "try")) {
      push(mCurrentState); // return to current state when statement finishes
      push(&SourceProcessor::s_tryStatement);
      resumeAt(&SourceProcessor::s_oneStatement);
      return;
    }
    if (uequals(mIdentifier, "catch")) {
      // just check to give sensible error message
      exitWithSyntaxError("'catch' without preceeding 'try'");
      return;
    }
    if (uequals(mIdentifier, "concurrent")) {
      // Syntax: concurrent as myThread {}
      //     or: concurrent {}
      mSrc.skipNonCode();
      mIdentifier.clear();
      if (mSrc.checkForIdentifier("as")) {
        mSrc.skipNonCode();
        if (mSrc.parseIdentifier(mIdentifier)) {
          // we want the thread be a variable in order to wait for it and stop it
          mSrc.skipNonCode();
        }
      }
      if (!mSrc.nextIf('{')) {
        exitWithSyntaxError("missing '{' to start concurrent block");
        return;
      }
      push(mCurrentState); // return to current state when statement finishes
      setState(&SourceProcessor::s_block);
      // "fork" the thread
      if (!mSkipping) {
        mSkipping = true; // for myself: just skip the next block
        startBlockThreadAndStoreInIdentifier(); // includes resume()
        return;
      }
      checkAndResume(); // skipping, no actual fork
      return;
    }
    // Check variable definition keywords
    if (uequals(mIdentifier, "var")) {
      processVarDefs(lvalue+create, true);
      return;
    }
    if (uequals(mIdentifier, "threadvar")) {
      processVarDefs(lvalue+create+threadlocal, true);
      return;
    }
    bool globvar = false;
    if (uequals(mIdentifier, "global")) {
      mSrc.skipNonCode();
      if (mSrc.checkForIdentifier("function")) {
        exitWithSyntaxError("global function declarations must be made before first script statement");
        return;
      }
      globvar = true;
    }
    if (globvar || uequals(mIdentifier, "glob")) {
      processVarDefs(lvalue+create+onlycreate+global, false);
      return;
    }
    if (uequals(mIdentifier, "let")) {
      processVarDefs(lvalue, true);
      return;
    }
    if (uequals(mIdentifier, "unset")) {
      processVarDefs(unset, false);
      return;
    }
    // check local function definition
    if (uequals(mIdentifier, "local")) {
      mSrc.skipNonCode();
      if (mSrc.checkForIdentifier("function")) {
        processFunction();
        return;
      }
      exitWithSyntaxError("missing 'function' keyword");
      return;
    }
    // check handler definition within script code (needed when trigger expression wants to refer to run-time created objects)
    if (uequals(mIdentifier, "on")) {
      // Note: on handlers can be in declaration part OR in running script code
      processOnHandler();
      return;
    }
    // just check to give sensible error message
    if (uequals(mIdentifier, "else")) {
      exitWithSyntaxError("'else' without preceeding 'if'");
      return;
    }
    if (uequals(mIdentifier, "function")) {
      exitWithSyntaxError("global function declarations must be made before first script statement");
      return;
    }
    // identifier we've parsed above is not a keyword, rewind cursor
    mSrc.mPos = memPos;
  }
  // is an expression or possibly an assignment, also handled in expression
  push(mCurrentState); // return to current state when expression evaluation and result checking completes
  push(&SourceProcessor::s_result); // but check result of statement level expressions first
  resumeAt(&SourceProcessor::s_assignmentExpression);
  return;
}


void SourceProcessor::processVarDefs(TypeInfo aVarFlags, bool aAllowInitializer, bool aDeclaration)
{
  mSrc.skipNonCode();
  // one of the variable definition keywords -> an identifier must follow
  if (!mSrc.parseIdentifier(mIdentifier)) {
    exitWithSyntaxError("missing variable name after '%s'", mIdentifier.c_str());
    return;
  }
  push(mCurrentState); // return to current state when var definion / unset statement finishes
  if (aVarFlags & unset) {
    // unset is special because it might address a subfield of a variable,
    // so it is modelled like a prefix operator
    mPendingOperation = op_delete;
    assignOrAccess(none);
    return;
  }
  else if ((aVarFlags & create)==0) {
    // not really a vardef, but just a "let"
    assignOrAccess(lvalue);
    return;
  }
  if (aDeclaration) mSkipping = false; // must enable processing now for actually assigning globals.
  mSrc.skipNonCode();
  ScriptOperator op = mSrc.parseOperator();
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
    // just create and initialize with null (if not already existing)
    if (aVarFlags & global) {
      mResult = new EventPlaceholderNullValue("uninitialized global");
    }
    else {
      mResult = new AnnotatedNullValue("uninitialized variable");
    }
    push(&SourceProcessor::s_assignOlder);
    setState(&SourceProcessor::s_nothrowResult);
    mResult.reset(); // look up on context level
    memberByIdentifier(aVarFlags); // lookup lvalue
    return;
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
  if (!mSrc.nextIf(')')) {
    exitWithSyntaxError("missing ')' after 'if' condition");
    return;
  }
  if (!mSkipping) {
    // a real if decision
    mSkipping = !mResult->boolValue();
    if (!mSkipping) mResult.reset(); // any executed if branch must cause skipping all following else branches
  }
  else {
    // nothing to decide any more
    mResult.reset();
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
  mSrc.skipNonCode();
  if (mSrc.checkForIdentifier("else")) {
    // else
    mSkipping = mOlderResult==NULL;
    mSrc.skipNonCode();
    if (mSrc.checkForIdentifier("if")) {
      // else if
      mSrc.skipNonCode();
      if (!mSrc.nextIf('(')) {
        exitWithSyntaxError("missing '(' after 'else if'");
        return;
      }
      // chained if: when preceeding "if" did execute (or would have if not already skipping),
      // rest of if/elseif...else chain will be skipped
      // Note: pushed skipping AND result is needed by s_ifCondition to determine further flow
      mResult = mOlderResult; // carry on the "entire if/elseif/else statement executing" marker
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




void SourceProcessor::s_foreachTarget()
{
  FOCUSLOGSTATE
  // foreach target expression is evaluated in result
  // - result contains target object to be iterated
  if (!mSrc.checkForIdentifier("as")) {
    exitWithSyntaxError("missing 'as' in 'foreach'");
    return;
  }
  // capture loop variable(s)
  mSrc.skipNonCode();
  if (!mSrc.parseIdentifier(mIdentifier)) {
    exitWithSyntaxError("missing variable name after 'as'");
    return;
  }
  setState(&SourceProcessor::s_foreachLoopVar1);
  if (!mSkipping) {
    mLoopController = new LoopController;
    mLoopController->mIterator = mResult->newIterator();
    mResult.reset(); // create error variable on scope level
    memberByIdentifier(lvalue+create);
    return;
  }
  checkAndResume();
  return;
}

void SourceProcessor::s_foreachLoopVar1()
{
  mSrc.skipNonCode();
  if (!mSrc.nextIf(',')) {
    // only value, we're done
    if (!mSkipping) mLoopController->mLoopValue = boost::dynamic_pointer_cast<ScriptLValue>(mResult); // first lvalue will receive value
    checkAndResumeAt(&SourceProcessor::s_foreachLoopStart);
    return;
  }
  // get second identifier
  mSrc.skipNonCode();
  if (!mSrc.parseIdentifier(mIdentifier)) {
    exitWithSyntaxError("missing value variable name after 'as key,'");
    return;
  }
  setState(&SourceProcessor::s_foreachLoopVars);
  if (!mSkipping) {
    mLoopController->mLoopKey = boost::dynamic_pointer_cast<ScriptLValue>(mResult); // first lvalue will receive key
    mResult.reset(); // create error variable on scope level
    memberByIdentifier(lvalue+create);
    return;
  }
  checkAndResume();
}


void SourceProcessor::s_foreachLoopVars()
{
  if (!mSkipping) mLoopController->mLoopValue = boost::dynamic_pointer_cast<ScriptLValue>(mResult); // this lvalue will receive value
  checkAndResumeAt(&SourceProcessor::s_foreachLoopStart);
}


void SourceProcessor::s_foreachLoopStart()
{
  // head parsed, this is where we need to return to in the loop
  if (mSkipping) {
    // no need to get loop vars
    s_foreachBody();
    return;
  }
  // reset iterator, obtain value
  mLoopController->mIterator->reset();
  resumeAt(&SourceProcessor::s_foreachLoopIteration);
}

void SourceProcessor::s_foreachLoopIteration()
{
  FOCUSLOGSTATE
  setState(&SourceProcessor::s_foreachValue);
  mLoopController->mIterator->obtainValue(boost::bind(&SourceProcessor::resumeAllowingNull, this, _1), none);
}


void SourceProcessor::s_foreachValue()
{
  FOCUSLOGSTATE
  if (mResult) {
    // there is a value, loop continues
    setState(mLoopController->mLoopKey ?  &SourceProcessor::s_foreachKeyNeeded : &SourceProcessor::s_foreachBody);
    mLoopController->mLoopValue->assignLValue(boost::bind(&SourceProcessor::resume, this, _1), mResult);
    return;
  }
  // no loop value -> done with loop
  mSkipping = true;
  s_foreachBody();
}


void SourceProcessor::s_foreachKeyNeeded()
{
  setState(&SourceProcessor::s_foreachKey);
  mLoopController->mIterator->obtainKey(boost::bind(&SourceProcessor::resume, this, _1), false);
}


void SourceProcessor::s_foreachKey()
{
  setState(&SourceProcessor::s_foreachBody);
  mLoopController->mLoopKey->assignLValue(boost::bind(&SourceProcessor::resume, this, _1), mResult);
}


void SourceProcessor::s_foreachBody()
{
  FOCUSLOGSTATE
  // mSkipping and loop variables are set, now run the body
  push(&SourceProcessor::s_foreachStatement, false); // beginning of loop statement (block) = position we'll have as mPoppedPos at s_foreachStatement
  checkAndResumeAt(&SourceProcessor::s_oneStatement);
}


void SourceProcessor::s_foreachStatement()
{
  FOCUSLOGSTATE
  // foreach statement (or block of statements) is executed
  // - mPoppedPos points to beginning of foreach body statement
  if (mSkipping) {
    // skipping because iterator exhausted or "break" set skipping in the stack with skipUntilReaching()
    pop(); // end foreach
    checkAndResume();
    return;
  }
  // not skipping, means we need to advance the iterator and possibly loop back
  mLoopController->mIterator->next();
  // TODO: optimize to avoid skip run at the end
  // go back to loop beginning
  mSrc.mPos = mPoppedPos;
  resumeAt(&SourceProcessor::s_foreachLoopIteration);
}


void SourceProcessor::s_whileCondition()
{
  FOCUSLOGSTATE
  // while condition is evaluated
  // - result contains result of the evaluation
  // - mPoppedPos points to beginning of while condition
  if (!mSrc.nextIf(')')) {
    exitWithSyntaxError("missing ')' after 'while' condition");
    return;
  }
  if (!mSkipping) mSkipping = !mResult->boolValue(); // set now, because following push must include "skipping" according to the decision!
  push(&SourceProcessor::s_whileStatement, true); // push poppedPos (again) = loopback position we'll need at s_whileStatement
  checkAndResumeAt(&SourceProcessor::s_oneStatement);
}

void SourceProcessor::s_whileStatement()
{
  FOCUSLOGSTATE
  // while statement (or block of statements) is executed
  // - mPoppedPos points to beginning of while condition
  if (mSkipping) {
    // skipping because condition was false or "break" set skipping in the stack with skipUntilReaching()
    pop(); // end while
    checkAndResume();
    return;
  }
  // not skipping, means we need to loop back to the condition
  mSrc.mPos = mPoppedPos;
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
  mSrc.skipNonCode();
  if (mSrc.checkForIdentifier("catch")) {
    // if olderResult is an error, we must catch it. Otherwise skip the catch statement.
    // Note: olderResult can be the try statement's regular result at this point
    mSkipping = !mOlderResult || !mOlderResult->isErr();
    // catch can set the error into a local var
    mSrc.skipNonCode();
    // run (or skip) what follows as one statement
    setState(&SourceProcessor::s_oneStatement);
    // check for error capturing variable
    if (mSrc.checkForIdentifier("as")) {
      mSrc.skipNonCode();
      if (!mSrc.parseIdentifier(mIdentifier)) {
        exitWithSyntaxError("missing error variable name after 'as'");
        return;
      }
      if (!mSkipping) {
        mResult = mOlderResult; // the error value
        push(mCurrentState); // want to return here
        push(&SourceProcessor::s_assignOlder); // push the error value
        setState(&SourceProcessor::s_nothrowResult);
        mResult.reset(); // create error variable on scope level
        memberByIdentifier(lvalue+create+threadlocal);
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

#endif // P44SCRIPT_FULL_SUPPORT


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
  complete(mResult);
}



// MARK: source processor execution hooks

void SourceProcessor::memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError)
{
  mResult.reset(); // base class cannot access members
  checkAndResume();
}


void SourceProcessor::memberByIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  mResult.reset(); // base class cannot access members
  checkAndResume();
}


void SourceProcessor::newFunctionCallContext()
{
  mResult.reset(); // base class cannot execute functions
  checkAndResume();
}

#if P44SCRIPT_FULL_SUPPORT

void SourceProcessor::startBlockThreadAndStoreInIdentifier()
{
  /* NOP */
  checkAndResume();
}

void SourceProcessor::storeHandler()
{
  checkAndResume(); // NOP on the base class level
}

void SourceProcessor::storeFunction()
{
  checkAndResume(); // NOP on the base class level
}

#endif // P44SCRIPT_FULL_SUPPORT

void SourceProcessor::pushFunctionArgument(ScriptObjPtr aArgument)
{
  checkAndResume(); // NOP on the base class level
}

#if P44SCRIPT_FULL_SUPPORT
void SourceProcessor::startOfBodyCode()
{
  // switch to body scanning
  mEvaluationFlags = (mEvaluationFlags & ~sourcecode) | scriptbody;
  checkAndResume(); // NOP on the base class level
}
#endif


void SourceProcessor::executeResult()
{
  mResult.reset(); // base class cannot evaluate
  checkAndResume();
}


void SourceProcessor::memberEventCheck()
{
  /* NOP here */
}


// MARK: - CompiledCode


void CompiledCode::setCursor(const SourceCursor& aCursor)
{
  mCursor = aCursor;
  FOCUSLOG("New code named '%s' @ 0x%p: %s", mName.c_str(), this, mCursor.displaycode(70).c_str());
}


bool CompiledCode::codeFromSameSourceAs(const CompiledCode &aCode) const
{
  return mCursor.refersTo(aCode.mCursor.mSourceContainer) && mCursor.mPos.posId()==aCode.mCursor.mPos.posId();
}


CompiledCode::~CompiledCode()
{
  FOCUSLOG("Released code named '%s' @ 0x%p", mName.c_str(), this);
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
  mArguments.push_back(arg);
}


bool CompiledCode::argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const
{
  size_t idx = aIndex;
  if (idx>=mArguments.size()) {
    // no argument with this index, check for open argument list
    if (mArguments.size()<1) return false;
    idx = mArguments.size()-1;
    if ((mArguments[idx].typeInfo & multiple)==0) return false;
  }
  aArgDesc = mArguments[idx];
  if (aArgDesc.typeInfo & multiple) {
    aArgDesc.name = string_format("%s%zu", mArguments[idx].name.c_str(), aIndex+1);
  }
  return true;
}


// MARK: - CompiledScript


void CompiledScript::deactivate()
{
  // abort all threads that are running any of my code
  if (mMainContext) {
    ScriptMainContextPtr mc = mMainContext;
    mMainContext.reset();
    mc->abortThreadsRunningSource(mCursor.mSourceContainer, new ErrorValue(ScriptError::Aborted, "deactivated"));
  }
  inherited::deactivate();
}



ExecutionContextPtr CompiledScript::contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const
{
  // compiled script bodies get their execution context assigned at compile time, just return it
  // - but maincontext passed should be the domain of our saved mainContext, so check that if aMainContext is passed
  if (aMainContext) {
    // Normal case during script execution
    if (mMainContext->domain().get()!=aMainContext.get()) {
      LOG(LOG_ERR, "internal error: script domain mismatch");
      return NULL; // mismatch, cannot use that context!
    }
  }
  return mMainContext;
}


// MARK: - Trigger


CompiledTrigger::CompiledTrigger(const string aName, ScriptMainContextPtr aMainContext) :
  inherited(aName, aMainContext),
  mTriggerMode(inactive),
  mBoolState(p44::undefined),
  mEvalFlags(expression|synchronously),
  mNextEvaluation(Never),
  mMostRecentEvaluation(Never),
  mFrozenEventPos(0),
  mOneShotEval(false),
  mMetAt(Never),
  mHoldOff(0)
{
}


void CompiledTrigger::deactivate()
{
  // reset everything that could be part of a retain cycle
  setTriggerCB(NoOP);
  mReEvaluationTicket.cancel();
  mFrozenResults.clear();
  mCurrentResult.reset();
  clearSources();
  inherited::deactivate();
}


ScriptObjPtr CompiledTrigger::initializeTrigger()
{
  // initialize it
  FOCUSLOG("\n---------- Initializing Trigger  : %s", mCursor.displaycode(130).c_str());
  mReEvaluationTicket.cancel();
  mNextEvaluation = Never; // reset
  mMostRecentEvaluation = MainLoop::now();
  mFrozenResults.clear(); // (re)initializing trigger unfreezes all values
  clearSources(); // forget all event sources
  ExecutionContextPtr ctx = contextForCallingFrom(NULL, NULL);
  if (!ctx) return  new ErrorValue(ScriptError::Internal, "no context for trigger");
  EvaluationFlags initFlags = (mEvalFlags&~runModeMask)|initial|keepvars; // need to keep vars as trigger might refer to them
  OLOG(LOG_INFO, "initial trigger evaluation: %s", mCursor.displaycode(130).c_str());
  if (mEvalFlags & synchronously) {
    #if DEBUGLOGGING
    ScriptObjPtr res = ctx->executeSynchronously(this, initFlags, ScriptObjPtr(), Infinite);
    #else
    ScriptObjPtr res = ctx->executeSynchronously(this, initFlags, ScriptObjPtr(), 2*Second);
    #endif
    triggerDidEvaluate(initFlags, res);
    return res;
  }
  else {
    triggerEvaluation(initFlags);
    return new AnnotatedNullValue("asynchonously initializing trigger");
  }
}


Tristate CompiledTrigger::boolState(bool aIgnoreHoldoff)
{
  if (aIgnoreHoldoff || mMetAt==Never) return mBoolState;
  return p44::undefined;
}


void CompiledTrigger::invalidateState()
{
  // reset completely unknown state
  mBoolState = p44::undefined;
  mCurrentResult.reset();
}


void CompiledTrigger::processEvent(ScriptObjPtr aEvent, EventSource &aSource, intptr_t aRegId)
{
  // If aRegId is 0 here, this means that the event source is not a oneshot source, such as a ValueSource.
  // Such sources may trigger evaluation, but the trigger evaluation will be stateful and will re-read the
  // values while processing the trigger expression (not using a frozen the oneshot value)
  if (aRegId!=0) {
    // event was registered for a one-shot value
    // Note: the aEvent that is delivered here might be a completely regular value, neither an event
    //   source nor being of type oneshot!
    mFrozenEventPos = (SourcePos::UniquePos)aRegId;
    mFrozenEventValue = aEvent;
  }
  triggerEvaluation(triggered);
}



void CompiledTrigger::triggerEvaluation(EvaluationFlags aEvalMode)
{
  FOCUSLOG("\n---------- Evaluating Trigger    : %s", mCursor.displaycode(130).c_str());
  mReEvaluationTicket.cancel();
  mNextEvaluation = Never; // reset
  mMostRecentEvaluation = MainLoop::now();
  mOneShotEval = false; // no oneshot encountered yet. Evaluation will set it via checkFrozenEventValue(), which is called for every leaf value (frozen or not)
  ExecutionContextPtr ctx = contextForCallingFrom(NULL, NULL);
  EvaluationFlags runFlags = ((aEvalMode&~runModeMask) ? aEvalMode : (mEvalFlags&~runModeMask)|aEvalMode)|keepvars; // always keep vars, use only runmode from aEvalMode if nothing else is set
  ctx->execute(ScriptObjPtr(this), runFlags, boost::bind(&CompiledTrigger::triggerDidEvaluate, this, runFlags, _1), nullptr, ScriptObjPtr(), 30*Second);
}


void CompiledTrigger::triggerDidEvaluate(EvaluationFlags aEvalMode, ScriptObjPtr aResult)
{
  OLOG(aEvalMode&initial ? LOG_INFO : LOG_DEBUG, "%s: evaluated: %s in evalmode=0x%x\n- with result: %s%s", getIdentifier().c_str(), mCursor.displaycode(90).c_str(), aEvalMode, mOneShotEval ? "(ONESHOT) " : "", ScriptObj::describe(aResult).c_str());
  bool doTrigger = false;
  Tristate newBoolState = aResult->defined() ? (aResult->boolValue() ? p44::yes : p44::no) : p44::undefined;
  if (mTriggerMode==onEvaluation) {
    doTrigger = true;
  }
  else if (mTriggerMode==onChange) {
    doTrigger = (*aResult) != *currentResult();
  }
  else {
    // bool modes (onGettingTrue, onChangingBool, onChangingBoolRisingHoldoffOnly)
    doTrigger = mBoolState!=newBoolState;
    if (doTrigger) {
      // bool state of trigger expression evaluation has changed
      if (newBoolState!=yes) {
        // trigger expression result has become false (or invalid)
        if (mTriggerMode==onGettingTrue) {
          // do NOT report trigger expression becoming non-true
          doTrigger = false;
        }
      }
      // A change of the trigger expression boolean result always terminates a waiting holdoff,
      // no matter if we are in onGettingTrue or onChangingBool(RisingHoldoffOnly) mode.
      if (mMetAt!=Never) {
        // we are waiting for a holdoff before firing the trigger with the current mBoolState -> cancel and report nothing
        OLOG(LOG_INFO, "%s: condition no longer met within holdoff period of %.2f seconds -> IGNORED", getIdentifier().c_str(), (double)mHoldOff/Second);
        doTrigger = false; // nothing to trigger
        mMetAt = Never; // holdoff time cancelled
      }
    }
  }
  // update state
  if (mOneShotEval || ((aEvalMode&initial) && aResult->hasType(oneshot))) {
    // oneshot triggers do not toggle status, but must return to undefined (also on initial, non-event-triggered evaluation)
    invalidateState();
  }
  else {
    // Not oneshot: update state (note: the trigger for this state might only come later when mHoldOff is set!)
    mBoolState = newBoolState;
    // check holdoff
    if (mHoldOff>0 && (aEvalMode&initial)==0) { // holdoff is only active for non-initial runs
      // we have a hold-off
      MLMicroSeconds now = MainLoop::now();
      if (doTrigger & (mTriggerMode==onChangingBool || newBoolState)) {
        // trigger would fire now, but may not yet do so -> (re)start hold-off period
        // Note: onChangingBool has holdoff for both edges, onChangingBoolRisingHoldoffOnly only for rising edge
        doTrigger = false; // can't trigger now
        mMetAt = now+mHoldOff;
        OLOG(LOG_INFO, "%s: condition became %s, but must await holdoff period of %.2f seconds - wait until %s", getIdentifier().c_str(), newBoolState ? "true" : "false", (double)mHoldOff/Second, MainLoop::string_mltime(mMetAt, 3).c_str());
        updateNextEval(mMetAt);
      }
      else if (mMetAt!=Never) {
        // not changed now, but waiting for holdoff -> check if holdoff has expired
        if (now>=mMetAt) {
          // holdoff expired, now we must trigger
          OLOG(LOG_INFO, "%s: condition has been stable for holdoff period of %.2f seconds -> fire now", getIdentifier().c_str(), (double)mHoldOff/Second);
          doTrigger = true;
          mMetAt = Never;
        }
        else {
          // not yet, silently re-schedule an evaluation not later than the end of the holdoff
          updateNextEval(mMetAt);
        }
      }
    } // holdoff
  }
  mCurrentResult = aResult->assignmentValue();
  // take unfreeze time of frozen results into account for next evaluation
  FrozenResultsMap::iterator fpos = mFrozenResults.begin();
  MLMicroSeconds now = MainLoop::now();
  while (fpos!=mFrozenResults.end()) {
    if (fpos->second.mFrozenUntil==Never) {
      // already detected expired -> erase
      // Note: delete only DETECTED ones, just expired ones in terms of now() MUST wait until checked in next evaluation!
      #if P44_CPP11_FEATURE
      fpos = mFrozenResults.erase(fpos);
      #else
      FrozenResultsMap::iterator dpos = fpos++;
      mFrozenResults.erase(dpos);
      #endif
      continue;
    }
    MLMicroSeconds frozenUntil = fpos->second.mFrozenUntil;
    if (frozenUntil<now) {
      // unfreeze time is in the past (should not!)
      OLOG(LOG_WARNING, "unfreeze time is in the past -> re-run in 30 sec: %s", mCursor.displaycode(70).c_str());
      frozenUntil = now+30*Second; // re-evaluate in 30 seconds to make sure it does not completely stall
    }
    updateNextEval(frozenUntil);
    fpos++;
  }
  // treat static trigger like oneshot (i.e. with no persistent current state)
  if (mNextEvaluation==Never && !hasSources()) {
    // Warn if trigger is unlikely to ever fire (note: still might make sense, e.g. as evaluator reset)
    if ((aEvalMode&initial)!=0) {
      OLOG(LOG_WARNING, "%s: probably will not work as intended (no timers nor events): %s", getIdentifier().c_str(), mCursor.displaycode(70).c_str());
    }
    invalidateState();
  }
  // frozen one-shot value always expires after one evaluation (also important to free no longer used event objects early)
  mFrozenEventValue.reset();
  mFrozenEventPos = 0;
  mOneShotEval = false;
  // schedule next timed evaluation if one is needed
  scheduleNextEval();
  // callback (always, even when initializing)
  if (doTrigger && mTriggerCB) {
    FOCUSLOG("\n---------- FIRING Trigger        : result = %s", ScriptObj::describe(aResult).c_str());
    OLOG(LOG_INFO, "%s: fires with result = %s", getIdentifier().c_str(), ScriptObj::describe(aResult).c_str());
    mTriggerCB(aResult);
  }
}


void CompiledTrigger::scheduleNextEval()
{
  if (mNextEvaluation!=Never) {
    OLOG(LOG_DEBUG, "%s: re-evaluation scheduled for %s: '%s'", getIdentifier().c_str(), MainLoop::string_mltime(mNextEvaluation, 3).c_str(), mCursor.displaycode(70).c_str());
    mReEvaluationTicket.executeOnceAt(
      boost::bind(&CompiledTrigger::triggerEvaluation, this, (EvaluationFlags)timed),
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
    if (aLatestEval<=mMostRecentEvaluation) {
      // requesting past evaluation: not allowed!
      OLOG(LOG_WARNING, "%s: immediate or past re-evaluation requested -> delaying it up to 10 seconds", getIdentifier().c_str());
      if (mNextEvaluation==Never || mNextEvaluation>mMostRecentEvaluation+10*Second) {
        // no other re-evaluation before 10 seconds scheduled yet, make sure we re-evaluate once in 10 secs for safety
        mNextEvaluation = mMostRecentEvaluation+10*Second;
        return true;
      }
      return false;
    }
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


void CompiledTrigger::checkFrozenEventValue(ScriptObjPtr &aResult, SourcePos::UniquePos aFreezeId)
{
  // Note: this is called for every leaf value, no matter if freezable or not
  // if the original value is a oneshot, switch to oneshot evaluation
  if (aResult->hasType(oneshot)) mOneShotEval = true;
  // independently of oneshot mode, there might be a frozen value delivered earlier by the event source
  // that must be used instead of the event source representing value (with might be permanently NULL)
  if (aFreezeId==mFrozenEventPos) {
    // use value delivered by event (which might itself not be a oneshot nor freezable)
    FOCUSLOG("                      replacing result by frozen event value : result = %s", ScriptObj::describe(mFrozenEventValue).c_str());
    aResult = mFrozenEventValue;
  }
}




CompiledTrigger::FrozenResult* CompiledTrigger::getTimeFrozenValue(ScriptObjPtr &aResult, SourcePos::UniquePos aFreezeId)
{
  FrozenResultsMap::iterator frozenVal = mFrozenResults.find(aFreezeId);
  FrozenResult* frozenResultP = NULL;
  if (frozenVal!=mFrozenResults.end()) {
    frozenResultP = &(frozenVal->second);
    // there is a frozen result for this position in the expression
    OLOG(LOG_DEBUG, "- frozen result (%s) for actual result (%s) for freezeId 0x%p exists - will expire %s",
      frozenResultP->mFrozenResult->stringValue().c_str(),
      aResult->stringValue().c_str(),
      aFreezeId,
      frozenResultP->frozen() ? MainLoop::string_mltime(frozenResultP->mFrozenUntil, 3).c_str() : "NOW"
    );
    aResult = frozenVal->second.mFrozenResult;
    if (!frozenResultP->frozen()) frozenVal->second.mFrozenUntil = Never; // mark expired
  }
  return frozenResultP;
}


bool CompiledTrigger::FrozenResult::frozen()
{
  return mFrozenUntil==Infinite || (mFrozenUntil!=Never && mFrozenUntil>MainLoop::now());
}


CompiledTrigger::FrozenResult* CompiledTrigger::newTimedFreeze(FrozenResult* aExistingFreeze, ScriptObjPtr aNewResult, SourcePos::UniquePos aFreezeId, MLMicroSeconds aFreezeUntil, bool aUpdate)
{
  if (!aExistingFreeze) {
    // nothing frozen yet, freeze it now
    FrozenResult newFreeze;
    newFreeze.mFrozenResult = aNewResult;
    newFreeze.mFrozenUntil = aFreezeUntil;
    mFrozenResults[aFreezeId] = newFreeze;
    OLOG(LOG_DEBUG, "- new result (%s) frozen for freezeId 0x%p until %s",
      aNewResult->stringValue().c_str(),
      aFreezeId,
      MainLoop::string_mltime(newFreeze.mFrozenUntil, 3).c_str()
    );
    return &mFrozenResults[aFreezeId];
  }
  else if (!aExistingFreeze->frozen() || aUpdate || aFreezeUntil==Never) {
    OLOG(LOG_DEBUG, "- existing freeze updated to value %s and to expire %s",
      aNewResult->stringValue().c_str(),
      aFreezeUntil==Never ? "IMMEDIATELY" : MainLoop::string_mltime(aFreezeUntil, 3).c_str()
    );
    aExistingFreeze->mFrozenResult = aNewResult;
    aExistingFreeze->mFrozenUntil = aFreezeUntil;
  }
  else {
    OLOG(LOG_DEBUG, "- no freeze created/updated");
  }
  return aExistingFreeze;
}


bool CompiledTrigger::unfreezeTimed(SourcePos::UniquePos aFreezeId)
{
  FrozenResultsMap::iterator frozenVal = mFrozenResults.find(aFreezeId);
  if (frozenVal!=mFrozenResults.end()) {
    mFrozenResults.erase(frozenVal);
    return true;
  }
  return false;
}



#if P44SCRIPT_FULL_SUPPORT

// MARK: - CompiledHandler

void CompiledHandler::installAndInitializeTrigger(ScriptObjPtr aTrigger)
{
  mTrigger = boost::dynamic_pointer_cast<CompiledTrigger>(aTrigger);
  // link trigger with my handler action
  if (mTrigger) {
    mTrigger->setTriggerCB(boost::bind(&CompiledHandler::triggered, this, _1));
    mTrigger->setTriggerEvalFlags(expression|synchronously|concurrently); // need to be concurrent because handler might run in same shared context as trigger does
    mTrigger->initializeTrigger();
  }
}


void CompiledHandler::triggered(ScriptObjPtr aTriggerResult)
{
  // execute the handler script now
  if (mMainContext) {
    OLOG(LOG_INFO, "%s triggered: '%s' with result = %s", mName.c_str(), mCursor.displaycode(50).c_str(), ScriptObj::describe(aTriggerResult).c_str());
    ExecutionContextPtr ctx = contextForCallingFrom(mMainContext->domain(), NULL);
    if (ctx) {
      SimpleVarContainer* handlerThreadLocals = NULL;
      if (!mTrigger->mResultVarName.empty()) {
        handlerThreadLocals = new SimpleVarContainer();
        handlerThreadLocals->setMemberByName(mTrigger->mResultVarName, aTriggerResult);
      }
      ctx->execute(this, scriptbody|keepvars|concurrently, boost::bind(&CompiledHandler::actionExecuted, this, _1), nullptr, handlerThreadLocals);
      return;
    }
  }
  OLOG(LOG_ERR, "%s action cannot execute - no context", mName.c_str());
}


void CompiledHandler::actionExecuted(ScriptObjPtr aActionResult)
{
  OLOG(LOG_INFO, "%s executed: result =  %s", mName.c_str(), ScriptObj::describe(aActionResult).c_str());
}


void CompiledHandler::deactivate()
{
  if (mTrigger) {
    mTrigger->deactivate();
    mTrigger.reset();
  }
  inherited::deactivate();
}


#endif // P44SCRIPT_FULL_SUPPORT

// MARK: - ScriptCompiler


static void flagSetter(bool* aFlag) { *aFlag = true; }

ScriptObjPtr ScriptCompiler::compile(SourceContainerPtr aSource, CompiledCodePtr aIntoCodeObj, EvaluationFlags aParsingMode, ScriptMainContextPtr aMainContext)
{
  if (!aSource) return new ErrorValue(ScriptError::Internal, "No source code");
  // set up starting point
  #if P44SCRIPT_FULL_SUPPORT
  if ((aParsingMode & (sourcecode|checking))==0) {
    // Shortcut for non-checked expression and scriptbody: no need to "compile"
    mBodyRef = aSource->getCursor();
  }
  else {
    // could contain declarations, must scan these now
    setCursor(aSource->getCursor());
    aParsingMode = (aParsingMode & ~runModeMask) | scanning | (aParsingMode&checking); // compiling only, with optional checking
    initProcessing(aParsingMode);
    bool completed = false;
    setCompletedCB(boost::bind(&flagSetter,&completed));
    mCompileForContext = aMainContext; // set for compiling other scriptlets (triggers, handlers) into the same context
    start();
    mCompileForContext.reset(); // release
    if (!completed) {
      // the compiler must complete synchronously!
      return new ErrorValue(ScriptError::Internal, "Fatal: compiler execution not synchronous!");
    }
    if (mResult && mResult->isErr()) {
      return mResult;
    }
  }
  #else
  // we only know expressions, no declarations
  bodyRef = aSource->getCursor();
  #endif
  if (aIntoCodeObj) {
    aIntoCodeObj->setCursor(mBodyRef);
  }
  return aIntoCodeObj;
}


#if P44SCRIPT_FULL_SUPPORT
void ScriptCompiler::startOfBodyCode()
{
  mBodyRef = mSrc; // rest of source code is body
  if ((mEvaluationFlags&checking)==0) {
    complete(new AnnotatedNullValue("compiled"));
    return;
  }
  // we want a full syntax scan, continue skipping
  inherited::startOfBodyCode();
}
#endif


void ScriptCompiler::memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError)
{
  // global members are available at compile time
  if (mSkipping) {
    mResult.reset();
    resume();
  }
  // non-skipping compilation means evaluating global var initialisation
  mResult = mDomain->memberByName(mIdentifier, aMemberAccessFlags);
  if (!mResult) {
    mResult = new ErrorPosValue(mSrc, ScriptError::Syntax, "'%s' cannot be accessed in declarations", mIdentifier.c_str());
  }
  checkAndResume();
}


#if P44SCRIPT_FULL_SUPPORT

void ScriptCompiler::storeFunction()
{
  if (!mResult->isErr()) {
    // functions are always global
    ErrorPtr err = mDomain->setMemberByName(mResult->getIdentifier(), mResult);
    if (Error::notOK(err)) {
      mResult = new ErrorPosValue(mSrc, err);
    }
  }
  checkAndResume();
}

void ScriptCompiler::storeHandler()
{
  if (!mResult->isErr()) {
    // only handlers in declaration part must be stored at compile time
    if (mEvaluationFlags & sourcecode) {
      mResult = mDomain->registerHandler(mResult);
    }
    else {
      // handler in script body must NOT be stored
      mResult->deactivate();
      mResult.reset();
    }
  }
  checkAndResume();
}

#endif // P44SCRIPT_FULL_SUPPORT


// MARK: - SourceContainer


SourceContainer::SourceContainer(ScriptHost* aHostSourceP, const string aSource) :
  mFloating(false),
  mScriptHostP(aHostSourceP)
{
  assert(mScriptHostP);
  mOriginLabel = mScriptHostP->getOriginLabel();
  mLoggingContextP = mScriptHostP->getLoggingContext();
  mSource = aSource;
}


SourceContainer::SourceContainer(const char *aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource) :
  mOriginLabel(aOriginLabel),
  mLoggingContextP(aLoggingContextP),
  mSource(aSource),
  mFloating(false),
  mScriptHostP(nullptr)
{
}


SourceContainer::SourceContainer(const SourceCursor &aCodeFrom, const SourcePos &aStartPos, const SourcePos &aEndPos) :
  mOriginLabel("copied"),
  mLoggingContextP(aCodeFrom.mSourceContainer->mLoggingContextP),
  mFloating(true), // copied source is floating
  mScriptHostP(nullptr)
{
  mSource.assign(aStartPos.mPtr, aEndPos.mPtr-aStartPos.mPtr);
}


SourceCursor SourceContainer::getCursor()
{
  return SourceCursor(this);
}


#if P44SCRIPT_DEBUGGING_SUPPORT

const BreakPoint* SourceContainer::breakPointAt(const SourcePos::UniquePos aPosId) const
{
  if (mBreakPoints.empty()) return nullptr; // optimization
  BreakPointMap::const_iterator pos = mBreakPoints.find(aPosId);
  if (pos==mBreakPoints.end()) return nullptr;
  return &pos->second;
}

#endif // P44SCRIPT_DEBUGGING_SUPPORT


// MARK: - ScriptHost


ScriptHost::ScriptHost() :
  mActiveParams(nullptr)
{
  isMemberVariable();
}


ScriptHost::ScriptHost(
  EvaluationFlags aDefaultFlags,
  const char* aOriginLabel,
  const char* aTitleTemplate,
  P44LoggingObj* aLoggingContextP
) :
  mActiveParams(nullptr)
{
  isMemberVariable();
  activate(aDefaultFlags, aOriginLabel, aTitleTemplate, aLoggingContextP);
}


ScriptHost::~ScriptHost()
{
  if (storable()) setSource(""); // force removal of global objects depending on this source
  if (mActiveParams) {
    // remove non-retaining references
    // - unregister
    #if P44SCRIPT_REGISTERED_SOURCE
    domain()->unregisterScriptHost(*this);
    #endif
    // - possible backreference in container
    if (mActiveParams->mSourceContainer && mActiveParams->mSourceContainer->mScriptHostP==this) {
      mActiveParams->mSourceContainer->mScriptHostP = nullptr;
    }
    delete mActiveParams;
    mActiveParams = nullptr;
  }
}


void ScriptHost::activate(EvaluationFlags aDefaultFlags, const char* aOriginLabel, const char* aTitleTemplate, P44LoggingObj* aLoggingContextP)
{
  if (!mActiveParams) {
    mActiveParams = new ActiveParams;
    mActiveParams->mDefaultFlags = aDefaultFlags;
    mActiveParams->mOriginLabel = nonNullCStr(aOriginLabel);
    mActiveParams->mTitleTemplate = nonNullCStr(aTitleTemplate);
    mActiveParams->mLoggingContextP = aLoggingContextP;
    mActiveParams->mSourceDirty = false;
    #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
    mActiveParams->mDomainSource = false;
    mActiveParams->mLocalDataReportedRemoved = false;
    #endif
  }
}


bool ScriptHost::active() const
{
  return mActiveParams!=nullptr;
}


bool ScriptHost::storable() const
{
  return active() && !mActiveParams->mUnstored;
}



#if P44SCRIPT_REGISTERED_SOURCE


ScriptHost::ScriptHost(SourceContainerPtr aSourceContainer) :
  mActiveParams(nullptr)
{
  activate(sourcecode|regular|keepvars|queue|ephemeralSource, aSourceContainer->mOriginLabel, nullptr, aSourceContainer->loggingContext());
  mActiveParams->mSourceContainer = aSourceContainer;
}


void ScriptHost::setScriptHostUid(const string aScriptHostUid, bool aUnstored)
{
  assert(active());
  mActiveParams->mUnstored = aUnstored;
  mActiveParams->mScriptHostUid = aScriptHostUid;
}


void ScriptHost::registerScript()
{
  if (active() && !mActiveParams->mScriptHostUid.empty()) {
    domain()->registerScriptHost(*this);
  }
}


void ScriptHost::registerUnstoredScript(const string aScriptHostUid)
{
  setScriptHostUid(aScriptHostUid);
  registerScript();
}


string ScriptHost::scriptSourceUid()
{
  if (!active()) return "<inactive>";
  return mActiveParams->mScriptHostUid;
}


string ScriptHost::getContextTitle()
{
  string t;
  if (active()) {
    P44LoggingObj* lcP = mActiveParams->mLoggingContextP;
    if (lcP) {
      t = lcP->contextName();
      if (t.empty()) {
        t = lcP->contextType() + " " + lcP->contextId();
      }
    }
  }
  return t;
}


string ScriptHost::getScriptTitle()
{
  string t;
  if (active()) {
    string tmpl = mActiveParams->mTitleTemplate;
    P44LoggingObj* lcP = mActiveParams->mLoggingContextP;
    if (tmpl.empty()) {
      // use default title
      tmpl = "%C (%O)";
    }
    t = string_substitute(tmpl, "%C", getContextTitle());
    t = string_substitute(t, "%O", getOriginLabel());
    if (lcP) {
      t = string_substitute(t, "%N", lcP->contextName());
      t = string_substitute(t, "%T", lcP->contextType());
      t = string_substitute(t, "%I", lcP->contextId());
    }
  }
  return t;
}


bool ScriptHost::loadAndActivate(
  const string& aScriptHostUid,
  EvaluationFlags aDefaultFlags,
  const char* aOriginLabel,
  const char* aTitleTemplate,
  P44LoggingObj* aLoggingContextP,
  ScriptingDomainPtr aInDomain,
  const char* aLocallyStoredSource
)
{
  // we need the domain before we decide about activation
  if (!aInDomain) aInDomain = ScriptingDomainPtr(&StandardScriptingDomain::sharedDomain());
  bool domainSource = false;
  string source;
  if (!aScriptHostUid.empty()) {
    // try to load from domain level
    domainSource = aInDomain->loadSource(aScriptHostUid, source);
  }
  if (!domainSource && aLocallyStoredSource && *aLocallyStoredSource) {
    source = aLocallyStoredSource;
  }
  if (!source.empty()) {
    // we do have non-empty source code
    activate(aDefaultFlags, aOriginLabel, aTitleTemplate, aLoggingContextP);
    // now activated, we can set the domain
    setDomain(aInDomain);
    // and the source text
    setSource(source);
    if (!aScriptHostUid.empty()) {
      mActiveParams->mScriptHostUid = aScriptHostUid;
      // now register in the domain
      registerScript();
      #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
      if (!domainSource) {
        // migrate to domain store
        mActiveParams->mSourceDirty = true;
        bool storedok = storeSource();
        POLOG(mActiveParams->mLoggingContextP, LOG_NOTICE,
          "%s copying '%s' lazily activated source to domain store with UID='%s'",
          storedok ? "succeeded" : "FAILED",
          mActiveParams->mOriginLabel.c_str(),
          mActiveParams->mScriptHostUid.c_str()
        );
      }
      #endif // P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
      // after load, source save status is always clean
      mActiveParams->mSourceDirty = false;
    }
  }
  return !source.empty();
}


bool ScriptHost::setSourceAndActivate(
  const string& aSource,
  const string& aScriptHostUid,
  EvaluationFlags aDefaultFlags,
  const char* aOriginLabel,
  const char* aTitleTemplate,
  P44LoggingObj* aLoggingContextP,
  ScriptingDomainPtr aInDomain
)
{
  if (!active() && !aSource.empty()) {
    // we need to activate first
    activate(aDefaultFlags, aOriginLabel, aTitleTemplate, aLoggingContextP);
    setDomain(aInDomain);
    mActiveParams->mScriptHostUid = aScriptHostUid;
    registerScript();
  }
  bool changed = setSource(aSource);
  storeSource();
  return changed;
}


bool ScriptHost::setAndStoreSource(const string& aSource)
{
  bool changed = setSource(aSource);
  if (changed) {
    if (storeSource()) {
      // stored successfully at domain level
      if (!aSource.empty()) {
        // make sure non-empty source gets registered
        registerScript();
      }
      #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
      // report as changed as long as getSourceToStoreLocally() has not been called at least once
      changed = !mActiveParams->mLocalDataReportedRemoved;
      #else
      changed = false; // all set, no need to propagate changed status to caller
      #endif
    }
  }
  return changed;
}


bool ScriptHost::loadSource(const char* aLocallyStoredSource)
{
  assert(active());
  string source;
  bool changed = false;
  if (!storable() || !domain()->loadSource(mActiveParams->mScriptHostUid, source)) {
    // use locally stored source, if any
    if (aLocallyStoredSource) source = aLocallyStoredSource;
    #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
    if (!source.empty() && storable()) {
      changed = setSource(source);
      storeSource();
      POLOG(mActiveParams->mLoggingContextP, LOG_NOTICE,
        "%s copying '%s' source to domain store with UID='%s'",
        mActiveParams->mDomainSource ? "succeeded" : "FAILED",
        mActiveParams->mOriginLabel.c_str(),
        mActiveParams->mScriptHostUid.c_str()
      );
    }
    #endif
  }
  else {
    #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
    mActiveParams->mDomainSource = true; // source from domain at load
    if (!aLocallyStoredSource || *aLocallyStoredSource==0) {
      // apparently, locally stored data is already gone
      mActiveParams->mLocalDataReportedRemoved = true;
    }
    #endif
    changed = setSource(source);
  }
  mActiveParams->mSourceDirty = false;
  registerScript();
  return changed;
}


bool ScriptHost::storeSource()
{
  if (!storable()) return false; // inactive or disabled storage is NOP (but ok)
  if (mActiveParams->mSourceDirty && !mActiveParams->mScriptHostUid.empty()) {
    bool storedok = domain()->storeSource(mActiveParams->mScriptHostUid, getSource());
    #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
    mActiveParams->mDomainSource = storedok;
    #endif
    mActiveParams->mSourceDirty = !storedok; // remains dirty when not actually stored
    return mActiveParams->mDomainSource;
  }
  return false; // no need or ability to store
}


void ScriptHost::deleteSource()
{
  if (!storable()) return; // inactive storage is NOP (but ok)
  setSource(""); // empty
  storeSource(); // make sure it gets stored
}


#if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
string ScriptHost::getSourceToStoreLocally() const
{
  #if P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE
  if (storable() && mActiveParams->mDomainSource) {
    if (!mActiveParams->mLocalDataReportedRemoved) {
      mActiveParams->mLocalDataReportedRemoved = true; // flag for preventing further caller-local storage changed reporting
      POLOG(mActiveParams->mLoggingContextP, LOG_WARNING,
        "migration of '%s' source to domain store with UID='%s' complete - locally stored version NOW EMPTY",
        mActiveParams->mOriginLabel.c_str(),
        mActiveParams->mScriptHostUid.c_str()
      );
    }
    return ""; // empty
  }
  #endif
  // no source from domain, just return it to be stored locally by the caller (e.g. in DB field)
  return getSource();
}
#endif // P44SCRIPT_MIGRATE_TO_DOMAIN_SOURCE


#endif // P44SCRIPT_REGISTERED_SOURCE


void ScriptHost::setDomain(ScriptingDomainPtr aDomain)
{
  assert(active());
  mActiveParams->mScriptingDomain = aDomain;
};


ScriptingDomainPtr ScriptHost::domain()
{
  assert(active());
  if (!mActiveParams->mScriptingDomain) {
    // none assigned so far, assign default
    mActiveParams->mScriptingDomain = ScriptingDomainPtr(&StandardScriptingDomain::sharedDomain());
  }
  return mActiveParams->mScriptingDomain;
}


void ScriptHost::setSharedMainContext(ScriptMainContextPtr aSharedMainContext)
{
  if (!aSharedMainContext && !active()) return; // no specific context can be set for inactive script
  assert(active());
  // cached executable gets invalid when setting new context
  if (mActiveParams->mSharedMainContext!=aSharedMainContext) {
    if (mActiveParams->mCachedExecutable) {
      mActiveParams->mCachedExecutable.reset(); // release cached executable (will release SourceCursor holding our source)
    }
    mActiveParams->mSharedMainContext = aSharedMainContext; // use this particular context for executing scripts
  }
}


ScriptMainContextPtr ScriptHost::sharedMainContext() const
{
  if (!active()) return nullptr;
  return mActiveParams->mSharedMainContext;
}



void ScriptHost::uncompile(bool aNoAbort)
{
  if (!active()) return; // cannot be compiled or running, just NOP
  if (mActiveParams->mSharedMainContext && !aNoAbort) {
    mActiveParams->mSharedMainContext->abortThreadsRunningSource(mActiveParams->mSourceContainer, new ErrorValue(ScriptError::Aborted, "Source code changed while executing"));
  }
  if (mActiveParams->mCachedExecutable) {
    mActiveParams->mCachedExecutable.reset(); // release cached executable (will release SourceCursor holding our source)
  }
  if (mActiveParams->mSourceContainer) {
    if (mActiveParams->mScriptingDomain) mActiveParams->mScriptingDomain->releaseObjsFromSource(mActiveParams->mSourceContainer); // release all global objects from this source
    if (mActiveParams->mSharedMainContext) mActiveParams->mSharedMainContext->releaseObjsFromSource(mActiveParams->mSourceContainer); // release all main context objects from this source
  }
}


bool ScriptHost::setSource(const string aSource, EvaluationFlags aEvaluationFlags)
{
  if (!storable()) {
    if (aSource.empty()) return false; // setting empty source on a non-active or non-storable script is the only allowed option, but is not a change
    assert(false); // must be active for everything else
  }
  if (aEvaluationFlags==inherit || mActiveParams->mDefaultFlags==aEvaluationFlags) {
    // same flags, check source
    if (mActiveParams->mSourceContainer && mActiveParams->mSourceContainer->mSource == aSource) {
      return false; // no change at all -> NOP
    }
  }
  // changed, invalidate everything related to the previous code
  uncompile(mActiveParams->mDefaultFlags & ephemeralSource);
  if (aEvaluationFlags!=inherit) mActiveParams->mDefaultFlags = aEvaluationFlags;
  mActiveParams->mSourceContainer.reset(); // release it myself
  // create new source container
  if (!aSource.empty()) {
    mActiveParams->mSourceContainer = SourceContainerPtr(new SourceContainer(this, aSource));
  }
  mActiveParams->mSourceDirty = true;
  return true; // source has changed
}


string ScriptHost::getSource() const
{
  return active() && mActiveParams->mSourceContainer ? mActiveParams->mSourceContainer->mSource : "";
}


bool ScriptHost::empty() const
{
  return active() && mActiveParams->mSourceContainer ? mActiveParams->mSourceContainer->mSource.empty() : true;
}


const char* ScriptHost::getOriginLabel()
{
  return nonNullCStr(active() ? mActiveParams->mOriginLabel.c_str() : nullptr);
}


P44LoggingObj* ScriptHost::getLoggingContext()
{
  return active() ? mActiveParams->mLoggingContextP : nullptr;
}



bool ScriptHost::refersTo(const SourceCursor& aCursor)
{
  return active() ? aCursor.refersTo(mActiveParams->mSourceContainer) : false; // can't refer to inactive source
}


ScriptObjPtr ScriptHost::getExecutable()
{
  if (active() && mActiveParams->mSourceContainer) {
    if (!mActiveParams->mCachedExecutable) {
      // need to compile
      ScriptCompiler compiler(domain());
      ScriptMainContextPtr mctx = mActiveParams->mSharedMainContext; // use shared context if one is set
      if (!mctx) {
        // default to independent execution in a non-object context (no instance pointer)
        mctx = domain()->newContext();
      }
      CompiledCodePtr code;
      if (mActiveParams->mDefaultFlags & anonymousfunction) {
        code = new CompiledCode("anonymous");
      }
      else if (mActiveParams->mDefaultFlags & (triggered|timed|initial)) {
        code = new CompiledTrigger(!mActiveParams->mOriginLabel.empty() ? mActiveParams->mOriginLabel : "trigger", mctx);
      }
      else {
        code = new CompiledScript(!mActiveParams->mOriginLabel.empty() ? mActiveParams->mOriginLabel : "script", mctx);
      }
      mActiveParams->mCachedExecutable = compiler.compile(mActiveParams->mSourceContainer, code, mActiveParams->mDefaultFlags, mctx);
    }
    return mActiveParams->mCachedExecutable;
  }
  return new ErrorValue(ScriptError::Internal, "no source -> no executable");
}


ScriptObjPtr ScriptHost::syntaxcheck()
{
  if (!active()) return ScriptObjPtr(); // no script at all is ok
  EvaluationFlags checkFlags = (mActiveParams->mDefaultFlags&~runModeMask)|scanning|checking;
  ScriptCompiler compiler(domain());
  ScriptMainContextPtr mctx = mActiveParams->mSharedMainContext; // use shared context if one is set
  if (!mctx) {
    // default to independent execution in a non-object context (no instance pointer)
    mctx = domain()->newContext();
  }
  return compiler.compile(mActiveParams->mSourceContainer, CompiledCodePtr(), checkFlags, mctx);
}


void ScriptHost::setScriptCommandHandler(ScriptCommandCB aScriptCommandCB)
{
  assert(active());
  mActiveParams->mScriptCommandCB = aScriptCommandCB;
}


void ScriptHost::setScriptResultHandler(EvaluationCB aScriptResultCB)
{
  assert(active());
  mActiveParams->mScriptResultCB = aScriptResultCB;
}


ScriptObjPtr ScriptHost::runCommand(ScriptCommand aCommand, EvaluationCB aScriptResultCB, ScriptObjPtr aThreadLocals)
{
  if (!active()) return new ErrorValue(ScriptError::Internal, "script is not active");
  if (!aScriptResultCB) aScriptResultCB = mActiveParams->mScriptResultCB;
  if (mActiveParams->mScriptCommandCB) {
    // use customized command implementation
    return mActiveParams->mScriptCommandCB(aCommand, aScriptResultCB, aThreadLocals, *this);
  }
  else {
    // run my own default implementation
    return defaultCommandImplementation(aCommand, aScriptResultCB, aThreadLocals);
  }
}


ScriptObjPtr ScriptHost::defaultCommandImplementation(ScriptCommand aCommand, EvaluationCB aScriptResultCB, ScriptObjPtr aThreadLocals)
{
  ScriptObjPtr ret;
  assert(active());
  EvaluationFlags flags = inherit;
  switch(aCommand) {
    case check:
      ret = syntaxcheck();
      break;
    case stop:
      // stop
      if (mActiveParams->mSharedMainContext) {
        // abort via main context
        mActiveParams->mSharedMainContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "manually aborted: %s", getScriptTitle().c_str()));
      }
      else  {
        ret = new ErrorValue(ScriptError::Internal, "cannot stop without context: %s", getScriptTitle().c_str());
      }
      break;
    case debug:
      // start in singlestep mode, i.e. break at first script statememnt
      flags |= singlestep;
      goto runnow;
    case restart:
      flags |= stopall;
      goto runnow;
    case start:
    runnow:
      // just start as configured at activation
      ret = run(flags, aScriptResultCB, aThreadLocals);
      break;
  }
  return ret;
}


ScriptObjPtr ScriptHost::run(EvaluationFlags aRunFlags, EvaluationCB aEvaluationCB, ScriptObjPtr aThreadLocals, MLMicroSeconds aMaxRunTime)
{
  if (!active()) return new AnnotatedNullValue("no script");
  if (!aEvaluationCB && mActiveParams->mScriptResultCB) aEvaluationCB = mActiveParams->mScriptResultCB; // use predefined callback handler
  EvaluationFlags flags = mActiveParams->mDefaultFlags; // default to predefined flags
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
      ExecutionContextPtr ctx = code->contextForCallingFrom(domain(), nullptr);
      if (ctx) {
        if (flags & synchronously) {
          result = ctx->executeSynchronously(code, flags, aThreadLocals, aMaxRunTime);
        }
        else {
          ctx->execute(code, flags, aEvaluationCB, nullptr, aThreadLocals, aMaxRunTime);
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
  bool changed = setSource(aSource);
  if (changed && aAutoInit) {
    compileAndInit();
  }
  return changed;
}


bool TriggerSource::setAndStoreTriggerSource(const string& aSource, bool aAutoInit)
{
  bool changed = setSource(aSource);
  if (changed) {
    if (storeSource()) {
      // stored successfully at domain level
      changed=false; // all set, no need to propagate changed status to caller
    }
    if (aAutoInit) {
      compileAndInit();
    }
  }
  return changed;
}


bool TriggerSource::loadTriggerSource(const char* aLocallyStoredSource, bool aAutoInit)
{
  bool changed = loadSource(aLocallyStoredSource);
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


bool TriggerSource::setTriggerMode(TriggerMode aTriggerMode, bool aAutoInit)
{
  if (aTriggerMode!=mTriggerMode) {
    mTriggerMode = aTriggerMode;
    if (aAutoInit) {
      compileAndInit();
    }
    return true;
  }
  return false;
}



ScriptObjPtr TriggerSource::compileAndInit()
{
  CompiledTriggerPtr trigger = boost::dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (!trigger) return  new ErrorValue(ScriptError::Internal, "is not a trigger");
  trigger->setTriggerMode(mTriggerMode, mHoldOffTime);
  trigger->setTriggerCB(mTriggerCB);
  trigger->setTriggerEvalFlags(mActiveParams->mDefaultFlags);
  return trigger->initializeTrigger();
}


void TriggerSource::invalidateState()
{
  CompiledTriggerPtr trigger = boost::dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (trigger) {
    trigger->invalidateState();
  }
}


CompiledTriggerPtr TriggerSource::getTrigger(bool aMustBeActive)
{
  CompiledTriggerPtr trigger = boost::dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (trigger && (!aMustBeActive || trigger->isActive())) return trigger;
  return CompiledTriggerPtr();
}


bool TriggerSource::evaluate(EvaluationFlags aRunMode)
{
  CompiledTriggerPtr trigger = getTrigger(false);
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


Tristate TriggerSource::currentBoolState()
{
  CompiledTriggerPtr trigger = getTrigger(true);
  if (trigger) return trigger->boolState(false); // undefined when trigger is in holdoff (settling) time
  return undefined; // undefined when trigger is not yet compiled
}


ScriptObjPtr TriggerSource::lastEvalResult()
{
  CompiledTriggerPtr trigger = getTrigger(true);
  if (trigger) return trigger->currentResult();
  return ScriptObjPtr();
}


void TriggerSource::nextEvaluationNotLaterThan(MLMicroSeconds aLatestEval)
{
  CompiledTriggerPtr trigger = boost::dynamic_pointer_cast<CompiledTrigger>(getExecutable());
  if (trigger) {
    trigger->scheduleEvalNotLaterThan(aLatestEval);
  }
}



// MARK: - ScriptingDomain

ScriptMainContextPtr ScriptingDomain::newContext(ScriptObjPtr aInstanceObj)
{
  return new ScriptMainContext(this, aInstanceObj);
}


#if P44SCRIPT_DEBUGGING_SUPPORT

/// called by threads when they get paused
void ScriptingDomain::threadPaused(ScriptCodeThreadPtr aThread)
{
  if (!mPauseHandlerCB) {
    OLOG(LOG_WARNING, "Thread %04d requested pause (reason: %s) but no pause handling active (any more) -> continuing w/o debugging", aThread->threadId(), ScriptCodeThread::pausingName(aThread->pauseReason()));
    aThread->continueWithMode(nopause);
  }
  else {
    // call handler
    mPauseHandlerCB(aThread);
  }
}

#endif // P44SCRIPT_DEBUGGING_SUPPORT


#if P44SCRIPT_REGISTERED_SOURCE


bool ScriptingDomain::registerScriptHost(ScriptHost &aHostSource)
{
  for(ScriptHostsVector::const_iterator pos = mScriptHosts.begin(); pos!=mScriptHosts.end(); ++pos) {
    if (&aHostSource==*pos) return false; // already registered
  }
  // not yet registered
  mScriptHosts.push_back(&aHostSource);
  return true;
}


bool ScriptingDomain::unregisterScriptHost(ScriptHost &aHostSource)
{
  for(ScriptHostsVector::const_iterator pos = mScriptHosts.begin(); pos!=mScriptHosts.end(); ++pos) {
    if (&aHostSource==*pos) {
      mScriptHosts.erase(pos);
      return true;
    }
  }
  return false;
}


ScriptHostPtr ScriptingDomain::getHostByIndex(size_t aSourceIndex) const
{
  if (aSourceIndex>mScriptHosts.size()) return nullptr;
  return mScriptHosts[aSourceIndex];
}


ScriptHostPtr ScriptingDomain::getHostByUid(const string aSourceUid) const
{
  for(ScriptHostsVector::const_iterator pos = mScriptHosts.begin(); pos!=mScriptHosts.end(); ++pos) {
    if (aSourceUid==(*pos)->scriptSourceUid()) return *pos;
  }
  return nullptr;
}


ScriptHostPtr ScriptingDomain::getHostForThread(const ScriptCodeThreadPtr aScriptCodeThread)
{
  SourceContainerPtr container = aScriptCodeThread->cursor().mSourceContainer;
  ScriptHostPtr host;
  if (container) {
    host = container->scriptHost();
    if (!host) {
      // create a ephemeral (unstored) host
      host = new ScriptHost(container);
      host->setScriptHostUid(string_format("thread_%08d", aScriptCodeThread->threadId()), true);
      host->setSharedMainContext(aScriptCodeThread->owner()->scriptmain());
      registerScriptHost(*host);
    }
    // register to make sure (usually already registered)
    registerScriptHost(*host);
  }
  return host;
}



#endif // P44SCRIPT_REGISTERED_SOURCE


// MARK: - ScriptCodeThread

ScriptCodeThread::ScriptCodeThread(ScriptCodeContextPtr aOwner, CompiledCodePtr aCode, const SourceCursor& aStartCursor, ScriptObjPtr aThreadLocals, ScriptCodeThreadPtr aChainedFromThread) :
  mOwner(aOwner),
  mCodeObj(aCode),
  mThreadLocals(aThreadLocals),
  mChainedFromThread(aChainedFromThread),
  mMaxBlockTime(0),
  mMaxRunTime(Infinite),
  mRunningSince(Never)
  #if P44SCRIPT_DEBUGGING_SUPPORT
  ,mPausingMode(nopause) // not debugging
  ,mPauseReason(nopause) // not paused
  #endif
{
  setCursor(aStartCursor);
  FOCUSLOG("\n%04x START        thread created : %s", (uint32_t)((intptr_t)static_cast<SourceProcessor *>(this)) & 0xFFFF, mSrc.displaycode(130).c_str());
}

ScriptCodeThread::~ScriptCodeThread()
{
  deactivate(); // even if deactivate() is usually called before dtor, make sure it happens even if not
  FOCUSLOG("\n%04x END          thread deleted : %s", (uint32_t)((intptr_t)static_cast<SourceProcessor *>(this)) & 0xFFFF, mSrc.displaycode(130).c_str());
}


void ScriptCodeThread::deactivate()
{
  // reset everything that could be part of a retain cycle
  mOwner.reset();
  mCodeObj.reset();
  mThreadLocals.reset();
  mChainedFromThread.reset();
  mRunningSince = Never; // just to make sure
}



P44LoggingObj* ScriptCodeThread::loggingContext()
{
  return mCodeObj ? mCodeObj->loggingContext() : NULL;
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
  #if P44SCRIPT_DEBUGGING_SUPPORT
  // setup pausing mode
  if (aEvalFlags & singlestep) {
    // thread explicitly started for singlestepping
    mPausingMode = step_over;
  }
  else if (aEvalFlags & neverpause) {
    mPausingMode = nopause;
  }
  else {
    // use domain's standard mode
    mPausingMode = owner()->domain()->defaultPausingMode();
  }
  #endif // P44SCRIPT_DEBUGGING_SUPPORT
  mMaxBlockTime = aMaxBlockTime;
  mMaxRunTime = aMaxRunTime;
}


string ScriptCodeThread::describePos(size_t aCodeMaxLen) const
{
  return string_format(
    "(%s:%zu,%zu):  %s",
    mSrc.originLabel(), mSrc.lineno()+1, mSrc.charpos()+1,
    mSrc.displaycode(aCodeMaxLen).c_str()
  );
}



void ScriptCodeThread::run()
{
  mRunningSince = MainLoop::now();
  OLOG(LOG_DEBUG, "starting %04d at %s", threadId(), describePos(90).c_str());
  start();
}


bool ScriptCodeThread::isExecutingSource(SourceContainerPtr aSource)
{
  if (mRunningSince==Never) return false; // not running at all
  if (mCodeObj && mCodeObj->originatesFrom(aSource)) return true; // this thread's starting point is in aSource
  if (mChainedExecutionContext && mChainedExecutionContext->isExecutingSource(aSource)) return true; // chained thread runs from aSource
  return false; // not running this source
}

ScriptCodeThreadPtr ScriptCodeThread::chainOriginThread()
{
  if (!mChainedFromThread) return this; // I am the potential origin of the current chain (which is not yet a chain)
  return mChainedFromThread->chainOriginThread(); // walk back recursively
}


void ScriptCodeThread::abort(ScriptObjPtr aAbortResult)
{
  if (mRunningSince==Never) {
    OLOG(LOG_DEBUG, "prevent aborting already completed %04d again", threadId());
    return;
  }
  // Note: calling abort must execute the callback passed to this thread when starting it
  inherited::abort(aAbortResult); // set the result
  if (mChainedExecutionContext) {
    // having a chained context means that a function (built-in or scripted) is executing
    mChainedExecutionContext->abort(stopall, aAbortResult); // will call resume() via the callback of the thread we've started the child context for
  }
  else {
    complete(aAbortResult); // complete now, will eventually invoke completion callback
  }
}


void ScriptCodeThread::abortOthers(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult)
{
  if (mOwner) {
    mOwner->abort(aAbortFlags, aAbortResult, this);
  }
}



ScriptObjPtr ScriptCodeThread::finalResult()
{
  if (mCurrentState==NULL) return mResult; // exit value of the thread
  return ScriptObjPtr(); // still running
}



void ScriptCodeThread::complete(ScriptObjPtr aFinalResult)
{
  mAutoResumeTicket.cancel();
  mRunningSince = Never; // flag non-running, prevents getting aborted (again)
  if (aFinalResult && aFinalResult->isErr()) {
    ErrorPtr err = aFinalResult->errorValue();
    bool fatal = err->isDomain(ScriptError::domain()) && err->getErrorCode()>=ScriptError::FatalErrors;
    POLOG(loggingContext(), LOG_ERR,
      "Aborting '%s' because of %s error: %s",
      mCodeObj->getIdentifier().c_str(),
      fatal ? "fatal" : "uncaught",
      aFinalResult->stringValue().c_str()
    );
  }
  // make sure this object is not released early while unwinding completion
  ScriptCodeThreadPtr keepAlive = ScriptCodeThreadPtr(this);
  #if P44SCRIPT_DEBUGGING_SUPPORT
  if (mChainedFromThread && mPausingMode>breakpoint) {
    // we are stepping out or singlestepping -> caller must continue single stepping (or at least do end-of-function checking)
    PausingMode needed = mPausingMode>step_out ? step_over : step_out;
    if (mChainedFromThread->mPausingMode<needed) {
      mChainedFromThread->mPausingMode = needed;
    }
  }
  #endif // P44SCRIPT_DEBUGGING_SUPPORT
  // now calling out to methods that might release this thread is safe
  inherited::complete(aFinalResult);
  OLOG(LOG_DEBUG,
    "complete %04d at (%s:%zu,%zu):  %s\n- with result: %s",
    threadId(),
    mSrc.originLabel(), mSrc.lineno()+1, mSrc.charpos()+1,
    mSrc.displaycode(90).c_str(),
    ScriptObj::describe(mResult).c_str()
  );
  sendEvent(mResult); // send the final result as event to registered EventSinks
  mChainedFromThread.reset();
  if (mOwner) mOwner->threadTerminated(this, mEvaluationFlags);
  #if P44SCRIPT_DEBUGGING_SUPPORT
  if (pauseCheck(terminate)) {
    // paused at termination
    OLOG(LOG_NOTICE, "thread paused at termination");
  }
  else
  #endif
  {
    // deactivate myself to break any remaining retain loops
    deactivate();
  }
  // now thread object might be actually released (unless kept as paused thread)
  keepAlive.reset();
}


void ScriptCodeThread::stepLoop()
{
  MLMicroSeconds loopingSince = MainLoop::now();
  do {
    MLMicroSeconds now = MainLoop::now();
    // check pausing
    // @note if pausing happens here, no state change has occurred in this step, so
    //   we can continue by just calling resume() (with mContinue set if we want to get past positional breakpoints etc.)
    #if P44SCRIPT_DEBUGGING_SUPPORT
    if (pauseCheck(scriptstep)) {
      return;
    }
    #endif
    // Check maximum execution time
    if (mMaxRunTime!=Infinite && now-mRunningSince>mMaxRunTime) {
      // Note: not calling abort as we are WITHIN the call chain
      complete(new ErrorPosValue(mSrc, ScriptError::Timeout, "Aborted because of overall execution time limit"));
      return;
    }
    else if (mMaxBlockTime!=Infinite && now-loopingSince>mMaxBlockTime) {
      // time expired
      if (mEvaluationFlags & synchronously) {
        // Note: not calling abort as we are WITHIN the call chain
        complete(new ErrorPosValue(mSrc, ScriptError::Timeout, "Aborted because of synchronous execution time limit"));
        return;
      }
      // in an async script, just give mainloop time to do other things for a while (but do not change result)
      mAutoResumeTicket.executeOnce(boost::bind(&selfKeepingResume, this, ScriptObjPtr()), 2*mMaxBlockTime);
      return;
    }
    // run next statemachine step
    mResumed = false; // start of a new step
    step(); // will cause resumed to be set when resume() is called in this call's chain
    // repeat as long as we are already resumed
  } while(mResumed && !mAborted);
}


void ScriptCodeThread::checkAndResume()
{
  ErrorValuePtr e = boost::dynamic_pointer_cast<ErrorValue>(mResult);
  if (e) {
    if (!e->caught()) {
      // need to throw
      OLOG(LOG_DEBUG, "   error at: %s\nwith result: %s", mSrc.displaycode(90).c_str(), ScriptObj::describe(e).c_str());
      throwOrComplete(e);
      return;
    }
    // already caught (and equipped with pos), just propagate as result
    mResult = e;
  }
  resume();
}


void ScriptCodeThread::memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError)
{
  if (mResult) {
    // explicit context is the result: look up member *only* here
    mResult = mResult->memberByName(mIdentifier, aMemberAccessFlags);
  }
  else {
    // implicit context
    if (!mThreadLocals && (aMemberAccessFlags&create) && (aMemberAccessFlags&threadlocal)) {
      // create thread locals on demand if none already set at thread preparation
      mThreadLocals = ScriptObjPtr(new SimpleVarContainer);
    }
    if (mThreadLocals) {
      // - try thread-level "this" context object
      TypeInfo fl = aMemberAccessFlags;
      if ((fl&threadlocal)==0) fl &= ~create; // do not create thread vars if not explicitly selected
      fl &= ~threadlocal; // do not pass on threadlocal flag
      FOCUSLOGCALLER("threadlocals");
      mResult = mThreadLocals->memberByName(mIdentifier, fl);
    }
    if (!mResult) {
      // - try owner context
      FOCUSLOGCALLER("owner context");
      mResult = mOwner->memberByName(mIdentifier, aMemberAccessFlags);
    }
    if (!mResult) {
      // on implicit context level, if nothing else was found, check overrideable convenience constants
      static const char * const weekdayNames[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
      if (mIdentifier.size()==3) {
        // Optimisation, all weekdays have 3 chars
        for (int w=0; w<7; w++) {
          if (uequals(mIdentifier, weekdayNames[w])) {
            mResult = new NumericValue(w);
            break;
          }
        }
      }
    }
  }
  if (!mResult && !aNoNotFoundError) {
    // not having a result here (not even a not-yet-created lvalue) means the member
    // does not exist and cannot/must not be created
    mResult = new ErrorPosValue(mSrc, ScriptError::NotFound , "'%s' unknown here", mIdentifier.c_str());
  }
  resume();
}


void ScriptCodeThread::memberByIndex(size_t aIndex, TypeInfo aMemberAccessFlags)
{
  if (mResult) {
    // look up member of the result itself
    mResult = mResult->memberAtIndex(aIndex, aMemberAccessFlags);
  }
  if (!mResult) {
    // not having a result here (not even a not-yet-created lvalue) means the member
    // does not exist and cannot/must not be created
    mResult = new ErrorPosValue(mSrc, ScriptError::NotFound , "array element %d unknown here", aIndex);
  }
  // no indexed members at the context level!
  resume();
}


void ScriptCodeThread::newFunctionCallContext()
{
  if (mResult) {
    mFuncCallContext = mResult->contextForCallingFrom(mOwner->scriptmain(), this);
  }
  if (!mFuncCallContext) {
    mResult = new ErrorPosValue(mSrc, ScriptError::NotCallable, "not a function");
  }
  checkAndResume();
}

#if P44SCRIPT_FULL_SUPPORT

void ScriptCodeThread::startBlockThreadAndStoreInIdentifier()
{
  ScriptCodeThreadPtr thread = mOwner->newThreadFrom(mCodeObj, mSrc, concurrently|block, NoOP, NULL);
  if (thread) {
    if (!mIdentifier.empty()) {
      push(mCurrentState); // skipping==true is pushed (as we're already skipping the concurrent block in the main thread)
      mSkipping = false; // ...but we need it off to store the thread var
      mResult = new ThreadValue(thread);
      push(&SourceProcessor::s_assignOlder);
      thread->run();
      mResult.reset();
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


void ScriptCodeThread::storeFunction()
{
  if (!mResult->isErr()) {
    // functions encountered duing execution (not in declaration part) are local to the context
    ErrorPtr err = owner()->scriptmain()->setMemberByName(mResult->getIdentifier(), mResult);
    if (Error::notOK(err)) {
      mResult = new ErrorPosValue(mSrc, err);
    }
  }
  checkAndResume();
}


void ScriptCodeThread::storeHandler()
{
  if (!mResult->isErr()) {
    // handlers processed at script run time will run in that script's main context
    mResult = owner()->scriptmain()->registerHandler(mResult);
  }
  checkAndResume();
}

#endif // P44SCRIPT_FULL_SUPPORT


/// apply the specified argument to the current result
void ScriptCodeThread::pushFunctionArgument(ScriptObjPtr aArgument)
{
  // apply the specified argument to the current function call context
  if (mFuncCallContext) {
    ScriptObjPtr errVal = mFuncCallContext->checkAndSetArgument(aArgument, mFuncCallContext->numIndexedMembers(), mResult);
    if (errVal) mResult = errVal;
  }
  checkAndResume();
}


// evaluate the current result and replace it with the output from the evaluation (e.g. function call)
void ScriptCodeThread::executeResult()
{
  if (mFuncCallContext && mResult) {
    // check for missing arguments after those we have
    ScriptObjPtr errVal = mFuncCallContext->checkAndSetArgument(ScriptObjPtr(), mFuncCallContext->numIndexedMembers(), mResult);
    if (errVal) {
      mResult = errVal;
      checkAndResume();
    }
    else {
      mChainedExecutionContext = mFuncCallContext; // as long as this executes, the function context becomes the child context of this thread
      #if P44SCRIPT_FULL_SUPPORT
      // Note: must have keepvars because these are the arguments!
      // Note: functions must not inherit their caller's evalscope but be run as script bodies
      // Note: must pass on threadvars. Custom functions technically run in a separate "thread", but that should be the same from a user's perspective
      EvaluationFlags dbg = 0;
      #if P44SCRIPT_DEBUGGING_SUPPORT
      // Note: must pass singlestep flag when current thread is in `into_function` (step-into) pausing mode
      if (mPausingMode==step_into) dbg |= singlestep;
      #endif // P44SCRIPT_DEBUGGING_SUPPORT
      mFuncCallContext->execute(mResult, (mEvaluationFlags&~scopeMask)|scriptbody|keepvars|dbg, boost::bind(&ScriptCodeThread::executedResult, this, _1), this, mThreadLocals);
      #else // P44SCRIPT_FULL_SUPPORT
      // only built-in functions can occur, eval scope flags are not relevant (only existing scope is expression)
      mFuncCallContext->execute(mResult, (mEvaluationFlags&~scopeMask)|expression|keepvars, boost::bind(&ScriptCodeThread::executedResult, this, _1), this, mThreadLocals);
      #endif // !P44SCRIPT_FULL_SUPPORT
    }
    // function call completion will call resume
    return;
  }
  mResult = new ErrorPosValue(mSrc, ScriptError::Internal, "cannot execute object");
  checkAndResume();
}


void ScriptCodeThread::executedResult(ScriptObjPtr aResult)
{
  if (!aResult) {
    aResult = new AnnotatedNullValue("no return value");
  }
  #if P44SCRIPT_DEBUGGING_SUPPORT
  bool wasChained = dynamic_cast<ScriptCodeContext*>(mChainedExecutionContext.get())!=nullptr;
  #endif
  mChainedExecutionContext.reset(); // release the child context
  if (aResult->isErr()) {
    // update (or add) position of error occurring to call site (log will show "call stack" as LOG_ERR messages)
    aResult = new ErrorPosValue(mSrc, aResult);
  }
  mResult = aResult;
  #if P44SCRIPT_DEBUGGING_SUPPORT
  // only chained function executions are script functions (built-ins do not count for step_out)
  if (!wasChained || !pauseCheck(step_out))
  #endif
  {
    resume();
  }
}


void ScriptCodeThread::memberEventCheck()
{
  // check for event sources in member and register them or use frozen one-shot result
  if (!mSkipping) {
    if (mEvaluationFlags&initial) {
      // initial run of trigger -> register event sourcing members to trigger event sink
      EventSource* eventSource = mResult->eventSource();
      if (eventSource) {
        // register the code object (the trigger) as event sink with the source
        EventSink* triggerEventSink = dynamic_cast<EventSink*>(mCodeObj.get());
        if (triggerEventSink) {
          // register the result as event source, along with the source position (for later freezing in trigger evaluation)
          // Note: only if this event source has type freezable, the source position is recorded for identifying frozen event values later
          FOCUSLOG("  leaf member is event source in trigger initialisation : register%s", mResult->hasType(freezable) ? " and record for freezing" : "");
          eventSource->registerForEvents(triggerEventSink, mResult->hasType(freezable) ? (intptr_t)mSrc.mPos.posId() : 0);
        }
      }
    }
    else if (mEvaluationFlags&triggered) {
      // we might have a frozen one-shot value delivered via event (which triggered this evaluation)
      CompiledTrigger* trigger = dynamic_cast<CompiledTrigger*>(mCodeObj.get());
      if (trigger) {
        trigger->checkFrozenEventValue(mResult, mSrc.mPos.posId());
      }
    }
  }
}


#if P44SCRIPT_DEBUGGING_SUPPORT


static const char* pausingModeNames[numPausingModes] = {
  "nopause",
  "unpause",
  "breakpoint",
  "step_out",
  "step_over",
  "step_into",
  "scriptstep",
  "interrupt",
  "terminate"
};

const char* ScriptCodeThread::pausingName(PausingMode aPausingMode)
{
  return pausingModeNames[aPausingMode];
}


PausingMode ScriptCodeThread::pausingModeNamed(const string aPauseName)
{
  for (int i=0; i<numPausingModes; i++) {
    if (aPauseName==pausingModeNames[i]) return static_cast<PausingMode>(i);
  }
  return nopause;
}



bool ScriptCodeThread::pauseCheck(PausingMode aPausingOccasion)
{
  if (mSkipping || mPausingMode==nopause) return false; // not debugging or not executing (just skipping)
//  DBGOLOG(LOG_ERR,
//    "pauseCheck: Occasion=%s, Mode=%s, Reason==%s: %s",
//    pausingName(aPausingOccasion), pausingName(mPausingMode), pausingName(mPauseReason),
//    describePos(20).c_str()
//  );
  if (mPausingMode==terminate) {
    abort(new ErrorValue(ScriptError::Aborted, "terminated from debugging pause"));
    return true; // do not continue normally
  }
  if (mPauseReason==unpause) {
    if (aPausingOccasion!=scriptstep || mPausingMode==scriptstep) {
      // continuing after a pause, must overcome the current pausing reason
      OLOG(LOG_NOTICE, "Thread continues in mode '%s' after pause", pausingName(mPausingMode));
      mRunningSince = MainLoop::currentMainLoop().now(); // re-start run time restriction
      mPauseReason = nopause;
    }
    return false;
  }
  // Actually check for pausing
  mPauseReason = aPausingOccasion; // default to the occasion
  switch (aPausingOccasion) {
    case breakpoint: // breakpoint() in code
    case interrupt: // interrupt from outside
      // unconditionally pause
      break;
    case step_out:
      if (mPausingMode!=step_out) return false; // mode is not "step out"
      // stop at end of function
      break;
    case step_over:
      if (mPausingMode<step_over) return false; // not in any of the singlestep modes
      // stop at statement
      break;
    case scriptstep:
      if (mPausingMode==scriptstep) break; // fine-grain stepping (p44script enginge debugging only, usually)
      // otherwise, thing to check at script step are breakpoints
      if (!mSrc.onBreakPoint()) return false; // not on a cursor position based breakpoint
      // stop at position based breakpoint
      mPauseReason = breakpoint; // report as breakpoint
      break;
    case terminate:
      if (!mResult || !mResult->isErr()) return false; // continue if this is not an error
      break;
    default:
      return false; // do not pause
  }
  // not continuing -> pause here
  // - find next code, that's (visually) where we are pausing
  mSrc.skipNonCode();
  // - now pause
  OLOG(LOG_NOTICE, "Thread paused with reason '%s' at %s", pausingName(mPauseReason), describePos(20).c_str());
  mOwner->domain()->threadPaused(this);
  return true; // signal pausing
}


void ScriptCodeThread::continueWithMode(PausingMode aNewPausingMode)
{
  if (mPauseReason==nopause) {
    OLOG(LOG_WARNING, "Trying to continue thread %04d which is NOT paused", threadId());
    return;
  }
  if (mPauseReason!=terminate) {
    // not pausing in terminated state, we can continue
    mPauseReason = unpause;
    mPausingMode = aNewPausingMode;
    resume();
  }
  else {
    // now the thread can finally be disposed of
    // - deactivate to break all retain loops
    deactivate();
    // when this call chain goes out of scope, thread object should get destructed
  }
}



#endif // P44SCRIPT_DEBUGGING_SUPPORT


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
  f->finish(new BoolValue(f->arg(0)->hasType(value)));
}


// ifok(a, b)   if a can be accessed w/o error, return it, otherwise return the default as specified by b
static const BuiltInArgDesc ifok_args[] = { { any|error|null }, { any|error|null } };
static const size_t ifok_numargs = sizeof(ifok_args)/sizeof(BuiltInArgDesc);
static void ifok_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->arg(0)->hasType(error) ? f->arg(1) : f->arg(0));
}

// isok(a)      if a does not produce an error
static const BuiltInArgDesc isok_args[] = { { any|error|null } };
static const size_t isok_numargs = sizeof(isok_args)/sizeof(BuiltInArgDesc);
static void isok_func(BuiltinFunctionContextPtr f)
{
  f->finish(new BoolValue(!f->arg(0)->hasType(error)));
}



// if (c, a, b)    if c evaluates to true, return a, otherwise b
static const BuiltInArgDesc if_args[] = { { value|null }, { any|error|null }, { any|error|null } };
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


// random (a,b [, resolution])  float random value from a up to and including b, with optional resolution step
static const BuiltInArgDesc random_args[] = { { numeric }, { numeric }, { numeric|optionalarg } };
static const size_t random_numargs = sizeof(random_args)/sizeof(BuiltInArgDesc);
static void random_func(BuiltinFunctionContextPtr f)
{
  double offs = f->arg(0)->doubleValue();
  double sz = f->arg(1)->doubleValue()-offs;
  double res = f->arg(2)->doubleValue();
  if (res>0) {
    sz += res-0.000001;
  }
  // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
  double rnd = (double)rand()*sz/((double)RAND_MAX);
  if (res>0) {
    rnd = int(rnd/res)*res;
  }
  f->finish(new NumericValue(rnd+offs));
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
  // Note: This is the only way to get the stringValue() of a derived StringValue which
  // signals "null" in its getTypeInfo().
  f->finish(new StringValue(f->arg(0)->stringValue())); // force convert to string, including nulls and errors
}


// describe(anything)
static const BuiltInArgDesc describe_args[] = { { any|error|null } };
static const size_t describe_numargs = sizeof(string_args)/sizeof(BuiltInArgDesc);
static void describe_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(ScriptObj::describe(f->arg(0))));
}


// annotation(anything)
static const BuiltInArgDesc annotation_args[] = { { any|error|null } };
static const size_t annotation_numargs = sizeof(string_args)/sizeof(BuiltInArgDesc);
static void annotation_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(f->arg(0)->getAnnotation()));
}


// number(anything)
static const BuiltInArgDesc number_args[] = { { any|error|null } };
static const size_t number_numargs = sizeof(number_args)/sizeof(BuiltInArgDesc);
static void number_func(BuiltinFunctionContextPtr f)
{
  // Note: This is the only way to get the doubleValue() of a derived NumericValue which
  // signals "null" in its getTypeInfo().
  f->finish(new NumericValue(f->arg(0)->doubleValue())); // force convert to numeric
}

// boolean(anything)
static const BuiltInArgDesc boolean_args[] = { { any|error|null } };
static const size_t boolean_numargs = sizeof(boolean_args)/sizeof(BuiltInArgDesc);
static void boolean_func(BuiltinFunctionContextPtr f)
{
  // Note: This is the only way to get the boolValue() of a derived NumericValue which
  // signals "null" in its getTypeInfo().
  f->finish(new BoolValue(f->arg(0)->boolValue())); // force convert to numeric boolean
}


#if SCRIPTING_JSON_SUPPORT

// json(anything [, allowcomments])     parse json from string, or get json representation of other objects that support it (=native JSON and JsonRepresentedValue)
static const BuiltInArgDesc json_args[] = { { any }, { numeric|optionalarg } };
static const size_t json_numargs = sizeof(json_args)/sizeof(BuiltInArgDesc);
static void json_func(BuiltinFunctionContextPtr f)
{
  JsonObjectPtr j;
  if (f->arg(0)->hasType(text)) {
    // parse from string
    string jstr = f->arg(0)->stringValue();
    ErrorPtr err;
    j = JsonObject::objFromText(jstr.c_str(), jstr.size(), &err, f->arg(1)->boolValue());
    if (Error::notOK(err)) {
      f->finish(new ErrorValue(err));
      return;
    }
  }
  else {
    // just the JSON representation of the object
    j = f->arg(0)->jsonValue();
  }
  f->finish(new JsonValue(j));
}


// elements(array)
static const BuiltInArgDesc elements_args[] = { { any|undefres } };
static const size_t elements_numargs = sizeof(elements_args)/sizeof(BuiltInArgDesc);
static void elements_func(BuiltinFunctionContextPtr f)
{
  if (f->arg(0)->hasType(structured)) {
    f->finish(new NumericValue((int)f->arg(0)->numIndexedMembers()));
    return;
  }
  f->finish(new AnnotatedNullValue("not an array or object"));
}


#if ENABLE_JSON_APPLICATION

static const BuiltInArgDesc jsonresource_args[] = { { text+undefres } };
static const size_t jsonresource_numargs = sizeof(jsonresource_args)/sizeof(BuiltInArgDesc);
static void jsonresource_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err;
  string fn = f->arg(0)->stringValue();
  #if !ALWAYS_ALLOW_ALL_FILES
  if (
    (fn.find("/")!=string::npos || fn.find("..")!=string::npos) &&
    Application::sharedApplication()->userLevel()<1 // user level 1 is allowed to read everywhere
  ) {
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no path reading privileges"));
    return;
  }
  #endif
  JsonObjectPtr j = Application::jsonResource(fn, &err);
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


#if P44SCRIPT_FULL_SUPPORT

// maprange(x, a1, b1, a2, b2)    map the range a1..a2 onto the range a2..b2
static const BuiltInArgDesc maprange_args[] = { { scalar|undefres }, { numeric }, { numeric }, { numeric }, { numeric } };
static const size_t maprange_numargs = sizeof(maprange_args)/sizeof(BuiltInArgDesc);
static void maprange_func(BuiltinFunctionContextPtr f)
{
  double x = f->arg(0)->doubleValue();
  double a1 = f->arg(1)->doubleValue();
  double b1 = f->arg(2)->doubleValue();
  double a2 = f->arg(3)->doubleValue();
  double b2 = f->arg(4)->doubleValue();
  double res = 0;
  double min1 = a1;
  double max1 = b1;
  if (a1>b1) { min1 = b1; max1 = a1; }
  if (x<min1) x = min1;
  else if (x>max1) x = max1;
  if (b1-a1==0) {
    res = a2; // no range: always start of output range
  }
  else {
    res = (x-a1)/(b1-a1)*(b2-a2)+a2;
  }
  f->finish(new NumericValue(res));
}


// ord(string)
static const BuiltInArgDesc ord_args[] = { { text } };
static const size_t ord_numargs = sizeof(ord_args)/sizeof(BuiltInArgDesc);
static void ord_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((uint8_t)*f->arg(0)->stringValue().c_str()));
}


// chr(number)
static const BuiltInArgDesc chr_args[] = { { numeric } };
static const size_t chr_numargs = sizeof(chr_args)/sizeof(BuiltInArgDesc);
static void chr_func(BuiltinFunctionContextPtr f)
{
  string s;
  s.append(1, (char)(f->arg(0)->intValue() & 0xFF));
  f->finish(new StringValue(s));
}


// hex(binarystring [,bytesep])
static const BuiltInArgDesc hex_args[] = { { text }, { text|optionalarg } };
static const size_t hex_numargs = sizeof(hex_args)/sizeof(BuiltInArgDesc);
static void hex_func(BuiltinFunctionContextPtr f)
{
  char sep = 0;
  if (f->numArgs()>1) sep = *(f->arg(1)->stringValue().c_str());
  f->finish(new StringValue(binaryToHexString(f->arg(0)->stringValue(), sep)));
}


// binary(hexstring [, spacesallowed])
static const BuiltInArgDesc binary_args[] = { { text }, { numeric|optionalarg } };
static const size_t binary_numargs = sizeof(binary_args)/sizeof(BuiltInArgDesc);
static void binary_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(hexToBinaryString(f->arg(0)->stringValue().c_str(), f->arg(1)->boolValue())));
}


static uint64_t bitmask(int aNextArg, int& aLoBit, int& aHiBit, BuiltinFunctionContextPtr f)
{
  aLoBit = f->arg(0)->intValue();
  aHiBit = aLoBit;
  if (aNextArg>1) {
    aHiBit = f->arg(1)->intValue();
    if (aHiBit<aLoBit) {
      swap(aHiBit, aLoBit);
    }
    if (aLoBit<0) aLoBit=0;
    if (aHiBit>63) aHiBit=63;
  }
  return (((uint64_t)-1)>>(63-aHiBit))<<aLoBit;
}

// bit(bitno, value) - get bit from value
// bit(firstbit, lastbit, value [, signed]) - get bit range from value
static const BuiltInArgDesc bit_args[] = { { numeric }, { numeric }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t bit_numargs = sizeof(bit_args)/sizeof(BuiltInArgDesc);
static void bit_func(BuiltinFunctionContextPtr f)
{
  int nextarg = f->numArgs()>2 ? 2 : 1;
  int loBit, hiBit;
  uint64_t mask = bitmask(nextarg, loBit, hiBit, f);
  uint64_t r = f->arg(nextarg)->int64Value();
  r = (r & mask)>>loBit;
  if (f->arg(3)->boolValue() && (r & (1<<hiBit))) {
    // extend sign
    r |= ~mask;
  }
  f->finish(new NumericValue((int64_t)r));
}


// setbit(bitno, newbit, value) - set a bit in value
// setbit(firstbit, lastbit, newvalue, value) - set bit range in value
static const BuiltInArgDesc setbit_args[] = { { numeric }, { numeric }, { numeric }, { numeric|optionalarg } };
static const size_t setbit_numargs = sizeof(setbit_args)/sizeof(BuiltInArgDesc);
static void setbit_func(BuiltinFunctionContextPtr f)
{
  int nextarg = f->numArgs()>3 ? 2 : 1;
  int loBit, hiBit;
  uint64_t mask = bitmask(nextarg, loBit, hiBit, f);
  uint64_t newbits = f->arg(nextarg)->int64Value();
  if (nextarg==1) newbits = (newbits!=0); // for single bits, treat newbit as bool
  uint64_t v = f->arg(nextarg+1)->int64Value();
  v = (v & ~mask) | ((newbits<<loBit) & mask);
  f->finish(new NumericValue((int64_t)v));
}


// flipbit(bitno, value)
// flipbit(firstbit, lastbit, value)
static const BuiltInArgDesc flipbit_args[] = { { numeric }, { numeric }, { numeric|optionalarg } };
static const size_t flipbit_numargs = sizeof(flipbit_args)/sizeof(BuiltInArgDesc);
static void flipbit_func(BuiltinFunctionContextPtr f)
{
  int nextarg = f->numArgs()>2 ? 2 : 1;
  int loBit, hiBit;
  uint64_t mask = bitmask(nextarg, loBit, hiBit, f);
  uint64_t v = f->arg(nextarg)->int64Value();
  v ^= mask;
  f->finish(new NumericValue((int64_t)v));
}


// strlen(string)
static const BuiltInArgDesc strlen_args[] = { { text|undefres } };
static const size_t strlen_numargs = sizeof(strlen_args)/sizeof(BuiltInArgDesc);
static void strlen_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((double)f->arg(0)->stringValue().size())); // length of string
}


// strrep(string, count)
static const BuiltInArgDesc strrep_args[] = { { text|undefres }, { numeric|undefres } };
static const size_t strrep_numargs = sizeof(strrep_args)/sizeof(BuiltInArgDesc);
static void strrep_func(BuiltinFunctionContextPtr f)
{
  string s = f->arg(0)->stringValue();
  size_t count = f->arg(1)->intValue();
  string r;
  r.reserve(count*s.length());
  while (count-- > 0) {
    r += s;
  }
  f->finish(new StringValue(r));
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
// find(haystack, needle, from, caseinsensitive)
static const BuiltInArgDesc find_args[] = { { text|undefres }, { text }, { numeric|optionalarg }  };
static const size_t find_numargs = sizeof(find_args)/sizeof(BuiltInArgDesc);
static void find_func(BuiltinFunctionContextPtr f)
{
  string haystack;
  string needle;
  if (f->arg(3)->boolValue()) {
    haystack = lowerCase(f->arg(0)->stringValue());
    needle = lowerCase(f->arg(1)->stringValue());
  }
  else {
    haystack = f->arg(0)->stringValue();
    needle = f->arg(1)->stringValue();
  }
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


// replace(haystack, needle, replacement)
// replace(haystack, needle, replacement, howmany)
static const BuiltInArgDesc replace_args[] = { { text|undefres }, { text }, { text }, { numeric|optionalarg } };
static const size_t replace_numargs = sizeof(replace_args)/sizeof(BuiltInArgDesc);
static void replace_func(BuiltinFunctionContextPtr f)
{
  string haystack;
  string needle;
  if (f->arg(4)->boolValue()) {
    haystack = lowerCase(f->arg(0)->stringValue());
    needle = lowerCase(f->arg(1)->stringValue());
  }
  else {
    haystack = f->arg(0)->stringValue();
    needle = f->arg(1)->stringValue();
  }
  int rep = 0;
  if (f->arg(3)->defined()) {
    rep = f->arg(3)->intValue();
  }
  f->finish(new StringValue(string_substitute(
    f->arg(0)->stringValue(), f->arg(1)->stringValue(),
    f->arg(2)->stringValue(), rep
  )));
}



// uppercase(string)
// lowercase(string)
static const BuiltInArgDesc xcase_args[] = { { text|undefres } };
static const size_t xcase_numargs = sizeof(xcase_args)/sizeof(BuiltInArgDesc);
static void uppercase_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(upperCase(f->arg(0)->stringValue())));
}
static void lowercase_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(lowerCase(f->arg(0)->stringValue())));
}


// shellquote(shellargument)
static const BuiltInArgDesc shellquote_args[] = { { any } };
static const size_t shellquote_numargs = sizeof(shellquote_args)/sizeof(BuiltInArgDesc);
static void shellquote_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(shellQuote(f->arg(0)->stringValue())));
}


// cquote(string)
static const BuiltInArgDesc cquote_args[] = { { any } };
static const size_t cquote_numargs = sizeof(cquote_args)/sizeof(BuiltInArgDesc);
static void cquote_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(cstringQuote(f->arg(0)->stringValue())));
}


// helper for log() and format()
static ScriptObjPtr format_string(BuiltinFunctionContextPtr f, size_t aFmtArgIdx)
{
  string fmt = f->arg(aFmtArgIdx)->stringValue();
  string res;
  const char* p = fmt.c_str();
  size_t ai = aFmtArgIdx+1;
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
          return new ErrorValue(ScriptError::Syntax, "invalid format string, only basic %%duxXeEgGfs specs allowed");
        }
        p=e;
      }
    }
  }
  return new StringValue(res);
}


// format(formatstring, value [, value...])
// only % + - 0..9 . d, x, and f supported
static const BuiltInArgDesc format_args[] = { { text }, { any|null|error|multiple } };
static const size_t format_numargs = sizeof(format_args)/sizeof(BuiltInArgDesc);
static void format_func(BuiltinFunctionContextPtr f)
{
  f->finish(format_string(f, 0));
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
  else if (t>Day || t<0) fmt = "%Y-%m-%d %H:%M:%S"; // negatives are dates before 1970 (only 1970-01-01 will be shown as time-only by default)
  else fmt = "%H:%M:%S";
  MainLoop::getLocalTime(disptim, NULL, t, t<Day);
  f->finish(new StringValue(string_ftime(fmt.c_str(), &disptim)));
}


// throw(value)       - throw value as-is if it is an error value, otherwise a user error with value converted to string as errormessage
static const BuiltInArgDesc throw_args[] = { { any|error } };
static const size_t throw_numargs = sizeof(throw_args)/sizeof(BuiltInArgDesc);
static void throw_func(BuiltinFunctionContextPtr f)
{
  // throw(errvalue)    - (re-)throw with the error of the value passed
  ScriptObjPtr throwVal;
  ErrorValuePtr e = dynamic_pointer_cast<ErrorValue>(f->arg(0));
  if (e) {
    e->setCaught(false); // make sure it will throw, even if generated e.g. by error() or in catch as x {}
    throwVal = e;
  }
  else {
    throwVal = new ErrorValue(ScriptError::User, "%s", f->arg(0)->stringValue().c_str());
  }
  f->finish(throwVal);
}


// error(value)       - create a user error value with the string value of value as errormessage, in all cases, even if value is already an error
static const BuiltInArgDesc error_args[] = { { any|error|null } };
static const size_t error_numargs = sizeof(error_args)/sizeof(BuiltInArgDesc);
static void error_func(BuiltinFunctionContextPtr f)
{
  ErrorValuePtr e = new ErrorValue(Error::err<ScriptError>(ScriptError::User, "%s", f->arg(0)->stringValue().c_str()));
  e->setCaught(true); // mark it caught already, so it can be passed as a regular value without throwing
  f->finish(e);
}


// errordomain(errvalue)
static const BuiltInArgDesc errordomain_args[] = { { error|undefres } };
static const size_t errordomain_numargs = sizeof(errordomain_args)/sizeof(BuiltInArgDesc);
static void errordomain_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("no error")); // no error, no domain
  f->finish(new StringValue(err->getErrorDomain()));
}


// errorcode(errvalue)
static const BuiltInArgDesc errorcode_args[] = { { error|undefres } };
static const size_t errorcode_numargs = sizeof(errorcode_args)/sizeof(BuiltInArgDesc);
static void errorcode_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("no error")); // no error, no code
  f->finish(new NumericValue((double)err->getErrorCode()));
}


// errormessage(value)
static const BuiltInArgDesc errormessage_args[] = { { error|undefres } };
static const size_t errormessage_numargs = sizeof(errormessage_args)/sizeof(BuiltInArgDesc);
static void errormessage_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("no error")); // no error, no message
  f->finish(new StringValue(err->getErrorMessage()));
}


// eval(string, [args...])    have string executed as script code, with access to optional args as arg1, arg2, ... argN
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
    ScriptHost src(
      scriptbody|anonymousfunction,
      "eval function", nullptr,
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
      ctx->execute(evalcode, scriptbody|mainthread|keepvars, boost::bind(&BuiltinFunctionContext::finish, f, _1), NULL);
      return;
    }
  }
  f->finish(evalcode); // return object itself, is an error or cannot be executed
}


// maxblocktime([time [, localthreadonly]])
static const BuiltInArgDesc maxblocktime_args[] = { { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t maxblocktime_numargs = sizeof(maxblocktime_args)/sizeof(BuiltInArgDesc);
static void maxblocktime_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==0) {
    f->finish(new NumericValue((double)(f->thread()->getMaxBlockTime())/Second));
  }
  else {
    MLMicroSeconds mbt = f->arg(0)->doubleValue()*Second;
    f->thread()->setMaxBlockTime(mbt); // always set for current thread
    if (!f->arg(1)->boolValue()) f->domain()->setMaxBlockTime(mbt); // if localthreadonly is not set, also change domain wide default
    f->finish();
  }
}


// maxruntime([time])
static const BuiltInArgDesc maxruntime_args[] = { { numeric|null|optionalarg } };
static const size_t maxruntime_numargs = sizeof(maxruntime_args)/sizeof(BuiltInArgDesc);
static void maxruntime_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==0) {
    MLMicroSeconds mrt = f->thread()->getMaxRunTime();
    if (mrt==Infinite) f->finish(new AnnotatedNullValue("no run time limit"));
    else f->finish(new NumericValue((double)mrt/Second));
  }
  else {
    double d = f->arg(0)->doubleValue();
    if (d>0) f->thread()->setMaxRunTime(d*Second);
    else f->thread()->setMaxRunTime(Infinite);
    f->finish();
  }
}


static void breakpoint_func(BuiltinFunctionContextPtr f)
{
  #if P44SCRIPT_DEBUGGING_SUPPORT
  if (f->thread()->pauseCheck(breakpoint)) {
    FLOG(f, LOG_WARNING, "breakpoint() in script source");
    return;
  }
  #endif
  f->finish();
}


#if !ESP_PLATFORM

// system(command_line)    execute given command line via system() in a shell and return the output
static void system_abort(pid_t aPid)
{
  // terminate the external command
  kill(aPid, SIGTERM);
}
static void system_done(BuiltinFunctionContextPtr f, ErrorPtr aError, const string &aOutputString)
{
  if (Error::isOK(aError)) {
    f->finish(new StringValue(aOutputString));
  }
  else {
    f->finish(new ErrorValue(aError));
  }
}
static const BuiltInArgDesc system_args[] = { { text } };
static const size_t system_numargs = sizeof(system_args)/sizeof(BuiltInArgDesc);
static void system_func(BuiltinFunctionContextPtr f)
{
  #if !ALWAYS_ALLOW_SYSTEM_FUNC
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<2)
  #endif
  {
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no privileges to use system() function"));
    return;
  }
  #endif // ALWAYS_ALLOW_SYSTEM_FUNC
  pid_t pid = MainLoop::currentMainLoop().fork_and_system(boost::bind(&system_done, f, _1, _2), f->arg(0)->stringValue().c_str(), true);
  if (pid>=0) {
    f->setAbortCallback(boost::bind(&system_abort, pid));
  }
}


#if ENABLE_APPLICATION_SUPPORT

// restartapp()
// restartapp(shutdown|reboot|upgrade)
static const BuiltInArgDesc restartapp_args[] = { { text|optionalarg } };
static const size_t restartapp_numargs = sizeof(restartapp_args)/sizeof(BuiltInArgDesc);
static void restartapp_func(BuiltinFunctionContextPtr f)
{
  int ec = 0; // default to regular termination
  string opt = f->arg(0)->stringValue();
  if (uequals(opt, "shutdown")) ec = P44_EXIT_SHUTDOWN; // p44 vdcd daemon specific exit code
  else if (uequals(opt, "reboot")) ec = P44_EXIT_REBOOT; // p44 vdcd daemon specific exit code
  else if (uequals(opt, "upgrade")) ec = P44_EXIT_FIRMWAREUPDATE; // p44 vdcd daemon specific exit code
  LOG(LOG_WARNING, "Application will terminate with exit code %d because script called restartapp()", ec);
  Application::sharedApplication()->terminateApp(ec); // regular termination
  f->finish();
}


// appversion()
static void appversion_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(Application::sharedApplication()->version()));
}


// writefile(filename, data [, append])
static const BuiltInArgDesc writefile_args[] = { { text }, { any|null }, { numeric|optionalarg } };
static const size_t writefile_numargs = sizeof(writefile_args)/sizeof(BuiltInArgDesc);
static void writefile_func(BuiltinFunctionContextPtr f)
{
  string fn = f->arg(0)->stringValue();
  if (fn.empty()) {
    f->finish(new ErrorValue(ScriptError::Invalid, "no filename"));
    return;
  }
  #if !ALWAYS_ALLOW_ALL_FILES
  size_t psz = fn.substr(0,2)=="_/" ? 2 : 0; // allow _/ temp prefix (but none of the others: =/ and +/)
  if (
    Application::sharedApplication()->userLevel()<2 && // only user level 2 is allowed to write everywhere
    (fn.find("/", psz)!=string::npos || fn.find("..", psz)!=string::npos)
  ) {
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no path writing privileges"));
    return;
  }
  #endif
  fn = Application::sharedApplication()->dataPath(fn, P44SCRIPT_DATA_SUBDIR "/", true);
  ErrorPtr err;
  if (f->arg(1)->defined()) {
    // write or append
    ErrorPtr err;
    FILE* file = fopen(fn.c_str(), f->arg(2)->boolValue() ? "a" : "w");
    if (file==NULL) {
      err = SysError::errNo();
    }
    else {
      string s = f->arg(1)->stringValue();
      if (fwrite(s.c_str(), s.size(), 1, file)<1) {
        err = SysError::errNo();
      }
      fclose(file);
    }
    if (err) err->prefixMessage("Cannot write file: ");
  }
  else {
    // delete
    if (unlink(fn.c_str())<0) err = SysError::errNo()->withPrefix("Cannot delete file: ");
  }
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err));
    return;
  }
  f->finish();
}


// readfile(filename)
static const BuiltInArgDesc readfile_args[] = { { text } };
static const size_t readfile_numargs = sizeof(readfile_args)/sizeof(BuiltInArgDesc);
static void readfile_func(BuiltinFunctionContextPtr f)
{
  string fn = f->arg(0)->stringValue();
  if (fn.empty()) {
    f->finish(new ErrorValue(ScriptError::Invalid, "no filename"));
    return;
  }
  #if !ALWAYS_ALLOW_ALL_FILES
  size_t psz = fn.substr(0,2)=="_/" ? 2 : 0; // allow _/ temp prefix (but none of the others: =/ and +/)
  if (
    Application::sharedApplication()->userLevel()<1 && // user level 1 is allowed to read everywhere
    (fn.find("/", psz)!=string::npos || fn.find("..", psz)!=string::npos)
  ) {
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no path reading privileges"));
    return;
  }
  #endif
  string data;
  ErrorPtr err = string_fromfile(Application::sharedApplication()->dataPath(fn, P44SCRIPT_DATA_SUBDIR "/", false), data);
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err));
    return;
  }
  f->finish(new StringValue(data));
}

#endif // ENABLE_APPLICATION_SUPPORT
#endif // !ESP_PLATFORM




typedef boost::function<void (bool aEntered)> LockCB;

// Lock Object for thread synchronisation, see lock()
class LockObj : public NumericValue, public EventSource
{
  typedef NumericValue inherited;
  ScriptCodeThreadPtr mCurrentThread;
  int mLockCount;
  class LockWaiter {
  public:
    LockWaiter() : threadP(NULL) {};
    LockWaiter(const LockWaiter& aOther);
    MLTicket timeoutTicket;
    ScriptCodeThread* threadP;
    LockCB lockCB;
  };
  typedef std::list<LockWaiter> WaitersList;
  WaitersList mWaiters;

  void locktimeout(LockWaiter &aWaiter);

public:

  LockObj() : inherited(1), mLockCount(0) { }
  LockObj(ScriptCodeThread *aEnteredForThread) : inherited(1), mCurrentThread(aEnteredForThread), mLockCount(1) { }

  virtual void deactivate() P44_OVERRIDE;
  virtual string getAnnotation() const P44_OVERRIDE { return "Lock"; }
  virtual TypeInfo getTypeInfo() const P44_OVERRIDE {
    return inherited::getTypeInfo()|oneshot|keeporiginal;
  }
  virtual double doubleValue() const P44_OVERRIDE { return mLockCount; };
  virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags = none) P44_OVERRIDE;

  /// try to enter the lock
  /// @param aThread the thread that wants to enter
  /// @return true if entered, false if locked by another thread
  bool enter(ScriptCodeThreadPtr aThread);

  /// register
  void registerLockCB(ScriptCodeThread* aThreadP, LockCB aLockCB, MLMicroSeconds aTimeout);

  /// leave the lock
  /// @return true if thread actually had entered that lock before and could leave now
  bool leave(ScriptCodeThreadPtr aThread);
};
typedef boost::intrusive_ptr<LockObj> LockObjPtr;

LockObj::LockWaiter::LockWaiter(const LockWaiter& aOther)
{
  timeoutTicket = aOther.timeoutTicket;
  const_cast<LockWaiter&>(aOther).timeoutTicket.defuse();
  threadP = aOther.threadP;
  lockCB = aOther.lockCB;
}

void LockObj::deactivate()
{
  mCurrentThread.reset();
  // fail all pending entering attempts
  while (!mWaiters.empty()) {
    mWaiters.front().timeoutTicket.cancel();
    LockCB cb = mWaiters.front().lockCB;
    mWaiters.pop_front();
    cb(false);
  }
  inherited::deactivate();
}


bool LockObj::enter(ScriptCodeThreadPtr aThread)
{
  if (mCurrentThread) {
    // already locked, see if by same thread
    if (mCurrentThread->chainOriginThread()!=aThread->chainOriginThread()) {
      // not same logical thread, cannot enter
      return false;
    }
    // same thread chain, can lock
    mLockCount++;
  }
  else {
    // not locked yet
    mCurrentThread = aThread;
    mLockCount = 1;
  }
  return true;
}

void LockObj::locktimeout(LockWaiter &aWaiter)
{
  // remove waiter
  for (WaitersList::iterator pos = mWaiters.begin(); pos!=mWaiters.end(); ++pos) {
    if (pos->threadP==aWaiter.threadP) {
      mWaiters.erase(pos);
      break;
    }
  }
  // inform caller of timeout occurred
  aWaiter.lockCB(false);
}

void LockObj::registerLockCB(ScriptCodeThread* aThreadP, LockCB aLockCB, MLMicroSeconds aTimeout)
{
  LockWaiter w;
  w.threadP = aThreadP; // for comparison only, never dereferenced
  w.lockCB = aLockCB;
  if (aTimeout!=Infinite) {
    w.timeoutTicket.executeOnce(boost::bind(&LockObj::locktimeout, this, w), aTimeout);
  }
  mWaiters.push_back(w);
}

bool LockObj::leave(ScriptCodeThreadPtr aThread)
{
  if (!mCurrentThread || aThread->chainOriginThread()!=mCurrentThread->chainOriginThread()) return false; // not locked by aThread, can't leave
  assert(mLockCount>0);
  if (mLockCount>1) {
    mLockCount--;
  }
  else {
    // this thread chain leaves the lock
    if (mWaiters.empty()) {
      // nobody waiting any more
      mLockCount = 0;
      mCurrentThread.reset();
    }
    else {
      // pass the lock to the longest waiting thread (first in mWaiters list)
      mWaiters.front().timeoutTicket.cancel();
      LockCB cb = mWaiters.front().lockCB;
      mCurrentThread = mWaiters.front().threadP; // pass lock to this thread
      mWaiters.pop_front();
      cb(true);
    }
  }
  return true;
}


void endLockWait(BuiltinFunctionContextPtr f, bool aEntered)
{
  f->finish(new BoolValue(aEntered));
}

// enter([timeout])    wait until we can enter or timeout expires. returns true if entered, false on timeout
static const BuiltInArgDesc enter_args[] = { { numeric|optionalarg } };
static const size_t enter_numargs = sizeof(enter_args)/sizeof(BuiltInArgDesc);
static void enter_func(BuiltinFunctionContextPtr f)
{
  LockObj* lock = dynamic_cast<LockObj *>(f->thisObj().get());
  MLMicroSeconds timeout = Infinite;
  if (f->numArgs()>=1) timeout = f->arg(0)->doubleValue()*Second;
  bool entered = lock->enter(f->thread());
  if (!entered && timeout!=0) {
    // wait
    lock->registerLockCB(f->thread().get(), boost::bind(&endLockWait, f, _1), timeout);
    return;
  }
  // report right now
  f->finish(new BoolValue(entered));
}

// leave()    leave (release) the lock for another thread to enter
static void leave_func(BuiltinFunctionContextPtr f)
{
  LockObj* lock = dynamic_cast<LockObj *>(f->thisObj().get());
  f->finish(new BoolValue(lock->leave(f->thread())));
}


static const BuiltinMemberDescriptor enter_desc =
  { "enter", executable|numeric|async, enter_numargs, enter_args, &enter_func };
static const BuiltinMemberDescriptor leave_desc =
  { "leave", executable|numeric, 0, NULL, &leave_func };

const ScriptObjPtr LockObj::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  if (uequals(aName, "enter")) {
    return new BuiltinFunctionObj(&enter_desc, this, NULL);
  }
  else if (uequals(aName, "leave")) {
    return new BuiltinFunctionObj(&leave_desc, this, NULL);
  }
  return inherited::memberByName(aName, aMemberAccessFlags);
}

// lock([createentered]) create a lock object, that can be entered and left
static const BuiltInArgDesc lock_args[] = { { numeric|optionalarg } };
static const size_t lock_numargs = sizeof(lock_args)/sizeof(BuiltInArgDesc);
static void lock_func(BuiltinFunctionContextPtr f)
{
  if (f->arg(0)->boolValue()) {
    f->finish(new LockObj(f->thread().get()));
  }
  else {
    f->finish(new LockObj());
  }
}


// Signal Object for event passing, see signal()
class SignalObj : public OneShotEventNullValue, public EventSource
{
  typedef OneShotEventNullValue inherited;
public:
  SignalObj() : inherited(static_cast<EventSource *>(this), "Signal") { }
  virtual string getAnnotation() const P44_OVERRIDE { return "Signal"; }
  virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags = none) P44_OVERRIDE;
};

// send([signaldata]) send the signal
static const BuiltInArgDesc signal_args[] = { { any|optionalarg } };
static const size_t signal_numargs = sizeof(signal_args)/sizeof(BuiltInArgDesc);
static void send_func(BuiltinFunctionContextPtr f)
{
  SignalObj* sig = dynamic_cast<SignalObj *>(f->thisObj().get());
  sig->sendEvent(f->numArgs()<1 ? new BoolValue(true) : f->arg(0)); // send first arg as signal value or nothing
  f->finish();
}
static const BuiltinMemberDescriptor answer_desc =
  { "send", executable|any, signal_numargs, signal_args, &send_func };

const ScriptObjPtr SignalObj::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  if (uequals(aName, "send")) {
    return new BuiltinFunctionObj(&answer_desc, this, NULL);
  }
  return inherited::memberByName(aName, aMemberAccessFlags);
}

// signal() create a signal object, that can be sent()t and await()ed
static void signal_func(BuiltinFunctionContextPtr f)
{
  f->finish(new SignalObj());
}


// await(event [, event...] [,timeout])    wait for an event (or one of serveral)
class AwaitEventSink : public EventSink
{
  BuiltinFunctionContextPtr f;
public:
  MLTicket timeoutTicket;
  AwaitEventSink(BuiltinFunctionContextPtr aF) : f(aF) {};
  virtual void processEvent(ScriptObjPtr aEvent, EventSource &aSource, intptr_t aRegId) P44_OVERRIDE
  {
    // unwind stack before actually responding (to avoid changing containers this event originates from)
    MainLoop::currentMainLoop().executeNow(boost::bind(&AwaitEventSink::finishWait, this, aEvent));
  }
  void finishWait(ScriptObjPtr aEvent)
  {
    f->finish(aEvent);
    f->setAbortCallback(NoOP);
    delete this;
  }
  void timeout()
  {
    f->finish(new AnnotatedNullValue("await timeout"));
    f->setAbortCallback(NoOP);
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
    ScriptObjPtr v = f->arg(ai);
    ScriptObjPtr cv = v->calculationValue(); // e.g. threadVars detect stopped thread only when being asked for calculation value first
    EventSource* ev = v->eventSource(); // ...but ask original value for event source (calculation value is not an event itself)
    if (!ev) {
      // must be last arg, and not first arg, and numeric to be timeout, otherwise just return it
      if (ai>0 && ai==f->numArgs()-1 && f->arg(ai)->hasType(numeric)) {
        to = f->arg(ai)->doubleValue()*Second;
        break;
      }
      // not an event source -> just immediately return the value itself
      delete awaitEventSink;
      f->finish(v);
      return;
    }
    // is an event source...
    if (v->hasType(nowait)) {
      // ...but should not be awaited now
      delete awaitEventSink;
      f->finish(v);
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


// abort(thread [,exitvalue [, ownchain])   abort specified thread
// abort()                                  abort all subthreads
static const BuiltInArgDesc abort_args[] = { { threadref|exacttype|optionalarg }, { any|optionalarg }, { numeric|optionalarg } };
static const size_t abort_numargs = sizeof(abort_args)/sizeof(BuiltInArgDesc);
static void abort_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()>0) {
    // single thread represented by arg0
    ThreadValue *t = dynamic_cast<ThreadValue *>(f->arg(0).get());
    if (t && t->running()) {
      // still running
      if (!f->arg(2)->boolValue() && f->thread()->chainOriginThread()==t->thread()) {
        // is in my chain of execution (apparent user thread)
        f->finish(new AnnotatedNullValue("not aborting calling thread"));
        return;
      }
      else {
        // really abort
        ScriptObjPtr exitValue;
        if (f->arg(1)->defined()) exitValue = f->arg(1);
        else exitValue = new AnnotatedNullValue("abort() function called");
        t->abort(exitValue);
      }
    }
  }
  else {
    // all subthreads
    f->thread()->abortOthers(stopall);
  }
  f->finish();
}


// delay(seconds)
static void delay_abort(TicketObjPtr aTicket)
{
  aTicket->mTicket.cancel();
}
static const BuiltInArgDesc delayx_args[] = { { numeric } };
static const size_t delayx_numargs = sizeof(delayx_args)/sizeof(BuiltInArgDesc);
static void delay_func(BuiltinFunctionContextPtr f)
{
  MLMicroSeconds delay = f->arg(0)->doubleValue()*Second;
  TicketObjPtr delayTicket = TicketObjPtr(new TicketObj);
  delayTicket->mTicket.executeOnce(boost::bind(&BuiltinFunctionContext::finish, f, new AnnotatedNullValue("delayed")), delay);
  f->setAbortCallback(boost::bind(&delay_abort, delayTicket));
}
static void delayuntil_func(BuiltinFunctionContextPtr f)
{
  MLMicroSeconds until;
  double u = f->arg(0)->doubleValue();
  if (u<24*60*60*365) {
    // small times (less than a year) are considered relative to 0:00 of today (this is what time literals represent)
    struct tm loctim; MainLoop::getLocalTime(loctim);
    loctim.tm_sec = (int)u;
    u -= loctim.tm_sec;
    loctim.tm_hour = 0;
    loctim.tm_min = 0;
    u += mktime(&loctim);
  }
  // now u is epoch time in seconds
  until = MainLoop::unixTimeToMainLoopTime(u*Second);
  TicketObjPtr delayTicket = TicketObjPtr(new TicketObj);
  delayTicket->mTicket.executeOnceAt(boost::bind(&BuiltinFunctionContext::finish, f, new AnnotatedNullValue("delayed")), until);
  f->setAbortCallback(boost::bind(&delay_abort, delayTicket));
}



// undeclare()    undeclare functions and handlers - only works in ephemeralSource threads
static void undeclare_func(BuiltinFunctionContextPtr f)
{
  if ((f->evalFlags() & ephemeralSource)==0) {
    f->finish(new ErrorValue(ScriptError::Invalid, "undeclare() can only be used in interactive sessions"));
    return;
  }
  // clear floating globals in the domain
  f->thread()->owner()->domain()->clearFloating();
  // clear floating globals in the current main context (especially: handlers)
  f->thread()->owner()->scriptmain()->clearFloating();
  f->finish();
}



// log (tobeloggedobj)
// log (logformat, ...)
// log (loglevel, tobeloggedobj)
// log (loglevel, logformat, ...)
static const BuiltInArgDesc log_args[] = { { any|null|error|multiple } };
static const size_t log_numargs = sizeof(log_args)/sizeof(BuiltInArgDesc);
static void log_func(BuiltinFunctionContextPtr f)
{
  int loglevel = LOG_NOTICE;
  size_t ai = 0;
  if (f->numArgs()>=2 && f->arg(0)->hasType(numeric)) {
    loglevel = f->arg(ai)->intValue();
    ai++;
  }
  if (LOGENABLED(loglevel)) {
    ScriptObjPtr msg;
    if (f->numArgs()>ai+1) {
      // only format if there are params beyond format string
      msg = BuiltinFunctions::format_string(f, ai);
    }
    else {
      msg = f->arg(ai);
    }
    FLOG(f, loglevel, "Script log: %s", msg->stringValue().c_str());
    f->finish(msg); // also return the message logged
  }
  else {
    f->finish(new AnnotatedNullValue("not logged, loglevel is disabled"));
  }
}


// loglevel()
// loglevel(newlevel [, deltatime, [, symbols [, coloring]])
static const BuiltInArgDesc loglevel_args[] = { { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t loglevel_numargs = sizeof(loglevel_args)/sizeof(BuiltInArgDesc);
static void loglevel_func(BuiltinFunctionContextPtr f)
{
  int oldLevel = LOGLEVEL;
  if (f->numArgs()>0) {
    if (f->arg(1)->hasType(numeric)) {
      int newLevel = f->arg(0)->intValue();
      if (newLevel==8) {
        // trigger statistics
        LOG(LOG_NOTICE, "\n========== script requested mainloop statistics");
        LOG(LOG_NOTICE, "\n%s", MainLoop::currentMainLoop().description().c_str());
        MainLoop::currentMainLoop().statistics_reset();
        LOG(LOG_NOTICE, "========== statistics shown\n");
      }
      else if (newLevel>=0 && newLevel<=7) {
        SETLOGLEVEL(newLevel);
        LOG(newLevel, "\n\n========== script changed log level from %d to %d ===============", oldLevel, newLevel);
      }
    }
    if (f->numArgs()>1 && f->arg(1)->hasType(value)) {
      SETDELTATIME(f->arg(1)->boolValue());
    }
    if (f->numArgs()>2 && f->arg(2)->hasType(value)) {
      SETLOGSYMBOLS(f->arg(2)->boolValue());
    }
    if (f->numArgs()>3 && f->arg(3)->hasType(value)) {
      SETLOGCOLORING(f->arg(3)->boolValue());
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

#endif // P44SCRIPT_FULL_SUPPORT


// is_weekday(w,w,w,...)
static const BuiltInArgDesc is_weekday_args[] = { { numeric|multiple } };
static const size_t is_weekday_numargs = sizeof(is_weekday_args)/sizeof(BuiltInArgDesc);
static void is_weekday_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; MainLoop::getLocalTime(loctim);
  // check if any of the weekdays match
  int weekday = loctim.tm_wday; // 0..6, 0=sunday
  SourcePos::UniquePos freezeId = f->argId(0); // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
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
  ScriptObjPtr newRes = new BoolValue(isday);
  // freeze until next check: next day 0:00:00
  loctim.tm_mday++;
  loctim.tm_hour = 0;
  loctim.tm_min = 0;
  loctim.tm_sec = 0;
  ScriptObjPtr res = newRes;
  if (CompiledTrigger* trigger = f->trigger()) {
    CompiledTrigger::FrozenResult* frozenP = trigger->getTimeFrozenValue(res, freezeId);
    trigger->newTimedFreeze(frozenP, newRes, freezeId, MainLoop::localTimeToMainLoopTime(loctim));
  }
  f->finish(res); // freeze time over, use actual, newly calculated result
}


#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function

// common implementation for after_time() and is_time()
static void timeCheckFunc(bool aIsTime, BuiltinFunctionContextPtr f)
{
  struct tm loctim; MainLoop::getLocalTime(loctim);
  int newSecs;
  SourcePos::UniquePos freezeId = f->argId(0);
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
  if (trigger) frozenP = trigger->getTimeFrozenValue(secs, freezeId);
  bool met = daySecs>=secs->intValue();
  // next check at specified time, today if not yet met, tomorrow if already met for today
  loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = secs->intValue();
  FOCUSLOG("is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim), 3).c_str());
  bool res = met;
  // limit to a few secs around target if it's "is_time"
  if (aIsTime && met && daySecs<secs->intValue()+IS_TIME_TOLERANCE_SECONDS) {
    // freeze again for a bit
    if (trigger) trigger->newTimedFreeze(frozenP, secs, freezeId, MainLoop::localTimeToMainLoopTime(loctim)+IS_TIME_TOLERANCE_SECONDS*Second);
  }
  else {
    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = newSecs;
    if (met) {
      loctim.tm_mday++; // already met today, check again at midnight tomorrow (to make sure result gets false before it gets true again!)
      loctim.tm_sec = 0; // midnight
      if (aIsTime) res = false;
    }
    if (trigger) trigger->newTimedFreeze(frozenP, new NumericValue(newSecs), freezeId, MainLoop::localTimeToMainLoopTime(loctim));
  }
  f->finish(new BoolValue(res));
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
  f->finish(new BoolValue((f->evalFlags()&initial)!=0));
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
    FLOG(f, LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", s, (double)MIN_RETRIGGER_SECONDS);
    s = MIN_RETRIGGER_SECONDS;
  }
  ScriptObjPtr secs = new NumericValue(s);
  ScriptObjPtr currentSecs = secs;
  SourcePos::UniquePos freezeId = f->argId(0);
  CompiledTrigger::FrozenResult* frozenP = trigger->getTimeFrozenValue(currentSecs, freezeId);
  bool evalNow = frozenP && !frozenP->frozen();
  if ((f->evalFlags()&timed)==0) {
    if ((f->evalFlags()&initial)==0 || retrigger) {
      // evaluating non-timed, non-initial (or retriggering) means "not yet ready" and must start or extend freeze period
      trigger->newTimedFreeze(frozenP, secs, freezeId, MainLoop::now()+s*Second, true);
    }
    evalNow = false; // never evaluate on non-timed run
  }
  else {
    // evaluating timed after frozen period means "now is later" and if retrigger is set, must start a new freeze
    if (frozenP && retrigger) {
      trigger->newTimedFreeze(frozenP, secs, freezeId, MainLoop::now()+secs->doubleValue()*Second);
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
    FLOG(f, LOG_WARNING, "every() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", s, (double)MIN_EVERY_SECONDS);
    s = MIN_EVERY_SECONDS;
  }
  ScriptObjPtr secs = new NumericValue(s);
  ScriptObjPtr currentSecs = secs;
  SourcePos::UniquePos freezeId = f->argId(0);
  CompiledTrigger::FrozenResult* frozenP = trigger->getTimeFrozenValue(currentSecs, freezeId);
  bool triggered = frozenP && !frozenP->frozen();
  if (triggered || (f->evalFlags()&initial)!=0) {
    // setup new interval
    double interval = s;
    if (syncoffset<0) {
      // no sync
      // - interval starts from now
      trigger->newTimedFreeze(frozenP, secs, freezeId, MainLoop::now()+s*Second, true);
      triggered = true; // fire even in initial evaluation
    }
    else {
      // synchronize with real time
      double fracSecs;
      struct tm loctim; MainLoop::getLocalTime(loctim, &fracSecs);
      double secondOfDay = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs; // second of day right now
      double untilNext = syncoffset+(floor((secondOfDay-syncoffset)/interval)+1)*interval - secondOfDay; // time to next repetition
      trigger->newTimedFreeze(frozenP, secs, freezeId, MainLoop::now()+untilNext*Second, true);
    }
    // also cause a immediate re-evaluation as every() is an instant that immediately goes away
    trigger->updateNextEval(MainLoop::now());
  }
  f->finish(new BoolValue(triggered));
}


static const BuiltInArgDesc between_dates_args[] = { { numeric }, { numeric } };
static const size_t between_dates_numargs = sizeof(between_dates_args)/sizeof(BuiltInArgDesc);
static void between_dates_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; MainLoop::getLocalTime(loctim);
  // days in year, 0=Jan 1st
  int from = (int)(f->arg(0)->doubleValue()); // from the START of this day
  int until = (int)(f->arg(1)->doubleValue()); // until the END of this day
  int currentYday = loctim.tm_yday;
  // start of the day
  loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = 0;
  loctim.tm_mon = 0;
  // determine if inside range and when to check next
  bool inside = false;
  int checkYday;
  bool checkNextYear = false;
  if (until<from) {
    // end day is in next year
    inside = currentYday>=from || currentYday<=until;
    // when to check next again
    if (inside) {
      checkYday = until+1; // inside: check again at the NEXT day after the last day inside...
      checkNextYear = currentYday>until; // ...which might be next year
    }
    else {
      checkYday = from; // outside: check again AT the first day inside
    }
  }
  else {
    // end day is same or after start day
    inside = currentYday>=from && currentYday<=until;
    // when to check next again - Note that tm_mday is 1 based!
    if (inside) {
      checkYday = until+1; // inside: check again at the NEXT day after the last day inside
    }
    else {
      checkYday = from; // outside: check again AT the first day inside...
      checkNextYear = currentYday>=from; // ...which might be next year
    }
  }
  // update next eval time
  CompiledTrigger* trigger = f->trigger();
  if (trigger) {
    // tm_mday is 1 based!
    if (checkNextYear) {
      loctim.tm_year += 1;
    }
    if (checkNextYear || currentYday+1!=checkYday) {
      // checkday is not tomorrow: schedule the check one day before to catch the day even over leap years and DST offsets
      checkYday--;
    }
    loctim.tm_mday = 1+checkYday;
    //DBGLOG(LOG_NOTICE, "between_dates_func: wants next check at %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim)).c_str());
    trigger->updateNextEval(loctim);
  }
  f->finish(new BoolValue(inside));
}


// helper for geolocation and datetime dependent functions, returns annotated NULL when no location is set
static bool checkSunParams(BuiltinFunctionContextPtr f, time_t &aTime)
{
  if (!f->geoLocation()) {
    f->finish(new AnnotatedNullValue("no geolocation information available"));
    return false;
  }
  //
  if (f->arg(0)->defined()) {
    aTime = f->arg(0)->int64Value(); // get
  }
  else {
    aTime = time(NULL);
  }
  return true;
}

// sunrise([epochtime])
static void sunrise_func(BuiltinFunctionContextPtr f)
{
  time_t time;
  if (checkSunParams(f, time)) {
    f->finish(new NumericValue(sunrise(time, *(f->geoLocation()), false)*3600));
  }
}


// dawn([epochtime])
static void dawn_func(BuiltinFunctionContextPtr f)
{
  time_t time;
  if (checkSunParams(f, time)) {
    f->finish(new NumericValue(sunrise(time, *(f->geoLocation()), true)*3600));
  }
}


// sunset([epochtime])
static void sunset_func(BuiltinFunctionContextPtr f)
{
  time_t time;
  if (checkSunParams(f, time)) {
    f->finish(new NumericValue(sunset(time, *(f->geoLocation()), false)*3600));
  }
}


// dusk([epochtime])
static void dusk_func(BuiltinFunctionContextPtr f)
{
  time_t time;
  if (checkSunParams(f, time)) {
    f->finish(new NumericValue(sunset(time, *(f->geoLocation()), true)*3600));
  }
}


// epochtime() - epoch time of right now
// epochtime(daysecond [, yearday [, year]]) - epoch time of given daysecond, yearday, year
// epochtime(hour, minute, second [,day [,month [,year]]]) - epoch time of given time and optional date components (second<1900)
static const BuiltInArgDesc epochtime_args[] = { { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t epochtime_numargs = sizeof(epochtime_args)/sizeof(BuiltInArgDesc);
static void epochtime_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==0) {
    f->finish(new NumericValue((double)MainLoop::unixtime()/Second)); // epoch time in seconds
    return;
  }
  struct tm loctim; MainLoop::getLocalTime(loctim);
  double r;
  if ((f->numArgs()==3 && f->arg(2)->intValue()<1900) || f->numArgs()>3) {
    // time component variant
    loctim.tm_hour = f->arg(0)->intValue();
    loctim.tm_min = f->arg(1)->intValue();
    r = f->arg(2)->doubleValue();
    loctim.tm_sec = (int)r;
    r -= loctim.tm_sec; // fractional need to be added later
    if (f->numArgs()>3) {
      // date specified
      loctim.tm_isdst = -1; // A negative value of arg->tm_isdst causes mktime to attempt to determine if Daylight Saving Time was in effect in the specified time.
      loctim.tm_mday = f->arg(3)->intValue();
      if (f->numArgs()>4) loctim.tm_mon = f->arg(4)->intValue()-1;
      if (f->numArgs()>5) loctim.tm_year = f->arg(5)->intValue()-1900;
    }
  }
  else {
    // daysecond + yearday + year variant
    r = f->arg(0)->doubleValue();
    loctim.tm_sec = (int)r;
    loctim.tm_hour = 0;
    loctim.tm_min = 0;
    r -= loctim.tm_sec; // fractional need to be added later
    if (f->numArgs()>1) {
      // date specified
      // note: this may not be precise, as day.month yeardays literals resolve to yeardays
      //   of the current year, not the specified one!
      // note: tm_yday is not processed by mktime, so we base on Jan 1st and add yeardays later
      loctim.tm_mon = 0;
//      loctim.tm_mday = 1;
//      r += f->arg(1)->doubleValue()*24*60*60; // seconds
      loctim.tm_mday = 1+f->arg(1)->doubleValue(); // use mday like tm_yday
      if (f->numArgs()>2) {
        loctim.tm_year = f->arg(2)->intValue()-1900;
      }
    }
  }
  f->finish(new NumericValue(mktime(&loctim)+r));
}


// epochdays()
static void epochdays_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue((double)MainLoop::unixtime()/Day)); // epoch time in days with fractional time
}

// common helper for all timegetter functions
static double prepTime(BuiltinFunctionContextPtr f, struct tm &aLocTim)
{
  MLMicroSeconds t;
  if (f->arg(0)->defined()) {
    t = f->arg(0)->doubleValue()*Second;
  }
  else {
    t = MainLoop::unixtime();
  }
  double fracSecs;
  MainLoop::getLocalTime(aLocTim, &fracSecs, t, t<=Day);
  return fracSecs;
}


// common argument descriptor for all time funcs
static const BuiltInArgDesc timegetter_args[] = { { numeric|optionalarg } };
static const size_t timegetter_numargs = sizeof(timegetter_args)/sizeof(BuiltInArgDesc);

// timeofday([epochtime])
static void timeofday_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; double fracSecs = prepTime(f, loctim);
  f->finish(new NumericValue(((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs));
}


// hour([epochtime])
static void hour_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_hour));
}


// minute([epochtime])
static void minute_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_min));
}


// second([epochtime])
static void second_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_sec));
}


// year([epochtime])
static void year_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_year+1900));
}


// month([epochtime])
static void month_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_mon+1));
}


// day([epochtime])
static void day_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_mday));
}


// weekday([epochtime])
static void weekday_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_wday));
}


// yearday([epochtime])
static void yearday_func(BuiltinFunctionContextPtr f)
{
  struct tm loctim; prepTime(f, loctim);
  f->finish(new NumericValue(loctim.tm_yday));
}


#if SCRIPTING_JSON_SUPPORT

static void globalbuiltins_func(BuiltinFunctionContextPtr f)
{
  f->finish(new JsonValue(f->thread()->owner()->domain()->builtinsInfo()));
}


static void contextbuiltins_func(BuiltinFunctionContextPtr f)
{
  f->finish(new JsonValue(f->thread()->owner()->scriptmain()->builtinsInfo()));
}

#if P44SCRIPT_FULL_SUPPORT

static void globalhandlers_func(BuiltinFunctionContextPtr f)
{
  f->finish(new JsonValue(f->thread()->owner()->domain()->handlersInfo()));
}


static void contexthandlers_func(BuiltinFunctionContextPtr f)
{
  f->finish(new JsonValue(f->thread()->owner()->scriptmain()->handlersInfo()));
}

#endif // P44SCRIPT_FULL_SUPPORT


static void globalvars_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->thread()->owner()->domain());
}

static ScriptObjPtr globals_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite)
{
  // the parent object of a global function is the scripting domain
  return aParentObj;
}


static void contextvars_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->thread()->owner()->scriptmain());
}

static void localvars_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->thread()->owner());
}

#endif // SCRIPTING_JSON_SUPPORT


// The standard function descriptor table
static const BuiltinMemberDescriptor standardFunctions[] = {
  { "ifok", executable|any, ifok_numargs, ifok_args, &ifok_func },
  { "ifvalid", executable|any, ifvalid_numargs, ifvalid_args, &ifvalid_func },
  { "isok", executable|numeric, isok_numargs, isok_args, &isok_func },
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
  { "boolean", executable|numeric, boolean_numargs, boolean_args, &boolean_func },
  { "describe", executable|text, describe_numargs, describe_args, &describe_func },
  { "annotation", executable|text, annotation_numargs, annotation_args, &annotation_func },
  { "lastarg", executable|any, lastarg_numargs, lastarg_args, &lastarg_func },
  #if SCRIPTING_JSON_SUPPORT
  { "json", executable|json, json_numargs, json_args, &json_func },
  #if ENABLE_JSON_APPLICATION
  { "jsonresource", executable|json|error, jsonresource_numargs, jsonresource_args, &jsonresource_func },
  #endif // ENABLE_JSON_APPLICATION
  #endif // SCRIPTING_JSON_SUPPORT
  #if P44SCRIPT_FULL_SUPPORT
  { "maprange", executable|numeric|null, maprange_numargs, maprange_args, &maprange_func },
  { "ord", executable|numeric, ord_numargs, ord_args, &ord_func },
  { "chr", executable|text, chr_numargs, chr_args, &chr_func },
  { "hex", executable|text, hex_numargs, hex_args, &hex_func },
  { "binary", executable|text, binary_numargs, binary_args, &binary_func },
  { "bit", executable|numeric, bit_numargs, bit_args, &bit_func },
  { "setbit", executable|numeric, setbit_numargs, setbit_args, &setbit_func },
  { "flipbit", executable|numeric, flipbit_numargs, flipbit_args, &flipbit_func },
  { "elements", executable|numeric|null, elements_numargs, elements_args, &elements_func },
  { "strlen", executable|numeric|null, strlen_numargs, strlen_args, &strlen_func },
  { "strrep", executable|text, strrep_numargs, strrep_args, &strrep_func },
  { "substr", executable|text|null, substr_numargs, substr_args, &substr_func },
  { "find", executable|numeric|null, find_numargs, find_args, &find_func },
  { "replace", executable|text, replace_numargs, replace_args, &replace_func },
  { "lowercase", executable|text, xcase_numargs, xcase_args, &lowercase_func },
  { "uppercase", executable|text, xcase_numargs, xcase_args, &uppercase_func },
  { "shellquote", executable|text, shellquote_numargs, shellquote_args, &shellquote_func },
  { "cquote", executable|text, cquote_numargs, cquote_args, &cquote_func },
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
  #endif // P44SCRIPT_FULL_SUPPORT
  { "is_weekday", executable|any, is_weekday_numargs, is_weekday_args, &is_weekday_func },
  { "after_time", executable|numeric, after_time_numargs, after_time_args, &after_time_func },
  { "is_time", executable|numeric, is_time_numargs, is_time_args, &is_time_func },
  { "initial", executable|numeric, 0, NULL, &initial_func },
  { "testlater", executable|numeric, testlater_numargs, testlater_args, &testlater_func },
  { "every", executable|numeric, every_numargs, every_args, &every_func },
  { "between_dates", executable|numeric, between_dates_numargs, between_dates_args, &between_dates_func },
  { "sunrise", executable|numeric|null, timegetter_numargs, timegetter_args, &sunrise_func },
  { "dawn", executable|numeric|null, timegetter_numargs, timegetter_args, &dawn_func },
  { "sunset", executable|numeric|null, timegetter_numargs, timegetter_args, &sunset_func },
  { "dusk", executable|numeric|null, timegetter_numargs, timegetter_args, &dusk_func },
  { "epochtime", executable|any, epochtime_numargs, epochtime_args, &epochtime_func },
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
  // Introspection
  #if SCRIPTING_JSON_SUPPORT
  { "globalvars", executable|structured, 0, NULL, &globalvars_func},
  { "globals", builtinmember|structured, 0, NULL, (BuiltinFunctionImplementation)&globals_accessor }, // Note: correct '.accessor=&lrg_accessor' form does not work with OpenWrt g++, so need ugly cast here
  { "contextvars", executable|structured, 0, NULL, &contextvars_func },
  { "localvars", executable|structured, 0, NULL, &localvars_func },
  #if P44SCRIPT_FULL_SUPPORT
  { "globalhandlers", executable|structured, 0, NULL, &globalhandlers_func },
  { "contexthandlers", executable|structured, 0, NULL, &contexthandlers_func },
  #endif
  { "globalbuiltins", executable|structured, 0, NULL, &globalbuiltins_func },
  { "contextbuiltins", executable|structured, 0, NULL, &contextbuiltins_func },
  #endif
  #if P44SCRIPT_FULL_SUPPORT
  { "lock", executable|any, lock_numargs, lock_args, &lock_func },
  { "signal", executable|any, 0, NULL, &signal_func },
  // Async
  { "await", executable|async|any, await_numargs, await_args, &await_func },
  { "delay", executable|async|null, delayx_numargs, delayx_args, &delay_func },
  { "delayuntil", executable|async|null, delayx_numargs, delayx_args, &delayuntil_func },
  { "eval", executable|async|any, eval_numargs, eval_args, &eval_func },
  { "maxblocktime", executable|any, maxblocktime_numargs, maxblocktime_args, &maxblocktime_func },
  { "maxruntime", executable|any, maxruntime_numargs, maxruntime_args, &maxruntime_func },
  { "breakpoint", executable|any, 0, NULL, &breakpoint_func },
  #if !ESP_PLATFORM
  { "system", executable|async|text, system_numargs, system_args, &system_func },
  // Other system/app stuff
  { "restartapp", executable|null, restartapp_numargs, restartapp_args, &restartapp_func },
  { "appversion", executable|null, 0, NULL, &appversion_func },
  #if ENABLE_APPLICATION_SUPPORT
  { "readfile", executable|error|text, readfile_numargs, readfile_args, &readfile_func },
  { "writefile", executable|error|null, writefile_numargs, writefile_args, &writefile_func },
  #endif // ENABLE_APPLICATION_SUPPORT
  #endif // !ESP_PLATFORM
  #endif // P44SCRIPT_FULL_SUPPORT
  { NULL } // terminator
};

} // BuiltinFunctions


// MARK: - Standard Scripting Domain

static ScriptingDomainPtr gStandardScriptingDomain;


StandardScriptingDomain::StandardScriptingDomain()
{
  // a standard scripting domains has the standard functions
  registerMemberLookup(new BuiltInMemberLookup(BuiltinFunctions::standardFunctions));
}


ScriptingDomain& StandardScriptingDomain::sharedDomain()
{
  if (!gStandardScriptingDomain) {
    gStandardScriptingDomain = new StandardScriptingDomain();
  }
  return *gStandardScriptingDomain.get();
};


void StandardScriptingDomain::setStandardScriptingDomain(ScriptingDomainPtr aStandardScriptingDomain)
{
  gStandardScriptingDomain = aStandardScriptingDomain;
}


#if P44SCRIPT_REGISTERED_SOURCE

// MARK: - File storage based standard scripting domain

#define P44SCRIPT_FILE_EXTENSION ".p44s"

bool FileStorageStandardScriptingDomain::loadSource(const string &aScriptHostUid, string &aSource)
{
  if (mScriptDir.empty()) return false;
  ErrorPtr err = string_fromfile(mScriptDir+"/"+aScriptHostUid+P44SCRIPT_FILE_EXTENSION, aSource);
  if (Error::isOK(err)) return true;
  if (Error::isError(err, SysError::domain(), ENOENT)) return false; // no such file, but that's ok
  LOG(LOG_ERR, "Cannot load script '%s" P44SCRIPT_FILE_EXTENSION "'", aScriptHostUid.c_str());
  return false; // error, nothing loaded
}


bool FileStorageStandardScriptingDomain::storeSource(const string &aScriptHostUid, const string &aSource)
{
  if (mScriptDir.empty()) return false;
  string scriptfn = mScriptDir+"/"+aScriptHostUid+P44SCRIPT_FILE_EXTENSION;
  ErrorPtr err;
  if (aSource.empty()) {
    // remove entirely empty script files
    err = SysError::err(unlink(scriptfn.c_str()));
  }
  else {
    // save as file
    err = string_tofile(scriptfn, aSource);
  }
  if (Error::isOK(err)) return true;
  LOG(LOG_ERR, "Cannot save source '%s" P44SCRIPT_FILE_EXTENSION "'", aScriptHostUid.c_str());
  return false;
}


#endif // P44SCRIPT_REGISTERED_SOURCE


#if SIMPLE_REPL_APP

// MARK: - Simple REPL (Read Execute Print Loop) App
#include "fdcomm.hpp"
#include "httpcomm.hpp"
#include "socketcomm.hpp"
#include "analogio.hpp"
#include "digitalio.hpp"
#include "dcmotor.hpp"

class SimpleREPLApp : public CmdLineApp
{
  typedef CmdLineApp inherited;

  ScriptHost source;
  ScriptMainContextPtr replContext;
  FdCommPtr input;

public:

  SimpleREPLApp() :
    source(sourcecode|regular|keepvars|concurrently|ephemeralSource, "REPL")
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
    #if ENABLE_ANALOGIO_SCRIPT_FUNCS
    source.domain()->registerMemberLookup(new AnalogIoLookup);
    #endif
    #if ENABLE_DIGITALIO_SCRIPT_FUNCS
    source.domain()->registerMemberLookup(new DigitalIoLookup);
    #endif
    #if ENABLE_DCMOTOR_SCRIPT_FUNCS
    source.domain()->registerMemberLookup(new DcMotorLookup);
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

