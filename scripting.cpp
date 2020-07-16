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

#include "scripting.hpp"

#if ENABLE_SCRIPTING

#include "math.h"

#if ENABLE_JSON_APPLICATION && SCRIPTING_JSON_SUPPORT
  #include "application.hpp"
#endif


using namespace p44;
using namespace p44::Script;


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


// MARK: - ScriptObj

ErrorPtr ScriptObj::setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes)
{
  if (aStorageAttributes & create) {
    return ScriptError::err(ScriptError::NotCreated, "cannot create '%s'", aName.c_str());
  }
  else {
    return ScriptError::err(ScriptError::NotFound, "'%s' not found", aName.c_str());
  }
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


// FIXME: move source code to ScriptObj
string ScriptObj::typeDescription(TypeInfo aInfo)
{
  string s;
  if ((aInfo & any)==any) {
    s = "any type";
    if (aInfo & null) s += " including undefined";
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
    if (aInfo & executable) {
      if (!s.empty()) s += ", ";
      s += "script";
    }
    if (aInfo & error) {
      if (!s.empty()) s += " or ";
      s += "error";
    }
    if (aInfo & null) {
      if (!s.empty()) s += " or ";
      s += "undefined";
    }
  }
  return s;
}


string ScriptObj::describe(ScriptObjPtr aObj)
{
  if (!aObj) return "<none>";
  return string_format(
    "'%s' [%s; %s]",
    aObj->stringValue().c_str(),
    aObj->getIdentifier().c_str(),
    typeDescription(aObj->getTypeInfo()).c_str()
  );
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
  return (this==&aRightSide) ; // undefined comparisons are always false, unless we have object _instance_ identity
}

bool NumericValue::operator==(const ScriptObj& aRightSide) const
{
  return num==aRightSide.numValue();
}

bool StringValue::operator==(const ScriptObj& aRightSide) const
{
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
  return num<aRightSide.numValue();
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
  return new NumericValue(num + aRightSide.numValue());
}


ScriptObjPtr StringValue::operator+(const ScriptObj& aRightSide) const
{
  return new StringValue(str + aRightSide.stringValue());
}


ScriptObjPtr NumericValue::operator-(const ScriptObj& aRightSide) const
{
  return new NumericValue(num - aRightSide.numValue());
}

ScriptObjPtr NumericValue::operator*(const ScriptObj& aRightSide) const
{
  return new NumericValue(num * aRightSide.numValue());
}

ScriptObjPtr NumericValue::operator/(const ScriptObj& aRightSide) const
{
  if (aRightSide.numValue()==0) {
    return new ErrorValue(ScriptError::DivisionByZero, "division by zero");
  }
  else {
    return new NumericValue(num / aRightSide.numValue());
  }
}

ScriptObjPtr NumericValue::operator%(const ScriptObj& aRightSide) const
{
  if (aRightSide.numValue()==0) {
    return new ErrorValue(ScriptError::DivisionByZero, "modulo by zero");
  }
  else {
    // modulo allowing float dividend and divisor, really meaning "remainder"
    double a = numValue();
    double b = aRightSide.numValue();
    int64_t q = a/b;
    return new NumericValue(a-b*q);
  }
}



// MARK: - Value classes

ErrorValue::ErrorValue(ScriptError::ErrorCodes aErrCode, const char *aFmt, ...)
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


double StringValue::numValue() const
{
  SourceCursor cursor(str);
  cursor.skipWhiteSpace();
  ScriptObjPtr n = cursor.parseNumericLiteral();
  // note: like parseInt/Float in JS we allow trailing garbage
  //   but UNLIKE JS we don't return NaN here, just 0 if there's no conversion to number
  if (n->isErr()) return 0; // otherwise we'd get error
  return n->numValue();
}


#if SCRIPTING_JSON_SUPPORT

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
  return JsonObject::newString(str);
  // TODO: old version did parse strings for json, but that would require Error return, not the right place any more here!
  // Sample code:
  //  ErrorPtr jerr;
  //  JsonObjectPtr j = JsonObject::objFromText(p, strValP->size(), &jerr, false);
  //  if (Error::isOK(jerr)) return j;
}


string JsonValue::stringValue() const
{
  if (!jsonval) return ScriptObj::stringValue(); // undefined
  if (jsonval->isType(json_type_string)) return jsonval->stringValue(); // string leaf fields as strings w/o quotes!
  return jsonval->json_str(); // other types in their native json representation
}


double JsonValue::numValue() const
{
  if (!jsonval) return ScriptObj::numValue(); // undefined
  return jsonval->doubleValue();
}


bool JsonValue::boolValue() const
{
  if (!jsonval) return ScriptObj::boolValue(); // undefined
  return jsonval->boolValue();
}


TypeInfo JsonValue::getTypeInfo() const
{
  if (!jsonval) return null;
  if (jsonval->isType(json_type_object)) return json+object;
  if (jsonval->isType(json_type_array)) return json+array;
  return json;
}


const ScriptObjPtr JsonValue::memberByName(const string aName, TypeInfo aTypeRequirements)
{
  ScriptObjPtr m;
  if (jsonval && ((aTypeRequirements & json)==aTypeRequirements)) {
    JsonObjectPtr j = jsonval->get(aName.c_str());
    if (j) {
      m = ScriptObjPtr(new JsonValue(j));
    }
  }
  return m;
}


size_t JsonValue::numIndexedMembers() const
{
  if (jsonval) return jsonval->arrayLength();
  return 0;
}


const ScriptObjPtr JsonValue::memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements)
{
  ScriptObjPtr m;
  if (aIndex>=0 && aIndex<numIndexedMembers()) {
    m = ScriptObjPtr(new JsonValue(jsonval->arrayGet((int)aIndex)));
  }
  return m;
}

#endif // SCRIPTING_JSON_SUPPORT


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


const ScriptObjPtr ExecutionContext::memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements)
{
  ScriptObjPtr v;
  if (aIndex<indexedVars.size()) {
    v = indexedVars[aIndex];
    if ((v->getTypeInfo()&aTypeRequirements)!=aTypeRequirements) return ScriptObjPtr();
  }
  return v;
}


ErrorPtr ExecutionContext::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  if (aIndex==indexedVars.size()) {
    // specially optimized case: appending
    indexedVars.push_back(aMember);
  }
  else {
    if (aIndex>indexedVars.size()) {
      // resize, will result in sparse array
      indexedVars.resize(aIndex+1);
    }
    indexedVars[aIndex] = aMember;
  }
  return ErrorPtr();
}


ErrorPtr ExecutionContext::checkAndSetArgument(ScriptObjPtr aArgument, size_t aIndex, ScriptObjPtr aCallee)
{
  if (!aCallee) return ScriptError::err(ScriptError::Internal, "missing callee");
  const ArgumentDescriptor* info = aCallee->argumentInfo(aIndex);
  if (!info) {
    if (aArgument) {
      return ScriptError::err(ScriptError::Syntax, "too many arguments for '%s'", aCallee->getIdentifier().c_str());
    }
  }
  if (!aArgument && info) {
    // check if there SHOULD be an argument at aIndex (but we have none)
    if ((info->typeInfo & optional)==0) {
      // at aIndex is a non-optional argument expected
      return ScriptError::err(ScriptError::Syntax,
        "missing argument %zu (%s) in call to '%s'",
        aIndex+1,
        typeDescription(info->typeInfo).c_str(),
        aCallee->getIdentifier().c_str()
      );
    }
  }
  if (aArgument) {
    // not just checking for required arguments
    TypeInfo allowed = info->typeInfo;
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
        else {
          return ScriptError::err(ScriptError::Syntax,
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
    setMemberAtIndex(aIndex, aArgument, nonNullCStr(info->name));
  }
  return ErrorPtr(); // ok
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
  NamedVarMap::iterator pos = namedVars.begin();
  while (pos!=namedVars.end()) {
    if (pos->second->originatesFrom(aSource)) {
      pos = namedVars.erase(pos); // source is gone -> remove
    }
    else {
      ++pos;
    }
  }
  inherited::releaseObjsFromSource(aSource);
}


void ScriptCodeContext::clearVars()
{
  namedVars.clear();
  inherited::clearVars();
}


const ScriptObjPtr ScriptCodeContext::memberByName(const string aName, TypeInfo aTypeRequirements)
{
  ScriptObjPtr m;
  // 1) local variables/objects
  if ((aTypeRequirements & (classscope+objscope))==0) {
    NamedVarMap::const_iterator pos = namedVars.find(aName);
    if (pos!=namedVars.end()) {
      m = pos->second;
      if ((m->getTypeInfo()&aTypeRequirements)!=aTypeRequirements) return ScriptObjPtr();
    }
  }
  // 2) access to ANY members of the _instance_ itself if running in a object context
  if (instance() && (m = instance()->memberByName(aName, aTypeRequirements) ))
  // 3) functions from the main level (but no local objects/vars of main, these must be passed into functions as arguments)
  if (mainContext && (m = mainContext->memberByName(aName, aTypeRequirements|classscope|constant|objscope))) return m;
  // nothing found
  return m;
}


ErrorPtr ScriptCodeContext::setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes)
{
  ErrorPtr err;
  // 1) ONLY local variables/objects
  if ((aStorageAttributes & (classscope+objscope))==0) {
    NamedVarMap::iterator pos = namedVars.find(aName);
    if (pos!=namedVars.end()) {
      // exists in local vars
      pos->second = aMember;
    }
    else if (aStorageAttributes & create) {
      // create it
      namedVars[aName] = aMember;
    }
    else {
      err = ScriptError::err(ScriptError::NotFound, "no local variable '%s'", aName.c_str());
    }
  }
  // 2) instance itself does not allow writable members (by design), but sub-members of them could well be writable
  // 3) main itself also does not allow writable member (by design), but sub-members of them could well be writable
  return err;
}


ErrorPtr ScriptCodeContext::setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName)
{
  ErrorPtr err = inherited::setMemberAtIndex(aIndex, aMember, aName);
  if (!aName.empty() && Error::isOK(err)) {
    err = setMemberByName(aName, aMember, create);
  }
  return err;
}


