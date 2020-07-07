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

  class ScriptObj;
  typedef boost::intrusive_ptr<ScriptObj> ScriptObjPtr;
  class ExpressionValue;
  typedef boost::intrusive_ptr<ExpressionValue> ExpressionValuePtr;
  class ErrorValue;
  class NumericValue;
  class StringValue;

  class ExecutableObj;
  class ScriptCodeObj;
  class ScriptingDomain;

  class ExecutionContext;
  typedef boost::intrusive_ptr<ExecutionContext> ExecutionContextPtr;
  class ExecutionThread;
  typedef boost::intrusive_ptr<ExecutionThread> ExecutionThreadPtr;

  class CodeItem;
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
      Syntax,
      DivisionByZero,
      CyclicReference,
      Immutable, ///< object/field is immutable and cannot be assigned
      Invalid, ///< invalid value
      Internal, ///< internal inconsistency
      Busy, ///< currently running
      NotFound, ///< referenced object not found at runtime
      Aborted, ///< externally aborted
      Timeout, ///< aborted because max execution time limit reached
      User, ///< user generated error (with throw)
    } ErrorCodes;
    static const char *domain() { return "ScriptError"; }
    virtual const char *getErrorDomain() const { return ScriptError::domain(); };
    ScriptError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    /// factory method to create string error fprint style
    static ErrorPtr err(ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
  };


  // MARK: - Script namespace types

  /// Evaluation flags
  typedef enum {
    initial = 0x0001,
    timed = 0x0002,
    synchronously = 0x1000
    // FIXME: real flags
  } EvaluationFlags;

  /// Type info
  enum {
    // exclusive content type identifiers (but combinable bits for type selection/filtering
    typeMask = 0x00FF,
    none = 0x0000, ///< no content type, because it is a container
    null = 0x0001, ///< NULL/undefined
    error = 0x0002, ///< Error
    numeric = 0x0004, ///< numeric value
    text = 0x0008, ///< text/string value
    json = 0x0010, ///< JSON value
    executable = 0x0080, ///< executable code
    any = 0x00FF, ///< any type
    // attributes
    attrMask = 0xFF00,
    field = 0x0100, ///< is a (leaf) field
    object = 0x0200, ///< is a object with named members
    array = 0x0400, ///< is an array with indexed elements
    mutablemembers = 0x0800, ///< members are mutable
    // for argument checking
    multiple = 0x1000, ///< this argument type can occur mutiple times (... signature)
    autoconvert = 0x2000, ///< type is not required to match, as long as it can be converted
  };
  typedef uint16_t TypeInfo;

  /// Argument descriptor
  typedef struct {
    TypeInfo allowed; ///< these type bits may be set
    TypeInfo required; ///< these type bits must be set
    const char* name; ///< the name of the argument, can be NULL if unnamed positional argument
  } ArgumentDesciptor;

  /// Script call signature
  typedef const ArgumentDesciptor* ScriptCallSignature;

  // MARK: - ScriptObj base class

  /// evaluation callback
  /// @param aEvaluationResult the result of the evaluation
  typedef boost::function<bool (ScriptObjPtr aEvaluationResult)> EvaluationCB;

  /// Base Object in scripting
  class ScriptObj : public P44Obj
  {
  public:

    /// @name information
    /// @{

    /// get type of this value
    /// @return get type info
    virtual TypeInfo getTypeInfo() const { return null; }; // base object is a null/undefined

    /// get annotation text
    virtual string getAnnotation() const { return "ScriptObj"; };

    /// @}

    /// @name evaluation
    /// @{

    /// @return true if this object is evaluated.
    /// @note to obtain a evaluated version of the object, evaluate() must be called
    virtual bool isEvaluated() const { return true; } // base class is always in evaluated state (a null object)

    /// @return the / a context in which this object can or should be evaluated.
    ///   parameters/arguments to be passed to the evaluation must be set into this context
    virtual ExecutionContextPtr newExecutionContext() { return ExecutionContextPtr(); };

    /// Evaluate the object
    /// @param aEvaluationCB will be called to deliver the result of the evaluation
    /// @param aEvalFlags evaluation flags
    /// @param aExecutionContext the context in which the execution should take place (vars, constants, domain)
    /// @note the evaluation result might be a Ptr to the same object
    virtual void evaluate(EvaluationCB aEvaluationCB, EvaluationFlags aEvalFlags, ExecutionContextPtr aExecutionContext);

    /// @}

    /// @name value getters
    /// @{

    virtual double numValue() const { return 0; }; ///< @return a conversion to numeric (using literal syntax), if value is string
    virtual bool boolValue() const { return numValue()!=0; }; ///< @return a conversion to boolean (true = not numerically 0, not JSON-falsish)
    virtual string stringValue() const { return "undefined"; }; ///< @return a conversion to string of the value
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
    /// @return ok or Error describing reason for assignment failure
    /// @note only possibly works on objects with type attribute "mutablemembers"
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

    /// Get signature (description of arguments) required to call this object
    /// @return NULL or pointer to an array of argument descriptors, terminated with a entry with .allowed==none
    virtual ScriptCallSignature callSignature() const { return NULL; };

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
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return field; };
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
    virtual string getAnnotation() const P44_OVERRIDE { return "error"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return field|error; };
    // value getters
    virtual double numValue() const P44_OVERRIDE { return err ? 0 : err->getErrorCode(); };
    virtual string stringValue() const P44_OVERRIDE { return Error::text(err); };
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif
  };


  class NumericValue : public ScriptValue
  {
    typedef ScriptValue inherited;
    double num;
  public:
    NumericValue(double aNumber) : num(aNumber) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "numeric"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return field+numeric; };
    // value getters
    virtual double numValue() const P44_OVERRIDE { return num; }; // native
    virtual string stringValue() const P44_OVERRIDE { return string_format("%lg", num); };
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE { return JsonObject::newDouble(num); };
    #endif
  };


  class StringValue : public ScriptValue
  {
    typedef ScriptValue inherited;
    string str;
  public:
    StringValue(string aString) : str(aString) {};
    virtual string getAnnotation() const P44_OVERRIDE { return "string"; };
    virtual TypeInfo getTypeInfo() const P44_OVERRIDE { return field+text; };
    // value getters
    virtual string stringValue() const P44_OVERRIDE { return str; }; // native
    virtual double numValue() const P44_OVERRIDE;
    #if SCRIPTING_JSON_SUPPORT
    virtual JsonObjectPtr jsonValue() const P44_OVERRIDE;
    #endif
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


  // MARK: - Executable Script Objects


  class ExecutableObj : public ScriptObj
  {
    typedef ScriptObj inherited;
  public:

  };



  /// Basic execution context, can hold indexed (positional) arguments as needed for callimng built-in functions
  class ExecutionContext : public ScriptObj
  {
    typedef ScriptObj inherited;

    typedef std::vector<ScriptObjPtr> IndexedVarVector;
    IndexedVarVector indexedVars;

  public:

    virtual size_t numIndexedMembers() const P44_OVERRIDE;
    virtual const ScriptObjPtr memberAtIndex(size_t aIndex, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

  };


  /// Execution context for function implemented as script with access to local variables (and named access to arguments)
  class LocalVarContext : public ExecutionContext
  {
    typedef ExecutionContext inherited;

    typedef std::map<string, ScriptObjPtr, lessStrucmp> NamedVarMap;
    NamedVarMap namedVars;

  public:

    virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aTypeRequirements = none) const P44_OVERRIDE;
    virtual ErrorPtr setMemberByName(const string aName, const ScriptObjPtr aMember) P44_OVERRIDE;

  };


  // MARK: - Member Lookup


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
    virtual ErrorPtr setMemberByNameFrom(ScriptObjPtr aThisObj, const string aName, const ScriptObjPtr aMember) { return ScriptError::err(ScriptError::Immutable, "cannot assign '%s'", aName.c_str()); };

  };



  // MARK: - Built-in function support

  class BuiltInFunctionLookup;
  class BuiltinFunctionObj;
  class BuiltinFunctionContext;
  typedef boost::intrusive_ptr<BuiltinFunctionContext> BuiltinFunctionContextPtr;


  /// Signature for built-in function/method implementation
  /// @param aContext execution context, containing parameters and expecting result
  /// @note must call resume() or resumeWithResult() on the context when function has finished executing.
  typedef void (*BuiltinFunctionImplementation)(BuiltinFunctionContextPtr aContext);

  typedef struct {
    const char* name;
    TypeInfo returnType;
    const ArgumentDesciptor* arguments;
    BuiltinFunctionImplementation implementation;
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
  class BuiltinFunctionObj : public ExecutableObj
  {
    typedef ExecutableObj inherited;

    const BuiltinFunctionDescriptor *descriptor; ///< function signature
    ScriptObjPtr thisObj; ///< the object this function is a method of

  public:
    BuiltinFunctionObj(const BuiltinFunctionDescriptor *aDescriptor, ScriptObjPtr aThisObj) : descriptor(aDescriptor), thisObj(aThisObj) {};

    /// Get signature (description of arguments) required to call this object
    /// @return NULL or pointer to an array of argument descriptors, terminated with a entry with .allowed==none
    virtual ScriptCallSignature callSignature() const P44_OVERRIDE { return descriptor->arguments; };

    /// @return a context for running built-in functions (only needs the arguments)
    virtual ExecutionContextPtr newExecutionContext() P44_OVERRIDE;

    /// Evaluate the (function) object
    /// @param aEvaluationCB will be called to deliver the result of the evaluation
    /// @param aEvalFlags evaluation flags
    /// @param aExecutionContext the context in which the execution should take place (vars, constants, domain)
    /// @note the evaluation result might be a Ptr to the same object
    virtual void evaluate(EvaluationCB aEvaluationCB, EvaluationFlags aEvalFlags, ExecutionContextPtr aExecutionContext) P44_OVERRIDE;

  };


  class BuiltinFunctionContext : public ExecutionContext
  {
    typedef ExecutionContext inherited;
    friend class BuiltinFunctionObj;

    ScriptObjPtr thisObject; ///< the object this function is a method of
    ScriptObj nullObj; ///< dummy null object for arg() to return reference to for invalid indices
    EvaluationCB evaluationCB; ///< to be called back

  public:

    BuiltinFunctionContext(ScriptObjPtr aThisObj) : thisObject(aThisObj) { nullObj.isMemberVariable(); };

    /// @name builtin function implementation interface
    /// @{

    /// convenience access to arguments for implementing built-in functions
    /// @param aArgIndex must be in the range of 0..numIndexedMembers()-1
    /// @note essentially is just a convenience wrapper for memberAtIndex()
    /// @note built-in functions are called with a context that matches their signature, so usually
    ///   implementation can just access the arguments it expects to be there by index w/o checking.
    const ScriptObj& arg(size_t aArgIndex) const;

    /// return result and execution thread back to script
    /// @param aResult the function result, if any.
    /// @note this must be called when a builtin function implementation completes
    void finish(ScriptObjPtr aResult = ScriptObjPtr());

    /// @return the object _instance_ this function is a method of
    /// @note can be null if this function is a plain global function
    ScriptObjPtr thisObj() { return thisObject; }

    /// builtin functions accept optional member names, but do not store them
    virtual ErrorPtr setMemberAtIndex(size_t aIndex, const ScriptObjPtr aMember, const string aName = "") P44_OVERRIDE;

    /// @}
  };






  #if 0

  /// expression value, consisting of a value and an error to indicate non-value and reason for it
  class ExpressionValue {
    friend class EvaluationContext;

    bool nullValue; ///< set if this is a null value
    string* strValP; ///< string values have a std::string here
    double numVal;
    ErrorPtr err;
    #if EXPRESSION_JSON_SUPPORT
    JsonObjectPtr json;
    #endif

    void clrStr();
    void clrExtensions();
  public:
    /// Constructors
    ExpressionValue() : nullValue(true), numVal(0), strValP(NULL) { };
    ExpressionValue(double aNumValue) : nullValue(false), numVal(aNumValue), strValP(NULL) { };
    ExpressionValue(const string &aStrValue) : nullValue(false), numVal(0), strValP(new string(aStrValue)) { };
    ExpressionValue(ErrorPtr aError) : nullValue(false), numVal(0), strValP(NULL), err(aError) { };
    ExpressionValue(const ExpressionValue& aVal); ///< copy constructor
    ~ExpressionValue();
    // Getters
    double numValue() const; ///< returns a conversion to numeric (using literal syntax), if value is string
    bool boolValue() const; ///< returns a conversion to boolean (true = not numerically 0, not JSON-falsish)
    int intValue() const { return (int)numValue(); }
    int64_t int64Value() const { return (int64_t)numValue(); }
    string stringValue() const; ///< returns a conversion to string if value is numeric
    #if EXPRESSION_JSON_SUPPORT
    JsonObjectPtr jsonValue(ErrorPtr *errP = NULL) const; ///< returns a conversion to a JSON value
    ExpressionValue subField(const string aFieldName);
    ExpressionValue arrayElement(int aArrayIndex);
    ExpressionValue subScript(const ExpressionValue& aSubScript);
    #endif
    ErrorPtr error() const { return err; }
    // Setters
    void setNull(const char *aWithInfo = NULL) { clrExtensions(); if (aWithInfo) setString(aWithInfo); nullValue = true; err = NULL; numVal = 0; }
    void setBool(bool aBoolValue) { nullValue = false; numVal = aBoolValue ? 1: 0; clrExtensions(); }
    void setNumber(double aNumValue) { nullValue = false; numVal = aNumValue; clrExtensions(); }
    void setString(const string& aStrValue) { nullValue = false; numVal = 0; clrExtensions(); strValP = new string(aStrValue); }
    void setString(const char *aCStrValue) { nullValue = false; numVal = 0; clrExtensions(); strValP = new string(aCStrValue); }
    void setError(ErrorPtr aError) { nullValue = false; clrExtensions(); err = aError; numVal = 0; }
    void setError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)  __printflike(3,4);
    void setSyntaxError(const char *aFmt, ...)  __printflike(2,3);
    #if EXPRESSION_JSON_SUPPORT
    void setJson(JsonObjectPtr aJson);
    #endif
    // tests
    bool isOK() const { return !err; } ///< not error, but actual value or null
    bool isValue() const { return !nullValue && !err; } ///< not null and not error -> real value
    bool notValue() const { return !isValue(); }
    bool isError() const { return (bool)err; }
    bool isNull() const { return nullValue; }
    bool syntaxOk() const { return !Error::isError(err, ExpressionError::domain(), ExpressionError::Syntax); } ///< might be ok for calculations, not a syntax problem
    bool isString() const { return !nullValue && strValP!=NULL; }
    #if EXPRESSION_JSON_SUPPORT
    bool isJson() const { return !nullValue && json; }
    #endif
    // Operators
    ExpressionValue& operator=(const ExpressionValue& aVal); ///< assignment operator
    ExpressionValue operator!() const;
    ExpressionValue operator<(const ExpressionValue& aRightSide) const;
    ExpressionValue operator>=(const ExpressionValue& aRightSide) const;
    ExpressionValue operator>(const ExpressionValue& aRightSide) const;
    ExpressionValue operator<=(const ExpressionValue& aRightSide) const;
    ExpressionValue operator==(const ExpressionValue& aRightSide) const;
    ExpressionValue operator!=(const ExpressionValue& aRightSide) const;
    ExpressionValue operator+(const ExpressionValue& aRightSide) const;
    ExpressionValue operator-(const ExpressionValue& aRightSide) const;
    ExpressionValue operator*(const ExpressionValue& aRightSide) const;
    ExpressionValue operator/(const ExpressionValue& aRightSide) const;
    ExpressionValue operator%(const ExpressionValue& aRightSide) const;
    ExpressionValue operator&&(const ExpressionValue& aRightSide) const;
    ExpressionValue operator||(const ExpressionValue& aRightSide) const;
  };

  class FunctionArguments
  {
    std::vector<pair<size_t, ExpressionValue> > args;
  public:
    const ExpressionValue operator[](unsigned int aArgIndex) const { if (aArgIndex<args.size()) return args[aArgIndex].second; else return ExpressionValue(); }
    size_t getPos(unsigned int aArgIndex) const { if (aArgIndex<args.size()) return args[aArgIndex].first; else return 0; }
    int size() const { return (int)args.size(); }
    void clear() { args.clear(); }
    void addArg(const ExpressionValue &aArg, size_t aAtPos) { args.push_back(make_pair(aAtPos, aArg)); }
  };

  /// callback function for obtaining variables
  /// @param aName the name of the value/variable to look up (any case, comparison must be case insensitive)
  /// @param aResult set the value here
  /// @return true if value returned, false if value is unknown
  typedef boost::function<bool (const string &aName, ExpressionValue &aResult)> ValueLookupCB;

  /// callback function for function evaluation
  /// @param aFunc the name of the function to execute, always passed in in all lowercase
  /// @param aArgs vector of function arguments, tuple contains expression starting position and value
  /// @param aResult set to function's result
  /// @return true if function executed, false if function signature (name, number of args) is unknown
  typedef boost::function<bool (EvaluationContext* aEvalContext, const string& aFunc, const FunctionArguments& aArgs, ExpressionValue& aResult)> FunctionLookupCB;


  /// evaluate expression in an ad-hoc, isolated context, providing function and value callbacks
  /// @param aExpression the expression text
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @param aLogLevel the log level to show evaluation messages on, defaults to LOG_DEBUG
  /// @return the result of the expression
  ExpressionValue evaluateExpression(const string& aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, int aLogOffset = 0);

  /// substitute "@{xxx}" type expression placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @param aNullText this will be used as substitution for expressions with errors or null value
  /// @return returns first error occurred during substitutions. Note that only unbalanced substitution brackets @{xxx} abort substitution,
  ///    other errors just cause substitution result to be aNullText.
  ErrorPtr substituteExpressionPlaceholders(string& aString, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, string aNullText = "null");


  /// Expression Evaluation Callback
  /// @param aEvaluationResult the evaluation result (can be error)
  /// @param aContext the evaluation context this result originates from
  typedef boost::function<void (ExpressionValue aEvaluationResult, EvaluationContext &aContext)> EvaluationResultCB;

  /// Evaluation mode
  typedef enum {
    evalmode_unspecific, ///< no specific mode
    evalmode_initial, ///< initial evaluator run
    evalmode_externaltrigger, ///< externally triggered evaluation
    evalmode_timed, ///< timed evaluation by timed retrigger
    evalmode_script, ///< evaluate as script code
    evalmode_syntaxcheck ///< just check syntax, no side effects
  } EvalMode;

  // Operator syntax mode
  #define EXPRESSION_OPERATOR_MODE_FLEXIBLE 0
  #define EXPRESSION_OPERATOR_MODE_C 1
  #define EXPRESSION_OPERATOR_MODE_PASCAL 2
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
  #ifndef EXPRESSION_OPERATOR_MODE
    #define EXPRESSION_OPERATOR_MODE EXPRESSION_OPERATOR_MODE_FLEXIBLE
  #endif


  typedef std::map<string, ExpressionValue, lessStrucmp> VariablesMap; ///< local or global variables container


  /// Basic Expression Evaluation Context
  class EvaluationContext;
  typedef boost::intrusive_ptr<EvaluationContext> EvaluationContextPtr;
  class EvaluationContext : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;

    friend class ExpressionValue;
    friend class ScriptGlobals;

    typedef std::list<FunctionLookupCB> FunctionCBList;
    FunctionCBList functionCallbacks;

  public:

    // operations with precedence
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
    } Operations;

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
    } EvalState; ///< the state of the evaluation in the current frame

  protected:

    /// @name Evaluation parameters (set before execution starts, not changed afterwards, no nested state)
    /// @{
    string contextInfo; ///< info string of the context the script runs in (for logging)
    P44LoggingObj *loggingContextP; ///< if set, logging occurs using the logging context prefix and logLevelOffset of this object
    EvalMode evalMode; ///< the current evaluation mode
    MLMicroSeconds execTimeLimit; ///< how long any script may run in total, or Infinite to have no limit
    MLMicroSeconds syncExecLimit; ///< how long a synchronous script may run (blocking everything), or Infinite to have no limit
    MLMicroSeconds syncRunTime; ///< how long a async script is let running synchronously, before it is made yield execution, or Infinite to have no limit
    const GeoLocation* geolocationP; ///< if set, the current geolocation (e.g. for sun time functions)
    string codeString; ///< the code to evaluate
    bool synchronous; ///< the code must run synchronously to the end, execution may NOT be yielded
    P44ObjPtr callerContext; ///< optional context of the caller of this script, might be accessed by externally implemented custom functions
    /// @}

    /// @name  Evaluation state
    /// @{
    MLMicroSeconds runningSince; ///< when the current evaluation has started, Never otherwise
    bool callBack; ///< if set and evaluation completes, the result callback is used
    size_t pos; ///< the scanning position within code, also indicates error position when evaluation fails
    ExpressionValue finalResult; ///< final result
    MLTicket execTicket; ///< a ticket for timed steps that are part of the execution

    // The stack
    class StackFrame {
    public:
      StackFrame(EvalState aState, bool aSkip, int aPrecedence) :
        state(aState), skipping(aSkip), flowDecision(false), precedence(aPrecedence), op(op_none)
      {}
      EvalState state; ///< current state
      int precedence; ///< encountering a binary operator with smaller precedence will end the expression
      Operations op; ///< operator
      size_t pos; ///< relevant position in the code, e.g. start of expression for s_expression, start of condition for s_whilecondition
      string identifier; ///< identifier (e.g. variable name, function name etc.)
      bool skipping; ///< if set, we are just skipping code, not really executing
      bool flowDecision; ///< flow control decision
      FunctionArguments args; ///< arguments
      ExpressionValue val; ///< private value for operations
      ExpressionValue res; ///< result value passed down at popAndPassResult()
    };
    typedef std::list<StackFrame> StackList;
    StackList stack; ///< the stack

    StackFrame &sp() { return stack.back(); } ///< current stackpointer
    /// @}

    /// @name timed evaluation and result freezing
    /// @{
    EvaluationResultCB evaluationResultHandler; ///< called when a evaluation started by triggerEvaluation() completes (includes re-evaluations)
    bool oneTimeResultHandler; ///< if set, the callback will be removed after calling
    MLMicroSeconds nextEvaluation; ///< time when executed functions would like to see the next evaluation (used by TimedEvaluationContext)

    /// unused here, only actually in use by TimedEvaluationContext
    class FrozenResult
    {
    public:
      ExpressionValue frozenResult; ///< the frozen result
      MLMicroSeconds frozenUntil; ///< until when the value remains frozen, Infinite if forever (until explicitly unfrozen)
      /// @return true if still frozen (not expired yet)
      bool frozen();
    };
    /// @}

  public:

    EvaluationContext(const GeoLocation* aGeoLocationP = NULL);
    virtual ~EvaluationContext();

    /// set name of the context (for log messages)
    /// @param aContextInfo info string of the context the script runs in (for logging)
    /// @param aLoggingContextP if set, logging occurs using the logging context prefix and logLevelOffset of this object
    void setContextInfo(const string aContextInfo, P44LoggingObj *aLoggingContextP = NULL);

    /// set code to evaluate
    /// @param aCode set the expression to be evaluated in this context
    /// @note setting an expression that differs from the current one unfreezes any frozen arguments
    /// @return true if expression actually changed (not just set same expession again)
    bool setCode(const string aCode);

    /// get current code
    const char *getCode() { return codeString.c_str(); };

    /// set caller context
    void setCallerContext(P44ObjPtr aContext) { callerContext = aContext; };

    /// get caller context
    P44ObjPtr getCallerContext() { return callerContext; };

    /// @return the index into code() of the current evaluation or error
    size_t getPos() { return pos; }

    /// @return a prefix for log messages from this addressable
    virtual string logContextPrefix() P44_OVERRIDE;

    /// @return log level offset (overridden to use something other than the P44LoggingObj's)
    virtual int getLogLevelOffset() P44_OVERRIDE;

    /// evaluate code synchonously
    /// @param aEvalMode if specified, the evaluation mode for this evaluation. Defaults to current evaluation mode.
    /// @param aScheduleReEval if true, re-evaluations as demanded by evaluated expression are scheduled (NOP in base class)
    /// @param aCallBack if true, result callback (if one is installed) is called (in addition to returning result directly
    /// @return expression result
    virtual ExpressionValue evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval = false, bool aCallBack = false);

    /// @return true if currently evaluating an expression.
    bool isEvaluating() { return runningSince!=Never; }

    /// @return the (final) result of an evaluation
    /// @note valid only if not isEvaluating()
    ExpressionValue& getResult() { return finalResult; }

    /// set re-evaluation callback
    /// @param aEvaluationResultHandler is called when a evaluation started by triggerEvaluation() completes
    ///   (which includes delayed re-evaluations the context triggers itself, e.g. when timed functions are called)
    void setEvaluationResultHandler(EvaluationResultCB aEvaluationResultHandler);

    /// register additional (synchronous) function handler
    /// @note function handlers will be called to check for functions in the order they are registered, before
    ///   checking the class hierarchy's built-in functions
    void registerFunctionHandler(FunctionLookupCB aFunctionLookupHandler);

    /// trigger a (re-)evaluation
    /// @param aEvalMode the evaluation mode for this evaluation.
    /// @note evaluation result handler, if set, will be called when complete
    /// @return ok, or error if expression could not be evaluated
    ErrorPtr triggerEvaluation(EvalMode aEvalMode);

    /// request aborting evaluation
    /// @note this is for asynchronous scripts, and does request aborting, but can only do so after
    ///    operations of the current step (such as a API call) complete, or can be aborted as well.
    /// @note subclasses can override this to abort async operations they know of.
    /// @param aDoCallBack if set, callback will be executed (if one is set)
    /// @return true if the script is aborted NOW for sure (was aborted before or could be aborted immediately)
    virtual bool abort(bool aDoCallBack = true);


    /// @name error reporting, can also be used from external function implementations
    /// @{

    /// set result to specified error and throw it as an exception, which can be caught by try/catch
    bool throwError(ErrorPtr aError);
    bool throwError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...) __printflike(3,4);

    /// set result to syntax error and unconditionally abort (can't be caught)
    bool abortWithSyntaxError(const char *aFmt, ...) __printflike(2,3);

    /// @}

    /// @name utilities for implementing functions
    /// @{

    /// helper to exit evaluateFunction indicating a argument with an error or null
    /// @param aArg the argument which is in error or is null when it should be a valid value
    /// @param aResult must be passed to allow being set to null when aArg is null
    /// @note if aArg is an error, this will throw the error
    /// @return always true
    bool errorInArg(const ExpressionValue& aArg, ExpressionValue& aResult); ///< for synchronous functions

    /// helper to exit evaluateAsyncFunction indicating a argument with an error or null
    /// @param aArg the argument which is in error or is null when it should be a valid value
    /// @param aReturnIfNull if set, the function returns with a null result when aArg is null
    /// @note if aArg is an error, this will throw the error
    bool errorInArg(const ExpressionValue& aArg, bool aReturnIfNull); ///< for asynchronous functions

    /// re-entry point for asynchronous functions to return a result
    /// @param aResult the result
    void continueWithAsyncFunctionResult(const ExpressionValue& aResult);

    /// set the function result from within function implementations
    bool returnFunctionResult(const ExpressionValue& aResult);

    /// @}

    /// @name virtual methods that can be overridden in subclasses to extend functionality
    /// @{

    /// release all evaluation state (none in base class)
    virtual void releaseState() { /* NOP: no state in base class */ };

    /// lookup variables by name
    /// @param aName the name of the value/variable to look up (any case, comparison must be case insensitive)
    /// @param aResult set the value here
    /// @return true if value returned, false if value is unknown
    virtual bool valueLookup(const string &aName, ExpressionValue &aResult);

    /// evaluation of synchronously implemented functions which immediately return a result
    /// @param aFunc the name of the function to execute, always passed in in all lowercase
    /// @param aResult set the function result here
    /// @return true if function executed, false if function signature is unknown
    virtual bool evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult);

    /// evaluation of asynchronously implemented functions which may yield execution and resume later
    /// @param aFunc the name of the function to execute
    /// @param aNotYielded
    /// - true when execution has not yieled and the function evaluation is complete
    /// - false when the execution of the function has yielded and resumeEvaluation() will be called to complete
    /// @return true if function executed, false if function signature is unknown
    /// @note this method will not be called when context is set to execute synchronously, so these functions will not be available then.
    virtual bool evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded);

    /// @}

  protected:

    /// @param aResult the result to return via callback
    /// @return true if a callback was set and executed
    bool runCallBack(const ExpressionValue &aResult);

    /// @name Evaluation state machine entry points
    /// @note At any one of these entry points the following conditions must be met
    /// - a stack frame exists, sp points to it
    /// - all possible whitespace is consumed, pos indicates next token to look at
    /// @{

    /// Internal initialisation: start expression evaluation from scratch
    /// @param aState the initial state
    bool startEvaluationWith(EvalState aState);

    /// Initialisation start new expression evaluation
    /// @note context-global evaluation parameters must be set already
    /// @note after that, resumeEvaluation() must be called until isEvaluating() returns false
    /// @note a script context can also evaluate single expressions
    virtual bool startEvaluation();

    /// re-entry point for callbacks - continue execution
    /// @return true if evaluation completed without yielding execution.
    bool continueEvaluation();

    /// Main entry point / dispatcher - resume evaluation where we left off when we last yielded
    /// @return
    /// - true when execution has not yieled - i.e the caller MUST initiate the next call directly or indirectly
    /// - false when the execution has yielded and will be resumed by resumeEvaluation() - i.e. caller MUST NOT call again!
    /// @note MUST NOT BE CALLED when the stack is empty (i.e. sp==stack.end())
    /// @note this is a dispatcher to call one of the branches below
    /// @note all routines called must have the same signature as resumeEvaluation()
    virtual bool resumeEvaluation();

    /// expression processing resume entry points
    bool resumeExpression();
    bool resumeGroupedExpression();
    bool resumeTerm();

    /// @}


    /// @name Stack management
    /// @{

    /// switch state in the current stack frame
    /// @param aNewState the new state to switch the current stack frame to
    /// @return true for convenience to be used in non-yieled returns
    bool newstate(EvalState aNewState);

    void extracted();

    /// push new stack frame
    /// @param aNewState the new stack frame's state
    /// @param aStartSkipping if set, new stack frame will have skipping set, otherwise it will inherit previous frame's skipping value
    /// @return true for convenience to be used in non-yieled returns
    bool push(EvalState aNewState, bool aStartSkipping = false);

    /// pop current stack frame
    /// @return true for convenience to be used in non-yieled returns
    bool pop();

    /// pop current stack frame and pass down a result
    /// @return true for convenience to be used in non-yieled returns
    bool popAndPassResult(ExpressionValue aResult);

    /// pop stack frames down to the last frame in aPreviousState
    /// @param aPreviousState the state we are looking for
    /// @return true when frame was found, false if not (which means stack remained untouched)
    bool popToLast(EvalState aPreviousState);

    /// dump the stack (when eval logging is at debug level)
    void logStackDump();

    /// @}


    /// @name code parsing utilities
    /// @{

    /// get char at position
    /// @param aPos position
    /// @return character at aPos, or 0 if aPos is out of range
    char code(size_t aPos) const { if (aPos>=codeString.size()) return 0; else return codeString[aPos]; }

    /// @return current character (at pos), or 0 if at end of code
    char currentchar() const { return code(pos); }

    /// skip non-code (whitespace and comments)
    void skipNonCode();
    void skipNonCode(size_t& aPos);

    /// @param aPos position
    /// @return c_str of the tail starting at aPos
    const char *tail(size_t aPos) const { if (aPos>codeString.size()) aPos=codeString.size(); return codeString.c_str()+aPos; }

    /// check for and get identifier
    /// @param aLen return size of identifier found (0 if none)
    /// @return pointer to start of identifier, or NULL if none found
    const char* getIdentifier(size_t& aLen);

    /// parse operator (also see EXPRESSION_OPERATOR_MODE definition)
    Operations parseOperator(size_t &aPos);

    /// parse a numeric literal (includes our special time and date formats)
    /// @param aResult will receive the result of the parsing
    /// @param aCode the code to parse within
    /// @param aPos the position of the literal to parse within the code
    static void parseNumericLiteral(ExpressionValue &aResult, const char* aCode, size_t& aPos);

    /// get value by (possibly structured) identifier
    /// @param aVariableMapP pointer to the variable map to look into
    /// @param aVarPath the variable, possibly a dot-separated path into a structured variable to look up (any case, comparison must be case insensitive)
    /// @param aResult set the value here
    /// @return true if value returned, false if value is unknown
    static bool variableLookup(VariablesMap *aVariableMapP, const string &aVarPath, ExpressionValue &aResult);

    /// @}


    /// @name API for timed evaluation and freezing values in functions that can be used in timed evaluations
    /// @note base class does not actually implement freezing, but API with dummy functionalizy
    ///   is defined here so function evaluation can use without knowing the context they will execute in
    /// @{

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression - pos member must be set!
    ///   On return: replaced by a frozen result, if one exists
    /// @param aRefPos the reference position that identifies the frozen result
    virtual FrozenResult* getFrozen(ExpressionValue &aResult, size_t aRefPos) { return NULL; /* base class has no frozen results */ }

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aRefPos te reference position that identifies the frozen result
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    virtual FrozenResult* newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, size_t aRefPos, MLMicroSeconds aFreezeUntil, bool aUpdate = false) { return NULL; /* base class cannot freeze */ }

    /// unfreeze frozen value at aAtPos
    /// @param aAtPos the starting character index of the subexpression to unfreeze
    /// @return true if there was a frozen result at aAtPos
    virtual bool unfreeze(size_t aAtPos) { return false; /* base class has no frozen results */ }

    /// Set time when next evaluation must happen, latest
    /// @param aLatestEval new time when evaluation must happen latest, Never if no next evaluation is needed
    /// @return true if aNextEval was updated
    bool updateNextEval(const MLMicroSeconds aLatestEval);
    /// @param aLatestEvalTm new local broken down time when evaluation must happen latest
    /// @return true if aNextEval was updated
    bool updateNextEval(const struct tm& aLatestEvalTm);

    /// @}

  private:

    static bool selfKeepingContinueEvaluation(EvaluationContextPtr aContext);

  };


  #if EXPRESSION_SCRIPT_SUPPORT

  class ScriptGlobals
  {
    friend class ScriptExecutionContext;
    friend class EvaluationContext;

  public:

    VariablesMap globalVariables; ///< global variables
    EvaluationContext::FunctionCBList globalFunctionCallbacks; ///< global function callbacks

    static bool exists();
    static ScriptGlobals& sharedScriptGlobals();

    /// register additional (synchronous) function handler
    /// @note function handlers will be called to check for functions in the order they are registered, before
    ///   checking the class hierarchy's built-in functions
    void registerFunctionHandler(FunctionLookupCB aFunctionLookupHandler);

  };

  static ScriptGlobals *scriptGlobals = NULL;



  // execution of scripts
  class ScriptExecutionContext : public EvaluationContext
  {
    typedef EvaluationContext inherited;

  protected:

    VariablesMap variables; ///< script local variables

  public:

    ScriptExecutionContext(const GeoLocation* aGeoLocationP = NULL);
    virtual ~ScriptExecutionContext();

    /// execute a script
    /// @param aOneTimeEvaluationResultHandler is called when the script completes or is aborted, then the handler is cleared
    /// @return true if script has not yielded control (i.e. completed running synchronously)
    /// @note always calls the evaluation result handler (aOneTimeEvaluationResultHandler if set,
    ///   or a permanent one set with setEvaluationResultHandler()
    bool execute(bool aAsynchronously, EvaluationResultCB aOneTimeEvaluationResultHandler = NULL);

    /// continue execution of the current script text in the 
    /// @param aChainResultHandler is called when the chained script part completes or is aborted
    /// @return true if chained script has not yielded control (i.e. completed running synchronously)
    /// @note always calls the evaluation result handler (aOneTimeEvaluationResultHandler if set,
    ///   or a permanent one set with setEvaluationResultHandler()
    bool chainContext(ScriptExecutionContext& aTargetContext, EvaluationResultCB aChainResultHandler);

    /// release all evaluation state (variables)
    virtual void releaseState() P44_OVERRIDE;

    /// lookup variables by name
    /// @param aName the name of the value/variable to look up (any case, comparison must be case insensitive)
    /// @param aResult set the value here
    /// @return true if value returned, false if value is unknown
    virtual bool valueLookup(const string &aName, ExpressionValue &aResult)  P44_OVERRIDE;

  protected:

    /// @name Evaluation state machine
    /// @{

    /// Initialisation: start new script execution
    /// @note context-global evaluation parameters must be set already
    /// @note after that, resumeEvaluation() must be called until isEvaluating() return false
    virtual bool startEvaluation() P44_OVERRIDE;

    /// resume evaluation where we left off when we last yielded
    virtual bool resumeEvaluation() P44_OVERRIDE;

    /// script processing resume entry points
    bool resumeStatements();
    bool resumeIfElse();
    bool resumeWhile();
    bool resumeAssignment();
    bool resumeTryCatch();

    /// @}
  };
  typedef boost::intrusive_ptr<ScriptExecutionContext> ScriptExecutionContextPtr;


  class ScriptQueue {
    typedef std::list<ScriptExecutionContextPtr> ScriptExecutionContextsList;
    ScriptExecutionContextsList queue;

  public:

    /// stop current script (and cause next to execute)
    /// @return true if there was a script to stop
    bool stopCurrent();

    /// clear queue, stop all scripts
    void clear();

    /// queue script for execution
    /// @param aScriptContext the script to run in sequence
    void queueScript(ScriptExecutionContextPtr aScriptContext);

  private:
    void runNextScript();
    void scriptDone(ScriptExecutionContextPtr aScript);
  };


  #endif // EXPRESSION_SCRIPT_SUPPORT


  // evaluation with time related functions that must trigger re-evaluations when used
  class TimedEvaluationContext : public EvaluationContext
  {
    typedef EvaluationContext inherited;

    typedef std::map<size_t, FrozenResult> FrozenResultsMap;
    FrozenResultsMap frozenResults; ///< map of expression starting indices and associated frozen results

    MLTicket reEvaluationTicket; ///< ticket for re-evaluation timer

  public:

    TimedEvaluationContext(const GeoLocation* aGeoLocationP);
    virtual ~TimedEvaluationContext();

    /// evaluate expression right now, return result
    /// @param aEvalMode if specified, the evaluation mode for this evaluation. Defaults to current evaluation mode.
    /// @param aScheduleReEval if true, re-evaluations as demanded by evaluated expression are scheduled (NOP in base class)
    /// @param aCallBack if true, result callback (if one is installed) is called (in addition to returning result directly
    /// @return expression result
    virtual ExpressionValue evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval = false, bool aCallBack = false) P44_OVERRIDE;

    /// schedule latest re-evaluation time. If an earlier evaluation time is already scheduled, nothing will happen
    void scheduleLatestEvaluation(MLMicroSeconds aAtTime);

    /// schedule a re-evaluation at given time
    /// @note this will cancel a possibly already scheduled re-evaluation unconditionally
    void scheduleReEvaluation(MLMicroSeconds aAtTime);

    /// timed context specific functions
    virtual bool evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult) P44_OVERRIDE;

  protected:

    /// release all evaluation state (such as frozen subexpressions)
    virtual void releaseState() P44_OVERRIDE;

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression - pos member must be set!
    ///   On return: replaced by a frozen result, if one exists
    virtual FrozenResult* getFrozen(ExpressionValue &aResult, size_t aRefPos) P44_OVERRIDE;

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    virtual FrozenResult* newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, size_t aRefPos, MLMicroSeconds aFreezeUntil, bool aUpdate = false) P44_OVERRIDE;

    /// unfreeze frozen value at aAtPos
    /// @param aAtPos the starting character index of the subexpression to unfreeze
    /// @return true if there was a frozen result at aAtPos
    virtual bool unfreeze(size_t aAtPos) P44_OVERRIDE;

  private:

    void timedEvaluationHandler(MLTimer &aTimer, MLMicroSeconds aNow);

  };
  typedef boost::intrusive_ptr<TimedEvaluationContext> TimedEvaluationContextPtr;


  #if EXPRESSION_LEGACY_PLACEHOLDERS

  /// callback function for obtaining string variables
  /// @param aValue the contents of this is looked up and possibly replaced
  /// @return ok or error
  typedef boost::function<ErrorPtr (const string aName, string &aValue)> StringValueLookupCB;

  /// substitute "@{xxx}" type placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to get variables resolved into values
  ErrorPtr substitutePlaceholders(string &aString, StringValueLookupCB aValueLookupCB);

  #endif


#endif // #if 0

}} // namespace p44::Script

#endif // ENABLE_SCRIPTING

#endif // defined(__p44utils__scripting__)
