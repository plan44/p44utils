//
//  Copyright (c) 2017-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__expressions__
#define __p44utils__expressions__

#include "p44utils_common.hpp"
#include "timeutils.hpp"
#include <string>

#ifndef EXPRESSION_SCRIPT_SUPPORT
  #define EXPRESSION_SCRIPT_SUPPORT 1 // on by default
#endif


using namespace std;

namespace p44 {

  /// Expression Error
  class ExpressionError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Null,
      Syntax,
      DivisionByZero,
      CyclicReference,
      Busy,
      NotFound, ///< variable, object, function not found (for callback)
    } ErrorCodes;
    static const char *domain() { return "ExpressionError"; }
    virtual const char *getErrorDomain() const { return ExpressionError::domain(); };
    ExpressionError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    /// factory method to create string error fprint style
    static ErrorPtr err(ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
    static ErrorPtr null() { return err(ExpressionError::Null, "undefined"); }
  };

  /// expression value, consisting of a value and an error to indicate non-value and reason for it
  class ExpressionValue {
    string* strValP; ///< string values have a std::string here
    double numVal;
  public:
    size_t pos; ///< starting position in expression string (for function arguments and subexpressions)
    ErrorPtr err;
    ExpressionValue() : numVal(0), pos(0), strValP(NULL) { withError(ExpressionError::Null, "undefined"); };
    ExpressionValue(double aNumValue) : numVal(aNumValue), pos(0), strValP(NULL) { };
    ExpressionValue(const string &aStrValue) : numVal(0), pos(0), strValP(new string(aStrValue)) { };
    ExpressionValue(const ExpressionValue& aVal); ///< copy constructor
    ExpressionValue& operator=(const ExpressionValue& aVal); ///< assignment operator
    ~ExpressionValue();
    bool operator<(const ExpressionValue& aRightSide) const;
    bool operator==(const ExpressionValue& aRightSide) const;
    bool operator!=(const ExpressionValue& aRightSide) const;
    ExpressionValue operator+(const ExpressionValue& aRightSide) const;
    ExpressionValue operator-(const ExpressionValue& aRightSide) const;
    ExpressionValue operator*(const ExpressionValue& aRightSide) const;
    ExpressionValue operator/(const ExpressionValue& aRightSide) const;
    ExpressionValue operator&&(const ExpressionValue& aRightSide) const;
    ExpressionValue operator||(const ExpressionValue& aRightSide) const;
    void clrStr();
    void setNull() { err = ExpressionError::null(); }
    void setNumber(double aNumValue) { err.reset(); numVal = aNumValue; clrStr(); }
    void setBool(bool aBoolValue) { err.reset(); numVal = aBoolValue ? 1: 0; clrStr(); }
    void setString(const string& aStrValue) { err.reset(); numVal = 0; clrStr(); strValP = new string(aStrValue); }
    static ExpressionValue errValue(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
    static ExpressionValue nullValue() { return ExpressionValue().withError(ExpressionError::null()); }
    ExpressionValue withError(ErrorPtr aError) { err = aError; return *this; }
    ExpressionValue withError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)  __printflike(3,4);
    ExpressionValue withSyntaxError(const char *aFmt, ...)  __printflike(2,3);
    ExpressionValue withNumber(double aNumValue) { err.reset(); setNumber(aNumValue); return *this; }
    ExpressionValue withString(const string& aStrValue) { err.reset(); setString(aStrValue); return *this; }
    ExpressionValue withValue(const ExpressionValue &aExpressionValue) { numVal = aExpressionValue.numVal; err = aExpressionValue.err; if (aExpressionValue.strValP) setString(*aExpressionValue.strValP); return *this; }
    ExpressionValue withPos(size_t aPos) { pos = aPos; return *this; }
    bool isOk() const { return Error::isOK(err); }
    bool valueOk() const { return isOk() || isNull(); } ///< ok as a value, but can be NULL
    bool isNull() const { return Error::isError(err, ExpressionError::domain(), ExpressionError::Null); } ///< ok as a value, but can be NULL
    bool syntaxOk() const { return !Error::isError(err, ExpressionError::domain(), ExpressionError::Syntax); } ///< ok for calculations, not a syntax problem
    bool notOk() const { return !isOk(); }
    bool isString() const { return strValP!=NULL; }
    string stringValue() const; ///< returns a conversion to string if value is numeric
    double numValue() const; ///< returns a conversion to numeric (using literal syntax), if value is string
    bool boolValue() const { return numValue()!=0; } ///< returns true if value is not 0
    int intValue() const { return (int)numValue(); }
    int64_t int64Value() const { return (int64_t)numValue(); }
  };


  /// callback function for obtaining variables
  /// @param aName the name of the value/variable to look up
  /// @param aResult set the value here
  /// @return true if value returned, false if value is unknown
  typedef boost::function<bool (const string &aName, ExpressionValue &aResult)> ValueLookupCB;


  typedef std::vector<ExpressionValue> FunctionArgumentVector;