void ScriptCodeContext::abort(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult)
{
  if (aAbortFlags & queue) {
    // empty queue first to make sure no queued threads get started when last running thread is killed below
    while (!queuedThreads.empty()) {
      queuedThreads.back()->abort(new ErrorValue(ScriptError::Aborted, "Removed queued execution before it could start"));
      queuedThreads.pop_back();
    }
  }
  if (aAbortFlags & stoprunning) {
    while (!threads.empty()) {
      threads.back()->abort(aAbortResult);
      threads.pop_back();
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
  CompiledFunctionPtr code = dynamic_pointer_cast<CompiledScript>(aToExecute);
  if (!code) {
    if (aEvaluationCB) aEvaluationCB(new ErrorValue(ScriptError::Internal, "Object to be run must be compiled code!"));
    return;
  }
  // can be evaluated
  if ((aEvalFlags & keepvars)==0) {
    clearVars();
  }
  // prepare a thread for executing now or later
  // Note: thread gets an owning Ptr back to this, so this context cannot be destructed before all
  //   threads have ended.
  ScriptCodeThreadPtr newThread = ScriptCodeThreadPtr(new ScriptCodeThread(this, code->cursor));
  MLMicroSeconds maxBlockTime = aEvalFlags&synchronously ? aMaxRunTime : domain()->getMaxBlockTime();
  newThread->prepareRun(aEvaluationCB, aEvalFlags, maxBlockTime, aMaxRunTime);
  // now check how and when to run it
  if (!threads.empty()) {
    // some threads already running
    if (aEvalFlags & stoprunning) {
      // kill all current threads first...
      abort(stopall, new ErrorValue(ScriptError::Aborted, "Aborted by another script starting"));
      // ...then start new
    }
    else if (aEvalFlags & queue) {
      // queue for later
      queuedThreads.push_back(newThread);
      return;
    }
    else if ((aEvalFlags & concurrently)==0) {
      // none of the multithread modes and already running: just report busy
      newThread->abort(new ErrorValue(ScriptError::Busy, "Already busy executing script"));
      return;
    }
  }
  // can start new thread now
  threads.push_back(newThread);
  newThread->run();
}


void ScriptCodeContext::threadTerminated(ScriptCodeThreadPtr aThread)
{
  // a thread has ended, remove it from the list
  ThreadList::iterator pos=threads.begin();
  while (pos!=threads.end()) {
    if (pos->get()==aThread.get()) {
      pos = threads.erase(pos);
      // thread object should get disposed now, along with its SourceRef
      continue;
    }
    ++pos;
  }
  // check for queued executions to start now
  if (!queuedThreads.empty()) {
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


const ScriptObjPtr ScriptMainContext::memberByName(const string aName, TypeInfo aTypeRequirements)
{
  ScriptObjPtr m;
  // member lookup during execution of a function or script body
  if ((aTypeRequirements & constant)==0) {
    // Only if not looking only for constant members (in the sense of: not settable by scripts)
    // 1) lookup local variables/arguments in this context...
    // 2) ...and members of the instance (if any)
    if ((m = inherited::memberByName(aName, aTypeRequirements))) return m;
  }
  // 3) members from registered lookups, which might or might not be instance related (depends on the lookup)
  LookupList::const_iterator pos = lookups.begin();
  while (pos!=lookups.end()) {
    ClassLevelLookupPtr lookup = *pos;
    if ((lookup->containsTypes() & aTypeRequirements)==aTypeRequirements) {
      if ((m = lookup->memberByNameFrom(instance(), aName, aTypeRequirements))) return m;
    }
    ++pos;
  }
  // 4) lookup global members in the script domain (vars, functions, constants)
  if (domain() && (m = domain()->memberByName(aName, aTypeRequirements))) return m;
  // nothing found (note that inherited was queried early above, already!)
  return m;
}


ErrorPtr ScriptMainContext::setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes)
{
  ErrorPtr err;
  if (domain() && (aStorageAttributes & global)) {
    // 5) explicitly requested global storage
    return domain()->setMemberByName(aName, aMember, aStorageAttributes);
  }
  else {
    // Not explicit global storage, use normal chain
    // 1) local variables have precedence
    if (Error::isOK(err = inherited::setMemberByName(aName, aMember, aStorageAttributes))) return err; // modified or created an existing local variable
    // 2) properties in the instance itself (if no local member exists)
    if (instance() && err->isError(ScriptError::domain(), ScriptError::NotFound)) {
      err = instance()->setMemberByName(aName, aMember, aStorageAttributes);
      if (Error::isOK(err)) return err; // modified or created a property in thisObj
    }
    // 3) properties in lookup chain on those lookups which have mutablemembers (if no local or thisObj variable exists),
    //    which might or might not be instance related (depends on the lookup)
    if (err->isError(ScriptError::domain(), ScriptError::NotFound)) {
      LookupList::const_iterator pos = lookups.begin();
      while (pos!=lookups.end()) {
        ClassLevelLookupPtr lookup = *pos;
        if (lookup->containsTypes() & mutablemembers) {
          if (Error::isOK(err =lookup->setMemberByNameFrom(instance(), aName, aMember, aStorageAttributes))) return err; // modified or created a property in mutable lookup
          if (!err->isError(ScriptError::domain(), ScriptError::NotFound)) {
            break;
          }
          // continue searching as long as property just does not exist.
        }
        ++pos;
      }
    }
    // 4) modify (but never create w/o global storage attribute) global variables (if no local, thisObj or lookup chain variable of this name exists)
    if (domain() && err->isError(ScriptError::domain(), ScriptError::NotFound)) {
      err = domain()->ScriptObj::setMemberByName(aName, aMember, aStorageAttributes & ~create);
    }
  }
  return err;
}


void ScriptMainContext::registerMemberLookup(ClassLevelLookupPtr aMemberLookup)
{
  if (aMemberLookup) {
    // last registered lookup overrides same named objects in lookups registered before
    lookups.push_front(aMemberLookup);
  }
}


// MARK: - Scripting Domain



// MARK: - Built-in function support

BuiltInFunctionLookup::BuiltInFunctionLookup(const BuiltinFunctionDescriptor* aFunctionDescriptors)
{
  // build name lookup map
  if (aFunctionDescriptors) {
    while (aFunctionDescriptors->name) {
      functions[aFunctionDescriptors->name]=aFunctionDescriptors;
      aFunctionDescriptors++;
    }
  }
}


ScriptObjPtr BuiltInFunctionLookup::memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements) const
{
  ScriptObjPtr func;

  if ((executable & aTypeRequirements)==aTypeRequirements) {
    FunctionMap::const_iterator pos = functions.find(aName);
    if (pos!=functions.end()) {
      func = ScriptObjPtr(new BuiltinFunctionObj(pos->second, aThisObj));
    }
  }
  return func;
}


ExecutionContextPtr BuiltinFunctionObj::contextForCallingFrom(ScriptMainContextPtr aMainContext) const
{
  // built-in functions get their this from the lookup they come from
  return new BuiltinFunctionContext(aMainContext);
}


const ArgumentDescriptor* BuiltinFunctionObj::argumentInfo(size_t aIndex) const
{
  if (aIndex<descriptor->numArgs) {
    return &(descriptor->arguments[aIndex]);
  }
  // no arguemnt with this index, check for open argument list
  if (descriptor->numArgs>0 && (descriptor->arguments[descriptor->numArgs-1].typeInfo & multiple)) {
    return &(descriptor->arguments[descriptor->numArgs-1]); // last descriptor is for all further args
  }
  return NULL; // no such argument
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


void BuiltinFunctionContext::abort(EvaluationFlags aAbortFlags, ScriptObjPtr aAbortResult)
{
  if (abortCB) abortCB(); // stop external things the function call has started
  abortCB = NULL;
  if (evaluationCB) {
    if (!aAbortResult) aAbortResult = new ErrorValue(ScriptError::Aborted, "builtin function '%s' aborted", func->descriptor->name);
    evaluationCB(aAbortResult);
    evaluationCB = NULL;
  }
  func = NULL;
}



ScriptObjPtr BuiltinFunctionContext::arg(size_t aArgIndex)
{
  if (aArgIndex<0 || aArgIndex>=numIndexedMembers()) {
    // no such argument, return a null as the argument might be optional
    return new AnnotatedNullValue("optional function argument");
  }
  return memberAtIndex(aArgIndex);
}


void BuiltinFunctionContext::finish(ScriptObjPtr aResult)
{
  abortCB = NULL; // finished
  func = NULL;
  evaluationCB(aResult);
  evaluationCB = NULL;
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
  ptr(aText.c_str()),
  bol(aText.c_str()),
  eot(ptr+aText.size()),
  line(0)
{
}


SourcePos::SourcePos(const SourcePos &aCursor) :
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
  "%s %s: stack=%zu, prec=%d, flowdec=%d --> result = %s", \
  skipping ? " SKIPPING" : "EXECUTING", \
  __func__, \
  stack.size(), precedence, flowDecision, \
  ScriptObj::describe(result).c_str() \
)

SourceProcessor::SourceProcessor() :
  aborted(false),
  resuming(false),
  resumed(false),
  evaluationFlags(none),
  nextState(NULL),
  skipping(false),
  precedence(0),
  pendingOperation(op_none),
  flowDecision(false)
{
}



void SourceProcessor::setCursor(const SourceCursor& aCursor)
{
  src = aCursor;
}


void SourceProcessor::initProcessing(EvaluationFlags aEvalFlags)
{
  evaluationFlags = aEvalFlags;
  // just scanning?
  skipping = ((evaluationFlags & runModeMask)==scanning);
  // scope to start in
  if (evaluationFlags & expression)
    setNextState(&SourceProcessor::s_newExpression);
//  else if (evaluationFlags & scriptbody)
//    setNextState(&SourceProcessor::s_simpleTerm);
//  else if (evaluationFlags & source)
//    startState = (&SourceProcessor::s_definitions);
  // FIXME: actually set correct starting points
  setNextState(&SourceProcessor::s_newExpression);
  stack.clear();
  push(&SourceProcessor::s_complete);
}


void SourceProcessor::setCompletedCB(EvaluationCB aCompletedCB)
{
  completedCB = aCompletedCB;
}


void SourceProcessor::start()
{
  FOCUSLOGSTATE
  resuming = false;
  resume();
}


void SourceProcessor::resume(ScriptObjPtr aResult)
{
  // Store latest result, if any (resuming with NULL pointer does not change the result)
  if (aResult) {
    result = aResult;
  }
  FOCUSLOGSTATE
  // Am I getting called from a chain of calls originating from
  // myself via step() in the execution loop below?
  if (resuming) {
    // YES: avoid creating an endless call chain recursively
    resumed = true; // flag having resumed already to allow looping below
    return; // but now let chain of calls wind down to our last call (originating from step() in the loop)
  }
  // NO: this is a real re-entry, re-start the sync execution loop
  if (aborted) return;
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
  src.skipNonCode();
  if (!src.EOT()) {
    result = new ErrorPosValue(src, ScriptError::Syntax, "trailing garbage");
  }
  else if (!result) {
    result = new AnnotatedNullValue("script produced no result");
  }
  if (completedCB) completedCB(result);
  completedCB = NULL;
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
  if (!nextState) {
    result = new ErrorPosValue(src, ScriptError::Internal, "Missing next state");
    doneAndGoto(&SourceProcessor::s_complete);
    return;
  }
  // call the state handler
  StateHandler sh = nextState;
  nextState = NULL; // avoid calling twice
  (this->*sh)(); // call the handler, which will call done() here or later
  // Info abour method pointers and their weird syntax:
  // - https://stackoverflow.com/a/1486279
  // - Also see: https://stackoverflow.com/a/6754821
}


// MARK: source processor internal state machine

void SourceProcessor::done()
{
  // simple result check
  if (result && result->isErr()) {
    // abort on errors
    complete(result);
    return;
  }
  resume();
}


void SourceProcessor::push(StateHandler aReturnToState)
{
  FOCUSLOG("   ++push");
  stack.push_back(StackFrame(src.pos, skipping, aReturnToState, result, funcCallContext, precedence, pendingOperation, flowDecision));
}


void SourceProcessor::pop()
{
  StackFrame &s = stack.back();
  // these are just restored as before the push
  skipping = s.skipping;
  precedence = s.precedence;
  pendingOperation = s.pendingOperation;
  flowDecision = s.flowDecision;
  funcCallContext = s.funcCallContext;
  // these are restored separately, returnToState must decide what to do
  poppedPos = s.pos;
  olderResult = s.result;
  FOCUSLOG(" --popped: olderResult = %s", ScriptObj::describe(olderResult).c_str());
  // continue here
  setNextState(s.returnToState);
  stack.pop_back();
}


void SourceProcessor::s_simpleTerm()
{
  FOCUSLOGSTATE;
  // at the beginning of a simple term, result is undefined
  if (src.c()=='"' || src.c()=='\'') {
    result = src.parseStringLiteral();
    doneAndGoto(&SourceProcessor::s_result);
    return;
  }
  else if (src.c()=='{') {
    // json or code block literal
    #if SCRIPTING_JSON_SUPPORT
    SourceCursor peek = src;
    peek.next();
    peek.skipNonCode();
    if (peek.c()=='"' || peek.c()=='\'') {
      // first thing within "{" is a quoted field name: must be JSON literal
      result = src.parseJSONLiteral();
      doneAndGoto(&SourceProcessor::s_result);
      return;
    }
    #endif
    // must be a code block
    result = src.parseCodeLiteral();
    doneAndGoto(&SourceProcessor::s_result);
    return;
  }
  #if SCRIPTING_JSON_SUPPORT
  else if (src.c()=='[') {
    // must be JSON literal array
    result = src.parseJSONLiteral();
    doneAndGoto(&SourceProcessor::s_result);
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
      // anyway, process current result (either it's a new number or the result already set earlier
      doneAndGoto(&SourceProcessor::s_result);
      return;
    }
    else {
      // identifier at script scope level
      result.reset(); // lookup from script scope
      olderResult.reset(); // represents the previous level in nested lookups
      src.skipNonCode();
      if (skipping) {
        // we must always assume structured values etc.
        doneAndGoto(&SourceProcessor::s_member);
        return;
      }
      else {
        // if it is a plain identifier, it could be one of the built-in constants that cannot be overridden
        if (src.c()!='(' && src.c()!='.' && src.c()!='[') {
          // - check them before doing an actual member lookup
          if (uequals(identifier, "true") || uequals(identifier, "yes")) {
            result = new NumericValue(1);
            doneAndGoto(&SourceProcessor::s_result);
            return;
          }
          else if (uequals(identifier, "false") || uequals(identifier, "no")) {
            result = new NumericValue(0);
            doneAndGoto(&SourceProcessor::s_result);
            return;
          }
          else if (uequals(identifier, "null") || uequals(identifier, "undefined")) {
            result = new AnnotatedNullValue(identifier); // use literal as annotation
            doneAndGoto(&SourceProcessor::s_result);
            return;
          }
        }
        // need to look up the identifier
        setNextState(&SourceProcessor::s_member);
        memberByIdentifier();
        return;
      }
    }
  }
}


void SourceProcessor::s_member()
{
  FOCUSLOGSTATE;
  // immediately following an identifier, result represents its value
  if (src.nextIf('.')) {
    // member access
    src.skipNonCode();
    if (!src.parseIdentifier(identifier)) {
      result = new ErrorPosValue(src, ScriptError::Syntax, "missing identifier after '.'");
      doneAndGoto(&SourceProcessor::s_result);
      return;
    }
    setNextState(&SourceProcessor::s_member);
    memberByIdentifier(); // will lookup from result
    return;
  }
  else if (src.nextIf('[')) {
    // subscript access
    src.skipNonCode();
    push(&SourceProcessor::s_subscriptArg);
    doneAndGoto(&SourceProcessor::s_newExpression);
    return;
  }
  else if (src.nextIf('(')) {
    // function call
    src.skipNonCode();
    // - we need a function call context
    if (!skipping) {
      newFunctionCallContext();
    }
    doneAndGoto(&SourceProcessor::s_funcContext);
    return;
  }
  else if (!olderResult && !result && !skipping) {
    // we are on script scope (olderResult==NULL) and haven't found something (result==NULL), so check for built-in constants that are overrideable
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
  // identifier as-is represents the value, which is now stored in result (if not skipping)
  if (!skipping && !result) {
    // having no object at this point means identifier could not be found
    result = new ErrorPosValue(src, ScriptError::NotFound , "cannot find '%s'", identifier.c_str());
  }
  doneAndGoto(&SourceProcessor::s_result);
  return;
}


void SourceProcessor::s_funcContext()
{
  FOCUSLOGSTATE;
  // - check for arguments
  if (src.nextIf(')')) {
    // function with no arguments
    doneAndGoto(&SourceProcessor::s_funcExec);
    return;
  }
  push(&SourceProcessor::s_funcArg);
  doneAndGoto(&SourceProcessor::s_newExpression);
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
    // end of subscript processing, what we'll be looking up below is final member
    setNextState(&SourceProcessor::s_member);
  }
  else if (src.nextIf(',')) {
    // more subscripts to apply to the member we'll be looking up below
    src.skipNonCode();
    setNextState(&SourceProcessor::s_nextSubscript);
  }
  else {
    result = new ErrorPosValue(src, ScriptError::NotFound , "missing , or ] after subscript", identifier.c_str());
    doneAndGoto(&SourceProcessor::s_result);
    return;
  }
  // actually get the object as indicated by the subscript
  if (skipping) {
    // no actual member access
    done();
    return;
  }
  else {
    if (result->hasType(numeric)) {
      // array access by index
      size_t index = result->numValue();
      result = olderResult;
      memberByIndex(index);
      return;
    }
    else {
      // member access by name
      identifier = result->stringValue();
      result = olderResult;
      memberByIdentifier();
      return;
    }
  }
}

void SourceProcessor::s_nextSubscript()
{
  // immediately following a subscript argument evaluation
  // - result is the object the next subscript should apply to
  push(&SourceProcessor::s_subscriptArg);
  doneAndGoto(&SourceProcessor::s_newExpression);
}




void SourceProcessor::s_funcArg()
{
  FOCUSLOGSTATE;
  // immediately following a subscript argument evaluation
  // - result is value of the function argument
  // - olderResult is the function the argument applies to
  ScriptObjPtr arg = result;
  result = olderResult; // restore the function
  src.skipNonCode();
  // determine how to proceed after pushing the argument...
  if (src.nextIf(')')) {
    // end of argument processing, execute the function after pushing the final argument below
    setNextState(&SourceProcessor::s_funcExec);
  }
  else if (src.nextIf(',')) {
    // more arguments follow, continue evaluating them after pushing the current argument below
    src.skipNonCode();
    push(&SourceProcessor::s_funcArg);
    setNextState(&SourceProcessor::s_newExpression);
  }
  else {
    result = new ErrorPosValue(src, ScriptError::NotFound , "missing , or ) after function argument", identifier.c_str());
    doneAndGoto(&SourceProcessor::s_result);
    return;
  }
  // now apply the function argument
  if (skipping) {
    done(); // just ignore the argument and continue
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
  setNextState(&SourceProcessor::s_result); // result of the function call
  if (skipping) {
    done(); // just NOP
  }
  else {
    execute(); // execute
  }
}


void SourceProcessor::s_result()
{
  FOCUSLOGSTATE;
  if (skipping || !result || result->valid()) {
    // no need for a validation step for loading lazy results
    pop(); // get state to continue with
    done();
    return;
  }
  // make valid (pull value from lazy loading objects)
  setNextState(&SourceProcessor::s_validResult);
  result->makeValid(boost::bind(&SourceProcessor::resume, this, _1));
}


void SourceProcessor::s_validResult()
{
  FOCUSLOGSTATE;
  pop(); // get state to continue with
  done();
}



void SourceProcessor::s_newExpression()
{
  FOCUSLOGSTATE;
  precedence = 0;
  doneAndGoto(&SourceProcessor::s_expression);
}


void SourceProcessor::s_expression()
{
  FOCUSLOGSTATE;
  // at start of an (sub)expression
  SourcePos epos = src.pos; // remember start of any expression, even if it's only a precedence terminated subexpression
  // - check for optional unary op
  pendingOperation = src.parseOperator(); // store for later
  if (pendingOperation!=op_none && pendingOperation!=op_subtract && pendingOperation!=op_add && pendingOperation!=op_not) {
    result = new ErrorPosValue(src, ScriptError::NotFound , "invalid unary operator", identifier.c_str());
    doneAndGoto(&SourceProcessor::s_result);
    return;
  }
  // evaluate first (or only) term
  // - check for paranthesis term
  if (src.nextIf('(')) {
    // term is expression in paranthesis
    push(&SourceProcessor::s_groupedExpression);
    doneAndGoto(&SourceProcessor::s_newExpression);
    return;
  }
  // must be simple term
  // - a variable/constant reference
  // - a function call
  // - a literal value
  // Note: a non-simple term is the parantesized expression as handled above
  push(&SourceProcessor::s_exprFirstTerm);
  doneAndGoto(&SourceProcessor::s_simpleTerm);
}


void SourceProcessor::s_groupedExpression()
{
  FOCUSLOGSTATE;
  if (!src.nextIf(')')) {
    result = new ErrorPosValue(src, ScriptError::Syntax, "missing ')'");
  }
  doneAndGoto(&SourceProcessor::s_exprFirstTerm);
}


void SourceProcessor::s_exprFirstTerm()
{
  FOCUSLOGSTATE;
  // res now has the first term of an expression, which might need applying unary operations
  if (!skipping && result && result->defined()) {
    switch (pendingOperation) {
      case op_not : result = new NumericValue(!result->boolValue()); break;
      case op_subtract : result = new NumericValue(-result->numValue()); break;
      case op_add: // dummy, is NOP, allowed for clarification purposes
      default: break;
    }
  }
  doneAndGoto(&SourceProcessor::s_exprLeftSide);
}


void SourceProcessor::s_exprLeftSide()
{
  FOCUSLOGSTATE;
  // check binary operators
  src.skipNonCode();
  SourcePos opos = src.pos; // position before possibly finding an operator
  ScriptOperator binaryop = src.parseOperator();
  int newPrecedence = binaryop & opmask_precedence;
  // end parsing here if no operator found or operator with a lower or same precedence as the passed in precedence is reached
  if (binaryop==op_none || newPrecedence<=precedence) {
    src.pos = opos; // restore position
    doneAndGoto(&SourceProcessor::s_result);
    return;
  }
  // must parse right side of operator as subexpression
  pendingOperation = binaryop;
  push(&SourceProcessor::s_exprRightSide); // push the old precedence
  precedence = newPrecedence; // subexpression needs to exit when finding an operator weaker than this one
  doneAndGoto(&SourceProcessor::s_expression);
}


void SourceProcessor::s_exprRightSide()
{
  FOCUSLOGSTATE;
  // olderResult = leftside, result = rightside
  if (!skipping) {
    // all operations involving nulls return null
    if (olderResult->defined() && result->defined()) {
      // both are values -> apply the operation between leftside and rightside
      switch (pendingOperation) {
        case op_not: {
          result = new ErrorPosValue(src, ScriptError::Syntax, "NOT operator not allowed here");
          break;
        }
        case op_divide:     result = *olderResult / *result; break;
        case op_modulo:     result = *olderResult % *result; break;
        case op_multiply:   result = *olderResult * *result; break;
        case op_add:        result = *olderResult + *result; break;
        case op_subtract:   result = *olderResult - *result; break;
        case op_equal:
        // boolean result
        case op_assignOrEq: result = new NumericValue(*olderResult == *result); break;
        case op_notequal:   result = new NumericValue(*olderResult != *result); break;
        case op_less:       result = new NumericValue(*olderResult <  *result); break;
        case op_greater:    result = new NumericValue(*olderResult >  *result); break;
        case op_leq:        result = new NumericValue(*olderResult <= *result); break;
        case op_geq:        result = new NumericValue(*olderResult >= *result); break;
        case op_and:        result = new NumericValue(*olderResult && *result); break;
        case op_or:         result = new NumericValue(*olderResult || *result); break;
        default: break;
      }
    }
    else if (olderResult->isErr()) {
      // if first is error, return that independently of what the second is
      result = olderResult;
    }
    else if (!result->isErr()) {
      // one or both operands undefined, and none of them an error
      result = new AnnotatedNullValue("operation between undefined values");
    }
  }
  doneAndGoto(&SourceProcessor::s_exprLeftSide); // back to leftside, more chained operators might follow
}


//   complete(new ErrorPosValue(src, ScriptError::Internal, "XXX not yet implemented"));




void SourceProcessor::s_complete()
{
  FOCUSLOGSTATE;
  complete(result);
}



// MARK: source processor execution hooks

void SourceProcessor::memberByIdentifier()
{
  result.reset(); // base class cannot access members
  done();
}

void SourceProcessor::memberByIndex(size_t aIndex)
{
  result.reset(); // base class cannot access members
  done();
}


void SourceProcessor::newFunctionCallContext()
{
  result.reset(); // base class cannot execute functions
  done();
}


void SourceProcessor::pushFunctionArgument(ScriptObjPtr aArgument)
{
  done(); // NOP on the base class level
}


void SourceProcessor::execute()
{
  result.reset(); // base class cannot evaluate
  done();
}



// MARK: - CompiledScript, CompiledFunction, CompiledHandler

ExecutionContextPtr CompiledFunction::contextForCallingFrom(ScriptMainContextPtr aMainContext) const
{
  // functions get executed in a private context linked to the caller's (main) context
  return new ScriptCodeContext(aMainContext);
}



ExecutionContextPtr CompiledScript::contextForCallingFrom(ScriptMainContextPtr aMainContext) const
{
  // compiled script bodies get their execution context assigned at compile time, just return it
  // - but maincontext passed should be the domain of our saved mainContext, so check that if aMainContext is passed
  if (aMainContext) {
    if (mainContext->domain().get()!=aMainContext.get()) {
      return NULL; // mismatch, cannot use that context!
    }
  }
  return mainContext;
}



// MARK: - ScriptCompiler


static void flagSetter(bool* aFlag) { *aFlag = true; }

ScriptObjPtr ScriptCompiler::compile(SourceContainerPtr aSource, EvaluationFlags aParsingMode, ScriptMainContextPtr aMainContext)
{
  // set up starting point
  if ((aParsingMode & source)==0) {
    // Shortcut for expression and scriptbody: no need to "compile"
    bodyRef = aSource->getCursor();
  }
  else {
    // could contain declarations, must scan these now
    // FIXME: the scan process must detect the first body statement and adjust bodyRef!
    bodyRef = aSource->getCursor(); // FIXME: test only
    setCursor(aSource->getCursor());
    aParsingMode = (aParsingMode & ~runModeMask)|scanning; // compiling only!
    initProcessing(aParsingMode);
    bool completed = false;
    setCompletedCB(boost::bind(&flagSetter,&completed));
    start();
    if (!completed) {
      // the compiler must complete synchronously!
      return new ErrorValue(ScriptError::Internal, "Fatal: compiler execution not synchronous!");
    }
  }
  return new CompiledScript(bodyRef, aMainContext);
}


// MARK: - SourceContainer

SourceCursor SourceContainer::getCursor()
{
  return SourceCursor(this);
}



// MARK: - ScriptSource

ScriptSource::ScriptSource(const char* aOriginLabel, P44LoggingObj* aLoggingContextP) :
  originLabel(aOriginLabel),
  loggingContextP(aLoggingContextP)
{
}

ScriptSource::ScriptSource(const char* aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource, ScriptingDomainPtr aDomain) :
  originLabel(aOriginLabel),
  loggingContextP(aLoggingContextP)
{
  setDomain(aDomain);
  setSource(aSource);
}

ScriptSource::ScriptSource(const string aSource) :
  originLabel("adhoc"),
  loggingContextP(NULL)
{
  // using standard domain
  setSource(aSource);
}


ScriptSource::~ScriptSource()
{
  setSource(""); // force removal of global objects depending on this
}


void ScriptSource::setDomain(ScriptingDomainPtr aDomain)
{
  scriptingDomain = aDomain;
};


void ScriptSource::setSharedMainContext(ScriptMainContextPtr aSharedMainContext)
{
  // cached executable gets invalid when setting new context
  if (cachedExecutable) {
    cachedExecutable.reset(); // release cached executable (will release SourceCursor holding our source)
  }
  sharedMainContext = aSharedMainContext; // use this particular context for executing scripts
}



void ScriptSource::setSource(const string aSource, EvaluationFlags aCompileAs)
{
  compileAs = aCompileAs & (source|expression|scriptbody);
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
}


ScriptObjPtr ScriptSource::getExecutable()
{
  if (sourceContainer) {
    if (!cachedExecutable) {
      if (!scriptingDomain) {
        // none assigned so far, assign default
        scriptingDomain = ScriptingDomainPtr(&StandardScriptingDomain::sharedDomain());
      }
      // need to compile
      ScriptCompiler compiler(scriptingDomain);
      ScriptMainContextPtr mctx = sharedMainContext; // use shared context if one is set
      if (!mctx) {
        // default to independent execution in a non-object context (no instance pointer)
        mctx = scriptingDomain->newContext();
      }
      cachedExecutable = compiler.compile(sourceContainer, compileAs, mctx);
    }
    return cachedExecutable;
  }
  return new ErrorValue(ScriptError::Internal, "no source -> no executable");
}


ScriptObjPtr ScriptSource::run(EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, MLMicroSeconds aMaxRunTime)
{
  ScriptObjPtr code = getExecutable();
  ScriptObjPtr result;
  // get the context to run it
  if (code && code->hasType(executable)) {
    ExecutionContextPtr ctx = code->contextForCallingFrom(scriptingDomain);
    if (ctx) {
      if (aEvalFlags & synchronously) {
        result = ctx->executeSynchronously(code, aEvalFlags, aMaxRunTime);
      }
      else {
        ctx->execute(code, aEvalFlags, aEvaluationCB, aMaxRunTime);
        return result; // null, callback will deliver result
      }
    }
    else {
      // cannot evaluate due to missing context
      result = new ErrorValue(ScriptError::Internal, "No context to execute code");
    }
  }
  if (!code) {
    result = new AnnotatedNullValue("no source code");
  }
  if (aEvaluationCB) aEvaluationCB(result);
  return result;
}


// MARK: - ScriptingDomain

ScriptMainContextPtr ScriptingDomain::newContext(ScriptObjPtr aInstanceObj)
{
  return new ScriptMainContext(this, aInstanceObj);
}



// MARK: - ScriptCodeThread

ScriptCodeThread::ScriptCodeThread(ScriptCodeContextPtr aOwner, const SourceCursor& aStartCursor) :
  owner(aOwner),
  maxBlockTime(0),
  maxRunTime(Infinite),
  runningSince(Never)
{
  setCursor(aStartCursor);
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
  start();
}


void ScriptCodeThread::abort(ScriptObjPtr aAbortResult)
{
  inherited::abort(aAbortResult);
  if (childContext) {
    // abort the child context and let it pass its abort result up the chain
    childContext->abort(stopall, aAbortResult); // will call resume() via its callback
  }
  else {
    resume(); // resume, but aborted state will immediately terminate thread
  }
}


void ScriptCodeThread::complete(ScriptObjPtr aFinalResult)
{
  autoResumeTicket.cancel();
  inherited::complete(aFinalResult);
  owner->threadTerminated(this);
}


void ScriptCodeThread::stepLoop()
{
  MLMicroSeconds loopingSince = MainLoop::now();
  do {
    MLMicroSeconds now = MainLoop::now();
    // check for abort
    // Check maximum execution time
    if (maxRunTime!=Infinite && now-runningSince) {
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


void ScriptCodeThread::done()
{
  if (result && result->hasType(error)) {
    // check special case, "create" marks an error just explicitly created that should not automatically throw
    if (!result->hasType(create)) {
      ErrorPtr err = result->errorValue();
      if (err->isDomain(ScriptError::domain()) && err->getErrorCode()>=ScriptError::FatalErrors) {
        // just end the thread unconditionally
        complete(result);
        return;
      }
      else {
        // TODO: walk back the stack and look for a catch()
        complete(result); // FIXME: for now, we just terminate as well
        return;
      }
    }
  }
  resume();
}


void ScriptCodeThread::memberByIdentifier()
{
  if (result) {
    // look up member of the result itself
    result = result->memberByName(identifier);
  }
  else {
    // context level
    result = owner->memberByName(identifier);
  }
  done();
}

/// must retrieve the indexed member from current result (or from the script scope if result==NULL)
/// @note must call done() when result contains the member (or NULL if not found)
void ScriptCodeThread::memberByIndex(size_t aIndex)
{
  if (result) {
    // look up member of the result itself
    result = result->memberAtIndex(aIndex);
  }
  // no indexed members at the context level!
  done();
}


void ScriptCodeThread::newFunctionCallContext()
{
  if (result) {
    funcCallContext = result->contextForCallingFrom(owner->scriptmain());
  }
  if (!funcCallContext) {
    string f = result ? result->getIdentifier() : "undefined";
    result = new ErrorPosValue(src, ScriptError::NotCallable, "'%s' is not a function", f.c_str());
  }
  done();
}


/// apply the specified argument to the current result
void ScriptCodeThread::pushFunctionArgument(ScriptObjPtr aArgument)
{
  if (funcCallContext) {
    ErrorPtr err = funcCallContext->checkAndSetArgument(aArgument, funcCallContext->numIndexedMembers(), result);
    if (Error::notOK(err)) result = new ErrorPosValue(src, err);
  }
  done();
}


/// evaluate the current result and replace it with the output from the evaluation (e.g. function call)
void ScriptCodeThread::execute()
{
  if (funcCallContext && result) {
    // check for missing arguments after those we have
    ErrorPtr err = funcCallContext->checkAndSetArgument(ScriptObjPtr(), funcCallContext->numIndexedMembers(), result);
    if (Error::notOK(err)) {
      result = new ErrorPosValue(src, err);
      done();
    }
    else {
      funcCallContext->execute(result, evaluationFlags, boost::bind(&ScriptCodeThread::selfKeepingResume, this, _1));
    }
    // function call completion will call resume
    return;
  }
  result = new ErrorPosValue(src, ScriptError::Internal, "cannot execute object");
  done();
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
static const ArgumentDescriptor ifvalid_args[] = { { any+null }, { any+null } };
static const size_t ifvalid_numargs = sizeof(ifvalid_args)/sizeof(ArgumentDescriptor);
static void ifvalid_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->arg(0)->hasType(value) ? f->arg(0) : f->arg(1));
}

// isvalid(a)      if a is a valid value, return true, otherwise return false
static const ArgumentDescriptor isvalid_args[] = { { any+null } };
static const size_t isvalid_numargs = sizeof(isvalid_args)/sizeof(ArgumentDescriptor);
static void isvalid_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->hasType(value) ? 1 : 0));
}


// if (c, a, b)    if c evaluates to true, return a, otherwise b
static const ArgumentDescriptor if_args[] = { { value+null }, { any+null }, { any+null } };
static const size_t if_numargs = sizeof(if_args)/sizeof(ArgumentDescriptor);
static void if_func(BuiltinFunctionContextPtr f)
{
  f->finish(f->arg(0)->boolValue() ? f->arg(1) : f->arg(2));
}

// abs (a)         absolute value of a
static const ArgumentDescriptor abs_args[] = { { scalar+undefres } };
static const size_t abs_numargs = sizeof(abs_args)/sizeof(ArgumentDescriptor);
static void abs_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(fabs(f->arg(0)->numValue())));
}


