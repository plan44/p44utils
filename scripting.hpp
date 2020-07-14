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

#ifndef __p44utils__scripting__
#define __p44utils__scripting__

#include "p44utils_common.hpp"

#if ENABLE_SCRIPTING

#include "timeutils.hpp"
#include <string>

#ifndef SCRIPTING_JSON_SUPPORT
  #define SCRIPTING_JSON_SUPPORT 1 // on by default
#endif

#if SCRIPTING_JSON_SUPPORT
  #include "jsonobject.hpp"
#endif


using namespace std;

namespace p44 { namespace Script {

  // MARK: - class and smart pointer forward definitions

  class ScriptObj;
  typedef boost::intrusive_ptr<ScriptObj> ScriptObjPtr;
  class ExpressionValue;
  typedef boost::intrusive_ptr<ExpressionValue> ExpressionValuePtr;
  class ErrorValue;
  class NumericValue;
  class StringValue;

  class ImplementationObj;
  class CompiledScript;
  typedef boost::intrusive_ptr<CompiledScript> CompiledCodePtr;

  class ExecutionContext;
  typedef boost::intrusive_ptr<ExecutionContext> ExecutionContextPtr;
  class ScriptCodeContext;
  typedef boost::intrusive_ptr<ScriptCodeContext> ScriptCodeContextPtr;
  class ScriptMainContext;
  typedef boost::intrusive_ptr<ScriptMainContext> ScriptMainContextPtr;
  class ScriptingDomain;
  typedef boost::intrusive_ptr<ScriptingDomain> ScriptingDomainPtr;

  class ScriptCodeThread;
  typedef boost::intrusive_ptr<ScriptCodeThread> ScriptCodeThreadPtr;

  class SourcePos;
  class SourceCursor;
  class SourceContainer;
  typedef boost::intrusive_ptr<SourceContainer> SourceContainerPtr;
  class ScriptSource;

  class SourceProcessor;
  class Compiler;
  class Processor;

  class ClassLevelLookup;
  typedef boost::intrusive_ptr<ClassLevelLookup> ClassMemberLookupPtr;


  // MARK: - Scripting Error

  /// Script Error
  class ScriptError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      // Catchable errors
      User, ///< user generated error (with throw)
      DivisionByZero,
      CyclicReference,
      Invalid, ///< invalid value
      NotFound, ///< referenced object not found at runtime (and cannot be created)
      NotCreated, ///< object/field does not exist and cannot be created, either
      Immutable, ///< object/field exists but is immutable and cannot be assigned
      Busy, ///< currently running
      // Fatal errors, cannot be catched
      FatalErrors,
      Syntax = FatalErrors, ///< script syntax error
      Aborted, ///< externally aborted
      Timeout, ///< aborted because max execution time limit reached
      AsyncNotAllowed, ///< async executable encountered during synchronous execution
      Internal, ///< internal inconsistency
      numErrorCodes
    } ErrorCodes;
    static const char *domain() { return "ScriptError"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return ScriptError::domain(); };
    ScriptError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    /// factory method to create string error fprint style
    static ErrorPtr err(ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "User",
      "DivisionByZero",
      "CyclicReference",
      "Invalid",
      "NotFound",
      "NotCreated",
      "Immutable",
      "Busy",
      "Syntax",
      "Aborted",
      "Timeout",
      "AsyncNotAllowed",
      "Internal",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  // MARK: - Script namespace types

  /// Evaluation flags
  enum {
    // run mode
    runModeMask = 0x000F,
    scanning = 0, ///< scanning only (compiling)
    initial = 0x0001, ///< handler, initial trigger expression run (no external event, implicit event is startup or code change)
    triggered = 0x0002, ///< handler, externally triggered (re-)evaluation
    timed = 0x0003, ///< handler, timed evaluation by timed retrigger
    regular = 0x0004, ///< regular script or expression code
    // modifiers
    modifierMask = 0xFFF0,
    expression = 0x0010, ///< evaluate as an expression (no flow control, variable assignments, blocks etc.)
    scriptbody = 0x0020, ///< evaluate as script body (no function or handler definitions)
    source = 0x0040, ///< evaluate as script (include parsing functions and handlers)
    synchronously = 0x0100, ///< evaluate synchronously, error out on async code
    stoprunning = 0x0200, ///< abort running evaluation in the same context before starting a new one
    queue = 0x0400, ///< queue for evaluation if other evaluations are still running/pending
    stopall = stoprunning+queue, ///< stop everything
    concurrently = 0x0800, ///< reset the context (clear local vars) before starting
    keepvars = 0x1000, ///< keep the local variables already set in the context
  };
  typedef uint16_t EvaluationFlags;

  /// Type info
  enum {
    // content type flags, usually one per object, but object/array can be combined with regular type
    typeMask = 0x00FF,
    scalarMask = 0x003F,
    none = 0x0000, ///< no type specification
    null = 0x0001, ///< NULL/undefined
    error = 0x0002, ///< Error
    numeric = 0x0004, ///< numeric value
    text = 0x0008, ///< text/string value
    json = 0x0010, ///< JSON value
    executable = 0x0020, ///< executable code
    structuredMask = 0x00C0,
    object = 0x0040, ///< is a object with named members
    array = 0x0080, ///< is an array with indexed elements
    // type classes
    any = typeMask-null, ///< any type except null
    scalar = numeric+text+json, ///< scalar types (json can also be structured)
    structured = object+array, ///< structured types
    value = scalar+structured, ///< value types (excludes executables)
    // attributes
    attrMask = 0xFFFFFF00,
    // - for argument checking
    optional = null, ///< if set, the argument is optional (means: is is allowed to be null even when null is not explicitly allowed)
    multiple = 0x0100, ///< this argument type can occur mutiple times (... signature)
    exacttype = 0x0200, ///< if set, type of argument must match, no autoconversion
    undefres = 0x0400, ///< if set, and an argument does not match type, the function result is automatically made null/undefined without executing the implementation
    async = 0x0800, ///< if set, the object cannot evaluate synchronously
    // - storage attributes and directives for named members
    mutablemembers = 0x1000, ///< members are mutable
    create = 0x2000, ///< set to create member if not yet existing
    global = 0x4000, ///< set to store in global context
    constant = 0x8000, ///< set to select only constant  (in the sense of: not settable by scripts) members
    objscope = 0x10000, ///< set to select only object scope members
    classscope = 0x20000, ///< set to select only class scope members
  };
  typedef uint32_t TypeInfo;

  /// Argument descriptor
  typedef struct {
    TypeInfo typeInfo; ///< info about allowed types, checking, open argument lists, etc.
    const char* name; ///< the name of the argument, can be NULL if unnamed positional argument
  } ArgumentDescriptor;

  // MARK: - ScriptObj base class

  /// evaluation callback
  /// @param aEvaluationResult the result of an evaluation
  typedef boost::function<void (ScriptObjPtr aEvaluationResult)> EvaluationCB;

  /// Base Object in scripting
  class ScriptObj : public P44LoggingObj
  {
  public:

    /// @name information
    /// @{

    /// get type of this value
    /// @return get type info
    virtual TypeInfo getTypeInfo() const { return null; }; // base object is a null/undefined

    /// @return a type description for logs and error messages
    static string typeDescription(TypeInfo aInfo);

    /// get name
    virtual string getIdentifier() const { return "unnamed"; };

    /// get annotation text - defaults to type description
    virtual string getAnnotation() const { return typeDescription(getTypeInfo()); };

    /// check type compatibility
    bool hasType(TypeInfo aTypeInfo) const { return (getTypeInfo() & aTypeInfo)!=0; }

    /// check for null/undefined
    bool undefined() const { return (getTypeInfo() & null)!=0; }

    /// check for null/undefined
    bool defined() const { return !undefined(); }

    /// check for error
    bool isErr() const { return (getTypeInfo() & error)!=0; }

    /// logging context to use
    virtual P44LoggingObj* loggingContext() const { return NULL; };

    /// @}

    /// @name lazy value loading / proxy objects
    /// @{

    /// @return true when the object's value is available. Might be false when this object is just a proxy
    /// @note call makeValid() to get a valid version from this object
    virtual bool valid() const { return true; } // base class objects are always valid

    /// @param aEvaluationCB will be called with a valid version of the object.
    /// @note if called on an already valid object, it returns itself in the callback, so
    ///   makeValid() can always be called. But for performance reasons, checking valid() before is recommended
    virtual void makeValid(EvaluationCB aEvaluationCB);

    /// @}


    /// @name value getters
    /// @{

    virtual double numValue() const { return 0; }; ///< @return a conversion to numeric (using literal syntax), if value is string
    virtual bool boolValue() const { return numValue()!=0; }; ///< @return a conversion to boolean (true = not numerically 0, not JSON-falsish)
    virtual string stringValue() const { return "undefined"; }; ///< @return a conversion to string of the value
    virtual ErrorPtr errorValue() const { return Error::ok(); } ///< @return error value (always an object, OK if not in error)
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const { return JsonObject::newNull(); } ///< @return a JSON value
    #endif
    // generic converters
    int intValue() const { return (int)numValue(); } ///< @return numeric value as int
    int64_t int64Value() const { return (int64_t)numValue(); } ///< @return numeric value as int64

    /// @}

    /// @name member access
    /// @{

    /// get object subfield/member by name
    /// @param aName name of the member to find
    /// @param aTypeRequirements what type and type attributes the returned member must have, defaults to no restriction
    /// @return ScriptObj representing the member, or NULL if none
    /// @note only possibly returns something in objects with type attribute "object"
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) { return ScriptObjPtr(); };

    /// number of members accessible by index (e.g. positional parameters or array elements)
    /// @return number of members
    /// @note may or may not overlap with named members of the same object
    virtual size_t numIndexedMembers() const { return 0; }

    /// get object subfield/member by index, for example positional arguments in a ExecutionContext
    /// @param aIndex index of the member to find
    /// @param aTypeRequirements what type and type attributes the returned member must have, defaults to no restriction
    /// @return ScriptObj representing the member with index
    /// @note only possibly returns something in objects with type attribute "array"
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) { return ScriptObjPtr(); };

