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
  class CompiledCode;
  typedef boost::intrusive_ptr<CompiledCode> CompiledCodePtr;

  class ExecutionContext;
  typedef boost::intrusive_ptr<ExecutionContext> ExecutionContextPtr;
  class ScriptCodeContext;
  typedef boost::intrusive_ptr<ScriptCodeContext> ScriptCodeContextPtr;
  class ScriptMainContext;
  class ScriptingDomain;
  typedef boost::intrusive_ptr<ScriptingDomain> ScriptingDomainPtr;

  class ScriptCodeThread;
  typedef boost::intrusive_ptr<ScriptCodeThread> ScriptCodeThreadPtr;

  class CodeCursor;
  class SourceRef;
  class SourceContainer;
  typedef boost::intrusive_ptr<SourceContainer> SourceContainerPtr;
  class ScriptSource;

  class SourceProcessor;
  class Compiler;
  class Processor;

  class ClassMemberLookup;
  typedef boost::intrusive_ptr<ClassMemberLookup> ClassMemberLookupPtr;


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
      "Syntax",
      "DivisionByZero",
      "CyclicReference",
      "AsyncNotAllowed",
      "Invalid",
      "Internal",
      "Busy",
      "NotFound",
      "NotCreated",
      "Immutable",
      "Aborted",
      "Timeout",
      "User",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  // MARK: - Script namespace types

  /// Evaluation flags
  enum {
    modeMask = 0x00FF,
    unspecific = 0, ///< no specific mode
    initial = 0x0001, ///< initial trigger expression run (no external event, implicit event is startup or code change)
    externaltrigger = 0x0002, ///< externally triggered evaluation
    timed = 0x0003, ///< timed evaluation by timed retrigger
    script = 0x0004, ///< evaluate as script code
    // modifiers
    modifierMask = 0xFF00,
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
    attrMask = 0xFF00,
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
  };
  typedef uint16_t TypeInfo;

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

    /// get annotation text
    virtual string getAnnotation() const { return "ScriptObj"; };

    /// check type compatibility
    bool hasType(TypeInfo aTypeInfo) const { return (getTypeInfo() & aTypeInfo)!=0; }

    /// check for null/undefined
    bool undefined() const { return (getTypeInfo() & null)!=0; }

    /// check for null/undefined
    bool defined() const { return !undefined(); }

    /// logging context to use
    virtual P44LoggingObj* loggingContext() const { return NULL; };

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
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) const { return ScriptObjPtr(); };

    /// number of members accessible by index (e.g. positional parameters or array elements)
    /// @return number of members
    /// @note may or may not overlap with named members of the same object
    virtual size_t numIndexedMembers() const { return 0; }

    /// get object subfield/member by index, for example positional arguments in a ExecutionContext
    /// @param aIndex index of the member to find
    /// @param aTypeRequirements what type and type attributes the returned member must have, defaults to no restriction
    /// @return ScriptObj representing the member with index
    /// @note only possibly returns something in objects with type attribute "array"
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) const { return ScriptObjPtr(); };

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

    /// get new subroutine context to call this object as a subroutine/function call from a given context
    /// @param aCallerContext the context from where to call from (evaluate in) this implementation
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ExecutionContextPtr aCallerContext) const { return ExecutionContextPtr(); }

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
    ErrorPtr err;
  public:
    ErrorValue(ErrorPtr aError) : err(aError) {};
    ErrorValue(ScriptError::ErrorCodes aErrCode, const char *aFmt, ...);
    ErrorValue(ScriptError::ErrorCodes aErrCode, const SourceRef &aSrcRef, const char *aFmt, ...);
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
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
    // TODO: also implement setting members later
  };
  #endif // SCRIPTING_JSON_SUPPORT


  // MARK: - Extendable class member lookup

  /// implements a lookup step that can be used
  class ClassMemberLookup : public P44Obj
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

    typedef std::vector<ScriptObjPtr> IndexedVarVector;
    IndexedVarVector indexedVars;
    ScriptingDomainPtr domain; ///< the scripting domain
    ScriptObjPtr thisObject; ///< the object being executed in this context

  public:

    ExecutionContext(ScriptObjPtr aExecObj, ScriptingDomainPtr aDomain) : thisObject(aExecObj) {};

    /// clear local variables (indexed arguments)
    virtual void clearVars();

    // access to function arguments (positional) by index plus optionally a name
    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

    /// @return the object _instance_ being executed
    /// @note can be null if this function is a plain global function
    ScriptObjPtr thisObj() const { return thisObject; }

    /// "Compile" (for now, just scan for function definitions and syntax errors)
    /// @param aSource the script source to compile
    /// @return an ImplementationObj when successful, a ErrorValue otherwise
    ScriptObjPtr compile(SourceContainerPtr aSource);

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

    /// @return return the domain, which is the context for resolving global variables
    virtual ScriptingDomainPtr globals() const { return domain; }

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

    typedef std::map<string, ScriptObjPtr, lessStrucmp> NamedVarMap;
    NamedVarMap namedVars; ///< the named local variables/objects of this context

    typedef std::list<ScriptCodeThreadPtr> ThreadList;
    ThreadList threads; ///< the running "threads" in this context. First is the main thread of the evaluation.
    ThreadList queuedThreads; ///< the queued threads in this context

    ExecutionContextPtr mainContext; ///< the main context

  public:

    ScriptCodeContext(ScriptObjPtr aExecObj, ExecutionContextPtr aMainContext, ScriptingDomainPtr aDomain) :
      inherited(aExecObj, aDomain), mainContext(aMainContext) {};
    virtual void releaseObjsFromSource(SourceContainerPtr aSource) P44_OVERRIDE;

    /// clear local variables (named members)
    virtual void clearVars() P44_OVERRIDE;

    // access to local variables by name
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
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

    /// @return return main's context
    ExecutionContextPtr main() const { return mainContext; }

  private:

    /// called by threads ending
    void threadTerminated(ScriptCodeThreadPtr aThread);

  };


  /// Context for a script's main body, which can bring objects and functions into scope
  /// from the script's environment in the overall application structure via member lookups
  class ScriptMainContext : public ScriptCodeContext
  {
    typedef ScriptCodeContext inherited;

    typedef std::list<ClassMemberLookupPtr> LookupList;
    LookupList lookups;

  public:
    ScriptMainContext(ScriptObjPtr aExecObj, ScriptingDomainPtr aDomain);

    // access to objects in the context hierarchy of a local execution
    // (local objects, parent context objects, global objects)
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember, TypeInfo aStorageAttributes = none) P44_OVERRIDE;

    /// register an additional lookup
    /// @param aMemberLookup a lookup object
    void registerMemberLookup(ClassMemberLookupPtr aMemberLookup);

  };


  /// Scripting domain, usually singleton, containing global variables and event handlers
  /// No code directly runs in this context
  class ScriptingDomain : public ScriptMainContext
  {
    typedef ScriptMainContext inherited;

    GeoLocation *geoLocationP;

    // TODO: global script defined function storage -> no, are just vars of the domains
    // TODO: global event handler storage

  public:

    ScriptingDomain() : inherited(ScriptObjPtr(), ScriptingDomainPtr()), geoLocationP(NULL) {};

    /// set geolocation to use for functions that refer to location
    void setGeoLocation(GeoLocation* aGeoLocationP) { geoLocationP = aGeoLocationP; };

    // environment
    virtual GeoLocation* geoLocation() P44_OVERRIDE { return geoLocationP; };

  };


  class ImplementationObj : public ScriptObj
  {
    typedef ScriptObj inherited;
  public:
    virtual TypeInfo getTypeInfo() const { return executable; };
  };


  // MARK: - Script code scanning / processing

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

  /// code scanning and basic parsing
  class CodeCursor
  {
    const char* ptr; ///< pointer to current position in the source text
    const char* bol; ///< pointer to beginning of current line
    const char* eot; ///< pointer to where the text ends (0 char or not)
    size_t line; ///< line number
  public:
    CodeCursor(const char* aText, size_t aLen);
    CodeCursor(const string &aText);
    CodeCursor(const CodeCursor &aCursor);
    // info
    size_t lineno() const; ///< 0-based line counter
    size_t charpos() const; ///< 0-based character offset
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
    ScriptOperator parseOperator(); ///< @return operator or op_none, advances cursor
    ErrorPtr parseNumericLiteral(double &aNumber); ///< @return ok or error, @aNumber gets result
    ErrorPtr parseStringLiteral(string &aString); ///< @return ok or error, @aString gets result
    #if SCRIPTING_JSON_SUPPORT
    ErrorPtr parseJSONLiteral(JsonObjectPtr &aJsonObject); ///< @return ok or error, @aJsonObject gets result
    #endif
  };


  class SourceRef
  {
  public:
    SourceContainerPtr source; ///< the source containing the string we're pointing to
    CodeCursor pos; ///< the position within the source
    bool refersTo(SourceContainerPtr aSource) const { return source==aSource; }
  };

  /// extended error class to carry source reference
  class SourceRefError : public ScriptError
  {
    SourceRef srcRef;
  public:
    virtual const char *getErrorDomain() const P44_OVERRIDE { return ScriptError::domain(); };
    SourceRefError(const SourceRef &aSourceRef, ErrorCodes aError) :
      ScriptError(aError), srcRef(aSourceRef) {};
    const SourceRef& sourceRef() const { return srcRef; };
  };





  /// the actual script source text, shared among ScriptSource and possibly multiple SourceRefs
  class SourceContainer : public P44Obj
  {
    friend class SourceRef;
    friend class ScriptSource;
    friend class CompiledCode;
    friend class ExecutionContext;

    const char *originLabel; ///< a label used for logging and error reporting
    P44LoggingObj* loggingContextP; ///< the logging context
    string source; ///< the source code as written by the script author

    SourceContainer(const char *aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource) : originLabel(aOriginLabel), source(aSource) {};
  };


  /// class representing a script source in its entiety
  class ScriptSource
  {
    ExecutionContextPtr compilerContext; ///< the compile context
    ScriptObjPtr cachedExecutable; ///< the compiled executable for the script's body.
    const char *originLabel; ///< a label used for logging and error reporting
    P44LoggingObj* loggingContextP; ///< the logging context
    SourceContainerPtr source; ///< the container of the source

  public:
    ScriptSource(const char* aOriginLabel = NULL, P44LoggingObj* aLoggingContextP = NULL);
    /// all-in-one adhoc script source constructor
    ScriptSource(const char* aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource, ExecutionContextPtr aCompilerContext = ExecutionContextPtr());
    ~ScriptSource();

    /// set compiler context
    /// @param aCompilerContext the compiler context. Defaults to StandardScriptingDomain::sharedDomain()
    void setCompilerContext(ExecutionContextPtr aCompilerContext);

    /// set source code
    void setSource(const string aSource);

    /// get executable ((re-)compile if needed)
    /// @return executable from this source
    ScriptObjPtr getExecutable();

    /// convenience quick runner
    void run(EvaluationCB aEvaluationCB);

  };


  class CompiledCode : public ImplementationObj
  {
    typedef ImplementationObj inherited;
    friend class ScriptCodeContext;

    SourceRef srcRef; ///< reference to the source part from which this object originates from
  public:
    virtual bool originatesFrom(SourceContainerPtr aSource) const P44_OVERRIDE { return srcRef.refersTo(aSource); };
    virtual P44LoggingObj* loggingContext() const P44_OVERRIDE { return srcRef.source ? srcRef.source->loggingContextP : NULL; };
  };


  /// Base class for parsing or executing script code
  /// This contains the state machine and strictly delegates any actual
  /// interfacing with the environment to subclasses
  class SourceProcessor
  {
  public:

    typedef enum {
      // Completion states
      s_unwound, ///< stack unwound, can't continue, check for trailing garbage
      s_complete, ///< completing evaluation
      s_abort, ///< aborting evaluation
      s_finalize, ///< ending, will pop last stack frame
      // Script States
      s_statement_states, ///< marker
      // - basic statements
      s_body = s_statement_states, ///< at the body level (end of expression ends body)
      s_block, ///< within a block, exists when '}' is encountered, but skips ';'
      s_oneStatement, ///< a single statement, exits when ';' is encountered
      s_noStatement, ///< pop back one level
      s_returnValue, ///< "return" statement value calculation
      // - if/then/else
      s_ifCondition, ///< executing the condition of an if
      s_ifTrueStatement, ///< executing the if statement
      s_elseStatement, ///< executing the else statement
      // - while
      s_whileCondition, ///< executing the condition of a while
      s_whileStatement, ///< executing the while statement
      // - try/catch
      s_tryStatement, ///< executing the statement to try
      s_catchStatement, ///< executing the handling of a thrown error
      // - assignment to variables
      s_assignToVar, ///< assign result of an expression to a variable
      // Special result passing state
      s_result, ///< result of an expression or term
      // Expression States
      s_expression_states, ///< marker
      // - expression
      s_newExpression = s_expression_states, ///< at the beginning of an expression which only ends syntactically (end of code, delimiter, etc.) -> resets precedence
      s_expression, ///< handle (sub)expression start, precedence inherited or set by caller
      s_groupedExpression, ///< handling a paranthesized subexpression result
      s_exprFirstTerm, ///< first term of an expression
      s_exprLeftSide, ///< left side of an ongoing expression
      s_exprRightSide, ///< further terms of an expression
      // - simple terms
      s_simpleTerm, ///< at the beginning of a term
      s_funcArg, ///< handling a function argument
      s_funcExec, ///< handling function execution
      s_subscriptArg, ///< handling a subscript parameter
      s_subscriptExec, ///< handling access based on subscript args
      numEvalStates
    } ScanState; ///< the state of the scanner

  protected:

    /// @name  Scanning state
    /// @{

    SourceRef pos; ///< the scanning position within code

    /// Scanner Stack frame
    class StackFrame {
    public:
      StackFrame(ScanState aState, bool aSkip, int aPrecedence) :
        state(aState), skipping(aSkip), flowDecision(false), precedence(aPrecedence), op(op_none)
      {}
      ScanState state; ///< current state
      int precedence; ///< encountering a binary operator with smaller precedence will end the expression
      ScriptOperator op; ///< operator
      size_t pos; ///< relevant position in the code, e.g. start of expression for s_expression, start of condition for s_whilecondition
      string identifier; ///< identifier (e.g. variable name, function name etc.)
      bool skipping; ///< if set, we are just skipping code, not really executing
      bool flowDecision; ///< flow control decision
//      FunctionArguments args; ///< arguments
//      ExpressionValue val; ///< private value for operations
//      ExpressionValue res; ///< result value passed down at popAndPassResult()
    };
    typedef std::list<StackFrame> StackList;
    StackList stack; ///< the stack

    StackFrame &sp() { return stack.back(); } ///< current stackpointer

    /// switch state in the current stack frame
    /// @param aNewState the new state to switch the current stack frame to
    /// @return true for convenience to be used in non-yieled returns
    bool newstate(ScanState aNewState);

    void extracted();

    /// push new stack frame
    /// @param aNewState the new stack frame's state
    /// @param aStartSkipping if set, new stack frame will have skipping set, otherwise it will inherit previous frame's skipping value
    /// @return true for convenience to be used in non-yieled returns
    bool push(ScanState aNewState, bool aStartSkipping = false);

    /// pop current stack frame
    /// @return true for convenience to be used in non-yieled returns
    bool pop();

    /// pop stack frames down to the last frame in aPreviousState
    /// @param aPreviousState the state we are looking for
    /// @return true when frame was found, false if not (which means stack remained untouched)
    bool popToLast(ScanState aPreviousState);

    /// dump the stack (when eval logging is at debug level)
    void logStackDump();

    /// @}




  };


  class ScriptCompiler : public SourceProcessor
  {
    typedef SourceProcessor inherited;

  public:

    /// Scan code, extract function definitions, global vars, event handlers into scripting domain, return actual code
    CompiledCodePtr compile(ScriptingDomain& aDomain, SourceContainerPtr aSource);

  protected:

    CompiledCodePtr compilePart(CodeCursor& aCursor);

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

    SourceRef pc; ///< the "program counter"
    ScriptObjPtr result; ///< current result
    bool aborted; ///< if set when entering continueThread, the thread will immediately end
    bool resuming; ///< detector for resume calling itself (synchronous execution)
    bool resumed; ///< detector for resume calling itself (synchronous execution)

  public:

    /// @param aOwner the context which owns this thread and will be notified when it ends
    /// @param aSourceRef the start point for the script
    ScriptCodeThread(ScriptCodeContextPtr aOwner, const SourceRef aSourceRef);

    /// prepare for running
    /// @param aTerminationCB will be called to deliver when the thread ends
    /// @param aEvalFlags evaluation control flags
    ///   (not how long it takes until aEvaluationCB is called, which can be much later for async execution)
    /// @param aMaxBlockTime max time this call may continue evaluating before returning
    /// @param aMaxRunTime max time this evaluation might take, even when call does not block
    void prepare(
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
    /// - step() will call back here
    /// - automatically takes care of winding back call chain if called recursively
    /// - automatically takes care of not running too long
    void resume(ScriptObjPtr aResult = ScriptObjPtr());
    static void selfKeepingResume(ScriptCodeThreadPtr aContext);

    /// run next statemachine step
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
  class BuiltInFunctionLookup : public ClassMemberLookup
  {
    typedef ClassMemberLookup inherited;
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

    /// @return a context for running built-in functions (only needs the arguments)
    virtual ExecutionContextPtr contextForCallingFrom(ExecutionContextPtr aCallerContext) const P44_OVERRIDE;

  };


  class BuiltinFunctionContext : public ExecutionContext
  {
    typedef ExecutionContext inherited;
    friend class BuiltinFunctionObj;

    BuiltinFunctionObjPtr func; ///< the currently executing function
    EvaluationCB evaluationCB; ///< to be called when built-in function has finished
    SimpleCB abortCB; ///< called when aborting. async built-in might set this to cause external operations to stop at abort

  public:

    BuiltinFunctionContext(ScriptObjPtr aThisObj, ScriptingDomainPtr aDomain) : inherited(aThisObj, aDomain) {};

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