// int (a)         integer value of a
static const ArgumentDescriptor int_args[] = { { scalar+undefres } };
static const size_t int_numargs = sizeof(int_args)/sizeof(ArgumentDescriptor);
static void int_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(int(f->arg(0)->int64Value())));
}


// frac (a)         fractional value of a
static const ArgumentDescriptor frac_args[] = { { scalar+undefres } };
static const size_t frac_numargs = sizeof(frac_args)/sizeof(ArgumentDescriptor);
static void frac_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->numValue()-f->arg(0)->int64Value())); // result retains sign
}


// round (a)       round value to integer
// round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
static const ArgumentDescriptor round_args[] = { { scalar+undefres }, { numeric+optional } };
static const size_t round_numargs = sizeof(round_args)/sizeof(ArgumentDescriptor);
static void round_func(BuiltinFunctionContextPtr f)
{
  double precision = 1;
  if (f->arg(1)->defined()) {
    precision = f->arg(1)->numValue();
  }
  f->finish(new NumericValue(round(f->arg(0)->numValue()/precision)*precision));
}


// random (a,b)     random value from a up to and including b
static const ArgumentDescriptor random_args[] = { { numeric }, { numeric } };
static const size_t random_numargs = sizeof(random_args)/sizeof(ArgumentDescriptor);
static void random_func(BuiltinFunctionContextPtr f)
{
  // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
  f->finish(new NumericValue(f->arg(0)->numValue() + (double)rand()*(f->arg(1)->numValue()-f->arg(0)->numValue())/((double)RAND_MAX)));
}


