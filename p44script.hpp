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

#ifndef __p44utils__p44script__
#define __p44utils__p44script__

#include "p44utils_common.hpp"

#if ENABLE_P44SCRIPT

#include "timeutils.hpp"
#include <string>
#include <set>

#ifndef P44SCRIPT_FULL_SUPPORT
  #define P44SCRIPT_FULL_SUPPORT 1 // on by default, can be switched off for small targets only needing expressions // TODO: actually implements sizing down!
#endif
#ifndef SCRIPTING_JSON_SUPPORT
  #define SCRIPTING_JSON_SUPPORT 1 // on by default
#endif

#if SCRIPTING_JSON_SUPPORT
  #include "jsonobject.hpp"
#endif

#ifndef P44SCRIPT_DATA_SUBDIR
  #define P44SCRIPT_DATA_SUBDIR "p44script"
#endif


using namespace std;

namespace p44 { namespace P44Script {

  // MARK: - class and smart pointer forward definitions

  class ScriptObj;
  typedef boost::intrusive_ptr<ScriptObj> ScriptObjPtr;
  class ExpressionValue;
  typedef boost::intrusive_ptr<ExpressionValue> ExpressionValuePtr;
  class ErrorValue;
  typedef boost::intrusive_ptr<ErrorValue> ErrorValuePtr;
  class NumericValue;
  class StringValue;
  class StructuredObject;
  class StructuredLookupObject;

  class ImplementationObj;
  class CompiledCode;
  typedef boost::intrusive_ptr<CompiledCode> CompiledCodePtr;
  class CompiledScript;
  typedef boost::intrusive_ptr<CompiledScript> CompiledScriptPtr;
  class CompiledTrigger;
  typedef boost::intrusive_ptr<CompiledTrigger> CompiledTriggerPtr;

  #if P44SCRIPT_FULL_SUPPORT
  class CompiledHandler;
  typedef boost::intrusive_ptr<CompiledHandler> CompiledHandlerPtr;
  #endif // P44SCRIPT_FULL_SUPPORT

  class ExecutionContext;
  typedef boost::intrusive_ptr<ExecutionContext> ExecutionContextPtr;
  class ScriptCodeContext;
  typedef boost::intrusive_ptr<ScriptCodeContext> ScriptCodeContextPtr;
  class ScriptMainContext;
  typedef boost::intrusive_ptr<ScriptMainContext> ScriptMainContextPtr;
  class ScriptingDomain;
  typedef boost::intrusive_ptr<ScriptingDomain> ScriptingDomainPtr;

  class SourcePos;
  class SourceCursor;
  class SourceContainer;
  typedef boost::intrusive_ptr<SourceContainer> SourceContainerPtr;
  class ScriptSource;

  class SourceProcessor;
  typedef boost::intrusive_ptr<SourceProcessor> SourceProcessorPtr;
  class Compiler;
  class ScriptCodeThread;
  typedef boost::intrusive_ptr<ScriptCodeThread> ScriptCodeThreadPtr;

  class MemberLookup;
  typedef boost::intrusive_ptr<MemberLookup> MemberLookupPtr;

  class EventSource;
  class EventSink;


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
      NotCallable, ///< object cannot be called as a function
      NotLvalue, ///< object is not an LValue and can't be assigned to
      NoPrivilege, ///< no privilege for requested action
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
      "NotCallable",
      "NotLvalue",
      "NoPrivilege",
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
    inherit = 0, ///< for ScriptSource::run() only: no flags, inherit from compile flags
    // run mode
    runModeMask = 0x00FF,
    regular = 0x01, ///< regular script or expression code
    initial = 0x02, ///< initial trigger expression run (no external event, implicit event is startup or code change)
    triggered = 0x04, ///< externally triggered (re-)evaluation
    timed = 0x08, ///< timed evaluation by timed retrigger
    scanning = 0x40, ///< scanning only (compiling)
    checking = 0x80, ///< scanning everything (for finding syntax errors early)
    // scope modifiers
    scopeMask = 0xF00,
    expression = 0x100, ///< evaluate as an expression (no flow control, variable assignments, blocks etc.)
    #if P44SCRIPT_FULL_SUPPORT
    scriptbody = 0x200, ///< evaluate as script body (no function or handler definitions)
    sourcecode = 0x400, ///< evaluate as script (include parsing functions and handlers)
    block = 0x800, ///< evaluate as a block (complete when reaching end of block)
    #endif // P44SCRIPT_FULL_SUPPORT
    // execution modifiers
    execModifierMask = 0xFF000,
    synchronously = 0x1000, ///< evaluate synchronously, error out on async code
    stoprunning = 0x2000, ///< abort running execution in the same context before starting a new one
    queue = 0x4000, ///< queue for execution (with concurrently also set, thread will start when all previously queued threads are done, but possibly concurrently with other threads)
    stopall = stoprunning+queue, ///< stop everything
    concurrently = 0x10000, ///< run concurrently with already executing code (when whith queued, thread is started when all previously queued threads are done)
    keepvars = 0x20000, ///< keep the local variables already set in the context
    mainthread = 0x40000, ///< if a thread with this flag set terminates, it also terminates all of its siblings
    // compilation modifiers
    floatingGlobs = 0x100000, ///< global function+handler definitions are kept floating (not deleted when originating source code is changed/deleted)
    anonymousfunction = 0x200000, ///< compile and run as anonymous function body
  };
  typedef uint32_t EvaluationFlags;

  /// Type info
  enum {
    // content type flags, usually one per object, but object/array can be combined with regular type
    typeMask = 0x0FFF,
    none = 0x000, ///< no type specification
    null = 0x001, ///< NULL/undefined
    error = 0x002, ///< Error
    numeric = 0x010, ///< numeric value
    text = 0x020, ///< text/string value
    json = 0x040, ///< JSON value
    executable = 0x080, ///< executable code
    threadref = 0x100, ///< represents a running thread
    object = 0x400, ///< is a object with named members
    array = 0x800, ///< is an array with indexed elements
    // type classes
    any = typeMask-null-error, ///< any type except null and error
    scalar = numeric+text+json, ///< scalar types (json can also be structured)
    structured = object+array, ///< structured types
    value = scalar+structured, ///< all value types (excludes executables)
    // attributes
    attrMask = 0xFFFFF000,
    // - for argument checking
    optionalarg = null, ///< if set, the argument is optional (means: is is allowed to be null even when null is not explicitly allowed)
    multiple = 0x01000, ///< this argument type can occur mutiple times (... signature)
    exacttype = 0x02000, ///< if set, type of argument must match, no autoconversion
    undefres = 0x04000, ///< if set, and an argument does not match type, the function result is automatically made null/undefined without executing the implementation
    async = 0x08000, ///< if set, the object cannot evaluate synchronously
    // - storage attributes and directives for named members
    lvalue = 0x10000, ///< is a left hand value (lvalue), possibly assignable, probably needs makeValid() to get real value
    create = 0x20000, ///< set to create member if not yet existing (special use also for explicitly created errors)
    onlycreate = 0x40000, ///< set to only create if new, but not overwrite
    nooverride = 0x80000, ///< do not override existing globals by creating a local var
    unset = 0x100000, ///< set to unset/delete member
    global = 0x200000, ///< set to store in global context
    threadlocal = 0x400000, ///< set to store as thread local
    constant = 0x800000, ///< set to select only constant  (in the sense of: not settable by scripts) members
    objscope = 0x1000000, ///< set to select only object scope members
    classscope = 0x2000000, ///< set to select only class scope members
    allscopes = classscope+objscope+global+threadlocal,
    builtinmember = 0x4000000, ///< special flag for use in built-in member descriptions to differentiate members from functions
    keeporiginal = 0x8000000, ///< special flag for values that should NOT be replaced by their actualValue()
    oneshot = 0x10000000, ///< special flag for values that occur only once, such as event messages. Relevant for triggers, which will auto-reset when oneshot values are involved
  };
  typedef uint32_t TypeInfo;


  /// trigger modes (note: enum values exposed in some API properties, do not change!)
  typedef enum {
    inactive = 0, ///< trigger is inactive
    onGettingTrue = 1, ///< trigger is fired when evaluation result is getting true
    onChangingBool = 2, ///< trigger is fired when evaluation result changes boolean value, including getting invalid
    onChange = 3, ///< trigger is fired when evaluation result changes (operator== with last result does not return true)
    onEvaluation = 4 ///< trigger is fired whenever it gets evaluated
  } TriggerMode;


  /// Argument descriptor
  typedef struct {
    TypeInfo typeInfo; ///< info about allowed types, checking, open argument lists, etc.
    string name; ///< the name of the argument
  } ArgumentDescriptor;

  // MARK: - ScriptObj base class

  /// evaluation callback
  /// @param aEvaluationResult the result of an evaluation
  typedef boost::function<void (ScriptObjPtr aEvaluationResult)> EvaluationCB;

  /// Event Sink
  class EventSink
  {
    friend class EventSource;
    typedef std::set<EventSource *> EventSourceSet;
    EventSourceSet eventSources;
  public:
    virtual ~EventSink();

    /// is called from sources to deliver an event
    /// @param aEvent the event object, can be NULL for unspecific events
    /// @param aSource the source sending the event
    virtual void processEvent(ScriptObjPtr aEvent, EventSource &aSource) { /* NOP in base class */ };

    /// clear all event sources (unregister from all)
    void clearSources();

    /// @return number of event sources (senders) this sink currently has
    size_t numSources() { return eventSources.size(); }

    /// @return true if sink has any sources
    bool hasSources() { return !eventSources.empty(); }

  };

  /// event handling callback
  typedef boost::function<void (ScriptObjPtr aEvent, EventSource &aSource)> EventHandlingCB;

  /// standalone event handler, delivering events via callback
  class EventHandler : public EventSink
  {
    typedef EventSink inherited;

    EventHandlingCB mEventHandlingCB;

  public:

    /// set handler to call when event arrives
    /// @param aEventHandlingCB will be called when handler receives a event from a EventSource
    void setHandler(EventHandlingCB aEventHandlingCB);

    /// is called from sources to deliver an event
    /// @param aEvent the event object, can be NULL for unspecific events
    /// @param aSource the source sending the event
    virtual void processEvent(ScriptObjPtr aEvent, EventSource &aSource) P44_OVERRIDE;

  };


  /// Event Source
  class EventSource
  {
    friend class EventSink;
    typedef std::set<EventSink *> EventSinkSet;
    EventSinkSet eventSinks;
    bool sinksModified;
  public:
    virtual ~EventSource();

    /// send event to all registered event sinks
    /// @param aEvent event object, can also be NULL pointer
    void sendEvent(ScriptObjPtr aEvent);

    /// register an event sink to get events from this source
    /// @param aEventSink the event sink (receiver) to register for events (NULL allowed -> NOP)
    /// @note registering the same event sink multiple times is allowed, but will not duplicate events sent
    void registerForEvents(EventSink* aEventSink);
    void registerForEvents(EventSink& aEventSink);

    /// release an event sink from getting events from this source
    /// @param aEventSink the event sink (receiver) to unregister from receiving events (NULL allowed -> NOP)
    /// @note tring to unregister a event sink that is not registered is allowed -> NOP
    void unregisterFromEvents(EventSink* aEventSink);
    void unregisterFromEvents(EventSink& aEventSink);

    /// @return number of event sinks (reveivers) this source currently has
    size_t numSinks() { return eventSinks.size(); }

    /// @return true if source has any sinks
    bool hasSinks() { return !eventSinks.empty(); }

    /// copy all sinks from another source
    /// @param aOtherSource the source to copy sinks from (can be NULL -> NOP)
    /// @note other source keeps the sinks registered
    void copySinksFrom(EventSource* aOtherSource);

  };



  /// Base Object in scripting
  class ScriptObj : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;
  public:

    /// @name information
    /// @{

    /// get type of this value
    /// @return get type info
    virtual TypeInfo getTypeInfo() const { return null; }; // base object is a null/undefined

    /// @return a type description for logs and error messages
    static string typeDescription(TypeInfo aInfo);

    /// @return text description for the passed aObj, NULL allowed
    static string describe(ScriptObjPtr aObj);