    /// set new object for named member
    /// @param aName name of the member to assign
    /// @param aMember the member to assign
    /// @param aStorageAttributes flags directing storage (such as global vs local vars).
    /// @return ok or Error describing reason for assignment failure
    /// @note only possibly works on objects with type attribute "mutablemembers"
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes = none);

    /// set new object for member at index (array, positional parameter)
    /// @param aIndex name of the member to assign
    /// @param aMember the member to assign
    /// @param aName optional name of the member (for containers where members have an index AND an name, such as function parameters)
    /// @return ok or Error describing reason for assignment failure
    /// @note only possibly works on objects with type attribute "mutablemembers"
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "");

    /// @}

    /// @name executable support
    /// @{

    /// get information (typeInfo and possibly a name) for a positional argument
    /// @param aIndex the argument index (0..N)
    /// @return the argument descriptor or NULL if there is no argument at this position
    /// @note functions might have an open argument list, so do not try to exhaust this
    virtual const ArgumentDescriptor* argumentInfo(size_t aIndex) const { return NULL; };

    /// get context to call this object as a (sub)routine of a given context
    /// @param aMainContext the context from where to call from (evaluate in) this implementation
    ///   For executing script body code, aMainContext is always the domain (but can be passed NULL as script bodies
    ///   should know their domain already (if passed, it will be checked for consistency)
    /// @note the context might be pre-existing (in case of scriptbody code which gets associated with the context it
    ///    is hosted in) or created new (in case of functions which run in a temporary private context)
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext) const { return ExecutionContextPtr(); }

    /// @return true if this object originates from the specified source
    /// @note this is needed to remove objects such as functions and handlers when their source changes or is deleted
    virtual bool originatesFrom(SourceContainerPtr aSource) const { return false; }

    /// @}


    /// @name operators
    /// @{

    // boolean, always returning native C++ boolean
    // - generic
    bool operator!() const;
    bool operator&&(const ScriptObj& aRightSide) const;
    bool operator||(const ScriptObj& aRightSide) const;
    // - derived
    bool operator!=(const ScriptObj& aRightSide) const;
    bool operator>=(const ScriptObj& aRightSide) const;
    bool operator>(const ScriptObj& aRightSide) const;
    bool operator<=(const ScriptObj& aRightSide) const;
    // - virtual, type-specific
    virtual bool operator<(const ScriptObj& aRightSide) const;
    virtual bool operator==(const ScriptObj& aRightSide) const;
    // arithmetic, returning a SciptObjPtr, type-specific
    virtual ScriptObjPtr operator+(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator-(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator*(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator/(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator%(const ScriptObj& aRightSide) const { return new ScriptObj(); };

    /// @}



    /// @name triggering support
    /// @{

    // TODO: add triggering support methods

    /// @}

  };


  // MARK: - Value classes

  /// a value for use in expressions
  class ScriptValue : public ScriptObj
  {
    typedef ScriptObj inherited;
  public:
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return scalar; };
  };

  /// an explicitly annotated null value (in contrast to ScriptObj base class which is a non-annotated null)
  class AnnotatedNullValue : public ScriptValue
  {
    typedef ScriptValue inherited;
    string annotation;
  public:
    AnnotatedNullValue(string aAnnotation) : annotation(aAnnotation) {};
    virtual string getAnnotation() const P44_OVERRIDE { return annotation; };
  };


  class ErrorValue : public ScriptValue
  {
    typedef ScriptValue inherited;
  protected:
    ErrorPtr err;
  public:
    ErrorValue(ErrorPtr aError) : err(aError) {};
    ErrorValue(ScriptError::ErrorCodes aErrCode, const char *aFmt, ...);
    virtual string getAnnotation() const P44_OVERRIDE { return "error"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return error; };
    // value getters
    virtual double numValue() const P44_OVERRIDE { return err ? 0 : err->getErrorCode(); };
    virtual string stringValue() const P44_OVERRIDE { return Error::text(err); };
    virtual ErrorPtr errorValue() const P44_OVERRIDE { return err ? err : Error::ok(); };
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif
    // operators
    virtual bool operator==(const ScriptObj& aRightSide) const P44_OVERRIDE;
  };


  class NumericValue : public ScriptValue
  {
    typedef ScriptValue inherited;
    double num;
  public:
    NumericValue(double aNumber) : num(aNumber) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "numeric"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return numeric; };
    // value getters
    virtual double numValue() const P44_OVERRIDE { return num; }; // native
    virtual string stringValue() const P44_OVERRIDE { return string_format("%lg", num); };
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE { return JsonObject::newDouble(num); };
    #endif
    // operators
    virtual bool operator<(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual bool operator==(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator+(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator-(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator*(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator/(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator%(const ScriptObj& aRightSide) const P44_OVERRIDE;
  };


  class StringValue : public ScriptValue
  {
    typedef ScriptValue inherited;
    string str;
  public:
    StringValue(string aString) : str(aString) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "string"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return text; };
    // value getters
    virtual string stringValue() const P44_OVERRIDE { return str; }; // native
    virtual double numValue() const P44_OVERRIDE;
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif
    // operators
    virtual bool operator<(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual bool operator==(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator+(const ScriptObj& aRightSide) const P44_OVERRIDE;
  };


  #if SCRIPTING_JSON_SUPPORT
  class JsonValue : public ScriptValue
  {
    typedef ScriptValue inherited;
    JsonObjectPtr jsonval;
  public:
    JsonValue(JsonObjectPtr aJson) : jsonval(aJson) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "json"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE;
    // value getters
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE { return jsonval; } // native
    virtual double numValue() const P44_OVERRIDE;
    virtual string stringValue() const P44_OVERRIDE;
    virtual bool boolValue() const P44_OVERRIDE;
    // member access
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) P44_OVERRIDE;
    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) P44_OVERRIDE;
    // TODO: also implement setting members later
  };
  #endif // SCRIPTING_JSON_SUPPORT


  // MARK: - Extendable class member lookup

  /// implements a lookup step that can be shared between multiple execution contexts. It does NOT
  /// hold any _instance_ state, but can still serve to lookup instances by using the _aThisObj_ provided
  /// by callers (which can be ignored when providing class members).
  class ClassLevelLookup : public P44Obj
  {
  public:

    /// return mask of all types that may be (but not necessarily are) in this lookup
    /// @note this is for optimizing lookups for certain types
    virtual TypeInfo containsTypes() const { return none; }

    /// get object subfield/member by name
    /// @param aThisObj the object _instance_ of which we want to access a member (can be NULL in case of singletons)
    /// @param aName name of the member to find
    /// @param aTypeRequirements what type and type attributes the returned member must have, defaults to no restriction
    /// @return ScriptObj representing the member, or NULL if none
    virtual ScriptObjPtr memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements = none) const = 0;

    /// set new object for named member
    /// @param aThisObj the object _instance_ of which we want to access a member (can be NULL in case of singletons)
    /// @param aName name of the member to assign
    /// @param aMember the member to assign
    /// @return ok or Error describing reason for assignment failure
    /// @note only possibly works on objects with type attribute "mutablemembers"
    virtual ErrorPtr setMemberByNameFrom(ScriptObjPtr aThisObj, const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes = none)
       { return ScriptError::err(ScriptError::NotFound, "cannot assign '%s'", aName.c_str()); }

  };



  // MARK: - Execution contexts

  /// Abstract base class for executables.
  /// Can hold indexed (positional) arguments as these are needed for all types
  /// of implementations (e.g. built-ins as well as script defined functions)
  class ExecutionContext : public ScriptObj
  {
    typedef ScriptObj inherited;
    friend class ScriptCodeContext;
    friend class BuiltinFunctionContext;

    typedef std::vector<ScriptObjPtr> IndexedVarVector;
    IndexedVarVector indexedVars;
    ScriptMainContextPtr mainContext; ///< the main context

    ExecutionContext(ScriptMainContextPtr aMainContext);

  public:

    /// clear local variables (indexed arguments)
    virtual void clearVars();

    // access to function arguments (positional) by index plus optionally a name
    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

//    /// "Compile" (for now, just scan for function definitions and syntax errors)
//    /// @param aSource the script source to compile
//    /// @return an ImplementationObj when successful, a Error(Pos)Value otherwise
//    ScriptObjPtr compile(SourceContainerPtr aSource);

    /// release all objects stored in this container and other known containers which were defined by aSource
    virtual void releaseObjsFromSource(SourceContainerPtr aSource); // no source-derived permanent objects here

    /// Evaluate a object
    /// @param aToEvaluate the object to be evaluated
    /// @param aEvalFlags evaluation control flags
    /// @param aEvaluationCB will be called to deliver the result of the evaluation
    virtual void evaluate(ScriptObjPtr aToEvaluate, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB) = 0;

    /// abort evaluation (of all threads if context has more than one)
    /// @param aAbortFlags set stoprunning to abort currently running threads, queue to empty the queued threads
    /// @param aAbortResult if set, this is what abort will report back
    virtual void abort(EvaluationFlags aAbortFlags = stoprunning+queue, ScriptObjPtr aAbortResult = ScriptObjPtr()) = 0;

    /// synchronously evaluate the object, abort if async executables are encountered
    ScriptObjPtr evaluateSynchronously(ScriptObjPtr aToEvaluate, EvaluationFlags aEvalFlags);

    /// check argument against signature and add to context if ok
    /// @param aArgument the object to be passed as argument. Pass NULL to check if aCallee has more non-optional arguments
    /// @param aIndex argument index
    /// @param aCallee the object to be called with this argument (provides the signature)
    ErrorPtr checkAndSetArgument(ScriptObjPtr aArgument, size_t aIndex, ScriptObjPtr aCallee);

    /// @name execution environment info
    /// @{

    /// @return the main context from which this context was called (as a subroutine)
    virtual ScriptMainContextPtr scriptmain() const { return mainContext; }

    /// @return the object _instance_ that
    /// @note plain execution contexts do not have a thisObj() of their own, but use the linked mainContext's
    virtual ScriptObjPtr instance() const;

    /// @return return the domain, which is the context for resolving global variables
    virtual ScriptingDomainPtr domain() const;

    /// @return geolocation for this context or NULL if none
    /// @note returns geolocation of domain by default.
    virtual GeoLocation* geoLocation();

    /// @}

  };


  /// Base class providing context for executing any script code (vs. built-in executables).
  /// Can hold named local variables (and provides named access to arguments).
  class ScriptCodeContext : public ExecutionContext
  {
    typedef ExecutionContext inherited;
    friend class ScriptCodeThread;
    friend class ScriptMainContext;
    friend class CompiledFunction;

    typedef std::map<string, ScriptObjPtr, lessStrucmp> NamedVarMap;
    NamedVarMap namedVars; ///< the named local variables/objects of this context

    typedef std::list<ScriptCodeThreadPtr> ThreadList;
    ThreadList threads; ///< the running "threads" in this context. First is the main thread of the evaluation.
    ThreadList queuedThreads; ///< the queued threads in this context

    ScriptCodeContext(ScriptMainContextPtr aMainContext);

  public:

    virtual void releaseObjsFromSource(SourceContainerPtr aSource) P44_OVERRIDE;

    /// clear local variables (named members)
    virtual void clearVars() P44_OVERRIDE;

    // access to local variables by name
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) P44_OVERRIDE;
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes = none) P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

    /// Evaluate a object
    /// @param aToEvaluate the object to be evaluated
    /// @param aEvalFlags evaluation mode/flags. Script thread can evaluate...
    /// - 
    /// @param aEvaluationCB will be called to deliver the result of the evaluation
    virtual void evaluate(ScriptObjPtr aToEvaluate, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB) P44_OVERRIDE;

    /// abort evaluation of all threads
    /// @param aAbortFlags set stoprunning to abort currently running threads, queue to empty the queued threads
    /// @param aAbortResult if set, this is what abort will report back
    virtual void abort(EvaluationFlags aAbortFlags = stoprunning+queue, ScriptObjPtr aAbortResult = ScriptObjPtr()) P44_OVERRIDE;

  private:

    /// called by threads ending
    void threadTerminated(ScriptCodeThreadPtr aThread);

  };


  /// Context for a script's main body, which can bring objects and functions into scope
  /// from the script's environment in the overall application structure via member lookups
  class ScriptMainContext : public ScriptCodeContext
  {
    typedef ScriptCodeContext inherited;
    friend class ScriptingDomain;

    typedef std::list<ClassMemberLookupPtr> LookupList;
    LookupList lookups;
    ScriptingDomainPtr domainObj; ///< the scripting domain (unless it's myself to avoid locking)
    ScriptObjPtr thisObj; ///< the object _instance_ scope of this execution context (if any)

    /// private constructor, only ScriptingDomain should use it
    /// @param aDomain owning link to domain - as long as context exists, domain may not get deleted.
    /// @param aThis can be NULL if there's no object instance scope for this script. This object is
    ///    passed to all registered member lookups
    ScriptMainContext(ScriptingDomainPtr aDomain, ScriptObjPtr aThis);

  public:

    // access to objects in the context hierarchy of a local execution
    // (local objects, parent context objects, global objects)
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) P44_OVERRIDE;
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes = none) P44_OVERRIDE;

    // direct access to this and domain (not via mainContext, as we can't set maincontext w/o self-locking)
    virtual ScriptObjPtr instance() const P44_OVERRIDE { return thisObj; }
    virtual ScriptingDomainPtr domain() const P44_OVERRIDE { return domainObj; }
    virtual ScriptMainContextPtr scriptmain() const P44_OVERRIDE { return dynamic_pointer_cast<ScriptMainContext>(domainObj) ; }

    /// register an additional lookup
    /// @param aMemberLookup a lookup object
    void registerMemberLookup(ClassMemberLookupPtr aMemberLookup);

  };




  class ImplementationObj : public ScriptObj
  {
    typedef ScriptObj inherited;
  public:
    virtual TypeInfo getTypeInfo() const { return executable; };
  };


  // MARK: - Common Script code scanning/executing mechanisms

  // Operator syntax mode
  #define SCRIPT_OPERATOR_MODE_FLEXIBLE 0
  #define SCRIPT_OPERATOR_MODE_C 1
  #define SCRIPT_OPERATOR_MODE_PASCAL 2
  // EXPRESSION_OPERATOR_MODE_FLEXIBLE:
  //   := is unambiguous assignment
  //   == is unambiguous comparison
  //   = works as assignment when used after a variable specification in scripts, and as comparison in expressions
  // EXPRESSION_OPERATOR_MODE_C:
  //   = and := are assignment
  //   == is comparison
  // EXPRESSION_OPERATOR_MODE_PASCAL:
  //   := is assignment
  //   = and == is comparison
  // Note: the unabiguous "==", "<>" and "!=" are both supported in all modes
  #ifndef SCRIPT_OPERATOR_MODE
    #define SCRIPT_OPERATOR_MODE SCRIPT_OPERATOR_MODE_FLEXIBLE
  #endif

  // operators with precedence
  typedef enum {
    op_none       = (0 << 3) + 6,
    op_not        = (1 << 3) + 6,
    op_multiply   = (2 << 3) + 5,
    op_divide     = (3 << 3) + 5,
    op_modulo     = (4 << 3) + 5,
    op_add        = (5 << 3) + 4,
    op_subtract   = (6 << 3) + 4,
    op_equal      = (7 << 3) + 3,
    op_assignOrEq = (8 << 3) + 3,
    op_notequal   = (9 << 3) + 3,
    op_less       = (10 << 3) + 3,
    op_greater    = (11 << 3) + 3,
    op_leq        = (12 << 3) + 3,
    op_geq        = (13 << 3) + 3,
    op_and        = (14 << 3) + 2,
    op_or         = (15 << 3) + 1,
    op_assign     = (16 << 3) + 0,
    opmask_precedence = 0x07
  } ScriptOperator;


  /// opaque position object within a source text contained elsewhere
  /// only SourceRef may access it
  class SourcePos
  {
    friend class SourceCursor;

    const char* ptr; ///< pointer to current position in the source text
    const char* bol; ///< pointer to beginning of current line
    const char* eot; ///< pointer to where the text ends (0 char or not)
    size_t line; ///< line number
  public:
    SourcePos(const char* aText, size_t aLen);
    SourcePos(const string &aText);
    SourcePos(const SourcePos &aCursor);
    SourcePos();
  };


  /// refers to a part of a source text, retains the container the source lives in
  /// provides basic element parsing generating values and possibly errors referring to
  /// the position they occur (and also retaining that source as long as the error lives)
  class SourceCursor
  {
  public:
    SourceContainerPtr source; ///< the source containing the string we're pointing to
    SourcePos pos; ///< the position within the source

    bool refersTo(SourceContainerPtr aSource) const { return source==aSource; } ///< check if this sourceref refers to a particular source

    // info
    size_t lineno() const; ///< 0-based line counter
    size_t charpos() const; ///< 0-based character offset

    /// @name source text access and parsing utilities
    /// @{

    // access
    char c(size_t aOffset=0) const; ///< @return character at offset from current position, 0 if none
    size_t charsleft() const; ///< @return number of chars to end of code
    bool EOT(); ///< true if we are at end of text
    bool next(); ///< advance to next char, @return false if not possible to advance
    bool advance(size_t aNumChars); ///< advance by specified number of chars, includes counting lines
    bool nextIf(char aChar); ///< @return true and advance cursor if @param aChar matches current char, false otherwise
    void skipNonCode(); ///< skip non-code, i.e. whitespace and comments
    // parsing utilities
//    const char* checkForIdentifier(size_t& aLen); ///< check for identifier, @return pointer to identifier or NULL if none
    bool parseIdentifier(string& aIdentifier, size_t* aIdentifierLenP = NULL); ///< @return true if identifier found, stored in aIndentifier and cursor advanced
    ScriptOperator parseOperator(); ///< @return operator or op_none, advances cursor on success

    ScriptObjPtr parseNumericLiteral(); ///< @return numeric or error, advances cursor on success
    ScriptObjPtr parseStringLiteral(); ///< @return string or error, advances cursor on success
    ScriptObjPtr parseCodeLiteral(); ///< @return executable or error, advances cursor on success
    #if SCRIPTING_JSON_SUPPORT
    ScriptObjPtr parseJSONLiteral(); ///< @return string or error, advances cursor on success
    #endif

    /// @}
  };


  class ErrorPosValue : public ErrorValue
  {
    typedef ErrorValue inherited;
    SourceCursor cursor;
  public:
    ErrorPosValue(const SourceCursor &aCursor, ErrorPtr aError) : inherited(aError), cursor(aCursor) {};
    ErrorPosValue(const SourceCursor &aCursor, ScriptError::ErrorCodes aErrCode, const char *aFmt, ...);
    void setSourceRef(const SourceCursor &aCursor) { cursor = aCursor; };
  };


  /// the actual script source text, shared among ScriptSource and possibly multiple SourceRefs
  class SourceContainer : public P44Obj
  {
    friend class SourceCursor;
    friend class ScriptSource;
    friend class CompiledScript;
    friend class CompiledFunction;
    friend class ExecutionContext;

    const char *originLabel; ///< a label used for logging and error reporting
    P44LoggingObj* loggingContextP; ///< the logging context
    string source; ///< the source code as written by the script author
  public:
    SourceContainer(const char *aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource) : originLabel(aOriginLabel), source(aSource) {};
    // get a reference to this source code
    SourceCursor getRef();
  };


  /// class representing a script source in its entiety including all context needed to run it
  class ScriptSource
  {
    ScriptingDomainPtr scriptingDomain; ///< the scripting domain
    ScriptMainContextPtr sharedMainContext; ///< a shared context to always run this source in. If not set, each script gets a new main context
    ScriptObjPtr cachedExecutable; ///< the compiled executable for the script's body.
    const char *originLabel; ///< a label used for logging and error reporting
    P44LoggingObj* loggingContextP; ///< the logging context
    SourceContainerPtr sourceContainer; ///< the container of the source

  public:
    /// create empty script source
    ScriptSource(const char* aOriginLabel = NULL, P44LoggingObj* aLoggingContextP = NULL);
    /// all-in-one adhoc script source constructor
    ScriptSource(const char* aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource, ScriptingDomainPtr aDomain = ScriptingDomainPtr());
    ~ScriptSource();

    /// set domain (where global objects from compilation will be stored)
    /// @param aDomain the domain. Defaults to StandardScriptingDomain::sharedDomain() if not explicitly set
    void setDomain(ScriptingDomainPtr aDomain);

    /// set pre-existing execution context to use, possibly shared with other script sources
    /// @param aSharedMainContext a context previously obtained from the domain with newContext()
    void setSharedMainContext(ScriptMainContextPtr aSharedMainContext);

    /// set source code
    void setSource(const string aSource);

    /// get executable ((re-)compile if needed)
    /// @return executable from this source
    ScriptObjPtr getExecutable();

    /// convenience quick runner
    void run(EvaluationCB aEvaluationCB);
  };


  /// Scripting domain, usually singleton, containing global variables and event handlers
  /// No code runs directly in this context
  class ScriptingDomain : public ScriptMainContext
  {
    typedef ScriptMainContext inherited;

    GeoLocation *geoLocationP;

    // TODO: global script defined function storage -> no, are just vars of the domains
    // TODO: global event handler storage

  public:

    ScriptingDomain() : inherited(ScriptingDomainPtr(), ScriptObjPtr()), geoLocationP(NULL) {};

    /// set geolocation to use for functions that refer to location
    void setGeoLocation(GeoLocation* aGeoLocationP) { geoLocationP = aGeoLocationP; };

    // environment
    virtual GeoLocation* geoLocation() P44_OVERRIDE { return geoLocationP; };

    /// get new execution context
    /// @param aInstanceObj the object _instance_ scope for scripts running in this context.
    ///   If set, the script main code is working as a method of aInstanceObj, i.e. has access
    ///   to members of aInstanceObj like other script-local variables.
    /// @note the scripts's _class_ scope is defined by the lookups that are registered.
    ///   The class scope can also bring in aInstanceObj related member functions (methods), but also
    ///   plain functions (static methods) and other members.
    ScriptMainContextPtr newContext(ScriptObjPtr aInstanceObj = ScriptObjPtr());

  };


  // MARK: generic source processor base class

  /// Base class for parsing or executing script code
  /// This contains the state machine and strictly delegates any actual
  /// interfacing with the environment to subclasses
  class SourceProcessor
  {
  public:

    SourceProcessor() : nextState(NULL), skipping(false) {};

    /// prepare processing
    /// @param aCursor the source (part) to process
    /// @param aStartFlags at what level to start (script, body, expression)
    void initProcessing(const SourceCursor& aCursor, EvaluationFlags aStartFlags);

    /// start processing
    /// @param aDoneCB will be called when process synchronously or asynchronously ends
    virtual void start(EvaluationCB aCompletedCB);

    /// resume processing
    /// @param aNewResult if not NULL, this object will be stored to result as first step of the resume
    /// @note must be called for every step of the process that does not lead to completion
    void resume(ScriptObjPtr aNewResult = ScriptObjPtr());

    /// complete processing
    /// @param aFinalResult the result to deliver with the completion callback
    /// @note inherited method must be called from subclasses as this must make sure stepLoop() will end.
    virtual void complete(ScriptObjPtr aFinalResult);

  protected:

    /// called by resume to perform next step(s).
    /// @note base class just steps synchronously as long as it can by calling step().
    ///    Detection of synchronous execution is done via the resuming/resumed flags.
    /// @note subclases might use different strategies for stepping, or override our step.
    ///   The only condition is that every step ends in a call to resume()
    virtual void stepLoop();

    /// internal statemachine step
    virtual void step();


    /// @name execution hooks. These are dummies in the base class, but implemented
    ///   in actual code execution subclasses. These must call resume()
    /// @{

    /// must retrieve the member with name==identifier from current result (or from the script scope if result==NULL)
    /// @note must call done() when result contains the member (or NULL if not found)
    virtual void memberByIdentifier();

    /// must retrieve the indexed member from current result (or from the script scope if result==NULL)
    /// @note must call done() when result contains the member (or NULL if not found)
    virtual void memberByIndex(size_t aIndex);

    /// apply the specified argument to the current result
    virtual void pushFunctionArgument(ScriptObjPtr aArgument);

    /// evaluate the current result and replace it with the output from the evaluation (e.g. function call)
    virtual void evaluate();

    /// @}

  private:

    /// @name source processor internal state machine
    /// @{

    ///< methods of this objects which handle a state
    typedef void (SourceProcessor::*StateHandler)(void);

    // state that can be pushed
    SourceCursor src; ///< the scanning position within code
    StateHandler nextState; ///< next state to call
    ScriptObjPtr result; ///< the current result object
    ScriptObjPtr poppedResult; ///< last result popped from stack
    bool skipping; ///< skipping

    // other internal state, not pushed
    string identifier; ///< for processing identifiers
    bool resuming; ///< detector for resume calling itself (synchronous execution)
    bool resumed; ///< detector for resume calling itself (synchronous execution)
    EvaluationCB completedCB; ///< called when completed

    /// Scanner Stack frame
    class StackFrame {
    public:
      StackFrame(
        SourcePos& aPos,
        bool aSkipping,
        StateHandler aReturnToState,
        ScriptObjPtr aResult
      ) :
        pos(aPos),
        skipping(aSkipping),
        returnToState(aReturnToState),
        result(aResult)
      {}
      SourcePos pos; ///< scanning position
      bool skipping; ///< set if only skipping code, not evaluating
      StateHandler returnToState; ///< next state to run after pop
      ScriptObjPtr result; ///< the current result object
    };

//    int precedence; ///< encountering a binary operator with smaller precedence will end the expression
//    ScriptOperator op; ///< operator
//    bool flowDecision; ///< flow control decision

    typedef std::list<StackFrame> StackList;
    StackList stack; ///< the stack


    /// convenience end of step using current result and checking for errors
    /// @note includes calling resume()
    void done();

    /// readability wrapper for setting the next state but NOT YET completing current state's processing
    inline void setNextState(StateHandler aNextState) { nextState = aNextState; }

    /// convenience function for transition to a new state, i.e. setting the new state and signalling done() in one step
    /// @param aNextState set the next state
    inline void doneAndGoto(StateHandler aNextState) { nextState = aNextState; done(); }

    /// push the current state
    /// @param aReturnToState the state to return to after pop().
    void push(StateHandler aReturnToState);

    /// return to the last pushed state
    void pop();


    /// state handlers
    /// @note MUST call stepDone() itself or make sure a callback will call it later

    void s_simpleTerm(); ///< at the beginning of a term
    void s_member(); ///< immediately after identifier
    void s_subscriptArg(); ///< immediately after subscript expression evaluation
    void s_funcArg(); ///< immediately after function argument evaluation


    void s_newExpression(); ///< at the beginning of an expression which only ends syntactically (end of code, delimiter, etc.) -> resets precedence

    void s_funcExec(); ///< ready to execute the function


    void s_result(); ///< result of an expression or term ready, pop the stack to see next state to run

    void s_ccomplete(); ///< nothing more to do, result represents result of entire scanning/evaluation process

/*
 typedef enum {
    // Completion states
    st_unwound, ///< stack unwound, can't continue, check for trailing garbage
    st_complete, ///< completing evaluation
    st_abort, ///< aborting evaluation
    st_finalize, ///< ending, will pop last stack frame
    // Script States
    st_statement_states, ///< marker
    // - basic statements
    st_body = st_statement_states, ///< at the body level (end of expression ends body)
    st_block, ///< within a block, exists when '}' is encountered, but skips ';'
    st_oneStatement, ///< a single statement, exits when ';' is encountered
    st_noStatement, ///< pop back one level
    st_returnValue, ///< "return" statement value calculation
    // - if/then/else
    st_ifCondition, ///< executing the condition of an if
    st_ifTrueStatement, ///< executing the if statement
    st_elseStatement, ///< executing the else statement
    // - while
    st_whileCondition, ///< executing the condition of a while
    st_whileStatement, ///< executing the while statement
    // - try/catch
    st_tryStatement, ///< executing the statement to try
    st_catchStatement, ///< executing the handling of a thrown error
    // - assignment to variables
    st_assignToVar, ///< assign result of an expression to a variable
    // Special result passing state
    st_result, ///< result of an expression or term
    // Expression States
    st_expression_states, ///< marker
    // - expression
    st_newExpression = st_expression_states, ///< at the beginning of an expression which only ends syntactically (end of code, delimiter, etc.) -> resets precedence
    st_expression, ///< handle (sub)expression start, precedence inherited or set by caller
    st_groupedExpression, ///< handling a paranthesized subexpression result
    st_exprFirstTerm, ///< first term of an expression
    st_exprLeftSide, ///< left side of an ongoing expression
    st_exprRightSide, ///< further terms of an expression
    // - simple terms
    st_simpleTerm, ///< at the beginning of a term
    st_funcArg, ///< handling a function argument
    st_funcExec, ///< handling function execution
    st_subscriptArg, ///< handling a subscript parameter
    st_subscriptExec, ///< handling access based on subscript args
    numEvalStates
  } ScanState; ///< the state of the scanner

 */
    /// @}

  };


  // MARK: "compiling" code

  class CompiledFunction : public ImplementationObj
  {
    typedef ImplementationObj inherited;
    friend class ScriptCodeContext;

  protected:
    SourceCursor cursor; ///< reference to the source part from which this object originates from

  public:
    CompiledFunction(const SourceCursor& aCursor) : cursor(aCursor) {};
    virtual bool originatesFrom(SourceContainerPtr aSource) const P44_OVERRIDE { return cursor.refersTo(aSource); };
    virtual P44LoggingObj* loggingContext() const P44_OVERRIDE { return cursor.source ? cursor.source->loggingContextP : NULL; };

    /// get subroutine context to call this object as a subroutine/function call from a given context
    /// @param aMainContext the context from where this function is now called (the same function can be called
    ///   from different contexts)
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext) const P44_OVERRIDE;
  };


  class CompiledScript : public CompiledFunction
  {
    typedef CompiledFunction inherited;
    friend class ScriptCompiler;

    ScriptMainContextPtr mainContext; ///< the main context this script should execute in

    CompiledScript(const SourceCursor& aCursor, ScriptMainContextPtr aMainContext) : inherited(aCursor), mainContext(aMainContext) {};

  public:

    /// get new main routine context for running this object as script main.
    /// @param aMainContext the main context for a script execution is always the domain, but this is used only
    ///    for consistency checking (the compiled code already knows its main context). It can be passed NULL when
    ///    no check is needed.
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext) const P44_OVERRIDE;

  };


  class ScriptCompiler : public SourceProcessor
  {
    typedef SourceProcessor inherited;

    ScriptingDomainPtr domain; ///< the domain to store compiled functions and handlers
    SourceCursor bodyRef; ///< where the script body starts

  public:

    ScriptCompiler(ScriptingDomainPtr aDomain) : domain(aDomain) {}

    /// Scan code, extract function definitions, global vars, event handlers into scripting domain, return actual code
    /// @param aSource the source code
    /// @param aParsingMode how to parse (as expression, scriptbody or full script with function+handler definitions)
    /// @param aCompilerContext the context in which this script was compiled.
    ///   (for scripts, this is the context that is stored in the executable for running it later and might maintain some state
    ///   between invocations via registered lookups - on the other hand, functions create their own private execution context
    ///   and don't refer to the compiler context)
    /// @return an executable object or error (syntax, other fatal problems)
    ScriptObjPtr compile(SourceContainerPtr aSource, EvaluationFlags aParsingMode, ScriptMainContextPtr aMainContext);

  protected:

    // TODO: CompiledCodePtr compilePart(const SourceRef& aCursor);

  };


  class CodeRunner : public SourceProcessor
  {
    typedef SourceProcessor inherited;
  public:

  };


  // MARK: - ScriptCodeThread

  #define DEFAULT_EXEC_TIME_LIMIT (Infinite)
  #define DEFAULT_MAX_BLOCK_TIME (50*MilliSecond)

  /// represents a code execution "thread" and its "stack"
  /// Note that the scope such a "thread" is only within one context.
  /// The "stack" is NOT a function calling stack, but only the stack
  /// needed to walk the nested code/expression structure with
  /// a state machine.
  class ScriptCodeThread : public P44Obj
  {
    ScriptCodeContextPtr owner; ///< the execution context which owns (has started) this thread
    EvaluationCB terminationCB; ///< to be called when the thread ends
    EvaluationFlags evaluationFlags; ///< the evaluation flags in use
    MLMicroSeconds maxBlockTime; ///< how long the thread is allowed to block in evaluate()
    MLMicroSeconds maxRunTime; ///< how long the thread is allowed to run overall

    MLMicroSeconds runningSince; ///< time the thread was started
    ExecutionContextPtr childContext; ///< set during calls to other contexts, e.g. to propagate abort()
    MLTicket autoResumeTicket; ///< auto-resume ticket

    SourceCursor pc; ///< the "program counter"
    ScriptObjPtr result; ///< current result
    bool aborted; ///< if set when entering continueThread, the thread will immediately end
    bool resuming; ///< detector for resume calling itself (synchronous execution)
    bool resumed; ///< detector for resume calling itself (synchronous execution)

  public:

    /// @param aOwner the context which owns this thread and will be notified when it ends
    /// @param aSourceRef the start point for the script
    ScriptCodeThread(ScriptCodeContextPtr aOwner, const SourceCursor aSourceRef);

    /// prepare for running
    /// @param aTerminationCB will be called to deliver when the thread ends
    /// @param aEvalFlags evaluation control flags
    ///   (not how long it takes until aEvaluationCB is called, which can be much later for async execution)
    /// @param aMaxBlockTime max time this call may continue evaluating before returning
    /// @param aMaxRunTime max time this evaluation might take, even when call does not block
    void prepareRun(
      EvaluationCB aTerminationCB,
      EvaluationFlags aEvalFlags,
      MLMicroSeconds aMaxBlockTime=DEFAULT_MAX_BLOCK_TIME,
      MLMicroSeconds aMaxRunTime=DEFAULT_EXEC_TIME_LIMIT
    );

    /// evaluate (= run the thread)
    virtual void run();

    /// abort the current thread, including child context
    /// @param aAbortResult if set, this is what abort will report back
    void abort(ScriptObjPtr aAbortResult = ScriptObjPtr());

  protected:

    /// end the thread, delivers current result via callback
    void endThread();

    /// resume running the script code
    /// @param aResult result, if any is expected at that stage
    /// @note : this is the main statemachine (re-)entry point.
    /// - child contexts evaluation will call back here
    /// - step() must always call back here
    /// - automatically takes care of winding back call chain if called recursively
    /// - automatically takes care of not running too long
    void resume(ScriptObjPtr aResult = ScriptObjPtr());
    static void selfKeepingResume(ScriptCodeThreadPtr aContext);

    /// run next statemachine step
    /// @note: MUST call resume() when done, synchronously or later!
    void step();

  };



  // MARK: - Built-in function support

  class BuiltInFunctionLookup;
  class BuiltinFunctionObj;
  typedef boost::intrusive_ptr<BuiltinFunctionObj> BuiltinFunctionObjPtr;
  class BuiltinFunctionContext;
  typedef boost::intrusive_ptr<BuiltinFunctionContext> BuiltinFunctionContextPtr;


  /// Signature for built-in function/method implementation
  /// @param aContext execution context, containing parameters and expecting result
  /// @note must call resume() or resumeWithResult() on the context when function has finished executing.
  typedef void (*BuiltinFunctionImplementation)(BuiltinFunctionContextPtr aContext);

  typedef struct {
    const char* name; ///< name of the function
    TypeInfo returnTypeInfo; ///< possible return types
    size_t numArgs; ///< number of arguemnts
    const ArgumentDescriptor* arguments; ///< arguments
    BuiltinFunctionImplementation implementation; ///< function pointer to implementation (as a plain function)
  } BuiltinFunctionDescriptor;


  /// member lookup for built-in functions, driven by static const struct table to describe functions and link implementations
  class BuiltInFunctionLookup : public ClassLevelLookup
  {
    typedef ClassLevelLookup inherited;
    typedef std::map<const string, const BuiltinFunctionDescriptor*, lessStrucmp> FunctionMap;
    FunctionMap functions;

  public:
    /// create a builtin function lookup from descriptor table
    /// @param aFunctionDescriptors pointer to an array of function descriptors, terminated with an entry with .name==NULL
    BuiltInFunctionLookup(const BuiltinFunctionDescriptor* aFunctionDescriptors);

    virtual TypeInfo containsTypes() const P44_OVERRIDE { return executable; } // executables only
    virtual ScriptObjPtr memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;

  };


  /// represents a built-in function
  class BuiltinFunctionObj : public ImplementationObj
  {
    typedef ImplementationObj inherited;
    friend class BuiltinFunctionContext;

    const BuiltinFunctionDescriptor *descriptor; ///< function signature, name and pointer to actual implementation function
    ScriptObjPtr thisObj; ///< the object this function is a method of (if it's not a plain function)

  public:
    BuiltinFunctionObj(const BuiltinFunctionDescriptor *aDescriptor, ScriptObjPtr aThisObj) : descriptor(aDescriptor), thisObj(aThisObj) {};

    /// Get description of arguments required to call this internal function
    virtual const ArgumentDescriptor* argumentInfo(size_t aIndex) const P44_OVERRIDE;

    /// get identifier (name) of this function object
    virtual string getIdentifier() const P44_OVERRIDE { return descriptor->name; };

    /// get context to call this object as a (sub)routine of a given context
    /// @param aMainContext the main context from where this function is called.
    /// @return a context for running built-in functions, with access to aMainContext's instance() object
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext) const P44_OVERRIDE;

  };


  class BuiltinFunctionContext : public ExecutionContext
  {
    typedef ExecutionContext inherited;
    friend class BuiltinFunctionObj;

    BuiltinFunctionObjPtr func; ///< the currently executing function
    EvaluationCB evaluationCB; ///< to be called when built-in function has finished
    SimpleCB abortCB; ///< called when aborting. async built-in might set this to cause external operations to stop at abort

  public:

    BuiltinFunctionContext(ScriptMainContextPtr aMainContext) : inherited(aMainContext) {};

    /// evaluate built-in function
    virtual void evaluate(ScriptObjPtr aToEvaluate, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB) P44_OVERRIDE;

    /// abort (async) built-in function
    /// @param aAbortFlags set stoprunning to abort currently running threads, queue to empty the queued threads
    /// @param aAbortResult if set, this is what abort will report back
    virtual void abort(EvaluationFlags aAbortFlags = stoprunning+queue, ScriptObjPtr aAbortResult = ScriptObjPtr()) P44_OVERRIDE;

    /// @name builtin function implementation interface
    /// @{

    /// @return convenience access to numIndexedMembers() in built-in function implementations
    inline size_t numArgs() const { return numIndexedMembers(); };

    /// convenience access to arguments for implementing built-in functions
    /// @param aArgIndex must be in the range of 0..numIndexedMembers()-1
    /// @note essentially is just a convenience wrapper for memberAtIndex()
    /// @note built-in functions should be called with a context that matches their signature
    ///   so implementation wants to just access the arguments it expects to be there by index w/o checking.
    ///   To avoid crashes in case a builtin function is evaluated w/o proper signature checking
    ScriptObjPtr arg(size_t aArgIndex);

    /// @return argument as reference for applying C++ operators to them (and not to the smart pointers)
    inline ScriptObj& argval(size_t aArgIndex) { return *(arg(aArgIndex)); }

    /// set abort callback
    /// @param aAbortCB will be called when context receives abort() before implementation call finish()
    /// @note async built-ins must set this callback implementing immediate termination of any ongoing action
    void setAbortCallback(SimpleCB aAbortCB);

    /// return result and execution thread back to script
    /// @param aResult the function result, if any.
    /// @note this must be called when a builtin function implementation completes
    void finish(const ScriptObjPtr aResult = ScriptObjPtr());

    /// @}
  };


  // MARK: - Standard scripting domain

  /// Standard scripting domain, with standard set of built-in functions
  class StandardScriptingDomain : public ScriptingDomain
  {
    typedef ScriptingDomain inherited;

  public:

    /// get shared global scripting domain with standard functions
    static ScriptingDomain& sharedDomain();

  };


}} // namespace p44::Script

#endif // ENABLE_SCRIPTING

#endif // defined(__p44utils__scripting__)