// min (a, b)    return the smaller value of a and b
static const ArgumentDescriptor min_args[] = { { scalar+undefres }, { value+undefres } };
static const size_t min_numargs = sizeof(min_args)/sizeof(ArgumentDescriptor);
static void min_func(BuiltinFunctionContextPtr f)
{
  if (f->argval(0)<f->argval(1)) f->finish(f->arg(0));
  else f->finish(f->arg(1));
}


// max (a, b)    return the bigger value of a and b
static const ArgumentDescriptor max_args[] = { { scalar+undefres }, { value+undefres } };
static const size_t max_numargs = sizeof(max_args)/sizeof(ArgumentDescriptor);
static void max_func(BuiltinFunctionContextPtr f)
{
  if (f->argval(0)>f->argval(1)) f->finish(f->arg(0));
  else f->finish(f->arg(1));
}


// limited (x, a, b)    return min(max(x,a),b), i.e. x limited to values between and including a and b
static const ArgumentDescriptor limited_args[] = { { scalar+undefres }, { numeric }, { numeric } };
static const size_t limited_numargs = sizeof(limited_args)/sizeof(ArgumentDescriptor);
static void limited_func(BuiltinFunctionContextPtr f)
{
  ScriptObj &a = f->argval(0);
  if (a<f->argval(1)) f->finish(f->arg(1));
  else if (a>f->argval(2)) f->finish(f->arg(2));
  else f->finish(f->arg(0));
}