    /// get name
    virtual string getIdentifier() const { return ""; };

    /// get annotation text - defaults to type description
    virtual string getAnnotation() const { return typeDescription(getTypeInfo()); };

    /// check type compatibility
    /// @param aTypeInfo what type(s) we are looking for
    /// @return true if this object has any of the types specified in aTypeInfo
    bool hasType(TypeInfo aTypeInfo) const { return (getTypeInfo() & aTypeInfo)!=0; }

    /// check type compatibility
    /// @param aRequirements what type flags MUST be set
    /// @param aMask what type flags are checked (defaults to typeMask)
    /// @return true if this object has all of the type flags requested within the mask
    bool meetsRequirement(TypeInfo aRequirements, TypeInfo aMask = typeMask) const
      { return typeRequirementMet(getTypeInfo(), aRequirements, aMask); }

    /// check type compatibility
    /// @param aRequirements what type flags MUST be set
    /// @param aMask what type flags are checked (defaults to typeMask)
    /// @return true if this object has all of the type flags requested within the mask
    static bool typeRequirementMet(TypeInfo aInfo, TypeInfo aRequirements, TypeInfo aMask = typeMask)
      { return (aInfo&aRequirements&aMask)==(aRequirements&aMask); }


    /// check for null/undefined
    bool undefined() const { return (getTypeInfo() & null)!=0; }

    /// check for defined value (but not error)
    bool defined() const { return !undefined() & !isErr(); }

    /// check for error
    bool isErr() const { return (getTypeInfo() & error)!=0; }

    /// logging context to use
    virtual P44LoggingObj* loggingContext() const { return NULL; };

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() P44_OVERRIDE;

    /// @return the per-instance log level offset
    /// @note is virtual because some objects might want to use the log level offset of another object
    virtual int getLogLevelOffset() P44_OVERRIDE;

    /// @return associated position, can be NULL
    virtual SourceCursor* cursor() { return NULL; }

    /// @}

    /// @name lazy value loading / proxy objects / lvalue assignment
    /// @{

    /// @return a pointer to the object's actual value when it is available.
    /// Might be false when this object is an lvalue or another type of proxy
    /// @note call makeValid() to get a valid version from this object
    virtual ScriptObjPtr actualValue() { return ScriptObjPtr(this); } // simple value objects including this base class have an immediate value

    /// Get the actual value of an object (which might be a lvalue or other type of proxy)
    /// @param aEvaluationCB will be called with a valid version of the object.
    /// @note if called on an already valid object, it returns itself in the callback, so
    ///   makeValid() can always be called. But for performance reasons, checking valid() before is recommended
    virtual void makeValid(EvaluationCB aEvaluationCB);