/// callback function for function evaluation
  /// @param aFunc the name of the function to execute
  /// @param aArgs vector of function arguments, tuple contains expression starting position and value
  /// @param aResult set to function's result
  /// @return true if function executed, false if function signature (name, number of args) is unknown
  typedef boost::function<bool (const string& aFunc, const FunctionArgumentVector& aArgs, ExpressionValue& aResult)> FunctionLookupCB;

  /// evaluate expression
  /// @param aExpression the expression text
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @return the result of the expression
  ExpressionValue evaluateExpression(const string& aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB);

  /// substitute "@{xxx}" type expression placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @param aNullText this will be used as substitution for expressions with errors or null value
  /// @return returns first error occurred during substitutions. Note that only unbalanced substitution brackets @{xxx} abort substitution,
  ///    other errors just cause substitution result to be aNullText.
  ErrorPtr substituteExpressionPlaceholders(string& aString, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, string aNullText = "null");



  class EvaluationContext;

  /// Expression Evaluation Callback
  /// @param aEvaluationResult the evaluation result (can be error)
  /// @param aContext the evaluation context this result originates from
  /// @return ok, or error in case the result processing wants to pass on a evaluation error or an error of its own.
  typedef boost::function<ErrorPtr (ExpressionValue aEvaluationResult, EvaluationContext &aContext)> EvaluationResultCB;


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

  /// Basic Expression Evaluation Context
  class EvaluationContext : public P44Obj
  {
    friend class ExpressionValue;

  protected:

    // ######################## NEW

    // operations with precedence
    typedef enum {
      op_none       = 0x06,
      op_not        = 0x16,
      op_multiply   = 0x25,
      op_divide     = 0x35,
      op_add        = 0x44,
      op_subtract   = 0x54,
      op_equal      = 0x63,
      op_assignOrEq = 0x73,
      op_notequal   = 0x83,
      op_less       = 0x93,
      op_greater    = 0xA3,
      op_leq        = 0xB3,
      op_geq        = 0xC3,
      op_and        = 0xD2,
      op_or         = 0xE2,
      op_assign     = 0xF0,
      opmask_precedence = 0x0F
    } Operations;

    // Evaluation parameters (set before execution starts, not changed afterwards, no nested state)
    EvalMode evalMode; ///< the current evaluation mode
    const GeoLocation* geolocationP; ///< if set, the current geolocation (e.g. for sun time functions)
    string codeString; ///< the code to evaluate
    bool synchronous; ///< the code must run synchronously to the end, execution may NOT be yielded
    int evalLogLevel; ///< if set, processing the script will output log info

    /// @name  Evaluation state
    /// @{
    MLMicroSeconds runningSince; ///< when the current evaluation has started, Never otherwise
    size_t pos; ///< the scanning position within code
    ExpressionValue finalResult; ///< final result
    MLTicket execTicket; ///< a ticket for timed steps that are part of the execution

    typedef enum {
      s_body, ///< at the body level (end of expression ends body)
      //s_functionbody, ///< within the body of a user defined function
      s_block, ///< within a block, exists when '}' is encountered, but skips ';'
      s_oneStatement, ///< a single statement, exits when ';' is encountered
      s_noStatement, ///< pop back one level
      s_returnValue, ///< return value calculation

      s_ifCondition, ///< executing the condition of an if
      s_ifTrueStatement, ///< executing the if statement
      s_elseStatement, ///< executing the else statement

      s_assignToVar, ///< assign result of an expression to a variable

      s_newExpression, ///< at the beginning of an expression which only ends syntactically (end of code, delimiter, etc.) -> resets precedence
      s_expression, ///< handle (sub)expression start, precedence inherited or set by caller
      s_groupedExpression, ///< handling a paranthesized subexpression result
      s_exprFirstTerm, ///< first term of an expression
      s_exprLeftSide, ///< left side of an ongoing expression
      s_exprRightSide, ///< further terms of an expression

      s_simpleTerm, ///< at the beginning of a term
      s_funcArg, ///< handling a function argument
      s_funcExec, ///< handling function execution

      s_result, ///< result of an expression or term


      s_complete, ///< completing evaluation
      s_abort, ///< aborting evaluation

      s_finalize, ///< ending, will pop last stack frame

    } EvalState; ///< the state of the evaluation in the current frame

    // The stack
    class StackFrame {
    public:
      StackFrame(EvalState aState, bool aSkip, int aPrecedence) :
        state(aState), skipping(aSkip), flowDecision(false), precedence(aPrecedence), op(op_none)
      {}
      EvalState state; ///< current state
      int precedence; ///< encountering a binary operator with smaller precedence will end the expression
      Operations op; ///< operator
      string identifier; ///< identifier (e.g. variable name, function name etc.)
      bool skipping; ///< if set, we are just skipping code, not really executing
      bool flowDecision; ///< flow control decision
      FunctionArgumentVector args; ///< arguments
      ExpressionValue val; ///< private value for operations
      ExpressionValue res; ///< result value passed down at popAndPassResult()
    };
    typedef std::list<StackFrame> StackList;
    StackList stack; ///< the stack

    StackFrame &sp() { return stack.back(); } ///< current stackpointer

    /// @}

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

    /// @return true if currently evaluating an expression.
    bool isEvaluating() { return runningSince!=Never; }

    /// Main entry point / dispatcher - resume evaluation where we left off when we last yielded
    /// @return
    /// - true when execution has not yieled - i.e the caller MUST initiate the next call directly or indirectly
    /// - false when the execution has yielded and will be resumed by resumeEvaluation() - i.e. caller MUST NOT call again!
    /// @note MUST NOT BE CALLED when the stack is empty (i.e. sp==stack.end())
    /// @note this is a dispatcher to call one of the branches below
    /// @note all routines called must have the same signature as resumeEvaluation()
    virtual bool resumeEvaluation();

    /// expression processing resume points
    bool resumeExpression();
    bool resumeGroupedExpression();
    bool resumeTerm();

    /// @}


    /// get char at position
    /// @param aPos position
    /// @return character at aPos, or 0 if aPos is out of range
    char code(size_t aPos) const { if (aPos>=codeString.size()) return 0; else return codeString[aPos]; }

    /// @return current character (at pos), or 0 if at end of code
    char currentchar() const { return code(pos); }

    /// @param aPos position
    /// @return c_str of the tail starting at aPos
    const char *tail(size_t aPos) const { if (aPos>codeString.size()) aPos=codeString.size(); return codeString.c_str()+aPos; }


    /// switch state in the current stack frame
    /// @param aNewState the new state to switch the current stack frame to
    /// @return true for convenience to be used in non-yieled returns
    bool newstate(EvalState aNewState);

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



    /// set result to specified error and abort
    bool abortWithError(ErrorPtr aError);
    bool abortWithError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...) __printflike(3,4);
    bool abortWithSyntaxError(const char *aFmt, ...) __printflike(2,3);



    /// skip white space
    void skipWhiteSpace();
    void skipWhiteSpace(size_t& aPos);

    /// skip identifier
    /// @param aLen return size of identifier found (0 if none)
    /// @return pointer to start of identifier, or NULL if none found
    const char* getIdentifier(size_t& aLen);

    /// parse operator (also see EXPRESSION_OPERATOR_MODE definition)
    Operations parseOperator(size_t &aPos);


  public:



  protected:

    // ######################## OLD, but keep

    EvaluationResultCB evaluationResultHandler; ///< called when a evaluation started by triggerEvaluation() completes (includes re-evaluations)
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

    // ######################## OLD, get rid of, probably

  public:

    EvaluationContext(const GeoLocation* aGeoLocationP = NULL);
    virtual ~EvaluationContext();

    /// set re-evaluation callback
    /// @param aEvaluationResultHandler is called when a evaluation started by triggerEvaluation() completes
    ///   (which includes delayed re-evaluations the context triggers itself, e.g. when timed functions are called)
    void setEvaluationResultHandler(EvaluationResultCB aEvaluationResultHandler);

    /// set code to evaluate
    /// @param aCode set the expression to be evaluated in this context
    /// @note setting an expression that differs from the current one unfreezes any frozen arguments
    /// @return true if expression actually changed (not just set same expession again)
    bool setCode(const string aCode);

    /// get current code
    const char *getCode() { return codeString.c_str(); };

    /// evaluate code synchonously
    /// @param aEvalMode if specified, the evaluation mode for this evaluation. Defaults to current evaluation mode.
    /// @param aScheduleReEval if true, re-evaluations as demanded by evaluated expression are scheduled (NOP in base class)
    /// @return expression result
    /// @note does NOT trigger the evaluation result handler
    virtual ExpressionValue evaluateSynchronously(EvalMode aEvalMode = evalmode_unspecific, bool aScheduleReEval = false);

    /// trigger a (re-)evaluation
    /// @param aEvalMode the evaluation mode for this evaluation.
    /// @note evaluation result handler will be called when complete
    /// @return ok, or error if expression could not be evaluated
    ErrorPtr triggerEvaluation(EvalMode aEvalMode);

    static void skipWhiteSpace(const char *aExpr, size_t& aPos);
    static bool skipIdentifier(const char *aExpr, size_t& aPos);

  protected:

    /// @name to be overridden and enhanced in subclasses
    /// @{

    /// release all evaluation state (none in base class)
    virtual void releaseState() { /* NOP: no state in base class */ };

    /// lookup variables by name
    /// @param aName the name of the value/variable to look up
    /// @param aResult set the value here
    /// @return true if value returned, false if value is unknown
    virtual bool valueLookup(const string &aName, ExpressionValue &aResult);

    /// evaluation of synchronously implemented functions which immediately return a result
    /// @param aFunc the name of the function to execute
    /// @param aResult set the function result here
    /// @return true if function executed, false if function signature is unknown
    virtual bool evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult);

    /// evaluation of asynchronously implemented functions which may yield execution and resume later
    /// @param aFunc the name of the function to execute
    /// @param aNotYielded
    /// - true when execution has not yieled and the function evaluation is complete
    /// - false when the execution of the function has yielded and resumeEvaluation() will be called to complete
    /// @return true if function executed, false if function signature is unknown
    /// @note this method will not be called when context is set to execute synchronously, so these functions will not be available then.
    virtual bool evaluateAsyncFunction(const string &aFunc, const FunctionArgumentVector &aArgs, bool &aNotYielded);

    /// helper to exit evaluateFunction/evaluateAsyncFunction indicating a argument with an error
    bool errorInArg(ExpressionValue aArg, const char *aExtraPrefix = NULL);

    /// @}

    /// @name API for timed evaluation and freezing values in functions that can be used in timed evaluations
    /// @note base class does not actually implement freezing, but API with dummy functionalizy
    ///   is defined here so function evaluation can use without knowing the context they will execute in
    /// @{

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression - pos member must be set!
    ///   On return: replaced by a frozen result, if one exists
    virtual FrozenResult* getFrozen(ExpressionValue &aResult) { return NULL; /* base class has no frozen results */ }

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    virtual FrozenResult* newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, MLMicroSeconds aFreezeUntil, bool aUpdate = false) { return NULL; /* base class cannot freeze */ }

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


    /// evaluate (sub)expression
    /// @param aExpr the beginning of the entire expression (important for freezing subexpressions via their string position)
    /// @param aPos the current position of evaluation within aExpr
    /// @param aPrecedence encountering a operator with precedence lower or same as aPrecedence will stop parsing the expression
    /// @param aStopChars list of characters that stop the evaluation of an expression (e.g. to make argument processing stop at ')' and ','
    /// @param aNeedStopChar if set, one of the stopchars is REQUIRED and will be skipped. If not stopped by a stopchar, error is returned
    /// @param aEvalMode evaluation mode
    /// @return expression result
    ExpressionValue evaluateExpressionPrivate(const char *aExpr, size_t &aPos, int aPrecedence, const char *aStopChars, bool aNeedStopChar, EvalMode aEvalMode);

  private:

    static void parseNumericLiteral(ExpressionValue &res, const char* aExpr, size_t& aPos);

    static void evaluateNumericLiteral(ExpressionValue &res, const string &term);
    ExpressionValue evaluateTerm(const char *aExpr, size_t &aPos, EvalMode aEvalMode);

  };
  typedef boost::intrusive_ptr<EvaluationContext> EvaluationContextPtr;


  #if EXPRESSION_SCRIPT_SUPPORT

  // execution of scripts
  class ScriptExecutionContext : public EvaluationContext
  {
    typedef EvaluationContext inherited;

    typedef std::map<string, ExpressionValue> VariablesMap;
    VariablesMap variables;

  public:

    ScriptExecutionContext(const GeoLocation* aGeoLocationP = NULL);
    virtual ~ScriptExecutionContext();

    /// clear all variables
    void clearVariables();

  protected:

    /// lookup variables by name
    /// @param aName the name of the value/variable to look up
    /// @param aResult set the value here
    /// @return true if value returned, false if value is unknown
    virtual bool valueLookup(const string &aName, ExpressionValue &aResult)  P44_OVERRIDE;

    /// script context specific functions
    virtual bool evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult) P44_OVERRIDE;

    /// @name Evaluation state machine
    /// @{

    /// Initialisation: start new script execution
    /// @note context-global evaluation parameters must be set already
    /// @note after that, resumeEvaluation() must be called until isEvaluating() return false
    virtual bool startEvaluation() P44_OVERRIDE;

    /// resume evaluation where we left off when we last yielded
    virtual bool resumeEvaluation() P44_OVERRIDE;

    /// resume running one or a list of statements
    bool resumeStatements();

    /// resume a if / else statement
    bool resumeIfElse();

    /// resume a assignment
    bool resumeAssignment();

    /// @}


  private:


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
    /// @return expression result
    /// @note does NOT trigger the evaluation result handler
    virtual ExpressionValue evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval = false) P44_OVERRIDE;

    /// schedule latest re-evaluation time. If an earlier evaluation time is already scheduled, nothing will happen
    /// @note this will cancel a possibly already scheduled re-evaluation unconditionally
    void scheduleLatestEvaluation(MLMicroSeconds aAtTime);

    /// schedule a re-evaluation at given time
    /// @note this will cancel a possibly already scheduled re-evaluation unconditionally
    void scheduleReEvaluation(MLMicroSeconds aAtTime);

  protected:

    /// release all evaluation state (such as frozen subexpressions)
    virtual void releaseState() P44_OVERRIDE;

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression - pos member must be set!
    ///   On return: replaced by a frozen result, if one exists
    virtual FrozenResult* getFrozen(ExpressionValue &aResult) P44_OVERRIDE;

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    virtual FrozenResult* newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, MLMicroSeconds aFreezeUntil, bool aUpdate = false) P44_OVERRIDE;

    /// unfreeze frozen value at aAtPos
    /// @param aAtPos the starting character index of the subexpression to unfreeze
    /// @return true if there was a frozen result at aAtPos
    virtual bool unfreeze(size_t aAtPos) P44_OVERRIDE;

    /// timed context specific functions
    virtual bool evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult) P44_OVERRIDE;

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


} // namespace p44



#endif // defined(__p44utils__expressions__)