// cyclic (x, a, b)    return x with wraparound into range a..b (not including b because it means the same thing as a)
static const ArgumentDescriptor cyclic_args[] = { { scalar+undefres }, { numeric }, { numeric } };
static const size_t cyclic_numargs = sizeof(cyclic_args)/sizeof(ArgumentDescriptor);
static void cyclic_func(BuiltinFunctionContextPtr f)
{
  double o = f->arg(1)->numValue();
  double x0 = f->arg(0)->numValue()-o; // make null based
  double r = f->arg(2)->numValue()-o; // wrap range
  if (x0>=r) x0 -= int(x0/r)*r;
  else if (x0<0) x0 += (int(-x0/r)+1)*r;
  f->finish(new NumericValue(x0+o));
}


// string(anything)
static const ArgumentDescriptor string_args[] = { { any+null } };
static const size_t string_numargs = sizeof(string_args)/sizeof(ArgumentDescriptor);
static void string_func(BuiltinFunctionContextPtr f)
{
  if (f->arg(0)->undefined())
    f->finish(new StringValue("undefined")); // make it visible
  else
    f->finish(new StringValue(f->arg(0)->stringValue())); // force convert to string, including nulls and errors
}


// number(anything)
static const ArgumentDescriptor number_args[] = { { any+null } };
static const size_t number_numargs = sizeof(number_args)/sizeof(ArgumentDescriptor);
static void number_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->numValue())); // force convert to numeric
}