    /// Assign a new value to a lvalue
    /// @param aNewValue the value to assign, NULL to remove the lvalue from its container
    /// @param aEvaluationCB will be called with a valid version of the object or an error or NULL in case the lvalue was deleted
    virtual void assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue);

    /// @}


    /// @name value getters
    /// @{

    /// @return the value that should be used to assign to a variable.
    ///   The purpose of this can be to detach the the assigned value from the original value (e.g. JSON which needs to copy
    ///   subfields when assiging). Simple values are immutable and can be shared between variables,
    ///   so this base implementation returns itself.
    virtual ScriptObjPtr assignmentValue() { return ScriptObjPtr(this); }

    /// @return a value to be used in calculations. This should return a basic type whenever possible
    ///    This is relevant for values like JSON, where e.g. a string field is not of type "text", but "json" (with a suitable stringValue())
    virtual ScriptObjPtr calculationValue() { return ScriptObjPtr(this); }

    virtual double doubleValue() const { return 0; }; ///< @return a conversion to numeric (using literal syntax), if value is string
    virtual bool boolValue() const { return doubleValue()!=0; }; ///< @return a conversion to boolean (true = not numerically 0, not JSON-falsish, not empty string)
    virtual string stringValue() const { return getAnnotation(); }; ///< @return a conversion to string of the value
    virtual ErrorPtr errorValue() const { return Error::ok(); } ///< @return error value (always an object, OK if not in error)
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const { return JsonObject::newNull(); } ///< @return a JSON value
    #endif
    // generic converters
    int intValue() const { return (int)doubleValue(); } ///< @return numeric value as int
    int64_t int64Value() const { return (int64_t)doubleValue(); } ///< @return numeric value as int64

    /// @}

    /// @name member access
    /// @{

    /// get object subfield/member by name
    /// @param aName name of the member to find
    /// @param aMemberAccessFlags what type and type attributes the returned member must have, defaults to no restriction.
    ///   If lvalue is set and the member can be created and/or assigned to, an ScriptLvalue might be returned
    /// @return ScriptObj representing the member, or NULL if none
    /// @note only possibly returns something for container objects marked with "object" type
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags) { return ScriptObjPtr(); };

    /// number of members accessible by index (e.g. positional parameters or array elements)
    /// @return number of members
    /// @note may or may not overlap with named members of the same object
    virtual size_t numIndexedMembers() const { return 0; }

    /// get object subfield/member by index, for example positional arguments in a ExecutionContext
    /// @param aIndex index of the member to find
    /// @param aMemberAccessFlags what type and type attributes the returned member must have, defaults to no restriction.
    ///   If lvalue is set and the member can be created and/or assigned to, an ScriptLvalue might be returned
    /// @return ScriptObj representing the member with index
    /// @note only possibly returns something in objects with type attribute "array"
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags) { return ScriptObjPtr(); };

    /// set new object for named member in this container (and nowhere else!)
    /// @param aName name of the member to assign
    /// @param aMember the member to assign
    /// @return ok or Error describing reason for assignment failure
    /// @note this method is primarily an interface for ScriptLvalue and usually should NOT be used directly
    /// @note this method must NOT do any scope walks, but apply only to this container. It's the task of
    ///   memberByName() to find the correct scope and return an lvalue
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember);

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
    /// @param aArgDesc where to store the descriptor
    /// @return true if aArgDesc was set, false if no argument exists at this index
    /// @note functions might have an open argument list, so do not try to exhaust this
    virtual bool argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const { return false; };

    /// get context to call this object as a (sub)routine of a given context
    /// @param aMainContext the context from where to call from (evaluate in) this implementation
    ///   For executing script body code, aMainContext is always the domain (but can be passed NULL as script bodies
    ///   should know their domain already (if passed, it will be checked for consistency)
    /// @param aThread the thread this call will originate from, e.g. when requesting context for a function call.
    ///   NULL means that code is not started from a running thread, such as scripts, handlers, triggers.
    /// @note the context might be pre-existing (in case of scriptbody code which gets associated with the context it
    ///    is hosted in) or created new (in case of functions which run in a temporary private context)
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const { return ExecutionContextPtr(); }

    /// @return true if this object originates from the specified source
    /// @note this is needed to remove objects such as functions and handlers when their source changes or is deleted
    virtual bool originatesFrom(SourceContainerPtr aSource) const { return false; }

    /// make ready for deletion, break links that might be parts of retain loops
    /// @note this is used before freeing a object which originatesFrom() a given source
    virtual void deactivate() {};

    /// @return true if this object's definition/declaration is floating, i.e. when it carries its own source code
    ///    that is no longer connected with the originating source code (and thus can't get removed/replaced by
    ///    changing source code)
    virtual bool floating() const { return false; }

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
    // arithmetic, returning a ScriptObjPtr, type-specific
    virtual ScriptObjPtr operator+(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator-(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator*(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator/(const ScriptObj& aRightSide) const { return new ScriptObj(); };
    virtual ScriptObjPtr operator%(const ScriptObj& aRightSide) const { return new ScriptObj(); };

    /// @}


    /// @name triggering support
    /// @{

    /// @return a souce of events for this object, or NULL if none
    /// @note objects that represent a one-time event (such as a thread ending) must not return an
    ///    event source (that will never emit an event) after the singular event has already happened!
    virtual EventSource *eventSource() const { return NULL; /* none in base class */ }

    /// @}

  };


  // MARK: - lvalues


  /// Base class for a value reference that might be assigned to
  class ScriptLValue  : public ScriptObj
  {
    typedef ScriptObj inherited;
  protected:
    ScriptObjPtr mCurrentValue;
    ScriptLValue(ScriptObjPtr aCurrentValue) : mCurrentValue(aCurrentValue) {};
    virtual ~ScriptLValue() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

  public:
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return lvalue; };
    virtual string getAnnotation() const P44_OVERRIDE { return "lvalue"; };
    virtual void deactivate() P44_OVERRIDE { mCurrentValue.reset(); inherited::deactivate(); }

    /// @return true when the object's value is available. Might be false when this object is an lvalue or another type of proxy
    /// @note call makeValid() to get a valid version from this object
    virtual ScriptObjPtr actualValue() P44_OVERRIDE { return mCurrentValue; } // LValues are valid if they have a current value

    /// Get the actual value of an object (which might be a lvalue or other type of proxy)
    /// @param aEvaluationCB will be called with a valid version of the object.
    /// @note if called on an already valid object, it returns itself in the callback, so
    ///   makeValid() can always be called. But for performance reasons, checking valid() before is recommended
    virtual void makeValid(EvaluationCB aEvaluationCB) P44_OVERRIDE;

    /// Assign a new value to a lvalue
    /// @param aNewValue the value to assign, NULL to remove the lvalue from its container
    /// @param aEvaluationCB will be called with a valid version of the object or an error or NULL in case the lvalue was deleted
    virtual void assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue) P44_OVERRIDE = 0;

  };


  class StandardLValue : public ScriptLValue
  {
    typedef ScriptLValue inherited;

    string mMemberName;
    size_t mMemberIndex;
    ScriptObjPtr mContainer;

  public:
    /// create a lvalue for a container which has setMemberByName()
    /// @note this should be called by suitable container's memberByName() only
    /// @note subclasses might provide optimized/different mechanisms
    StandardLValue(ScriptObjPtr aContainer, const string aMemberName, ScriptObjPtr aCurrentValue);
    /// create a lvalue for a container which has setMemberAtIndex()
    StandardLValue(ScriptObjPtr aContainer, size_t aMemberIndex, ScriptObjPtr aCurrentValue);
    virtual ~StandardLValue() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

    virtual void deactivate() P44_OVERRIDE { mContainer.reset(); inherited::deactivate(); }

    /// Assign a new value to a lvalue
    /// @param aNewValue the value to assign, NULL to remove the lvalue from its container
    /// @param aEvaluationCB will be called with a valid version of the object or an error or NULL in case the lvalue was deleted
    virtual void assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue) P44_OVERRIDE;

    /// get identifier (name) of this lvalue object
    virtual string getIdentifier() const P44_OVERRIDE { return mMemberName; };

  };


  // MARK: - Special NULL values

  /// an explicitly annotated null value (in contrast to ScriptObj base class which is a non-annotated null)
  class AnnotatedNullValue : public ScriptObj
  {
    typedef ScriptObj inherited;
    string annotation;
  public:
    AnnotatedNullValue(string aAnnotation) : annotation(aAnnotation) {};
    virtual string getAnnotation() const P44_OVERRIDE { return annotation; }; // specific annotation...
    virtual string stringValue() const P44_OVERRIDE { return inherited::getAnnotation(); }; // ...but stringValue must return the default annotation!
  };


  /// a NULL value that event sinks can register to, so in case it is replaced (i.e. as a global variable) it can
  /// pass over the registered sinks to the replacing object
  class EventPlaceholderNullValue : public AnnotatedNullValue, public EventSource
  {
    typedef AnnotatedNullValue inherited;
  public:
    EventPlaceholderNullValue(string aAnnotation);
    EventSource* eventSource() const;
  };


  // MARK: - Error Values

  /// An error value
  class ErrorValue : public ScriptObj
  {
    typedef ScriptObj inherited;
  protected:
    ErrorPtr err;
    bool thrown;
  public:
    ErrorValue(ErrorPtr aError) : err(aError), thrown(false) {};
    ErrorValue(ScriptError::ErrorCodes aErrCode, const char *aFmt, ...);
    ErrorValue(ErrorValuePtr aErrVal) : err(aErrVal->err), thrown(aErrVal->thrown) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "error"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return error; };
    // value getters
    virtual double doubleValue() const P44_OVERRIDE { return err ? 0 : err->getErrorCode(); };
    virtual string stringValue() const P44_OVERRIDE { return Error::text(err); };
    virtual ErrorPtr errorValue() const P44_OVERRIDE { return err ? err : Error::ok(); };
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif
    bool wasThrown() { return thrown; }
    void setThrown(bool aThrown) { thrown = aThrown; }
    // operators
    virtual bool operator==(const ScriptObj& aRightSide) const P44_OVERRIDE;
  };


  // MARK: - ThreadValue

  class ThreadValue : public ScriptObj
  {
    typedef ScriptObj inherited;
    ScriptCodeThreadPtr mThread;
    ScriptObjPtr threadExitValue;
  public:
    ThreadValue(ScriptCodeThreadPtr aThread);
    virtual ~ThreadValue() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not
    virtual string getAnnotation() const P44_OVERRIDE { return "thread"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return threadref+keeporiginal; };
    virtual void deactivate() P44_OVERRIDE { threadExitValue.reset(); mThread.reset(); inherited::deactivate(); }
    virtual ScriptObjPtr calculationValue() P44_OVERRIDE; /// < ThreadValue calculates to NULL as long as running or to the thread's exit value
    virtual EventSource *eventSource() const P44_OVERRIDE; ///< ThreadValue is an event source, event is the exit value of a thread terminating
    ScriptCodeThreadPtr thread() { return mThread; }; ///< @return the thread
    bool running(); ///< @return true if still running
    void abort(ScriptObjPtr aAbortResult = ScriptObjPtr()); ///< abort the thread
  };



  // MARK: - Regular value classes

  class NumericValue : public ScriptObj
  {
    typedef ScriptObj inherited;
  protected:
    double num;
  public:
    NumericValue(double aNumber) : num(aNumber) {};
    NumericValue(bool aBool) : num(aBool ? 1 : 0) {};
    NumericValue(int aInt) : num(aInt) {};
    NumericValue(int64_t aInt) : num(aInt) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "numeric"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return numeric; };
    // value getters
    virtual double doubleValue() const P44_OVERRIDE { return num; }; // native
    virtual string stringValue() const P44_OVERRIDE { return string_format("%lg", doubleValue()); };
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE { return JsonObject::newDouble(doubleValue()); };
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


  class StringValue : public ScriptObj
  {
    typedef ScriptObj inherited;
    string str;
  public:
    StringValue(string aString) : str(aString) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "string"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return text; };
    // value getters
    virtual string stringValue() const P44_OVERRIDE { return str; }; // native
    virtual double doubleValue() const P44_OVERRIDE;
    virtual bool boolValue() const P44_OVERRIDE;
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif
    // operators
    virtual bool operator<(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual bool operator==(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator+(const ScriptObj& aRightSide) const P44_OVERRIDE;
  };


  #if SCRIPTING_JSON_SUPPORT
  class JsonRepresentedValue : public ScriptObj
  {
    typedef ScriptObj inherited;
  public:
    virtual ScriptObjPtr calculationValue() P44_OVERRIDE;
    JsonRepresentedValue() {};
    virtual string getAnnotation() const P44_OVERRIDE { return "jsonrepresented"; };
    // value getters
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE = 0; // JsonRepresentedValue MUST have a json representation
    virtual double doubleValue() const P44_OVERRIDE;
    virtual string stringValue() const P44_OVERRIDE;
    virtual bool boolValue() const P44_OVERRIDE;
    // member access
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags = none) P44_OVERRIDE;
    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags = none) P44_OVERRIDE;
    // operators
    virtual bool operator<(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual bool operator==(const ScriptObj& aRightSide) const P44_OVERRIDE;
    virtual ScriptObjPtr operator+(const ScriptObj& aRightSide) const P44_OVERRIDE;
  };


  class JsonValue : public JsonRepresentedValue
  {
    typedef JsonRepresentedValue inherited;
    JsonObjectPtr jsonval;
  public:
    virtual ScriptObjPtr assignmentValue() P44_OVERRIDE;
    JsonValue(JsonObjectPtr aJson) : jsonval(aJson) {};
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE;
    virtual string getAnnotation() const P44_OVERRIDE { return "json"; };
    // value getters
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE { return jsonval; } // native
    // modifying
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember) P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;
  };

  #endif // SCRIPTING_JSON_SUPPORT


  // MARK: - Structured objects

  /// structured object base class
  class StructuredObject : public JsonRepresentedValue
  {
    typedef JsonRepresentedValue inherited;
  public:
    virtual string getAnnotation() const P44_OVERRIDE { return "object"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return object; } // only object, although JSON representation exists

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE = 0;
    #endif

  };


  /// simple variable container
  class SimpleVarContainer : public StructuredObject
  {
    typedef StructuredObject inherited;

    typedef std::map<string, ScriptObjPtr, lessStrucmp> NamedVarMap;
    NamedVarMap namedVars; ///< the named local variables/objects of this context

  public:

    /// clear local variables (named members)
    void clearVars();

    /// clear floating globals (only called as inherited from domain)
    void clearFloatingGlobs();

    /// release all objects stored in this container and other known containers which were defined by aSource
    void releaseObjsFromSource(SourceContainerPtr aSource);

    /// access to local variables by name
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags) P44_OVERRIDE;

    // internal for StandardLValue
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember) P44_OVERRIDE;

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif

  };


  // MARK: - Extendable class member lookup

  class BuiltInMemberLookup;

  /// structured object with the ability to register member lookups
  class StructuredLookupObject : public StructuredObject
  {
    typedef StructuredObject inherited;
    typedef std::list<MemberLookupPtr> LookupList;
    LookupList lookups;
    MemberLookupPtr mSingleMembers;
  public:

    // access to (sub)objects in the installed lookups
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements) P44_OVERRIDE;
    virtual ~StructuredLookupObject() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

    virtual void deactivate() P44_OVERRIDE { mSingleMembers.reset(); lookups.clear(); inherited::deactivate(); }

    /// register an additional lookup
    /// @param aMemberLookup a lookup object.
    /// @note if same lookup object is registered more than once, only the first registration counts, others are ignored
    void registerMemberLookup(MemberLookupPtr aMemberLookup);

    /// register a single member
    /// @param aName the name for the member
    /// @param aMember the object corresponding to aName
    void registerMember(const string aName, ScriptObjPtr aMember);

    /// register a shared lookup (singleton) for built-in members
    /// @param aSingletonLookupP pointer to the (usually static global) variable holding the shared lookup
    /// @param aMemberDescriptors pointer to the builtin member description table to use for constructing the lookup if not yet existing
    void registerSharedLookup(BuiltInMemberLookup*& aSingletonLookupP, const struct BuiltinMemberDescriptor* aMemberDescriptors);

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif

  };


  /// implements a lookup step that can be shared between multiple execution contexts. It does NOT
  /// hold any _instance_ state by itself, but can still serve to lookup instances by using the _aThisObj_ provided
  /// by callers (which can be ignored when providing _class_ members).
  class MemberLookup : public P44Obj
  {
  public:

    /// return mask of all types that may be (but not necessarily are) in this lookup
    /// @note this is for optimizing lookups for certain types. Base class potentially has all kind of objects
    virtual TypeInfo containsTypes() const { return any+null+constant+allscopes; }

    /// get object subfield/member by name
    /// @param aThisObj the object _instance_ of which we want to access a member (can be NULL in case of singletons)
    /// @param aName name of the member to find
    /// @param aTypeRequirements what type and type attributes the returned member must have, defaults to no restriction
    /// @return ScriptObj representing the member, or NULL if none
    virtual ScriptObjPtr memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements) const = 0;

    /// register a single member
    /// @param aName the name for the member
    /// @param aMember the object corresponding to aName
    virtual void registerMember(const string aName, ScriptObjPtr aMember) { /* NOP in base class */ }

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual void addJsonValues(JsonObjectPtr &aObj) const { /* NOP here: some objects cannot represent their members */ };
    #endif

  };


  /// a simple lookup for predefined ScriptObj members
  class PredefinedMemberLookup : public MemberLookup
  {
    typedef MemberLookup inherited;

    typedef std::map<string, ScriptObjPtr, lessStrucmp> NamedVarMap;
    NamedVarMap members; ///< predefined scriptobjs (immutable)

    virtual ScriptObjPtr memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements) const P44_OVERRIDE;
    virtual void registerMember(const string aName, ScriptObjPtr aMember) P44_OVERRIDE;

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual void addJsonValues(JsonObjectPtr &aObj) const P44_OVERRIDE;
    #endif

  };



  // MARK: - Execution contexts

  #define DEFAULT_SYNC_MAX_RUN_TIME (10*Second)
  #define DEFAULT_MAX_BLOCK_TIME (50*MilliSecond)

  /// Abstract base class for executables.
  /// Can hold indexed (positional) arguments as these are needed for all types
  /// of implementations (e.g. built-ins as well as script defined functions)
  class ExecutionContext : public StructuredLookupObject
  {
    typedef StructuredLookupObject inherited;
    friend class ScriptCodeContext;
    friend class BuiltinFunctionContext;

    typedef std::vector<ScriptObjPtr> IndexedVarVector;
    IndexedVarVector indexedVars;
    ScriptMainContextPtr mainContext; ///< the main context

    ExecutionContext(ScriptMainContextPtr aMainContext);

  protected:

    bool undefinedResult; ///< special shortcut to make a execution return a "undefined" result w/o actually executing

  public:

    virtual ~ExecutionContext() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not
    virtual void deactivate() P44_OVERRIDE { clearVars(); mainContext.reset(); inherited::deactivate(); }

    /// clear local variables (indexed arguments)
    virtual void clearVars();

    // access to function arguments (positional) by index plus optionally a name
    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aMemberAccessFlags = none) P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

    /// release all objects stored in this container and other known containers which were defined by aSource
    virtual void releaseObjsFromSource(SourceContainerPtr aSource); // no source-derived permanent objects here

    /// Execute a object
    /// @param aToExecute the object to be executed in this context
    /// @param aEvalFlags evaluation control flags
    /// @param aEvaluationCB will be called to deliver the result of the execution
    /// @param aChainOriginThread the origin of the sequential "thread" chain (as user defined functions always start a "thread")
    /// @param aThreadLocals optionally, the (structured) object that provides thread local members
    /// @param aMaxRunTime optionally, maximum time the thread may run before it is aborted by timeout
    virtual void execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainOriginThread, ScriptObjPtr aThreadLocals = ScriptObjPtr(), MLMicroSeconds aMaxRunTime = Infinite) = 0;

    /// abort evaluation (of all threads if context has more than one)
    /// @param aAbortFlags set stoprunning to abort currently running threads, queue to empty the queued threads
    /// @param aAbortResult if set, this is what abort will report back
    /// @return true if any thread was aborted
    virtual bool abort(EvaluationFlags aAbortFlags = stoprunning+queue, ScriptObjPtr aAbortResult = ScriptObjPtr(), ScriptCodeThreadPtr aExceptThread = ScriptCodeThreadPtr()) = 0;

    /// synchronously evaluate the object, abort if async executables are encountered
    ScriptObjPtr executeSynchronously(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, ScriptObjPtr aThreadLocals = ScriptObjPtr(), MLMicroSeconds aMaxRunTime = Infinite);

    /// check argument against signature and add to context if ok
    /// @param aArgument the object to be passed as argument. Pass NULL to check if aCallee has more non-optional arguments
    /// @param aIndex argument index
    /// @param aCallee the object to be called with this argument (provides the signature)
    /// @return NULL if argument is ok and can be passed, otherwise result to get checked for throwing
    ScriptObjPtr checkAndSetArgument(ScriptObjPtr aArgument, size_t aIndex, ScriptObjPtr aCallee);

    /// @name execution environment info
    /// @{

    /// @return the main context from which this context was called (as a subroutine)
    virtual ScriptMainContextPtr scriptmain() const { return mainContext; }

    /// @return the object _instance_ that is the implicit "this" for the context
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
    friend class CompiledCode;
    friend class CompiledScript;

    SimpleVarContainer localVars;

    typedef std::list<ScriptCodeThreadPtr> ThreadList;
    ThreadList threads; ///< the running "threads" in this context. First is the main thread of the evaluation.
    ThreadList queuedThreads; ///< the queued threads in this context

    ScriptCodeContext(ScriptMainContextPtr aMainContext);

  protected:
    /// clear floating globals (only called as inherited from domain)
    void clearFloatingGlobs();

  public:

    virtual ~ScriptCodeContext() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

    virtual void releaseObjsFromSource(SourceContainerPtr aSource) P44_OVERRIDE;

    virtual void deactivate() P44_OVERRIDE { abort(); inherited::deactivate(); }

    /// clear local variables (named members)
    virtual void clearVars() P44_OVERRIDE;

    /// access to local variables by name
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags) P44_OVERRIDE;

    // internal for StandardLValue
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember) P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

    /// Execute a executable object in this context
    /// @param aToExecute the object to be evaluated
    /// @param aEvalFlags evaluation mode/flags. Script thread can evaluate...
    /// @param aEvaluationCB will be called to deliver the result of the evaluation
    /// @param aChainOriginThread the origin of the sequential "thread" chain (as user defined functions always start a "thread")
    /// @param aThreadLocals optionally, the (structured) object that provides thread local members
    /// @param aMaxRunTime optionally, maximum time the thread may run before it is aborted by timeout
    virtual void execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainOriginThread, ScriptObjPtr aThreadLocals = ScriptObjPtr(), MLMicroSeconds aMaxRunTime = Infinite) P44_OVERRIDE;

    /// Start a new thread (usually, a block, concurrently) from a given cursor
    /// @param aCodeObj the code object this thread runs (maybe only a part of)
    /// @param aFromCursor where to start executing
    /// @param aEvalFlags how to initiate the thread and what syntax level to evaluate
    /// @param aEvaluationCB callback when thread has evaluated (ends)
    /// @param aChainOriginThread the origin of the sequential "thread" chain (as user defined functions always start a "thread")
    /// @param aThreadLocals optionally, the (structured) object that provides thread local members
    /// @param aMaxRunTime optionally, maximum time the thread may run before it is aborted by timeout
    ScriptCodeThreadPtr newThreadFrom(CompiledCodePtr aCodeObj, SourceCursor &aFromCursor, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainOriginThread, ScriptObjPtr aThreadLocals = ScriptObjPtr(), MLMicroSeconds aMaxRunTime = Infinite);

    /// abort evaluation of all threads
    /// @param aAbortFlags set stoprunning to abort currently running threads, queue to empty the queued threads
    /// @param aAbortResult if set, this is what abort will report back
    /// @param aExceptThread if set, this thread is not aborted
    /// @return true if any thread was aborted
    virtual bool abort(EvaluationFlags aAbortFlags = stopall, ScriptObjPtr aAbortResult = ScriptObjPtr(), ScriptCodeThreadPtr aExceptThread = ScriptCodeThreadPtr()) P44_OVERRIDE;

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif

  private:

    /// called by threads ending
    void threadTerminated(ScriptCodeThreadPtr aThread, EvaluationFlags aThreadEvalFlags);

  };


  /// Context for a script's main body, which can bring objects and functions into scope
  /// from the script's environment in the overall application structure via member lookups
  class ScriptMainContext : public ScriptCodeContext
  {
    typedef ScriptCodeContext inherited;
    friend class ScriptingDomain;

    ScriptingDomainPtr domainObj; ///< the scripting domain (unless it's myself to avoid locking)
    ScriptObjPtr thisObj; ///< the object _instance_ scope of this execution context (if any)

    #if P44SCRIPT_FULL_SUPPORT
    typedef std::list<CompiledHandlerPtr> HandlerList;
    HandlerList handlers;
    #endif // P44SCRIPT_FULL_SUPPORT

    /// private constructor, only ScriptingDomain should use it
    /// @param aDomain owning link to domain - as long as context exists, domain may not get deleted.
    /// @param aThis can be NULL if there's no object instance scope for this script. This object is
    ///    passed to all registered member lookups
    ScriptMainContext(ScriptingDomainPtr aDomain, ScriptObjPtr aThis);

  public:

    virtual ~ScriptMainContext() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

    virtual void deactivate() P44_OVERRIDE { handlers.clear(); domainObj.reset(); thisObj.reset(); inherited::deactivate(); }

    /// @return info about handlers
    JsonObjectPtr handlersInfo();

    /// clear context-local variables and handlers (those that were created run-time, not declared)
    virtual void clearVars() P44_OVERRIDE;

    // access to objects in the context hierarchy of a local execution
    // (local objects, parent context objects, global objects)
    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags) P44_OVERRIDE;

    // direct access to this and domain (not via mainContext, as we can't set maincontext w/o self-locking)
    virtual ScriptObjPtr instance() const P44_OVERRIDE { return thisObj; }
    virtual ScriptingDomainPtr domain() const P44_OVERRIDE { return domainObj; }
    virtual ScriptMainContextPtr scriptmain() const P44_OVERRIDE { return ScriptMainContextPtr(const_cast<ScriptMainContext*>(this)); }

    #if P44SCRIPT_FULL_SUPPORT

    virtual void releaseObjsFromSource(SourceContainerPtr aSource) P44_OVERRIDE;

    /// register a handler in this main context
    /// @param aHandler the handler to register
    /// @return Ok or error
    ScriptObjPtr registerHandler(ScriptObjPtr aHandler);

    #endif // P44SCRIPT_FULL_SUPPORT

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
  // SCRIPT_OPERATOR_MODE_FLEXIBLE:
  //   := is unambiguous assignment
  //   == is unambiguous comparison
  //   = works as assignment when used after a variable specification in scripts, and as comparison in expressions
  // SCRIPT_OPERATOR_MODE_C:
  //   = and := are assignment
  //   == is comparison
  // SCRIPT_OPERATOR_MODE_PASCAL:
  //   := is assignment
  //   = and == is comparison
  // Note: the unabiguous "==", "<>" and "!=" are both supported in all modes
  #ifndef SCRIPT_OPERATOR_MODE
    #define SCRIPT_OPERATOR_MODE SCRIPT_OPERATOR_MODE_FLEXIBLE
  #endif

  // operators with precedence
  typedef enum {
    op_none       = (0 << 3) + 7,
    op_not        = (1 << 3) + 7,
    op_multiply   = (2 << 3) + 6,
    op_divide     = (3 << 3) + 6,
    op_modulo     = (4 << 3) + 6,
    op_add        = (5 << 3) + 5,
    op_subtract   = (6 << 3) + 5,
    op_equal      = (7 << 3) + 4,
    op_assignOrEq = (8 << 3) + 4,
    op_notequal   = (9 << 3) + 4,
    op_less       = (10 << 3) + 4,
    op_greater    = (11 << 3) + 4,
    op_leq        = (12 << 3) + 4,
    op_geq        = (13 << 3) + 4,
    op_and        = (14 << 3) + 3,
    op_or         = (15 << 3) + 2,
    // precedence 1 is reserved to mark non-assignable lvalue
    op_assign     = (16 << 3) + 0,
    op_delete     = (17 << 3) + 0, // "unset" prefix "operator"
    opmask_precedence = 0x07
  } ScriptOperator;


  /// opaque position object within a source text contained elsewhere
  /// only SourceRef may access it
  class SourcePos
  {
    friend class SourceCursor;
    friend class SourceContainer;

    const char* bot; ///< beginning of text (beginning of first line)
    const char* ptr; ///< pointer to current position in the source text
    const char* bol; ///< pointer to beginning of current line
    const char* eot; ///< pointer to where the text ends (0 char or not)
    size_t line; ///< line number
  public:
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
    typedef const char* UniquePos;
    SourceCursor() {};
    SourceCursor(SourceContainerPtr aContainer);
    SourceCursor(SourceContainerPtr aContainer, SourcePos aStart, SourcePos aEnd);
    SourceCursor(string aString, const char *aLabel = NULL); ///< create cursor on the passed string

    SourceContainerPtr source; ///< the source containing the string we're pointing to
    SourcePos pos; ///< the position within the source

    bool refersTo(SourceContainerPtr aSource) const { return source==aSource; } ///< check if this sourceref refers to a particular source

    // info
    size_t lineno() const; ///< 0-based line counter
    size_t charpos() const; ///< 0-based character offset
    size_t textpos() const; ///< offset of current text from beginning of text
    UniquePos posId() const { return pos.ptr; } ///< unique position within a source, only for comparison (call site for frozen arguments...)

    /// @name source text access and parsing utilities
    /// @{

    // access
    bool valid() const; ///< @return true when the cursor points to something
    char c(size_t aOffset=0) const; ///< @return character at offset from current position, 0 if none
    size_t charsleft() const; ///< @return number of chars to end of code
    const char* text() const { return nonNullCStr(pos.bot); } ///< @return c string starting at current pos
    const char* linetext() const { return nonNullCStr(pos.bol); } ///< @return c string starting at beginning of current line
    const char *postext() const { return nonNullCStr(pos.ptr); } ///< @return c string starting at current pos
    bool EOT() const; ///< true if we are at end of text
    bool next(); ///< advance to next char, @return false if not possible to advance
    bool advance(size_t aNumChars); ///< advance by specified number of chars, includes counting lines
    bool nextIf(char aChar); ///< @return true and advance cursor if @param aChar matches current char, false otherwise
    void skipWhiteSpace(); ///< skip whitespace (but NOT comments)
    void skipNonCode(); ///< skip non-code, i.e. whitespace and comments
    string displaycode(size_t aMaxLen); ///< @return code on single line for displaying from current position, @param aMaxLen how much to show max before abbreviating with "..."
    const char *originLabel() const; ///< @return the origin label of the source container

    // parsing utilities
    bool parseIdentifier(string& aIdentifier, size_t* aIdentifierLenP = NULL); ///< @return true if identifier found, stored in aIndentifier and cursor advanced
    bool checkForIdentifier(const char *aIdentifier); ///< return true if specified identifier found at this position
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
    SourceCursor sourceCursor;
  public:
    ErrorPosValue(const SourceCursor &aCursor, ErrorPtr aError) : inherited(aError), sourceCursor(aCursor) {};
    ErrorPosValue(const SourceCursor &aCursor, ScriptError::ErrorCodes aErrCode, const char *aFmt, ...);
    ErrorPosValue(const SourceCursor &aCursor, ErrorValuePtr aErrValue) : inherited(aErrValue), sourceCursor(aCursor) {};
    void setErrorCursor(const SourceCursor &aCursor) { sourceCursor = aCursor; };
    virtual SourceCursor* cursor() P44_OVERRIDE { return &sourceCursor; } // has a position
    virtual string stringValue() const P44_OVERRIDE;
  };


  /// the actual script source text, shared among ScriptSource and possibly multiple SourceRefs
  class SourceContainer : public P44Obj
  {
    friend class SourceCursor;
    friend class ScriptSource;
    friend class CompiledScript;
    friend class CompiledCode;
    friend class ExecutionContext;

    const char *originLabel; ///< a label used for logging and error reporting
    P44LoggingObj* loggingContextP; ///< the logging context
    string source; ///< the source code as written by the script author
    bool mFloating; ///< if set, the source is not linked but is a private copy
  public:
    /// create source container
    SourceContainer(const char *aOriginLabel, P44LoggingObj* aLoggingContextP, const string aSource);

    /// create source container copying a source part from another container
    SourceContainer(const SourceCursor &aCodeFrom, const SourcePos &aStartPos, const SourcePos &aEndPos);

    /// @return a cursor for this source code, starting at the beginning
    SourceCursor getCursor();

    /// @return true if this source is floating, i.e. not part of a still existing script
    bool floating() { return mFloating; }

    /// return a logging context
    P44LoggingObj *loggingContext() { return loggingContextP; };

  };


  /// class representing a script source in its entiety including all context needed to run it
  class ScriptSource
  {
  protected:
    ScriptingDomainPtr scriptingDomain; ///< the scripting domain
    ScriptMainContextPtr sharedMainContext; ///< a shared context to always run this source in. If not set, each script gets a new main context
    ScriptObjPtr cachedExecutable; ///< the compiled executable for the script's body.
    EvaluationFlags defaultFlags; ///< default flags for how to compile (as expression, scriptbody, source), also used as default run flags
    const char *originLabel; ///< a label used for logging and error reporting
    P44LoggingObj* loggingContextP; ///< the logging context
    SourceContainerPtr sourceContainer; ///< the container of the source

  public:
    /// create empty script source
    ScriptSource(EvaluationFlags aDefaultFlags, const char* aOriginLabel = NULL, P44LoggingObj* aLoggingContextP = NULL);

    ~ScriptSource();

    /// set domain (where global objects from compilation will be stored)
    /// @param aDomain the domain. Defaults to StandardScriptingDomain::sharedDomain() if not explicitly set
    void setDomain(ScriptingDomainPtr aDomain);

    /// get the domain assiciated with this source.
    /// If none was set specifically, the StandardScriptingDomain is returned.
    /// @return scripting domain
    ScriptingDomainPtr domain();

    /// set pre-existing execution context to use, possibly shared with other script sources
    /// @param aSharedMainContext a context previously obtained from the domain with newContext()
    void setSharedMainContext(ScriptMainContextPtr aSharedMainContext);

    /// set source code and compile mode
    /// @param aSource the source code
    /// @param aEvaluationFlags if set (not ==0==inherit), these flags control compilation and are default
    ///   flags for running the script via run().
    /// @return true if source or compile flags have actually changed. Otherwise, nothing happens and false is returned
    bool setSource(const string aSource, EvaluationFlags aEvaluationFlags = inherit);

    /// get the source code
    /// @return the source code as set by setSource()
    string getSource() const;

    /// @return the origin label string
    const char *getOriginLabel() { return nonNullCStr(originLabel); }

    /// check if a cursor refers to this source
    /// @param aCursor the cursor to check
    /// @return true if the cursor is in this source
    bool refersTo(const SourceCursor& aCursor);

    /// get executable ((re-)compile if needed)
    /// @return executable from this source
    ScriptObjPtr getExecutable();

    /// convenience quick syntax checker
    /// @return error in case of syntax errors or other fatal conditions
    ScriptObjPtr syntaxcheck();

    /// @return true if empty
    bool empty() const;

    /// convenience quick runner
    /// @param aRunFlags additional run flags.
    ///   Notes: - if synchronously is set here, the result will be delivered directly (AND with the callback if one is set)
    ///          - by default, flags are inherited from those set at setSource().
    ///          - if a scope flag is set, all scope flags are used from aRunFlags
    ///          - if a run mode flag is set, all run mode flags are used from aRunFlags
    ///          - execution modfier flags from aRunFlags are ADDED to those already set with setSource()
    /// @param aEvaluationCB will be called with the result
    /// @param aThreadLocals optionally, the (structured) object that provides thread local members
    /// @param aMaxRunTime optionally, maximum time the thread may run before it is aborted by timeout
    ScriptObjPtr run(EvaluationFlags aRunFlags, EvaluationCB aEvaluationCB = NULL, ScriptObjPtr aThreadLocals = ScriptObjPtr(), MLMicroSeconds aMaxRunTime = Infinite);

    /// for single-line tests
    ScriptObjPtr test(EvaluationFlags aEvalFlags, const string aSource)
      { setSource(aSource, aEvalFlags); return run(aEvalFlags|regular|synchronously, NULL, ScriptObjPtr(), Infinite); }

  };


  /// convenience class for standalone triggers
  class TriggerSource : public ScriptSource
  {
    typedef ScriptSource inherited;

    EvaluationCB mTriggerCB;
    TriggerMode mTriggerMode;
    MLMicroSeconds mHoldOffTime;
  public:
    TriggerSource(const char* aOriginLabel, P44LoggingObj* aLoggingContextP, EvaluationCB aTriggerCB, TriggerMode aTriggerMode, MLMicroSeconds aHoldOffTime, EvaluationFlags aEvalFlags) :
      inherited(aEvalFlags|triggered, aOriginLabel, aLoggingContextP), // make sure one of the trigger flags is set for the compile to produce a CompiledTrigger
      mTriggerCB(aTriggerCB),
      mTriggerMode(aTriggerMode),
      mHoldOffTime(aHoldOffTime)
    {
    }

    /// set new trigger source with the callback/mode/evalFlags as set with the constructor
    /// @param aSource the trigger source code to set
    /// @param aAutoInit if set, and source code has actually changed, compileAndInit() will be called
    /// @return true if changed.
    bool setTriggerSource(const string aSource, bool aAutoInit = false);

    /// set new trigger mode
    /// @param aHoldOffTime the new holdoff time
    /// @param aAutoInit if set, and holdoff time has actually changed, compileAndInit() will be called
    /// @return true if changed.
    bool setTriggerHoldoff(MLMicroSeconds aHoldOffTime, bool aAutoInit = false);

    /// @return current holdoff time
    MLMicroSeconds getTriggerHoldoff() { return mHoldOffTime; }

    /// set new trigger holdoff time
    /// @param aTriggerMode the new trigger mode
    /// @param aAutoInit if set, and mode has actually changed, compileAndInit() will be called
    /// @return true if changed.
    bool setTriggerMode(TriggerMode aTriggerMode, bool aAutoInit = false);

    /// @return current trigger mode
    TriggerMode getTriggerMode() { return mTriggerMode; }

    /// re-initialize the trigger
    /// @return the result of the initialisation run
    ScriptObjPtr compileAndInit();

    /// (re-)evaluate the trigger outside of the evaluations caused by timing and event sources
    /// @param aRunMode runmode flags (combined with evaluation flags set at initialisation), non-runmode bits here are ignored
    /// @return false if trigger evaluation could not be started
    /// @note automatically runs compileAndInit() if the trigger is not yet active
    /// @note will execute the callback when done
    bool evaluate(EvaluationFlags aRunMode = triggered);


    /// schedule a (re-)evaluation at the specified time latest
    /// @param aLatestEval new time when evaluation must happen latest
    /// @note this makes sure there is an evaluation NOT LATER than the given time, but does not guarantee a
    ///   evaluation actually does happen AT that time. So the trigger callback might want to re-schedule when the
    ///   next evaluation happens too early.
    void nextEvaluationNotLaterThan(MLMicroSeconds aLatestEval);

  };




  /// Scripting domain, usually singleton, containing global variables and event handlers
  /// No code runs directly in this context
  class ScriptingDomain : public ScriptMainContext
  {
    typedef ScriptMainContext inherited;

    GeoLocation *geoLocationP;
    MLMicroSeconds maxBlockTime;

  public:

    ScriptingDomain() : inherited(ScriptingDomainPtr(), ScriptObjPtr()), geoLocationP(NULL), maxBlockTime(DEFAULT_MAX_BLOCK_TIME) {};

    /// clear global functions and handlers that have embedded source (i.e. not linked to a still accessible source)
    void clearFloatingGlobs();

    /// set geolocation to use for functions that refer to location
    void setGeoLocation(GeoLocation* aGeoLocationP) { geoLocationP = aGeoLocationP; };

    /// set max block time (how long async scripts run in sync mode maximally until
    /// releasing execution and schedule continuation later)
    /// @param aMaxBlockTime max block time - if reached, execution will pause for 2 * aMaxBlockTime
    void setMaxBlockTime(MLMicroSeconds aMaxBlockTime) { maxBlockTime = aMaxBlockTime; };


    // environment
    virtual GeoLocation* geoLocation() P44_OVERRIDE { return geoLocationP; };
    MLMicroSeconds getMaxBlockTime() { return maxBlockTime; };

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
    friend class ScriptCodeThread;

    static int cThreadIdGen;
    int mThreadId;

  public:

    SourceProcessor();

    /// set the source to process
    /// @param aCursor the source (part) to process
    void setCursor(const SourceCursor& aCursor);

    /// set the source to process
    /// @param aCompletedCB will be called when process synchronously or asynchronously ends
    void setCompletedCB(EvaluationCB aCompletedCB);

    /// prepare processing
    /// @param aEvalFlags determine at what level to start processing (source, scriptbody, expression)
    void initProcessing(EvaluationFlags aEvalFlags);

    /// start processing
    /// @note setCursor(), setCompletedCB() and initProcessing() must be called before!
    virtual void start();

    /// resume processing
    /// @param aNewResult if not NULL, this object will be stored to result as first step of the resume
    /// @note must be called for every step of the process that does not lead to completion
    void resume(ScriptObjPtr aNewResult = ScriptObjPtr());

    /// abort processing
    /// @param aAbortResult if set, this is what aborted process will report back
    virtual void abort(ScriptObjPtr aAbortResult = ScriptObjPtr());

    /// complete processing
    /// @param aFinalResult the result to deliver with the completion callback
    /// @note inherited method must be called from subclasses as this must make sure stepLoop() will end.
    virtual void complete(ScriptObjPtr aFinalResult);

    // This static method can be passed to timers and makes sure that "this" is kept alive by the callback
    // boost::bind object because it is a smart pointer argument.
    // Using this makes sure that no running thread can get destructed before being terminated properly
    static void selfKeepingResume(ScriptCodeThreadPtr aContext, ScriptObjPtr aAbortResult);

    /// @return a unique ID for this source processor (which is the basis of any thread)
    int threadId() { return mThreadId; }

  protected:

    // processing control vars
    bool aborted; ///< if set, stepLoop must immediately end
    bool resuming; ///< detector for resume calling itself (synchronous execution)
    bool resumed; ///< detector for resume calling itself (synchronous execution)
    EvaluationCB completedCB; ///< called when completed
    EvaluationFlags evaluationFlags; ///< the evaluation flags in use

    /// called by resume to perform next step(s).
    /// @note base class just steps synchronously as long as it can by calling step().
    ///    Detection of synchronous execution is done via the resuming/resumed flags.
    /// @note subclases might use different strategies for stepping, or override our step.
    ///   The only condition is that every step ends in a call to resume()
    virtual void stepLoop();

    /// internal statemachine step
    void step();


    /// @name execution hooks. These are dummies in the base class, but implemented
    ///   in actual code execution subclasses. These must call resume()
    /// @{

    /// must retrieve the member as specified with storageSpecifier from current result (or from the script scope if result==NULL)
    /// @param aMemberAccessFlags if this has lvalue set, caller would like to get an ScriptLValue which allows assigning a new value
    /// @param aNoNotFoundError if this is set, even finding nothing will not raise an error, but just return a NULL result
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError=false);

    /// must retrieve the indexed member from current result (or from the script scope if result==NULL)
    /// @param aMemberAccessFlags if this has lvalue set, caller would like to get an ScriptLValue which allows assigning a new value
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void memberByIndex(size_t aIndex, TypeInfo aMemberAccessFlags);

    /// execute the current result and replace it with the output from the evaluation (e.g. function call)
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void executeResult();

    /// check if member can issue event that should be connected to trigger
    virtual void memberEventCheck();

    #if P44SCRIPT_FULL_SUPPORT

    /// fork executing a block at the current position, if identifier is not empty, store a new ThreadValue.
    /// @note MUST NOT call resume() directly. This call will return when the new thread yields execution the first time.
    virtual void startBlockThreadAndStoreInIdentifier();

    /// must store result as a compiled function in the scripting domain
    /// @note must cause calling resume()
    virtual void storeFunction();

    /// must store result as a event handler (trigger+action script) in the scripting domain
    /// @note must cause calling resume()
    virtual void storeHandler();

    #endif // P44SCRIPT_FULL_SUPPORT

    /// must set a new funcCallContext suitable to execute result as a function
    /// @note must set result to an ErrorValue if no context can be created
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void newFunctionCallContext();

    /// apply the specified argument to the current function context
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void pushFunctionArgument(ScriptObjPtr aArgument);

    /// capture code between poppedPos and current position into specified object
    /// @note embeddedGlobs determines if code is embedded into the code container (and lives on with it) or
    ///   just references source code (so it will get deleted when source code goes away)
    ScriptObjPtr captureCode(ScriptObjPtr aCodeContainer);

    /// indicates start of script body (at current src.pos)
    /// @note must cause calling resume()
    virtual void startOfBodyCode();

    /// @return the main context for running triggers and handlers. This is used to associate scripts defined as part of a
    /// source (e.g. "on"-handlers) with a execution context to call them later
    virtual ScriptMainContextPtr getTriggerAndHandlerMainContext() { return ScriptMainContextPtr(); } // none in base class

    /// @}

    /// @name source processor internal state machine
    /// @{

    ///< methods of this objects which handle a state
    typedef void (SourceProcessor::*StateHandler)(void);

    // state that can be pushed
    SourceCursor src; ///< the scanning position within code
    SourcePos poppedPos; ///< the position popped from the stack (can be applied to jump back for loops)
    StateHandler currentState; ///< next state to call
    ScriptObjPtr result; ///< the current result object
    ScriptObjPtr olderResult; ///< an older result, e.g. the result popped from stack, or previous lookup in nested member lookups
    int precedence; ///< encountering a binary operator with smaller precedence will end the expression
    ScriptOperator pendingOperation; ///< operator
    ExecutionContextPtr funcCallContext; ///< the context of the currently preparing function call
    bool skipping; ///< skipping

    // other internal state, not pushed
    string identifier; ///< for processing identifiers

    /// Scanner Stack frame
    class StackFrame {
    public:
      StackFrame(
        SourcePos& aPos,
        bool aSkipping,
        StateHandler aReturnToState,
        ScriptObjPtr aResult,
        ExecutionContextPtr aFuncCallContext,
        int aPrecedence,
        ScriptOperator aPendingOperation
      ) :
        pos(aPos),
        skipping(aSkipping),
        returnToState(aReturnToState),
        result(aResult),
        funcCallContext(aFuncCallContext),
        precedence(aPrecedence),
        pendingOperation(aPendingOperation)
      {}
      SourcePos pos; ///< scanning position
      bool skipping; ///< set if only skipping code, not evaluating
      StateHandler returnToState; ///< next state to run after pop
      ScriptObjPtr result; ///< the current result object
      ExecutionContextPtr funcCallContext; ///< the context of the currently preparing function call
      int precedence; ///< encountering a binary operator with smaller precedence will end the expression
      ScriptOperator pendingOperation; ///< operator
    };

    typedef std::list<StackFrame> StackList;
    StackList stack; ///< the stack

    /// convenience end of step using current result and checking for errors
    /// @note includes calling resume()
    /// @note this is the place to implement different result checking strategies between compiler and runner
    virtual void checkAndResume();

    /// readability wrapper for setting the next state but NOT YET completing current state's processing
    inline void setState(StateHandler aNextState) { currentState = aNextState; }

    /// convenience functions for transition to a new state, i.e. setting the new state and checkAndResume() or resume() in one step
    /// @param aNextState set the next state
    inline void checkAndResumeAt(StateHandler aNextState) { currentState = aNextState; checkAndResume(); }
    inline void resumeAt(StateHandler aNextState) { currentState = aNextState; resume(); }

    /// push the current state
    /// @param aReturnToState the state to return to after pop().
    /// @param aPushPoppedPos poppedPos will be pushed instead of src.pos
    void push(StateHandler aReturnToState, bool aPushPoppedPos = false);

    /// return to the last pushed state
    void pop();

    /// validate the result if needed, check for errors and then pop the stack to continue
    /// @param aThrowErrors if set, result is checked for being an error and thrown if so
    void popWithResult(bool aThrowErrors = true);

    /// assuming result already validated, optionally check for errors and then pop the stack to continue
    /// @param aThrowErrors if set, result is checked for being an error and thrown if so
    void popWithValidResult(bool aThrowErrors = true);

    /// throw an error and resume()
    /// @param aErrValue the error to throw. If it's not an error, nothing will be thrown
    void throwError(ErrorValuePtr aErrValue);

    /// unwind stack until a entry with the specified state is found
    /// @param aPreviousState state to look for
    /// @return true if actually unwound (entry with specified state popped),
    ///   false if no such state found (and stack unchanged)
    bool unWindStackTo(StateHandler aPreviousState);

    /// look for the specified state on the stack, and if one is found, enter skipping mode
    /// and modify all stack entries up to and including the found entry to skipping=true
    /// causing the execution to run in skipping mode until the stack pops beyound the found entry.
    /// @param aPreviousState the state to search
    /// @param aThrowValue if set, the result in the found entry is replaced by this
    bool skipUntilReaching(StateHandler aPreviousState, ScriptObjPtr aThrowValue = ScriptObjPtr());

    /// throw the value to be caught by the next try/catch. If no try/catch, the current thread will end with aThrowValue
    /// @param aError the value to throw, must be an error
    void throwOrComplete(ErrorValuePtr aError);

    /// convenience shortcut for creating and throwing a syntax error at current position
    void exitWithSyntaxError(const char *aFmt, ...) __printflike(2,3);

    /// state handlers
    /// @note these all MUST eventually cause calling resume(). This can happen from
    ///   the implementation via checkAndResume(), doneAndGoto() or complete()
    ///   or via a callback which eventually calls resume().

    // Simple Term
    void s_simpleTerm(); ///< at the beginning of a term
    void s_member(); ///< immediately after accessing a member for reading
    void s_subscriptArg(); ///< immediately after subscript expression evaluation
    void s_nextSubscript(); ///< multi-dimensional subscripts, 2nd and further arguments
    void assignOrAccess(bool aAllowAssign); ///< access or assign identifier
    void s_funcArg(); ///< immediately after function argument evaluation
    void s_funcContext(); ///< after getting function calling context
    void s_funcExec(); ///< ready to execute the function

    // Expression
    void s_assignmentExpression(); ///< at the beginning of an expression which might also be an assignment
    void s_expression(); ///< at the beginning of an expression which only ends syntactically (end of code, delimiter, etc.) -> resets precedence
    void s_subExpression(); ///< handle (sub)expression start, precedence inherited or set by caller
    void processExpression(); ///< common processing for expression states
    void s_groupedExpression(); ///< handling a paranthesized subexpression result
    void s_exprFirstTerm(); ///< first term of an expression
    void s_exprLeftSide(); ///< left side of an ongoing expression
    void s_exprRightSide(); ///< further terms of an expression
    void s_assignExpression(); ///< evaluate expression and assign to (lvalue) result
    void s_assignOlder(); ///< assign older result to freshly obtained (lvalue) result, special language construct only!
    void s_checkAndAssignLvalue(); ///< check result for throwing error, then assign to lvalue
    void s_assignLvalue(); ///< assign to lvalue
    void s_unsetMember(); ///< unset the current result

    #if P44SCRIPT_FULL_SUPPORT
    // Script Body
    void s_block(); ///< within a block, exits when '}' is encountered, but skips ';'
    void s_noStatement(); ///< no more statements can follow, but an extra separator MAY follow
    void s_oneStatement(); ///< a single statement, exits when ';' is encountered
    void s_body(); ///< at the body level of a function or script (end of expression ends body)
    void processStatement(); ///< common processing for statement states
    void processVarDefs(TypeInfo aVarFlags, bool aAllowInitializer, bool aDeclaration = false); ///< common processing of variable declarations/assignments
    // - if/then/else
    void s_ifCondition(); ///< executing the condition of an if
    void s_ifTrueStatement(); ///< executing the if statement
    // - while
    void s_whileCondition(); ///< executing the condition of a while
    void s_whileStatement(); ///< executing the while statement
    // - try/catch
    void s_tryStatement(); ///< executing the statement to try

    // Declarations
    void s_declarations(); ///< declarations (functions and handlers)
    void s_defineFunction(); ///< store the defined function
    void processOnHandler(); ///< common processing for "on" handlers, which can be in declaration or code
    void s_defineTrigger(); ///< store the trigger expression of a on(...) {...} statement
    void s_defineHandler(); ///< store the handler script of a of a on(...) {...} statement
    #endif // P44SCRIPT_FULL_SUPPORT

    // Generic
    void s_result(); ///< result of an expression or term available as ScriptObj. May need makeValid() if not already valid() here.
    void s_nothrowResult(); ///< result of an expression, made valid if needed, but not throwing errors here
    void s_validResult(); ///< final result of an expression or term ready, check for error and pop the stack to see next state to run
    void s_validResultCheck(); ///< final result of an expression or term ready, pop the stack but do not check errors, but pass them on
    void s_uncheckedResult(); ///< final result of an expression or term ready, check for error and pop the stack to see next state to run
    void s_complete(); ///< nothing more to do, result represents result of entire scanning/evaluation process

    /// @}

  };


  // MARK: "compiled" code

  /// compiled code, by default as a subroutine/function called from a context
  class CompiledCode : public ImplementationObj
  {
    typedef ImplementationObj inherited;
    friend class ScriptCodeContext;
    friend class SourceProcessor;
    friend class ScriptMainContext;

    std::vector<ArgumentDescriptor> arguments;

  protected:
    string name;
    SourceCursor cursor; ///< reference to the source part from which this object originates from

    /// define argument
    void pushArgumentDefinition(TypeInfo aTypeInfo, const string aArgumentName);

  public:
    virtual string getAnnotation() const P44_OVERRIDE { return "function"; };

    CompiledCode(const string aName) : name(aName) {};
    CompiledCode(const string aName, const SourceCursor& aCursor) : name(aName), cursor(aCursor) {};
    virtual ~CompiledCode();
    void setCursor(const SourceCursor& aCursor);
    bool codeFromSameSourceAs(const CompiledCode &aCode) const; ///< return true if both compiled codes are from the same source position
    virtual bool originatesFrom(SourceContainerPtr aSource) const P44_OVERRIDE { return cursor.refersTo(aSource); };
    virtual bool floating() const P44_OVERRIDE { return cursor.source->floating(); }
    virtual P44LoggingObj* loggingContext() const P44_OVERRIDE { return cursor.source ? cursor.source->loggingContextP : NULL; };

    /// get subroutine context to call this object as a subroutine/function call from a given context
    /// @param aMainContext the context from where this function is now called (the same function can be called
    ///   from different contexts)
    /// @param aThread the thread this call will originate from, e.g. when requesting context for a function call.
    ///   NULL means that code is not started from a running thread, such as scripts, handlers, triggers.
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const P44_OVERRIDE;

    /// Get description of arguments required to call this internal function
    virtual bool argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const P44_OVERRIDE;

    /// get identifier (name) of this function object
    virtual string getIdentifier() const P44_OVERRIDE { return name; };


  };


  /// compiled main script
  class CompiledScript : public CompiledCode
  {
    typedef CompiledCode inherited;
    friend class ScriptCompiler;

  protected:
    ScriptMainContextPtr mainContext; ///< the main context this script should execute in

  public:
    CompiledScript(const string aName, ScriptMainContextPtr aMainContext) : inherited(aName), mainContext(aMainContext) {};
    CompiledScript(const string aName, ScriptMainContextPtr aMainContext, const SourceCursor& aCursor) : inherited(aName, aCursor), mainContext(aMainContext) {};

    /// get new main routine context for running this object as a main script or expression
    /// @param aMainContext the context from where a script is "called" is always the domain.
    ///   This parameter is used for consistency checking (the compiled code already knows its main context,
    ///   which must have the same domain as the aMainContext provided here).
    ///   It can be passed NULL when no check is needed.
    /// @param aThread the thread this call will originate from, e.g. when requesting context for a function call.
    ///   NULL means that code is not started from a running thread, such as scripts, handlers, triggers.
    /// @return new context suitable for evaluating this implementation, NULL if none
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const P44_OVERRIDE;

  };


  /// a compiled trigger expression
  /// is also an event sink
  class CompiledTrigger : public CompiledScript, public EventSink
  {
    typedef CompiledScript inherited;

  public:

    class FrozenResult
    {
    public:
      ScriptObjPtr frozenResult; ///< the frozen result
      MLMicroSeconds frozenUntil; ///< until when the value remains frozen, Infinite if forever (until explicitly unfrozen)
      /// @return true if still frozen (not expired yet)
      bool frozen();
    };

    string mResultVarName; ///< name of the variable that should represent the trigger result in handler code

  private:
    EvaluationCB mTriggerCB;
    TriggerMode mTriggerMode;
    EvaluationFlags mEvalFlags;
    ScriptObjPtr mCurrentResult;
    Tristate mCurrentState;
    bool mOneShotEvent;
    MLMicroSeconds mNextEvaluation;

    typedef std::map<SourceCursor::UniquePos, FrozenResult> FrozenResultsMap;
    FrozenResultsMap mFrozenResults; ///< map of expression starting indices and associated frozen results
    MLTicket mReEvaluationTicket; ///< ticket for re-evaluation timer

    MLMicroSeconds mHoldOff; ///< how long the evaluation result must be stable in order to fire the trigger
    MLMicroSeconds mMetAt; ///< time when holdoff is over and current trigger result can be fired

  public:

    CompiledTrigger(const string aName, ScriptMainContextPtr aMainContext);

    virtual ~CompiledTrigger() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

    virtual string getAnnotation() const P44_OVERRIDE { return "trigger"; };

    virtual void deactivate() P44_OVERRIDE;

    /// set the callback to fire on every trigger event
    /// @note callback will get the trigger expression result
    void setTriggerCB(EvaluationCB aTriggerCB) { mTriggerCB = aTriggerCB; }

    /// set the trigger mode and optional holdoff time
    void setTriggerMode(TriggerMode aTriggerMode, MLMicroSeconds aHoldOffTime) { mTriggerMode = aTriggerMode; mHoldOff = aHoldOffTime; }

    /// set the trigger evaluation flags
    void setTriggerEvalFlags(EvaluationFlags aEvalFlags) { mEvalFlags = aEvalFlags; }

    /// check if trigger is active (could possibly trigger)
    bool isActive() { return mTriggerCB!=NULL && mTriggerMode!=inactive; };

    /// the current result of the trigger (the result of the last evaluation that happened)
    ScriptObjPtr currentResult() { return mCurrentResult ? mCurrentResult : new AnnotatedNullValue("trigger never evaluated"); }

    /// the current boolean evaluation of the trigger
    Tristate currentState() { return mCurrentState; }

    /// initialize (activate) the trigger
    /// @return result of the initialisation (can be null when not requested synchronous execution)
    ScriptObjPtr initializeTrigger();

    /// called from event sources related to this trigger
    virtual void processEvent(ScriptObjPtr aEvent, EventSource &aSource) P44_OVERRIDE;

    /// trigger an evaluation
    /// @param aEvalMode primarily, sets the trigger evaluation run mode (triggered/timed/initial) and uses the other flags as set in
    ///   initializeTrigger(). Only if non-runmode flags are set, the evaluation will use aEvalMode as is and ignore the stored flags from initialisation.
    void triggerEvaluation(EvaluationFlags aEvalMode);


    /// schedule a (re-)evaluation at the specified time latest
    /// @param aLatestEval new time when next evaluation must happen latest
    /// @note this makes sure there is an evaluation NOT LATER than the given time, but does not guarantee a
    ///   evaluation actually does happen AT that time. So the trigger callback might want to re-schedule when the
    ///   next evaluation happens too early.
    void scheduleEvalNotLaterThan(const MLMicroSeconds aLatestEval);


    /// @name API for timed evaluation and freezing values in functions that can be used in timed evaluations
    /// @{

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression
    ///   On return: replaced by a frozen result, if one exists
    /// @param aFreezeId the reference position that identifies the frozen result
    FrozenResult* getFrozen(ScriptObjPtr &aResult, SourceCursor::UniquePos aFreezeId);

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aFreezeId te reference position that identifies the frozen result
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    FrozenResult* newFreeze(FrozenResult* aExistingFreeze, ScriptObjPtr aNewResult, SourceCursor::UniquePos aFreezeId, MLMicroSeconds aFreezeUntil, bool aUpdate = false);

    /// unfreeze frozen value at aAtPos
    /// @param aFreezeId the starting character index of the subexpression to unfreeze
    /// @return true if there was a frozen result at aAtPos
    bool unfreeze(SourceCursor::UniquePos aFreezeId);

    /// Set time when next evaluation must happen, latest
    /// @param aLatestEval new time when evaluation must happen latest, Never if no next evaluation is needed
    /// @return true if aNextEval was updated
    bool updateNextEval(const MLMicroSeconds aLatestEval);
    /// @param aLatestEvalTm new local broken down time when evaluation must happen latest
    /// @return true if aNextEval was updated
    bool updateNextEval(const struct tm& aLatestEvalTm);

    /// @}

  private:

    /// called whenever trigger was evaluated, fires callback depending on aEvalFlags and triggerMode
    void triggerDidEvaluate(EvaluationFlags aEvalMode, ScriptObjPtr aResult);

    /// schedule the next evaluation according to consolidated result of all updateNextEval() calls
    void scheduleNextEval();

  };


  #if P44SCRIPT_FULL_SUPPORT

  /// compiled handler (script with an embedded trigger)
  class CompiledHandler : public CompiledScript
  {
    typedef CompiledScript inherited;

    CompiledTriggerPtr trigger; ///< the trigger
  public:
    CompiledHandler(const string aName, ScriptMainContextPtr aMainContext) : inherited(aName, aMainContext) {};
    virtual ~CompiledHandler() { deactivate(); } // even if deactivate() is usually called before dtor, make sure it happens even if not

    virtual string getAnnotation() const P44_OVERRIDE { return "handler"; };

    void installAndInitializeTrigger(ScriptObjPtr aTrigger);
    virtual bool originatesFrom(SourceContainerPtr aSource) const P44_OVERRIDE
      { return inherited::originatesFrom(aSource) || (trigger && trigger->originatesFrom(aSource)); };
    virtual void deactivate() P44_OVERRIDE;
    virtual bool floating() const P44_OVERRIDE { return inherited::floating() || (trigger && trigger->floating()); }

  private:
    void triggered(ScriptObjPtr aTriggerResult);
    void actionExecuted(ScriptObjPtr aActionResult);

  };

  #endif // P44SCRIPT_FULL_SUPPORT


  // MARK: - ScriptCompiler

  class ScriptCompiler : public SourceProcessor
  {
    typedef SourceProcessor inherited;

    ScriptingDomainPtr domain; ///< the domain to store compiled functions and handlers
    SourceCursor bodyRef; ///< where the script body starts
    ScriptMainContextPtr compileForContext; ///< the main context this script is compiled for and should execute in later

  public:

    ScriptCompiler(ScriptingDomainPtr aDomain) : domain(aDomain) {}

    /// Scan code, extract function definitions, global vars, event handlers into scripting domain, return actual code
    /// @param aSource the source code
    /// @param aIntoCodeObj the CompiledCode object where to store the main code of the script compiled
    /// @param aParsingMode how to parse (as expression, scriptbody or full script with function+handler definitions)
    /// @param aMainContext the context in which this script should execute in. It is stored with the
    /// @return an executable object or error (syntax, other fatal problems)
    ScriptObjPtr compile(SourceContainerPtr aSource, CompiledCodePtr aIntoCodeObj, EvaluationFlags aParsingMode, ScriptMainContextPtr aMainContext);

    #if P44SCRIPT_FULL_SUPPORT

    /// must store result as a compiled function in the scripting domain
    /// @note must cause calling resume()
    virtual void storeFunction() P44_OVERRIDE;

    /// must store result as a event handler (trigger+action script) in the scripting domain
    /// @note must cause calling resume()
    virtual void storeHandler() P44_OVERRIDE;

    #endif // P44SCRIPT_FULL_SUPPORT

    /// indicates end of declarations
    /// @note must cause calling resume()
    virtual void startOfBodyCode() P44_OVERRIDE;

    /// must retrieve the member as specified. Note that compiler only can access global scope
    /// @param aMemberAccessFlags if this has lvalue set, caller would like to get an ScriptLValue which allows assigning a new value
    /// @param aNoNotFoundError if this is set, even finding nothing will not raise an error, but just return a NULL result
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    void memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError) P44_OVERRIDE;

    /// @return the main context for running triggers and handlers. This is used to associate scripts defined as part of a
    /// source (e.g. "on"-handlers) with a execution context to call them later
    /// Note: For triggers/handlers created in the declaration part of a script, this is the compiler's context
    ///   (no script running yet), and thus usually the ScriptingDomain
    virtual ScriptMainContextPtr getTriggerAndHandlerMainContext() P44_OVERRIDE { return compileForContext; }

  };


  // MARK: - ScriptCodeThread

  /// represents a code execution "thread" and its "stack"
  /// Note that the scope such a "thread" is only within one context.
  /// The "stack" is NOT a function calling stack, but only the stack
  /// needed to walk the nested code/expression structure with
  /// a state machine.
  class ScriptCodeThread : public P44LoggingObj, public SourceProcessor, public EventSource
  {
    typedef SourceProcessor inherited;
    friend class ScriptCodeContext;
    friend class BuiltinFunctionContext;

    ScriptCodeContextPtr mOwner; ///< the execution context which owns (has started) this thread
    ScriptObjPtr mThreadLocals; ///< the thread locals (might be set at thread creation already, or gets created on demand as SimpleVarContainer later)
    CompiledCodePtr mCodeObj; ///< the code object this thread is running
    MLMicroSeconds mMaxBlockTime; ///< how long the thread is allowed to block in evaluate()
    MLMicroSeconds mMaxRunTime; ///< how long the thread is allowed to run overall

    MLMicroSeconds mRunningSince; ///< time the thread was started
    ExecutionContextPtr mChainedExecutionContext; ///< set during calls to other contexts, e.g. to propagate abort()
    ScriptCodeThreadPtr mChainOriginThread; ///< the origin of this sequentially chained thread, if any
    MLTicket mAutoResumeTicket; ///< auto-resume ticket

  public:

    /// @param aOwner the context which owns this thread and will be notified when it ends
    /// @param aCode the code object that is running in this context
    /// @param aStartCursor the start point for the script
    /// @param aThreadLocals the (structured) object that provides thread local members (can be NULL)
    /// @param aChainOriginThread the origin of the sequential "thread" chain (as user defined functions always start a "thread")
    ScriptCodeThread(ScriptCodeContextPtr aOwner, CompiledCodePtr aCode, const SourceCursor& aStartCursor, ScriptObjPtr aThreadLocals, ScriptCodeThreadPtr aChainOriginThread);

    virtual ~ScriptCodeThread();

    /// logging context to use
    P44LoggingObj* loggingContext();

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() P44_OVERRIDE;

    /// @return the per-instance log level offset
    /// @note is virtual because some objects might want to use the log level offset of another object
    virtual int getLogLevelOffset() P44_OVERRIDE;


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
      MLMicroSeconds aMaxRunTime=Infinite
    );

    /// run the thread
    virtual void run();

    /// the original thread this chain of threads started from (can be this)
    ScriptCodeThreadPtr chainOriginThread() { if (mChainOriginThread) return mChainOriginThread; else return this; }

    /// request aborting the current thread, including child context
    /// @param aAbortResult if set, this is what abort will report back
    virtual void abort(ScriptObjPtr aAbortResult = ScriptObjPtr()) P44_OVERRIDE;

    /// abort all threads in the same context execpt this one
    /// @param aAbortResult if set, this is what abort will report back
    void abortOthers(EvaluationFlags aAbortFlags = stopall, ScriptObjPtr aAbortResult = ScriptObjPtr());

    /// @return NULL when the thread is still running, final result value otherwise
    ScriptObjPtr finalResult();

    /// complete the current thread
    virtual void complete(ScriptObjPtr aFinalResult) P44_OVERRIDE;

    /// @return the owner (the execution context that has started this thread)
    ScriptCodeContextPtr owner() { return mOwner; }

    /// convenience end of step using current result and checking for errors
    virtual void checkAndResume() P44_OVERRIDE;

    /// @name execution hooks. These must call resume()
    /// @{

    /// must retrieve the member as specified with storageSpecifier from current result (or from the script scope if result==NULL)
    /// @param aMemberAccessFlags if this has lvalue set, caller would like to get an ScriptLValue which allows assigning a new value
    /// @param aNoNotFoundError if this is set, even finding nothing will not raise an error, but just return a NULL result
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void memberByIdentifier(TypeInfo aMemberAccessFlags, bool aNoNotFoundError=false) P44_OVERRIDE;

    /// must retrieve the indexed member from current result (or from the script scope if result==NULL)
    /// @param aMemberAccessFlags if this has lvalue set, caller would like to get an ScriptLValue which allows assigning a new value
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void memberByIndex(size_t aIndex, TypeInfo aMemberAccessFlags) P44_OVERRIDE;

    /// @return the main context for running triggers and handlers. This is used to associate scripts defined as part of a
    /// source (e.g. "on"-handlers) with a execution context to call them later
    /// Note: triggers/handlers created when running the script live in the main context of the running script
    ///   such that it has access to context-level vars and functions
    virtual ScriptMainContextPtr getTriggerAndHandlerMainContext() P44_OVERRIDE { return owner()->scriptmain(); }

    #if P44SCRIPT_FULL_SUPPORT

    /// fork executing a block at the current position, if identifier is not empty, store a new ThreadValue.
    /// @note MUST NOT call resume() directly. This call will return when the new thread yields execution the first time.
    virtual void startBlockThreadAndStoreInIdentifier() P44_OVERRIDE;

    /// must store result as a event handler (trigger+action script) in the scripting domain
    /// @note must cause calling resume()
    virtual void storeHandler() P44_OVERRIDE;

    #endif // P44SCRIPT_FULL_SUPPORT

    /// must set a new funcCallContext suitable to execute result as a function
    /// @note must set result to an ErrorValue if no context can be created
    /// @note must cause calling resume() when funcCallContext is set
    virtual void newFunctionCallContext() P44_OVERRIDE;

    /// apply the specified argument to the current function context
    /// @note must cause calling resume() when result contains the member (or NULL if not found)
    virtual void pushFunctionArgument(ScriptObjPtr aArgument) P44_OVERRIDE;

    /// evaluate the current result and replace it with the output from the evaluation (e.g. function call)
    virtual void executeResult() P44_OVERRIDE;

    /// check if member can issue event that should be connected to trigger
    virtual void memberEventCheck() P44_OVERRIDE;

    /// @}

  protected:

    /// called by resume to perform next step(s).
    /// @note implements step timing/pacing/limiting and abort checking
    virtual void stepLoop() P44_OVERRIDE;

  private:

    void executedResult(ScriptObjPtr aResult);

  };



  // MARK: - Built-in function support

  class BuiltInMemberLookup;
  typedef boost::intrusive_ptr<BuiltInMemberLookup> BuiltInMemberLookupPtr;
  class BuiltinFunctionObj;
  typedef boost::intrusive_ptr<BuiltinFunctionObj> BuiltinFunctionObjPtr;
  class BuiltinFunctionContext;
  typedef boost::intrusive_ptr<BuiltinFunctionContext> BuiltinFunctionContextPtr;

  /// Builtin function Argument descriptor
  typedef struct {
    TypeInfo typeInfo; ///< info about allowed types, checking, open argument lists, etc.
    const char* name; ///< the name of the argument, can be NULL if unnamed positional argument
  } BuiltInArgDesc;

  /// Signature for built-in function/method implementation
  /// @param aContext execution context, containing parameters and expecting result
  /// @note must call finish() on the context when function has finished executing (before or after returning)
  typedef void (*BuiltinFunctionImplementation)(BuiltinFunctionContextPtr aContext);

  /// Signature for built-in member accessor.
  /// @param aParentObj the parent obj (if it's not a global member)
  /// @param aObjToWrite if not NULL, this is the value to write to the member
  /// @return if aObjToWrite==NULL, accessor must return the current value. Otherwise, the return value is ignored
  typedef ScriptObjPtr (*BuiltinMemberAccessor)(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite);

  typedef struct BuiltinMemberDescriptor {
    const char* name; ///< name of the function / member
    TypeInfo returnTypeInfo; ///< possible return types (for functions, this must have set "executable", but also contains the type(s) the functions might return. Members must have "lvalue" set to become assignable.
    size_t numArgs; ///< for functions: number of arguemnts, can be 0
    const BuiltInArgDesc* arguments; ///< for functions: arguments, can be NULL
    union {
      BuiltinFunctionImplementation implementation; ///< function pointer to implementation (as a plain function)
      BuiltinMemberAccessor accessor; ///< function pointer to accessor (as a plain function)
    };
  } BuiltinMemberDescriptor;


  class BuiltInLValue : public ScriptLValue
  {
    typedef ScriptLValue inherited;
    BuiltInMemberLookupPtr mLookup;
    ScriptObjPtr mThisObj;
    const BuiltinMemberDescriptor *descriptor; ///< function signature, name and pointer to actual implementation function
  public:
    BuiltInLValue(const BuiltInMemberLookupPtr aLookup, const BuiltinMemberDescriptor *aMemberDescriptor, ScriptObjPtr aThisObj, ScriptObjPtr aCurrentValue);
    virtual void assignLValue(EvaluationCB aEvaluationCB, ScriptObjPtr aNewValue) P44_OVERRIDE;
    virtual string getIdentifier() const P44_OVERRIDE { return descriptor ? descriptor->name : ""; };
  };

  /// member lookup for built-in functions, driven by static const struct table to describe functions and link implementations
  class BuiltInMemberLookup : public MemberLookup
  {
    typedef MemberLookup inherited;
    typedef std::map<const string, const BuiltinMemberDescriptor*, lessStrucmp> MemberMap;
    MemberMap members;

  public:
    /// create a builtin member lookup from descriptor table
    /// @param aMemberDescriptors pointer to an array of member descriptors, terminated with an entry with .name==NULL
    BuiltInMemberLookup(const BuiltinMemberDescriptor* aMemberDescriptors);

    virtual TypeInfo containsTypes() const P44_OVERRIDE { return constant+allscopes+any; } // constant, from all scopes, any type
    virtual ScriptObjPtr memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aMemberAccessFlags) const P44_OVERRIDE;

    #if SCRIPTING_JSON_SUPPORT
    /// FIXME: this is a simplistic partial solution to get at least some introspection for debugging purposes.
    ///   Once we have P44Value hierarchy with iterators, this can be done properly
    virtual void addJsonValues(JsonObjectPtr &aObj) const P44_OVERRIDE;
    #endif

  };


  /// represents a built-in function
  class BuiltinFunctionObj : public ImplementationObj
  {
    typedef ImplementationObj inherited;
    friend class BuiltinFunctionContext;

    const BuiltinMemberDescriptor *descriptor; ///< function signature, name and pointer to actual implementation function
    ScriptObjPtr thisObj; ///< the object this function is a method of (if it's not a plain function)
    const BuiltInMemberLookup* mMemberLookupP; ///< where the function was found (might be needed as context for executing it later)

  public:

    virtual string getAnnotation() const P44_OVERRIDE { return "built-in function"; };

    BuiltinFunctionObj(const BuiltinMemberDescriptor *aDescriptor, ScriptObjPtr aThisObj, const BuiltInMemberLookup* aMemberLookupP) :
      descriptor(aDescriptor), thisObj(aThisObj), mMemberLookupP(aMemberLookupP) {};

    /// get the lookup object
    BuiltInMemberLookup* getMemberLookup() { return const_cast<BuiltInMemberLookup*>(mMemberLookupP); }

    /// Get description of arguments required to call this internal function
    virtual bool argumentInfo(size_t aIndex, ArgumentDescriptor& aArgDesc) const P44_OVERRIDE;

    /// get identifier (name) of this function object
    virtual string getIdentifier() const P44_OVERRIDE { return descriptor->name; };

    /// get context to call this object as a (sub)routine of a given context
    /// @param aMainContext the main context from where this function is called.
    /// @param aThread the thread this call will originate from, e.g. when requesting context for a function call.
    ///   NULL would mean code is not started from a running thread, but that is unlikely for a builtin function
    /// @return a context for running built-in functions, with access to aMainContext's instance() object
    virtual ExecutionContextPtr contextForCallingFrom(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread) const P44_OVERRIDE;

  };


  class BuiltinFunctionContext : public ExecutionContext
  {
    typedef ExecutionContext inherited;
    friend class BuiltinFunctionObj;

    BuiltinFunctionObjPtr func; ///< the currently executing function
    EvaluationCB evaluationCB; ///< to be called when built-in function has finished
    SimpleCB abortCB; ///< called when aborting. async built-in might set this to cause external operations to stop at abort
    CompiledTrigger* mTrigger; ///< set when the function executes as part of a trigger expression
    ScriptCodeThreadPtr mThread; ///< thread this call originates from
    SourceCursor::UniquePos callSite; ///< from where in the source code the function was called

  public:

    BuiltinFunctionContext(ScriptMainContextPtr aMainContext, ScriptCodeThreadPtr aThread);

    /// evaluate built-in function
    virtual void execute(ScriptObjPtr aToExecute, EvaluationFlags aEvalFlags, EvaluationCB aEvaluationCB, ScriptCodeThreadPtr aChainOriginThread, ScriptObjPtr aThreadLocals = ScriptObjPtr(), MLMicroSeconds aMaxRunTime = Infinite) P44_OVERRIDE;

    /// abort (async) built-in function
    /// @param aAbortFlags set stoprunning to abort currently running threads, queue to empty the queued threads
    /// @param aAbortResult if set, this is what abort will report back
    /// @return true if aborted
    virtual bool abort(EvaluationFlags aAbortFlags = stoprunning+queue, ScriptObjPtr aAbortResult = ScriptObjPtr(), ScriptCodeThreadPtr aExceptThread = ScriptCodeThreadPtr()) P44_OVERRIDE;

    /// @name builtin function implementation interface
    /// @{

    /// @return convenience access to numIndexedMembers() in built-in function implementations
    inline size_t numArgs() const { return numIndexedMembers(); };

    /// convenience access to arguments for implementing built-in functions
    /// @param aArgIndex must be in the range of 0..numIndexedMembers()-1
    /// @note essentially is just a convenience wrapper for memberAtIndex()
    /// @note built-in functions should be called with a context that matches their signature
    ///   so implementation wants to just access the arguments it expects to be there by index w/o checking.
    ///   To avoid crashes in case a builtin function is evaluated w/o proper signature checking,
    ///   accessing non-existing arguments return a annotated ("optional function argument") null.
    ScriptObjPtr arg(size_t aArgIndex);

    /// @return unique (opaque) id for re-identifying this argument's definition for this call in the source code
    SourceCursor::UniquePos argId(size_t aArgIndex) const;

    /// @return argument as reference for applying C++ operators to them (and not to the smart pointers)
    inline ScriptObj& argval(size_t aArgIndex) { return *(arg(aArgIndex)); }

    /// set abort callback
    /// @param aAbortCB will be called when context receives abort() before implementation call finish()
    /// @note async built-ins must set this callback implementing immediate termination of any ongoing action
    void setAbortCallback(SimpleCB aAbortCB);

    /// pass result and execution thread back to script
    /// @param aResult the function result, if any.
    /// @note this must be called when a builtin function implementation completes
    void finish(const ScriptObjPtr aResult = ScriptObjPtr());

    /// @return the object this function was called on as a method, NULL for plain functions
    ScriptObjPtr thisObj() { return func->thisObj; }

    /// @return the function object
    BuiltinFunctionObjPtr funcObj() { return func; }

    /// @return the thread this function was called in
    ScriptCodeThreadPtr thread() { return mThread; }

    /// @return the evaluation flags of the current evaluation
    EvaluationFlags evalFlags() { return mThread ? mThread->evaluationFlags : (EvaluationFlags)regular; }

    /// @return the trigger object if this function is executing as part of a trigger expression
    CompiledTrigger* trigger() { return mThread ? dynamic_cast<CompiledTrigger *>(mThread->mCodeObj.get()) : NULL; }

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

#endif // ENABLE_P44SCRIPT

#endif // defined(__p44utils__p44script__)