// copy(anything) // make a value copy, including json object references
static const ArgumentDescriptor copy_args[] = { { any+null } };
static const size_t copy_numargs = sizeof(copy_args)/sizeof(ArgumentDescriptor);
static void copy_func(BuiltinFunctionContextPtr f)
{
  #if SCRIPTING_JSON_SUPPORT
  if (f->arg(0)->hasType(json)) {
    // need to make a value copy of the JsonObject itself
    f->finish(new JsonValue(JsonObjectPtr(new JsonObject(*(f->arg(0)->jsonValue())))));
  }
  else
  #endif
  {
    f->finish(f->arg(0)); // just copy the ExpressionValue
  }
}


#if SCRIPTING_JSON_SUPPORT

// json(anything)
static const ArgumentDescriptor json_args[] = { { any+null } };
static const size_t json_numargs = sizeof(json_args)/sizeof(ArgumentDescriptor);
static void json_func(BuiltinFunctionContextPtr f)
{
  f->finish(new JsonValue(f->arg(0)->jsonValue()));
}


// TODO: jsonvalue should have writable member access
//  // bool setfield(var, fieldname, value)
//  static const ArgumentDescriptor setfield_args[] = { { any+null } };
//  static const size_t setfield_numargs = sizeof(setfield_args)/sizeof(ArgumentDescriptor);
//  static void setfield_func(BuiltinFunctionContextPtr f)
//  {
//    if (!f->arg(0)->hasType(json)) f->finish(new NullValue()); // not JSON, cannot set value
//    else {
//
//      f->finish(new JsonValue(f->arg(0)->jsonValue()));
//      if (f->arg(1)->notValue()) return errorInArg(f->arg(1), aResult); // return error/null from argument
//      aResult.jsonValue()->add(f->arg(1)->stringValue().c_str(), f->arg(2)->jsonValue());
//    }
//  }
//
//
//  // bool setelement(var, index, value) // set
//  static const ArgumentDescriptor setelement_args[] = { { any+null } };
//  static const size_t setelement_numargs = sizeof(setelement_args)/sizeof(ArgumentDescriptor);
//  static void setelement_func(BuiltinFunctionContextPtr f)
//  {
//    // bool setelement(var, value) // append
//    if (!f->arg(0)->isJson()) f->finish(new NullValue()); // not JSON, cannot set value
//    else {
//      f->finish(new JsonValue(f->arg(0)->jsonValue()));
//      if (f->arg(1)->notValue()) return errorInArg(f->arg(1), aResult); // return error/null from argument
//      if (f->numArgs()==2) {
//        // append
//        aResult.jsonValue()->arrayAppend(f->arg(1)->jsonValue());
//      }
//      else {
//        aResult.jsonValue()->arrayPut(f->arg(1)->intValue(), f->arg(2)->jsonValue());
//      }
//    }
//  }


#if ENABLE_JSON_APPLICATION


static const ArgumentDescriptor jsonresource_args[] = { { text+undefres } };
static const size_t jsonresource_numargs = sizeof(jsonresource_args)/sizeof(ArgumentDescriptor);
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
static const ArgumentDescriptor lastarg_args[] = { { any+null+multiple, "side-effect" } };
static const size_t lastarg_numargs = sizeof(lastarg_args)/sizeof(ArgumentDescriptor);
static void lastarg_func(BuiltinFunctionContextPtr f)
{
  // (for executing side effects of non-last arg evaluation, before returning the last arg)
  if (f->numArgs()==0) f->finish(); // no arguments -> null
  else f->finish(f->arg(f->numArgs()-1)); // value of last argument
}


// strlen(string)
static const ArgumentDescriptor strlen_args[] = { { text+undefres } };
static const size_t strlen_numargs = sizeof(strlen_args)/sizeof(ArgumentDescriptor);
static void strlen_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NumericValue(f->arg(0)->stringValue().size())); // length of string
}


// substr(string, from)
// substr(string, from, count)
static const ArgumentDescriptor substr_args[] = { { text+undefres }, { numeric }, { numeric+optional } };
static const size_t substr_numargs = sizeof(substr_args)/sizeof(ArgumentDescriptor);
static void substr_func(BuiltinFunctionContextPtr f)
{
  string s = f->arg(0)->stringValue();
  size_t start = f->arg(1)->intValue();
  if (start>s.size()) start = s.size();
  size_t count = string::npos; // to the end
  if (f->arg(2)->defined()) {
    count = f->arg(2)->intValue();
  }
  f->finish(new StringValue(s.substr(start, count)));
}


// find(haystack, needle)
// find(haystack, needle, from)
static const ArgumentDescriptor find_args[] = { { text+undefres }, { text }, { numeric+optional }  };
static const size_t find_numargs = sizeof(find_args)/sizeof(ArgumentDescriptor);
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
    f->finish(new NumericValue(p));
  else
    f->finish(new AnnotatedNullValue("not found")); // not found
}


// format(formatstring, number)
// only % + - 0..9 . d, x, and f supported
static const ArgumentDescriptor format_args[] = { { text }, { numeric } };
static const size_t format_numargs = sizeof(format_args)/sizeof(ArgumentDescriptor);
static void format_func(BuiltinFunctionContextPtr f)
{
  string fmt = f->arg(0)->stringValue();
  if (
    fmt.size()<2 ||
    fmt[0]!='%' ||
    fmt.substr(1,fmt.size()-2).find_first_not_of("+-0123456789.")!=string::npos || // excluding last digit
    fmt.find_first_not_of("duxXeEgGf", fmt.size()-1)!=string::npos // which must be d,x or f
  ) {
    f->finish(new ErrorValue(ScriptError::Syntax, "invalid format string, only basic %%duxXeEgGf specs allowed"));
  }
  else {
    if (fmt.find_first_of("duxX", fmt.size()-1)!=string::npos)
      f->finish(new StringValue(string_format(fmt.c_str(), f->arg(1)->intValue()))); // int format
    else
      f->finish(new StringValue(string_format(fmt.c_str(), f->arg(1)->numValue()))); // double format
  }
}


// TODO: refresh implementation of throw() later
//
//  // throw(value)       - throw a expression user error with the string value of value as errormessage
//  static const ArgumentDescriptor throw_args[] = { { any } };
//  static const size_t throw_numargs = sizeof(throw_args)/sizeof(ArgumentDescriptor);
//  static void throw_func(BuiltinFunctionContextPtr f)
//  {
//    // throw(errvalue)    - (re-)throw with the error of the value passed
//    if (f->arg(0)->isError()) return throwError(f->arg(0)->error()); // just pass as is
//    else return throwError(ExpressionError::User, "%s", f->arg(0)->stringValue().c_str());
//  }


// error(value)       - create a user error value with the string value of value as errormessage, in all cases, even if value is already an error
static const ArgumentDescriptor error_args[] = { { any+null } };
static const size_t error_numargs = sizeof(error_args)/sizeof(ArgumentDescriptor);
static void error_func(BuiltinFunctionContextPtr f)
{
  f->finish(new NoThrowErrorValue(Error::err<ScriptError>(ScriptError::User, "%s", f->arg(0)->stringValue().c_str())));
}


// TODO: refresh implementation of error() within throw() later
//  // error()            - within a catch context only: the error thrown
//  static const ArgumentDescriptor error_args[] = { { any } };
//  static const size_t error_numargs = sizeof(error_args)/sizeof(ArgumentDescriptor);
//  static void error_func(BuiltinFunctionContextPtr f)
//  {
//    StackList::iterator spos = stack.end();
//    while (spos!=stack.begin()) {
//      spos--;
//      if (spos->state==s_catchStatement) {
//        // here the error is stored
//        f->finish(spos->res);
//        return true;
//      }
//    }
//    // try to use error() not within catch
//    return abortWithSyntaxError("error() can only be called from within catch statements");
//  }
//  else if (!synchronous && oneTimeResultHandler && aFunc=="earlyresult" && f->numArgs()==1) {
//    // send the one time result now, but keep script running
//    if (f->arg(0)->notValue()) return errorInArg(f->arg(0), aResult); // return error/null from argument
//    OLOG(LOG_INFO, "earlyresult sends '%s' to caller, script continues running", aResult.stringValue().c_str());
//    runCallBack(f->arg(0));
//  }


// errordomain(errvalue)
static const ArgumentDescriptor errordomain_args[] = { { error+undefres } };
static const size_t errordomain_numargs = sizeof(errordomain_args)/sizeof(ArgumentDescriptor);
static void errordomain_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("no error")); // no error, no domain
  f->finish(new StringValue(err->getErrorDomain()));
}


// errorcode(errvalue)
static const ArgumentDescriptor errorcode_args[] = { { error+undefres } };
static const size_t errorcode_numargs = sizeof(errorcode_args)/sizeof(ArgumentDescriptor);
static void errorcode_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("no error")); // no error, no code
  f->finish(new NumericValue(err->getErrorCode()));
}


// errormessage(value)
static const ArgumentDescriptor errormessage_args[] = { { error+undefres } };
static const size_t errormessage_numargs = sizeof(errormessage_args)/sizeof(ArgumentDescriptor);
static void errormessage_func(BuiltinFunctionContextPtr f)
{
  ErrorPtr err = f->arg(0)->errorValue();
  if (Error::isOK(err)) f->finish(new AnnotatedNullValue("no error")); // no error, no message
  f->finish(new StringValue(err->getErrorMessage()));
}


// eval(string, [args...])    have string executed as script code, with access to optional args as arg0, arg1, argN...
static const ArgumentDescriptor eval_args[] = { { text+executable }, { any+null+multiple } };
static const size_t eval_numargs = sizeof(eval_args)/sizeof(ArgumentDescriptor);
static void eval_func(BuiltinFunctionContextPtr f)
{
  ScriptObjPtr evalcode;
  if (f->arg(0)->hasType(executable)) {
    evalcode = f->arg(0);
  }
  else {
    // need to compile string first
    ScriptSource src(
      "eval function",
      f->instance() ? f->instance()->loggingContext() : NULL,
      f->arg(0)->stringValue(),
      f->domain()
    );
    evalcode = src.getExecutable();
  }
  if (!evalcode->hasType(executable)) {
    f->finish(evalcode); // return object itself, is an error
  }
  else {
    // get the context to run it
    ExecutionContextPtr ctx = evalcode->contextForCallingFrom(f->scriptmain());
    // pass args, if any
    for (size_t i = 1; i<f->numArgs(); i++) {
      ctx->setMemberAtIndex(i-1, f->arg(i-1), string_format("arg%lu", i-1));
    }
    // evaluate
    ctx->execute(evalcode, scriptbody, boost::bind(&BuiltinFunctionContext::finish, f, _1));
  }
}


// log (logmessage)
// log (loglevel, logmessage)
static const ArgumentDescriptor log_args[] = { { text+numeric }, { text+optional } };
static const size_t log_numargs = sizeof(log_args)/sizeof(ArgumentDescriptor);
static void log_func(BuiltinFunctionContextPtr f)
{
  int loglevel = LOG_INFO;
  size_t ai = 0;
  if (f->numArgs()>1) {
    loglevel = f->arg(ai)->intValue();
    ai++;
  }
  LOG(loglevel, "Script log: %s", f->arg(ai)->stringValue().c_str());
  f->finish();
}


// loglevel()
// loglevel(newlevel)
static const ArgumentDescriptor loglevel_args[] = { { numeric+optional } };
static const size_t loglevel_numargs = sizeof(loglevel_args)/sizeof(ArgumentDescriptor);
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
static const ArgumentDescriptor logleveloffset_args[] = { { numeric+optional } };
static const size_t logleveloffset_numargs = sizeof(logleveloffset_args)/sizeof(ArgumentDescriptor);
static void logleveloffset_func(BuiltinFunctionContextPtr f)
{
  int oldOffset = f->getLogLevelOffset();
  if (f->numArgs()>0) {
    int newOffset = f->arg(0)->intValue();
    f->setLogLevelOffset(newOffset);
  }
  f->finish(new NumericValue(oldOffset));
}


// TODO: implement when event handler mechanisms are in place
// is_weekday(w,w,w,...)
static const ArgumentDescriptor is_weekday_args[] = { { numeric+multiple } };
static const size_t is_weekday_numargs = sizeof(is_weekday_args)/sizeof(ArgumentDescriptor);
static void is_weekday_func(BuiltinFunctionContextPtr f)
{
  f->finish(new ErrorValue(ScriptError::Internal, "To be implemented"));
//  struct tm loctim; MainLoop::getLocalTime(loctim);
//  // check if any of the weekdays match
//  int weekday = loctim.tm_wday; // 0..6, 0=sunday
//  ExpressionValue newRes(0);
//  size_t refpos = aArgs.getPos(0); // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
//  for (int i = 0; i<f->numArgs(); i++) {
//    if (f->arg(i).notValue()) return errorInArg(f->arg(i), aResult); // return error/null from argument
//    int w = (int)f->arg(i).numValue();
//    if (w==7) w=0; // treat both 0 and 7 as sunday
//    if (w==weekday) {
//      // today is one of the days listed
//      newRes.setNumber(1);
//      break;
//    }
//  }
//  // freeze until next check: next day 0:00:00
//  loctim.tm_mday++;
//  loctim.tm_hour = 0;
//  loctim.tm_min = 0;
//  loctim.tm_sec = 0;
//  ExpressionValue res = newRes;
//  FrozenResult* frozenP = getFrozen(res,refpos);
//  newFreeze(frozenP, newRes, refpos, MainLoop::localTimeToMainLoopTime(loctim));
//  f->finish(res); // freeze time over, use actual, newly calculated result
}


// TODO: implement when event handler mechanisms are in place

#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function
// common implementation for after_time() and is_time()
static void timeCheckFunc(bool aIsTime, BuiltinFunctionContextPtr f)
{
  f->finish(new ErrorValue(ScriptError::Internal, "To be implemented"));
//  struct tm loctim; MainLoop::getLocalTime(loctim);
//  ExpressionValue newSecs;
//  if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
//  size_t refpos = aArgs.getPos(0); // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
//  if (aArgs.size()==2) {
//    // legacy spec
//    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
//    newSecs.setNumber(((int32_t)aArgs[0].numValue() * 60 + (int32_t)aArgs[1].numValue()) * 60);
//  }
//  else {
//    // specification in seconds, usually using time literal
//    newSecs.setNumber((int32_t)(aArgs[0].numValue()));
//  }
//  ExpressionValue secs = newSecs;
//  FrozenResult* frozenP = getFrozen(secs, refpos);
//  int32_t daySecs = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec;
//  bool met = daySecs>=secs.numValue();
//  // next check at specified time, today if not yet met, tomorrow if already met for today
//  loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)secs.numValue();
//  OLOG(LOG_INFO, "is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim)).c_str());
//  bool res = met;
//  // limit to a few secs around target if it's is_time
//  if (aIsTime && met && daySecs<secs.numValue()+IS_TIME_TOLERANCE_SECONDS) {
//    // freeze again for a bit
//    newFreeze(frozenP, secs, refpos, MainLoop::localTimeToMainLoopTime(loctim)+IS_TIME_TOLERANCE_SECONDS*Second);
//  }
//  else {
//    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)newSecs.numValue();
//    if (met) {
//      loctim.tm_mday++; // already met today, check again tomorrow
//      if (aIsTime) res = false;
//    }
//    newFreeze(frozenP, newSecs, refpos, MainLoop::localTimeToMainLoopTime(loctim));
//  }
//  aResult = res;
}

// after_time(time)
static const ArgumentDescriptor after_time_args[] = { { numeric } };
static const size_t after_time_numargs = sizeof(after_time_args)/sizeof(ArgumentDescriptor);
static void after_time_func(BuiltinFunctionContextPtr f)
{
  timeCheckFunc(false, f);
}

// is_time(time)
static const ArgumentDescriptor is_time_args[] = { { numeric } };
static const size_t is_time_numargs = sizeof(is_time_args)/sizeof(ArgumentDescriptor);
static void is_time_func(BuiltinFunctionContextPtr f)
{
  timeCheckFunc(true, f);
}



// TODO: implement when event handler mechanisms are in place
static const ArgumentDescriptor between_dates_args[] = { { numeric }, { numeric } };
static const size_t between_dates_numargs = sizeof(between_dates_args)/sizeof(ArgumentDescriptor);
static void between_dates_func(BuiltinFunctionContextPtr f)
{
  f->finish(new ErrorValue(ScriptError::Internal, "To be implemented"));
//  struct tm loctim; MainLoop::getLocalTime(loctim);
//  int smaller = (int)(f->arg(0)->numValue());
//  int larger = (int)(f->arg(1)->numValue());
//  int currentYday = loctim.tm_yday;
//  loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = 0;
//  loctim.tm_mon = 0;
//  bool lastBeforeFirst = smaller>larger;
//  if (lastBeforeFirst) swap(larger, smaller);
//  if (currentYday<smaller) loctim.tm_mday = 1+smaller;
//  else if (currentYday<=larger) loctim.tm_mday = 1+larger;
//  else { loctim.tm_mday = smaller; loctim.tm_year += 1; } // check one day too early, to make sure no day is skipped in a leap year to non leap year transition
//  updateNextEval(loctim);
//  f->finish(new BoolValue((currentYday>=smaller && currentYday<=larger)!=lastBeforeFirst));
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
  f->finish(new NumericValue((double)MainLoop::unixtime()/Day)); // epoch time in days with fractional time
}



// TODO: convert into single function returning a structured time object

// helper macro for getting time
#define prepTime \
  MLMicroSeconds t; \
  if (f->arg(0)->defined()) { \
    t = f->arg(0)->numValue()*Second; \
  } \
  else { \
    t = MainLoop::unixtime(); \
  } \
  double fracSecs; \
  struct tm loctim; \
  MainLoop::getLocalTime(loctim, &fracSecs, t);

// common argument descriptor for all time funcs
static const ArgumentDescriptor timegetter_args[] = { { numeric+optional } };
static const size_t timegetter_numargs = sizeof(timegetter_args)/sizeof(ArgumentDescriptor);

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
static const ArgumentDescriptor delay_args[] = { { numeric } };
static const size_t delay_numargs = sizeof(delay_args)/sizeof(ArgumentDescriptor);
static void delay_func(BuiltinFunctionContextPtr f)
{
  MLMicroSeconds delay = f->arg(0)->numValue()*Second;
  TicketObjPtr delayTicket = TicketObjPtr(new TicketObj);
  delayTicket->ticket.executeOnce(boost::bind(&BuiltinFunctionContext::finish, f, new AnnotatedNullValue("delayed")), delay);
  f->setAbortCallback(boost::bind(&MLTicket::cancel, &(delayTicket->ticket)));
}




// The standard function descriptor table
static const BuiltinFunctionDescriptor standardFunctions[] = {
  { "ifvalid", any, ifvalid_numargs, ifvalid_args, &ifvalid_func },
  { "ifvalid", any, ifvalid_numargs, ifvalid_args, &ifvalid_func },
  { "isvalid", any, isvalid_numargs, isvalid_args, &isvalid_func },
  { "if", any, if_numargs, if_args, &if_func },
  { "abs", numeric+null, abs_numargs, abs_args, &abs_func },
  { "int", numeric+null, int_numargs, int_args, &int_func },
  { "frac", numeric+null, frac_numargs, frac_args, &frac_func },
  { "round", numeric+null, round_numargs, round_args, &round_func },
  { "random", numeric, random_numargs, random_args, &random_func },
  { "min", numeric+null, min_numargs, min_args, &min_func },
  { "max", numeric+null, max_numargs, max_args, &max_func },
  { "limited", numeric+null, limited_numargs, limited_args, &limited_func },
  { "cyclic", numeric+null, cyclic_numargs, cyclic_args, &cyclic_func },
  { "string", text, string_numargs, string_args, &string_func },
  { "number", numeric, number_numargs, number_args, &number_func },
  { "copy", any, copy_numargs, copy_args, &copy_func },
  { "json", json, json_numargs, json_args, &json_func },
  //  { "setfield", any, setfield_numargs, setfield_args, &setfield_func },
  //  { "setelement", any, setelement_numargs, setelement_args, &setelement_func },
  { "jsonresource", json+error, jsonresource_numargs, jsonresource_args, &jsonresource_func },
  { "lastarg", any, lastarg_numargs, lastarg_args, &lastarg_func },
  { "strlen", numeric+null, strlen_numargs, strlen_args, &strlen_func },
  { "substr", text+null, substr_numargs, substr_args, &substr_func },
  { "find", numeric+null, find_numargs, find_args, &find_func },
  { "format", text, format_numargs, format_args, &format_func },
  //  { "throw", any, throw_numargs, throw_args, &throw_func },
  { "error", error, error_numargs, error_args, &error_func },
  //  { "error", any, error_numargs, error_args, &error_func },
  { "errordomain", text+null, errordomain_numargs, errordomain_args, &errordomain_func },
  { "errorcode", numeric+null, errorcode_numargs, errorcode_args, &errorcode_func },
  { "errormessage", text+null, errormessage_numargs, errormessage_args, &errormessage_func },
  { "eval", any, eval_numargs, eval_args, &eval_func },
  { "log", null, log_numargs, log_args, &log_func },
  { "loglevel", numeric, loglevel_numargs, loglevel_args, &loglevel_func },
  { "logleveloffset", numeric, logleveloffset_numargs, logleveloffset_args, &logleveloffset_func },
  { "is_weekday", any, is_weekday_numargs, is_weekday_args, &is_weekday_func },
  { "after_time", numeric, after_time_numargs, after_time_args, &after_time_func },
  { "is_time", numeric, is_time_numargs, is_time_args, &is_time_func },
  { "between_dates", numeric, between_dates_numargs, between_dates_args, &between_dates_func },
  { "sunrise", numeric+null, 0, NULL, &sunrise_func },
  { "dawn", numeric+null, 0, NULL, &dawn_func },
  { "sunset", numeric+null, 0, NULL, &sunset_func },
  { "dusk", numeric+null, 0, NULL, &dusk_func },
  { "epochtime", any, 0, NULL, &epochtime_func },
  { "timeofday", numeric, timegetter_numargs, timegetter_args, &timeofday_func },
  { "hour", any, timegetter_numargs, timegetter_args, &hour_func },
  { "minute", any, timegetter_numargs, timegetter_args, &minute_func },
  { "second", any, timegetter_numargs, timegetter_args, &second_func },
  { "year", any, timegetter_numargs, timegetter_args, &year_func },
  { "month", any, timegetter_numargs, timegetter_args, &month_func },
  { "day", any, timegetter_numargs, timegetter_args, &day_func },
  { "weekday", any, timegetter_numargs, timegetter_args, &weekday_func },
  { "yearday", any, timegetter_numargs, timegetter_args, &yearday_func },
  // Async
  { "delay", null+async, delay_numargs, delay_args, &delay_func },
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
    standardScriptingDomain->registerMemberLookup(new BuiltInFunctionLookup(BuiltinFunctions::standardFunctions));
  }
  return *standardScriptingDomain.get();
};



#if SIMPLE_REPL_APP

// MARK: - Simple REPL (Read Execute Print Loop) App

class SimpleREPLApp : public CmdLineApp
{
  typedef CmdLineApp inherited;

  ScriptSource source;
  char *buffer;
  size_t bufsize = 4096;
  size_t characters;

public:

  SimpleREPLApp() :
    source("REPL")
  {
    buffer = (char *)malloc(bufsize * sizeof(char));
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
    printf("p44Script REPL - type 'quit' to leave\n\n");
    RE();
  }

  void RE()
  {
    printf("p44Script: ");
    characters = getline(&buffer,&bufsize,stdin);
    if (strucmp(buffer, "quit", 4)==0) {
      printf("\nquitting p44Script REPL - bye!\n");
      terminateApp(EXIT_SUCCESS);
      return;
    }
    source.setSource(buffer, scriptbody);
    source.run(scriptbody+regular, boost::bind(&SimpleREPLApp::PL, this, _1));
  }

  void PL(ScriptObjPtr aResult)
  {
    if (aResult) {
      SourceCursor *cursorP = aResult->cursor();
      if (cursorP) {
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
    MainLoop::currentMainLoop().executeNow(boost::bind(&SimpleREPLApp::RE, this));
  }
};


int main(int argc, char **argv)
{
  // prevent debug output before application.main scans command line
  SETLOGLEVEL(LOG_NOTICE);
  SETERRLEVEL(LOG_NOTICE, false); // messages, if any, go to stderr
  // create app with current mainloop
  static SimpleREPLApp application;
  // pass control
  return application.main(argc, argv);
}


#endif // SIMPLE_REPL_APP

#endif // ENABLE_EXPRESSIONS

