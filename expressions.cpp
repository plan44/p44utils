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

#include "expressions.hpp"

#if ENABLE_EXPRESSIONS

#include "math.h"

using namespace p44;


// MARK: - expression value

// copy constructor
ExpressionValue::ExpressionValue(const ExpressionValue& aVal) :
  strValP(NULL)
{
  *this = aVal;
}

// assignment operator
ExpressionValue& ExpressionValue::operator=(const ExpressionValue& aVal)
{
  nullValue = aVal.nullValue;
  numVal = aVal.numVal;
  err = aVal.err;
  clrStr();
  if (aVal.strValP) {
    strValP = new string(*aVal.strValP);
  }
  #if EXPRESSION_JSON_SUPPORT
  json = aVal.json;
  #endif
  return *this;
}

void ExpressionValue::clrStr()
{
  if (strValP) delete strValP;
  strValP = NULL;
}


void ExpressionValue::clrExtensions()
{
  clrStr();
  err.reset();
  #if EXPRESSION_JSON_SUPPORT
  json.reset();
  #endif
}



ExpressionValue::~ExpressionValue()
{
  clrExtensions();
}


void ExpressionValue::setError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)
{
  nullValue = false;
  err = new ExpressionError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
}


void ExpressionValue::setSyntaxError(const char *aFmt, ...)
{
  nullValue = false;
  err = new ExpressionError(ExpressionError::Syntax);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
}


string ExpressionValue::stringValue() const
{
  if (isString()) {
    return *strValP;
  }
  else if (isNull()) {
    if (strValP) return *strValP; // null value with info
    return "undefined";
  }
  #if EXPRESSION_JSON_SUPPORT
  else if (json) {
    return json->json_str();
  }
  #endif
  else if (err) {
    return err->description();
  }
  else {
    return string_format("%lg", numVal);
  }
}


double ExpressionValue::numValue() const
{
  if (isString()) {
    ExpressionValue v(0);
    size_t lpos = 0;
    EvaluationContext::parseNumericLiteral(v, strValP->c_str(), lpos);
    return v.numVal;
  }
  else if (isNull()) {
    return 0;
  }
  #if EXPRESSION_JSON_SUPPORT
  else if (json) {
    return json->doubleValue();
  }
  #endif
  else if (err) {
    return err->getErrorCode();
  }
  else {
    return numVal;
  }
}


#if EXPRESSION_JSON_SUPPORT

void ExpressionValue::setJson(JsonObjectPtr aJson)
{
  // try to break down to non-JSON
  if (!aJson || aJson->isType(json_type_null)) {
    setNull();
    return;
  }
  switch (aJson->type()) {
    case json_type_int:
    case json_type_double:
      setNumber(aJson->doubleValue());
      break;
    case json_type_boolean:
      setBool(aJson->boolValue());
      break;
    case json_type_string:
      setString(aJson->stringValue());
      break;
    default:
      // strings and arrays
      clrExtensions();
      json = aJson;
      nullValue = false;
      break;
  }
}


JsonObjectPtr ExpressionValue::jsonValue(ErrorPtr *errP) const
{
  if (json) {
    return json;
  }
  else if (isNull()) {
    return JsonObject::newNull();
  }
  else if (isError()) {
    JsonObjectPtr j = JsonObject::newObj();
    j->add("ErrorCode", JsonObject::newInt32((int32_t)err->getErrorCode()));
    j->add("ErrorDomain", JsonObject::newString(err->getErrorDomain()));
    j->add("ErrorMessage", JsonObject::newString(err->getErrorMessage()));
    return j;
  }
  else if (isString()) {
    const char *p = strValP->c_str();
    if (*p=='{' || *p=='[') {
      // try to parse as JSON
      ErrorPtr jerr;
      JsonObjectPtr j = JsonObject::objFromText(p, strValP->size(), &jerr, false);
      if (Error::isOK(jerr)) return j;
      if (errP) *errP = jerr;
    }
    // plain string
    return JsonObject::newString(*strValP);
  }
  else {
    return JsonObject::newDouble(numVal);
  }
}


ExpressionValue ExpressionValue::subField(const string aFieldName)
{
  ExpressionValue ret;
  JsonObjectPtr j = jsonValue();
  if (j && j->isType(json_type_object)) {
    JsonObjectPtr sf = j->get(aFieldName.c_str());
    ret.setJson(sf);
  }
  return ret;
}


ExpressionValue ExpressionValue::arrayElement(int aArrayIndex)
{
  ExpressionValue ret;
  JsonObjectPtr j = jsonValue();
  if (j && j->isType(json_type_array)) {
    JsonObjectPtr sf = j->arrayGet(aArrayIndex);
    ret.setJson(sf);
  }
  return ret;
}


ExpressionValue ExpressionValue::subScript(const ExpressionValue& aSubScript)
{
  if (aSubScript.isString()) {
    return subField(aSubScript.stringValue());
  }
  else {
    return arrayElement(aSubScript.intValue());
  }
}


#endif // EXPRESSION_JSON_SUPPORT




ExpressionValue ExpressionValue::operator!() const
{
  ExpressionValue res;
  res.setBool(!boolValue());
  return res;
}



ExpressionValue ExpressionValue::operator<(const ExpressionValue& aRightSide) const
{
  if (isString()) return *strValP < aRightSide.stringValue();
  return numVal < aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator!=(const ExpressionValue& aRightSide) const
{
  return !operator==(aRightSide);
}

ExpressionValue ExpressionValue::operator==(const ExpressionValue& aRightSide) const
{
  ExpressionValue res;
  if (isString()) res.setBool(*strValP == aRightSide.stringValue());
  else res.setBool(numVal == aRightSide.numValue());
  return res;
}

ExpressionValue ExpressionValue::operator+(const ExpressionValue& aRightSide) const
{
  if (isString()) return *strValP + aRightSide.stringValue();
  return numVal + aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator-(const ExpressionValue& aRightSide) const
{
  return numValue() - aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator*(const ExpressionValue& aRightSide) const
{
  return numValue() * aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator/(const ExpressionValue& aRightSide) const
{
  ExpressionValue res;
  if (aRightSide.numValue()==0) {
    res.setError(ExpressionError::DivisionByZero, "division by zero");
  }
  else {
    res.setNumber(numValue() / aRightSide.numValue());
  }
  return res;
}

ExpressionValue ExpressionValue::operator&&(const ExpressionValue& aRightSide) const
{
  return numValue() && aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator||(const ExpressionValue& aRightSide) const
{
  return numValue() || aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator>=(const ExpressionValue& aRightSide) const
{
  return !operator<(aRightSide);
}

ExpressionValue ExpressionValue::operator>(const ExpressionValue& aRightSide) const
{
  return !operator<(aRightSide) && !operator==(aRightSide);
}

ExpressionValue ExpressionValue::operator<=(const ExpressionValue& aRightSide) const
{
  return operator<(aRightSide) || operator==(aRightSide);
}





// MARK: - expression error


ErrorPtr ExpressionError::err(ErrorCodes aErrCode, const char *aFmt, ...)
{
  Error *errP = new ExpressionError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args);
  va_end(args);
  return ErrorPtr(errP);
}


// MARK: - EvaluationContext

#define DEFAULT_EXEC_TIME_LIMIT (Infinite)
#define DEFAULT_SYNC_EXEC_LIMIT (10*Second)
#define DEFAULT_SYNC_RUN_TIME (50*MilliSecond)

EvaluationContext::EvaluationContext(const GeoLocation* aGeoLocationP) :
  geolocationP(aGeoLocationP),
  runningSince(Never),
  nextEvaluation(Never),
  synchronous(true),
  evalLogLevel(FOCUSLOGGING ? FOCUSLOGLEVEL : LOG_INFO), // default to focus level if any, LOG_INFO normally
  execTimeLimit(DEFAULT_EXEC_TIME_LIMIT),
  syncExecLimit(DEFAULT_SYNC_EXEC_LIMIT),
  syncRunTime(DEFAULT_SYNC_RUN_TIME),
  oneTimeResultHandler(false),
  callBack(false)
{
}


EvaluationContext::~EvaluationContext()
{
}


void EvaluationContext::setEvaluationResultHandler(EvaluationResultCB aEvaluationResultHandler)
{
  evaluationResultHandler = aEvaluationResultHandler;
  oneTimeResultHandler = false;
}


void EvaluationContext::registerFunctionHandler(FunctionLookupCB aFunctionLookupHandler)
{
  functionCallbacks.push_back(aFunctionLookupHandler);
}



bool EvaluationContext::setCode(const string aCode)
{
  if (aCode!=codeString) {
    abort(); // asynchronously running script, if any, will abort when EvaluationContext gets control next time
    releaseState(); // changing expression unfreezes everything
    codeString = aCode;
    return true;
  }
  return false;
}


void EvaluationContext::skipNonCode(size_t& aPos)
{
  bool recheck;
  do {
    recheck = false;
    while (code(aPos)==' ' || code(aPos)=='\t' || code(aPos)=='\n' || code(aPos)=='\r') aPos++;
    // also check for comments
    if (code(pos)=='/') {
      if (code(pos+1)=='/') {
        pos += 2;
        // C++ style comment, lasts until EOT or EOL
        while (code(aPos) && code(aPos)!='\n' && code(aPos)!='\r') aPos++;
        recheck = true;
      }
      else if (code(pos+1)=='*') {
        // C style comment, lasts until '*/'
        pos += 2;
        while (code(aPos) && code(aPos)!='*') aPos++;
        if (code(aPos+1)=='/') {
          pos += 2;
        }
        recheck = true;
      }
    }
  } while(recheck);
}


void EvaluationContext::skipNonCode()
{
  skipNonCode(pos);
}


const char* EvaluationContext::getIdentifier(size_t& aLen)
{
  aLen = 0;
  const char* p = tail(pos);
  if (!isalpha(*p)) return NULL; // is ot an identifier
  // is identifier
  const char* b = p; // begins here
  p++;
  while (*p && (isalnum(*p) || *p=='_')) p++;
  aLen = p-b;
  return b;
}


static const char *evalstatenames[EvaluationContext::numEvalStates] = {
  "s_unwound",
  "s_complete",
  "s_abort",
  "s_finalize",
  // Script States
  // - basic statements
  "s_body",
  "s_block",
  "s_oneStatement",
  "s_noStatement",
  "s_returnValue",
  // - if/then/else
  "s_ifCondition",
  "s_ifTrueStatement",
  "s_elseStatement",
  // - while
  "s_whileCondition",
  "s_whileStatement",
  // - try/catch
  "s_tryStatement",
  "s_catchStatement",
  // - assignment to variables
  "s_assignToVar",
  // Special result passing state
  "s_result",
  // Expression States
  "s_newExpression",
  "s_expression",
  "s_groupedExpression",
  "s_exprFirstTerm",
  "s_exprLeftSide",
  "s_exprRightSide",
  // - simple terms
  "s_simpleTerm",
  "s_funcArg",
  "s_funcExec",
  "s_subscriptArg",
  "s_subscriptExec"
};


bool EvaluationContext::newstate(EvalState aNewState)
{
  sp().state = aNewState;
  return true; // not yielded
}


void EvaluationContext::logStackDump() {
  StackList::iterator spos = stack.end();
  size_t spidx = stack.size();
  while (spos!=stack.begin()) {
    spos--;
    ELOG_DBG("- %zu: %s state=%s, identifier='%s', code(sp->pos): '%.50s...'",
      spidx,
      spos->skipping ? "SKIPPING" : "running ",
      evalstatenames[spos->state],
      spos->identifier.c_str(),
      tail(spos->pos)
    );
    spidx--;
  }
}

bool EvaluationContext::push(EvalState aNewState, bool aStartSkipping)
{
  skipNonCode();
  stack.push_back(StackFrame(aNewState, aStartSkipping || sp().skipping, sp().precedence));
  if (ELOGGING_DBG) {
    ELOG_DBG("+++ pushed new frame - code(pos): '%.50s...'", tail(pos));
    logStackDump();
  }
  return true; // not yielded
}


bool EvaluationContext::pop()
{
  skipNonCode();
  if (stack.size()>1) {
    // regular pop
    stack.pop_back();
    if (ELOGGING_DBG) {
      ELOG_DBG("--- popped frame - code(pos): '%.50s...'", tail(pos));
      logStackDump();
    }
    return true; // not yielded
  }
  // trying to pop last entry - switch to complete/abort first
  if (isEvaluating()) {
    return newstate(sp().res.isOK() ? s_unwound : s_abort);
  }
  return true; // not yielded
}


bool EvaluationContext::popAndPassResult(ExpressionValue aResult)
{
  bool wasSkipping = sp().skipping;
  pop();
  if (stack.empty())
    finalResult = aResult;
  else if (!wasSkipping) {
    // auto-throw error results if:
    // - it is a syntax error
    // - between complete expressions/statements
    if (aResult.isError() && (sp().state<s_expression_states || !aResult.syntaxOk())) {
      ELOG("Statement result passed to %s: '%.*s' is error -> THROW: %s", evalstatenames[sp().state],  (int)(pos-sp().pos), tail(sp().pos), aResult.error()->text());
      return throwError(aResult.error());
    }
    ELOG_DBG("Result passed to %s: '%.*s' is '%s'", evalstatenames[sp().state],  (int)(pos-sp().pos), tail(sp().pos), aResult.stringValue().c_str());
    sp().res = aResult;
  }
  return true; // not yielded
}


bool EvaluationContext::popToLast(EvalState aPreviousState)
{
  StackList::iterator spos = stack.end();
  while (spos!=stack.begin()) {
    spos--;
    if (spos->state==aPreviousState) {
      // found, pop everything on top
      stack.erase(++spos, stack.end());
      return true;
    }
  }
  return false;
}


bool EvaluationContext::throwError(ErrorPtr aError)
{
  // search back the stack for a "try"
  StackList::iterator spos = stack.end();
  while (spos!=stack.begin()) {
    spos--;
    if (spos->state==s_tryStatement) {
      // set the decision
      spos->flowDecision = true; // error caught
      spos->res.setError(aError); // store the error at the try/catch stack level for later referencing in catch
      // set everything up to here to skip...
      while (++spos!=stack.end()) {
        spos->skipping = true;
      }
      // ...and let it run (skip)
      return true;
    }
  }
  // uncaught, abort
  sp().res.setError(aError);
  return newstate(s_abort);
}


bool EvaluationContext::throwError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)
{
  ErrorPtr err = Error::err<ExpressionError>(aErrCode);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  return throwError(err);
}


bool EvaluationContext::abortWithSyntaxError(const char *aFmt, ...)
{
  ErrorPtr err = Error::err<ExpressionError>(ExpressionError::Syntax);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  sp().res.err = err;
  return newstate(s_abort);
}



bool EvaluationContext::errorInArg(const ExpressionValue& aArg, bool aReturnIfNull)
{
  ExpressionValue res;
  errorInArg(aArg, res);
  if (aArg.isNull() && aReturnIfNull) continueWithAsyncFunctionResult(res);
  return true; // function known
}


bool EvaluationContext::errorInArg(const ExpressionValue& aArg, ExpressionValue& aResult)
{
  if (aArg.isNull()) {
    // function cannot process nulls -> just pass null as result
    aResult = aArg;
  }
  else if (aArg.error()) {
    // argument is in error -> throw
    throwError(aArg.error());
  }
  else {
    // not null and not error, consider it invalid value
    throwError(ExpressionError::err(ExpressionError::Invalid, "Invalid argument: %s", aArg.stringValue().c_str()));
  }
  return true; // function known
}




bool EvaluationContext::startEvaluationWith(EvalState aState)
{
  if (runningSince!=Never) {
    LOG(LOG_WARNING, "Already evaluating (since %s) -> cannot start again: '%s'", MainLoop::string_mltime(runningSince).c_str(), getCode());
    return false; // MUST NOT call again!
  }
  // can start
  runningSince = MainLoop::now();
  // reset state
  stack.clear();
  finalResult.setNull();
  nextEvaluation = Never;
  pos = 0;
  // push first frame with initial values
  stack.push_back(StackFrame(aState, evalMode==evalmode_syntaxcheck, 0));
  return true;
}


bool EvaluationContext::startEvaluation()
{
  return startEvaluationWith(s_newExpression);
}


bool EvaluationContext::continueEvaluation()
{
  if (!isEvaluating()) {
    LOG(LOG_WARNING, "EvaluationContext: cannot continue, script was already aborted asynchronously, code = %.40s...", getCode());
    return true; // already ran to end
  }
  MLMicroSeconds syncRunSince = MainLoop::now();
  MLMicroSeconds now = syncRunSince;
  MLMicroSeconds limit = synchronous ? syncExecLimit : execTimeLimit;
  while(isEvaluating()) {
    if (limit!=Infinite && now-runningSince>limit) {
      throwError(ExpressionError::Timeout, "Script ran too long -> aborted");
      limit = Infinite; // must not throw another error
      // but must run to end, so don't exit here!
    }
    if (!resumeEvaluation()) return false; // execution yielded anyway
    if (isEvaluating()) {
      // not yielded yet, and still evaluating after resumeEvaluation(): check run time
      now = MainLoop::now();
      if (!synchronous && syncRunTime!=Infinite && now-syncRunSince>syncRunTime) {
        // is an async script, yield now and continue later
        // - yield execution for twice the time we were allowed to run
        execTicket.executeOnce(boost::bind(&EvaluationContext::continueEvaluation, this), 2*DEFAULT_SYNC_RUN_TIME);
        // - yield now
        return false;
      }
    }
  }
  return true; // ran to end without yielding
}


bool EvaluationContext::returnFunctionResult(const ExpressionValue& aResult)
{
  if (stack.size()>=1) {
    sp().res = aResult;
  }
  return true;
}


void EvaluationContext::continueWithAsyncFunctionResult(const ExpressionValue& aResult)
{
  if (!isEvaluating()) {
    LOG(LOG_WARNING, "Asynchronous function call ended after calling script was already aborted");
    return;
  }
  returnFunctionResult(aResult);
  continueEvaluation();
}


bool EvaluationContext::runCallBack(const ExpressionValue &aResult)
{
  if (evaluationResultHandler && callBack) {
    EvaluationResultCB cb = evaluationResultHandler;
    if (oneTimeResultHandler) evaluationResultHandler = NULL;
    // this is where cyclic references could cause re-evaluation
    // - if running synchronously, keep evaluation in running state until callback is done to prevent tight loops
    // - if running asynchronously, consider script terminated already
    if (!synchronous) runningSince = Never;
    cb(aResult, *this);
    return true; // called back
  }
  return false; // not called back
}


bool EvaluationContext::abort(bool aDoCallBack)
{
  if (!isEvaluating()) return true; // already aborted / not running in the first place
  if (synchronous) {
    // apparently, this call is from within synchronous execution (no "outside" code flow can exist)
    LOG(LOG_WARNING, "Evaluation: abort() called from within synchronous script -> probably implementation problem");
    newstate(s_abort); // cause abort at next occasion, i.e. when caller returns back to state machine dispatcher
    return true;
  }
  // asynchronous execution
  execTicket.cancel(); // abort pending callback (e.g. from delay())
  finalResult.setError(Error::err<ExpressionError>(ExpressionError::Aborted, "asynchronously running script aborted"));
  stack.clear();
  if (aDoCallBack) runCallBack(finalResult);
  runningSince = Never; // force end
  ELOG("Evaluation: asynchronous execution aborted (by external demand)");
  return false; // not sure
}


ExpressionValue EvaluationContext::evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval, bool aCallBack)
{
  synchronous = true; // force synchronous operation, disable functionality that would need yielding execution
  callBack = aCallBack;
  evalMode = aEvalMode;
  if (runningSince!=Never) {
    LOG(LOG_WARNING, "Another evaluation is running (since %s) -> cannot start synchronous evaluation: '%s'", MainLoop::string_mltime(runningSince).c_str(), getCode());
    ExpressionValue res;
    res.setError(ExpressionError::Busy, "Evaluation busy since %s -> cannot start now", MainLoop::string_mltime(runningSince).c_str());
    return res;
  }
  bool notYielded = startEvaluation();
  if (notYielded && continueEvaluation()) {
    // has run to end
    return finalResult;
  }
  // FATAL ERROR: has yielded execution
  LOG(LOG_CRIT, "EvaluationContext: state machine has yielded execution while synchronous is set -> implementation error!");
  finalResult.setError(ExpressionError::Busy, "state machine has yielded execution while synchronous is set -> implementation error!");
  return finalResult;
}


ErrorPtr EvaluationContext::triggerEvaluation(EvalMode aEvalMode)
{
  ErrorPtr err;
  if (!evaluationResultHandler) {
    LOG(LOG_WARNING, "triggerEvaluation() with no result handler for expression: '%s'", getCode());
  }
  evaluateSynchronously(aEvalMode, true, true);
  if (finalResult.notValue()) err = finalResult.err;
  return err;
}



// MARK: - re-evaluation timing mechanisms

bool EvaluationContext::updateNextEval(const MLMicroSeconds aLatestEval)
{
  if (aLatestEval==Never || aLatestEval==Infinite) return false; // no next evaluation needed, no need to update
  if (nextEvaluation==Never || aLatestEval<nextEvaluation) {
    // new time is more recent than previous, update
    nextEvaluation = aLatestEval;
    return true;
  }
  return false;
}


bool EvaluationContext::updateNextEval(const struct tm& aLatestEvalTm)
{
  MLMicroSeconds latestEval = MainLoop::localTimeToMainLoopTime(aLatestEvalTm);
  return updateNextEval(latestEval);
}



// MARK: - Evaluation State machine

bool EvaluationContext::resumeEvaluation()
{
  if (runningSince==Never) {
    LOG(LOG_ERR, "resumeEvaluation() while not started");
    return false; // DO NOT CALL AGAIN!
  }
  switch (sp().state) {
    // completion states
    case s_unwound:
      // check for actual end of text
      skipNonCode();
      if (currentchar()) {
        sp().res.setSyntaxError("Unexpected character '%c'", currentchar());
        return newstate(s_abort);
      }
      // otherwise, treat like complete
    case s_complete:
      ELOG("Evaluation: execution completed");
      return newstate(s_finalize);
    case s_abort:
      ELOG("Evaluation: execution aborted (from within script)");
      return newstate(s_finalize);
    case s_finalize:
      finalResult = sp().res;
      if (ELOGGING) {
        string errInd;
        if (!finalResult.syntaxOk()) {
          errInd = "\n                ";
          errInd.append(pos, '-');
          errInd += '^';
        }
        ELOG("- code        = %s%s", getCode(), errInd.c_str());
        ELOG("- finalResult = %s - err = %s", finalResult.stringValue().c_str(), Error::text(finalResult.err));
      }
      stack.clear();
      execTicket.cancel(); // really stop here
      runCallBack(finalResult); // call back if configured
      runningSince = Never;
      return true;
    // expression evaluation states
    case s_newExpression:
    case s_expression:
    case s_exprFirstTerm:
    case s_exprLeftSide:
    case s_exprRightSide:
    case s_subscriptArg:
    case s_subscriptExec:
      return resumeExpression();
    // grouped expression
    case s_groupedExpression:
      return resumeGroupedExpression();
    case s_simpleTerm:
    case s_funcArg:
    case s_funcExec:
      return resumeTerm();
    // end of expressions, groups, terms
    case s_result:
      if (!sp().res.syntaxOk()) {
        return throwError(sp().res.err);
      }
      // successful expression result
      return popAndPassResult(sp().res);
    default:
      break;
  }
  return throwError(TextError::err("resumed in invalid state %d", sp().state));
}


// only global variables in base class
bool EvaluationContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  VariablesMap* varsP = &ScriptGlobals::sharedScriptGlobals().globalVariables;
  return variableLookup(varsP, aName, aResult);
}


bool EvaluationContext::variableLookup(VariablesMap* aVariableMapP, const string &aVarPath, ExpressionValue &aResult)
{
  if (!aVariableMapP) return false;
  const char *p = aVarPath.c_str();
  string enam;
  nextPart(p, enam, '.');
  VariablesMap::iterator vpos = aVariableMapP->find(enam);
  if (vpos!=aVariableMapP->end()) {
    // found base var
    aResult = vpos->second;
    while (nextPart(p, enam, '.')) {
      #if EXPRESSION_JSON_SUPPORT
      if (!aResult.isJson()) return false; // not found
      aResult = aResult.subField(enam);
      #else
      return false; // no structured variables without JSON support
      #endif
    }
    return true;
  }
  // not found
  return false;
}




static const char * const monthNames[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
static const char * const weekdayNames[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

void EvaluationContext::parseNumericLiteral(ExpressionValue &aResult, const char* aCode, size_t& aPos)
{
  double v;
  int i;
  if (sscanf(aCode+aPos, "%lf%n", &v, &i)!=1) {
    // Note: sscanf %d also handles hex!
    aResult.setSyntaxError("invalid number, time or date");
    return;
  }
  else {
    size_t b = aPos;
    aPos += i; // past consumation of sscanf
    // check for time/date literals
    // - time literals (returned in seconds) are in the form h:m or h:m:s, where all parts are allowed to be fractional
    // - month/day literals (returned in yeardays) are in the form dd.monthname or dd.mm. (mid the closing dot)
    if (aCode[aPos]) {
      if (aCode[aPos]==':') {
        // we have 'v:', could be time
        double t;
        if (sscanf(aCode+aPos+1, "%lf%n", &t, &i)!=1) {
          aResult.setSyntaxError("invalid time specification - use hh:mm or hh:mm:ss");
          return;
        }
        else {
          aPos += i+1; // past : and consumation of sscanf
          // we have v:t, take these as hours and minutes
          v = (v*60+t)*60; // in seconds
          if (aCode[aPos]==':') {
            // apparently we also have seconds
            if (sscanf(aCode+aPos+1, "%lf%n", &t, &i)!=1) {
              aResult.setSyntaxError("Time specification has invalid seconds - use hh:mm:ss");
              return;
            }
            aPos += i+1; // past : and consumation of sscanf
            v += t; // add the seconds
          }
        }
      }
      else {
        int m = -1; int d = -1;
        if (aCode[aPos-1]=='.' && isalpha(aCode[aPos])) {
          // could be dd.monthname
          for (m=0; m<12; m++) {
            if (strucmp(aCode+aPos, monthNames[m], 3)==0) {
              // valid monthname following number
              // v = day, m = month-1
              m += 1;
              d = v;
              break;
            }
          }
          aPos += 3;
          if (d<0) {
            aResult.setSyntaxError("Invalid date specification - use dd.monthname");
            return;
          }
        }
        else if (aCode[aPos]=='.') {
          // must be dd.mm. (with mm. alone, sscanf would have eaten it)
          aPos = b;
          int l;
          if (sscanf(aCode+aPos, "%d.%d.%n", &d, &m, &l)!=2) {
            aResult.setSyntaxError("Invalid date specification - use dd.mm.");
            return;
          }
          aPos += l;
        }
        if (d>=0) {
          struct tm loctim; MainLoop::getLocalTime(loctim);
          loctim.tm_hour = 12; loctim.tm_min = 0; loctim.tm_sec = 0; // noon - avoid miscalculations that could happen near midnight due to DST offsets
          loctim.tm_mon = m-1;
          loctim.tm_mday = d;
          mktime(&loctim);
          v = loctim.tm_yday;
        }
      }
    }
  }
  aResult.setNumber(v);
}


EvaluationContext::Operations EvaluationContext::parseOperator(size_t &aPos)
{
  skipNonCode(aPos);
  // check for operator
  Operations op = op_none;
  switch (code(aPos++)) {
    // assignment and equality
    case ':': {
      if (code(aPos)!='=') goto no_op;
      aPos++; op = op_assign; break;
    }
    case '=': {
      if (code(aPos)=='=') {
        aPos++; op = op_equal; break;
      }
      #if EXPRESSION_OPERATOR_MODE==EXPRESSION_OPERATOR_MODE_C
      op = op_assign; break;
      #elif EXPRESSION_OPERATOR_MODE==EXPRESSION_OPERATOR_MODE_PASCAL
      op = op_equal; break;
      #else
      op = op_assignOrEq; break;
      #endif
    }
    case '*': op = op_multiply; break;
    case '/': op = op_divide; break;
    case '+': op = op_add; break;
    case '-': op = op_subtract; break;
    case '&': op = op_and; if (code(aPos)=='&') aPos++; break;
    case '|': op = op_or; if (code(aPos)=='|') aPos++; break;
    case '<': {
      if (code(aPos)=='=') {
        aPos++; op = op_leq; break;
      }
      else if (code(aPos)=='>') {
        aPos++; op = op_notequal; break;
      }
      op = op_less; break;
    }
    case '>': {
      if (code(aPos)=='=') {
        aPos++; op = op_geq; break;
      }
      op = op_greater; break;
    }
    case '!': {
      if (code(aPos)=='=') {
        aPos++; op = op_notequal; break;
      }
      op = op_not; break;
      break;
    }
    default:
    no_op:
      --aPos; // no expression char
      return op_none;
  }
  skipNonCode(aPos);
  return op;
}


// MARK: - expression state machine entry points


bool EvaluationContext::resumeExpression()
{
  if (sp().state==s_newExpression) {
    sp().precedence = 0;
    sp().state = s_expression;
  }
  if (sp().state==s_expression) {
    // at start of an expression
    sp().pos = pos; // remember start of any expression, even if it's only a precedence terminated subexpression
    // - check for optional unary op
    Operations unaryop = parseOperator(pos);
    sp().op = unaryop; // store for later
    if (unaryop!=op_none && unaryop!=op_subtract && unaryop!=op_add && unaryop!=op_not) {
      return abortWithSyntaxError("invalid unary operator");
    }
    // evaluate first (or only) term
    newstate(s_exprFirstTerm);
    // - check for paranthesis term
    if (currentchar()=='(') {
      // term is expression in paranthesis
      pos++;
      push(s_groupedExpression);
      return push(s_newExpression);
    }
    // must be simple term
    // - a variable reference
    // - a function call
    // - a literal number or timespec (h:m or h:m:s)
    // - a literal string (C-string like)
    // Note: a non-simple term is the parantesized expression as handled above
    return push(s_simpleTerm);
  }
  if (sp().state==s_exprFirstTerm) {
    // res now has the first term of an expression, which might need applying unary operations
    Operations unaryop = sp().op;
    // assign to val, applying unary op (only if not null)
    if (sp().res.isValue()) {
      switch (unaryop) {
        case op_not : sp().res.setBool(!sp().res.boolValue()); break;
        case op_subtract : sp().res.setNumber(-sp().res.numValue()); break;
        case op_add: // dummy, is NOP, allowed for clarification purposes
        default: break;
      }
    }
    return newstate(s_exprLeftSide);
  }
  #if EXPRESSION_JSON_SUPPORT
  if (sp().state==s_subscriptArg) {
    // a subscript argument, collect like function args
    sp().args.addArg(sp().res, sp().pos);
    skipNonCode();
    if (currentchar()==',') {
      // more arguments
      pos++; // consume comma
      sp().pos = pos; // update argument position
      return push(s_newExpression);
    }
    else if (currentchar()==']') {
      pos++; // consume closing ']'
      return newstate(s_subscriptExec);
    }
    return abortWithSyntaxError("missing closing ']'");
  }
  if (sp().state==s_subscriptExec) {
    ExpressionValue base = sp().val;
    ELOG_DBG("Resolving Subscript for '%s'", sp().identifier.c_str());
    for (int ai=0; ai<sp().args.size(); ai++) {
      ELOG_DBG("- subscript at char pos=%zu: %s (err=%s)", sp().args.getPos(ai), sp().args[ai].stringValue().c_str(), Error::text(sp().args[ai].err));
      sp().res = base.subScript(sp().args[ai]);
      if (sp().res.isNull()) break;
      base = sp().res;
    }
    // after subscript, continue with leftside
    return newstate(s_exprLeftSide);
  }
  #endif // EXPRESSION_JSON_SUPPORT
  if (sp().state==s_exprLeftSide) {
    // res now has the left side value
    #if EXPRESSION_JSON_SUPPORT
    // - check postfix "operators": dot and subscript
    if (currentchar()=='[') {
      // array-style [x,..] subscript, works similar to function
      pos++; // skip opening bracket
      sp().args.clear();
      skipNonCode();
      newstate(s_subscriptArg);
      sp().val = sp().res; // res will get overwritten by subscript argument collection
      sp().pos = pos; // where the subscript starts
      return push(s_newExpression);
    }
    else if (currentchar()=='.') {
      // dot notation subscript
      pos++; // skip dot
      size_t ssz; const char *ss = getIdentifier(ssz); // get implicit subscript
      if (ssz==0) return abortWithSyntaxError("missin object field name after dot");
      pos+=ssz;
      string fn(ss,ssz);
      sp().res = sp().res.subField(fn);
      // after subscript, continue with leftside
      return newstate(s_exprLeftSide);
    }
    #endif // EXPRESSION_JSON_SUPPORT
    // check binary operators
    size_t opIdx = pos;
    Operations binaryop = parseOperator(opIdx);
    int precedence = binaryop & opmask_precedence;
    // end parsing here if no operator found or operator with a lower or same precedence as the passed in precedence is reached
    if (binaryop==op_none || precedence<=sp().precedence) {
      return newstate(s_result); // end of this expression
    }
    // must parse right side of operator as subexpression
    pos = opIdx; // advance past operator
    sp().op = binaryop;
    sp().val = sp().res; // duplicate so rightside expression can be put into res
    newstate(s_exprRightSide);
    push(s_expression);
    sp().precedence = precedence; // subexpression needs to exit when finding an operator weaker than this one
    return true;
  }
  if (sp().state==s_exprRightSide) {
    Operations binaryop = sp().op;
    // val = leftside, res = rightside
    if (!sp().skipping) {
      ExpressionValue opRes;
      // all operations involving nulls return null
      if (sp().res.isValue() && sp().val.isValue()) {
        // both are values -> apply the operation between leftside and rightside
        switch (binaryop) {
          case op_not: {
            return abortWithSyntaxError("NOT operator not allowed here");
          }
          case op_divide: opRes = sp().val / sp().res; break;
          case op_multiply: opRes = sp().val * sp().res; break;
          case op_add: opRes = sp().val + sp().res; break;
          case op_subtract: opRes = sp().val - sp().res; break;
          case op_equal:
          case op_assignOrEq: opRes = sp().val == sp().res; break;
          case op_notequal: opRes = sp().val != sp().res; break;
          case op_less: opRes = sp().val < sp().res; break;
          case op_greater: opRes = sp().val > sp().res; break;
          case op_leq: opRes = sp().val <= sp().res; break;
          case op_geq: opRes = sp().val >= sp().res; break;
          case op_and: opRes = sp().val && sp().res; break;
          case op_or: opRes = sp().val || sp().res; break;
          default: break;
        }
      }
      else if (sp().val.isError()) {
        // if first is error, return that
        opRes = sp().val;
      }
      else if (sp().res.isError()) {
        // if first is ok but second is error, return that
        opRes = sp().res;
      }
      sp().val = opRes;
      // duplicate into res in case evaluation ends
      sp().res = sp().val;
      if (sp().res.isValue()) {
        ELOG_DBG("Intermediate expression '%.*s' evaluation result: %s", (int)(pos-sp().pos), tail(sp().pos), sp().res.stringValue().c_str());
      }
      else {
        ELOG_DBG("Intermediate expression '%.*s' evaluation result is INVALID", (int)(pos-sp().pos), tail(sp().pos));
      }
    }
    return newstate(s_exprLeftSide); // back to leftside, more chained operators might follow
  }
  return throwError(TextError::err("expression resumed in invalid state %d", sp().state));
}


bool EvaluationContext::resumeGroupedExpression()
{
  if (currentchar()!=')') {
    return abortWithSyntaxError("Missing ')'");
  }
  pos++;
  return popAndPassResult(sp().res);
}


bool EvaluationContext::resumeTerm()
{
  if (sp().state==s_simpleTerm) {
    if (currentchar()=='"' || currentchar()=='\'') {
      // string literal (c-like with double quotes or php-like with single quotes and no escaping inside)
      char delimiter = currentchar();
      // string literal
      string str;
      pos++;
      char c;
      while(true) {
        c = currentchar();
        if (c==delimiter) {
          if (delimiter=='\'' && code(pos+1)==delimiter) {
            // single quoted strings allow including delimiter by doubling it
            str += delimiter;
            pos += 2;
            continue;
          }
          break; // end of string
        }
        if (c==0) return abortWithSyntaxError("unterminated string, missing %c delimiter", delimiter);
        if (delimiter!='\'' && c=='\\') {
          c = code(++pos);
          if (c==0) abortWithSyntaxError("incomplete \\-escape");
          else if (c=='n') c='\n';
          else if (c=='r') c='\r';
          else if (c=='t') c='\t';
          else if (c=='x') {
            unsigned int h = 0;
            pos++;
            if (sscanf(tail(pos), "%02x", &h)==1) pos++;
            c = (char)h;
          }
          // everything else
        }
        str += c;
        pos++;
      }
      pos++; // skip closing quote
      sp().res.setString(str);
    }
    #if EXPRESSION_JSON_SUPPORT
    else if (currentchar()=='{' || currentchar()=='[') {
      // JSON object or array literal
      const char* p = tail(pos);
      ssize_t n;
      ErrorPtr err;
      JsonObjectPtr j = JsonObject::objFromText(p, -1, &err, false, &n);
      if (Error::notOK(err)) {
        return abortWithSyntaxError("invalid JSON literal: %s", err->text());
      }
      else {
        pos += n;
        sp().res.setJson(j);
      }
    }
    #endif
    else {
      // identifier (variable, function)
      size_t idsz; const char *id = getIdentifier(idsz);
      if (!id) {
        // we can get here depending on how statement delimiters are used, and should not try to parse a numeric, then
        if (currentchar() && currentchar()!='}' && currentchar()!=';') {
          // checking for statement separating chars is safe, there's no way one of these could appear at the beginning of a term
          parseNumericLiteral(sp().res, getCode(), pos);
        }
        return newstate(s_result);
      }
      else {
        // identifier, examine
        const char *idpos = id;
        pos += idsz;
        // - include all dot notation subfields in identifier, because overridden valueLookup() implementations might
        //   get subfields by another means than diving into JSON values. Still, we need "." and "[" operators in
        //   other places
        while (currentchar()=='.') {
          pos++; idsz++;
          size_t s; id = getIdentifier(s);
          if (id) pos += s;
          idsz += s;
        }
        sp().identifier.assign(idpos, idsz); // save the name, in original case, including dot notation
        skipNonCode();
        if (currentchar()=='(') {
          // function call
          pos++; // skip opening paranthesis
          sp().args.clear();
          skipNonCode();
          if (currentchar()!=')') {
            // start scanning argument
            newstate(s_funcArg);
            sp().pos = pos; // where the argument starts
            return push(s_newExpression);
          }
          // function w/o arguments, directly go to execute
          pos++; // skip closing paranthesis
          return newstate(s_funcExec);
        } // function call
        else {
          // plain identifier
          if (strucmp(idpos, "true", idsz)==0 || strucmp(idpos, "yes", idsz)==0) {
            sp().res.setNumber(1);
          }
          else if (strucmp(idpos, "false", idsz)==0 || strucmp(idpos, "no", idsz)==0) {
            sp().res.setNumber(0);
          }
          else if (strucmp(idpos, "null", idsz)==0 || strucmp(idpos, "undefined", idsz)==0) {
            sp().res.setNull(string(idpos, idsz).c_str());
          }
          else if (!sp().skipping) {
            // must be identifier representing a variable value
            if (!valueLookup(sp().identifier, sp().res)) {
              // also match some convenience pseudo-vars
              bool pseudovar = false;
              if (idsz==3) {
                // Optimisation, all weekdays have 3 chars
                for (int w=0; w<7; w++) {
                  if (strucmp(idpos, weekdayNames[w], idsz)==0) {
                    sp().res.setNumber(w); // return numeric value of weekday
                    pseudovar = true;
                    break;
                  }
                }
              }
              if (!pseudovar) {
                return abortWithSyntaxError("no variable named '%s'", sp().identifier.c_str());
              }
            }
          }
        } // plain identifier
      } // plain identifier or function call
    } // not string literal
    // res is what we've got, return it
    return newstate(s_result);
  } // simpleterm
  else if (sp().state==s_funcArg) {
    // a function argument, push it
    sp().args.addArg(sp().res, sp().pos);
    skipNonCode();
    if (currentchar()==',') {
      // more arguments
      pos++; // consume comma
      sp().pos = pos; // update argument position
      return push(s_newExpression);
    }
    else if (currentchar()==')') {
      pos++; // consume closing ')'
      return newstate(s_funcExec);
    }
    return abortWithSyntaxError("missing closing ')' in function call");
  }
  else if (sp().state==s_funcExec) {
    ELOG_DBG("Calling Function '%s'", sp().identifier.c_str());
    for (int ai=0; ai<sp().args.size(); ai++) {
      ELOG_DBG("- argument at char pos=%zu: %s (err=%s)", sp().args.getPos(ai), sp().args[ai].stringValue().c_str(), Error::text(sp().args[ai].err));
    }
    // run function
    newstate(s_result); // expecting result from function
    if (!sp().skipping) {
      string funcname = lowerCase(sp().identifier);
      // - try registered synchronous function handlers first
      for (FunctionCBList::iterator pos = functionCallbacks.begin(); pos!=functionCallbacks.end(); ++pos) {
        if ((*pos)(this, funcname, sp().args, sp().res)) {
          return true; // not yielded
        }
      }
      if (ScriptGlobals::exists()) {
        for (
          FunctionCBList::iterator pos = ScriptGlobals::sharedScriptGlobals().globalFunctionCallbacks.begin();
          pos!=ScriptGlobals::sharedScriptGlobals().globalFunctionCallbacks.end();
          ++pos
        ) {
          if ((*pos)(this, funcname, sp().args, sp().res)) {
            return true; // not yielded
          }
        }
      }

      // - then try built-in synchronous functions first
      if (evaluateFunction(funcname, sp().args, sp().res)) {
        return true; // not yielded
      }
      // - finally: must be async function
      bool notYielded = true; // default to not yielded, especially for errorInArg()
      if (synchronous || !evaluateAsyncFunction(funcname, sp().args, notYielded)) {
        return abortWithSyntaxError("Unknown function '%s' with %d arguments", sp().identifier.c_str(), sp().args.size());
      }
      return notYielded;
    }
    return true; // not executed -> not yielded
  }
  return throwError(TextError::err("resumed term in invalid state %d", sp().state));
}


ExpressionValue TimedEvaluationContext::evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval, bool aCallBack)
{
  ExpressionValue res = inherited::evaluateSynchronously(aEvalMode, aScheduleReEval, aCallBack);
  // take unfreeze time of frozen results into account for next evaluation
  FrozenResultsMap::iterator fpos = frozenResults.begin();
  while (fpos!=frozenResults.end()) {
    if (fpos->second.frozenUntil==Never) {
      // already detected expired -> erase (Note: just expired ones in terms of now() MUST wait until checked in next evaluation!)
      FrozenResultsMap::iterator dpos = fpos++;
      frozenResults.erase(dpos);
      continue;
    }
    updateNextEval(fpos->second.frozenUntil);
    fpos++;
  }
  if (nextEvaluation!=Never) {
    ELOG("Expression demands re-evaluation at %s: '%s'", MainLoop::string_mltime(nextEvaluation).c_str(), getCode());
  }
  if (aScheduleReEval) {
    scheduleReEvaluation(nextEvaluation);
  }
  return res;
}


// MARK: - standard functions available in every context

#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function

// standard functions available in every context
bool EvaluationContext::evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="ifvalid" && aArgs.size()==2) {
    // ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
    aResult = aArgs[0].isValue() ? aArgs[0] : aArgs[1];
  }
  else if (aFunc=="isvalid" && aArgs.size()==1) {
    // isvalid(a)      if a is a valid value, return true, otherwise return false
    aResult.setNumber(aArgs[0].isValue() ? 1 : 0);
  }
  else if (aFunc=="if" && aArgs.size()==3) {
    // if (c, a, b)    if c evaluates to true, return a, otherwise b
    if (!aArgs[0].isOK()) return errorInArg(aArgs[0], aResult); // return error from condition
    aResult = aArgs[0].boolValue() ? aArgs[1] : aArgs[2];
  }
  else if (aFunc=="abs" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    aResult.setNumber(fabs(aArgs[0].numValue()));
  }
  else if (aFunc=="int" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    aResult.setNumber(int(aArgs[0].int64Value()));
  }
  else if (aFunc=="round" && aArgs.size()>=1 && aArgs.size()<=2) {
    // round (a)       round value to integer
    // round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    double precision = 1;
    if (aArgs.size()>=2 && aArgs[1].isValue()) {
      precision = aArgs[1].numValue();
    }
    aResult.setNumber(round(aArgs[0].numValue()/precision)*precision);
  }
  else if (aFunc=="random" && aArgs.size()==2) {
    // random (a,b)     random value from a up to and including b
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
    aResult.setNumber(aArgs[0].numValue() + (double)rand()*(aArgs[1].numValue()-aArgs[0].numValue())/((double)RAND_MAX));
  }
  else if (aFunc=="min" && aArgs.size()==2) {
    // min (a, b)    return the smaller value of a and b
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    if ((aArgs[0]<aArgs[1]).boolValue()) aResult = aArgs[0];
    else aResult = aArgs[1];
  }
  else if (aFunc=="max" && aArgs.size()==2) {
    // max (a, b)    return the bigger value of a and b
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    if ((aArgs[0]>aArgs[1]).boolValue()) aResult = aArgs[0];
    else aResult = aArgs[1];
  }
  else if (aFunc=="limited" && aArgs.size()==3) {
    // limited (x, a, b)    return min(max(x,a),b), i.e. x limited to values between and including a and b
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    if (aArgs[2].notValue()) return errorInArg(aArgs[2], aResult); // return error/null from argument
    aResult = aArgs[0];
    if ((aResult<aArgs[1]).boolValue()) aResult = aArgs[1];
    else if ((aResult>aArgs[2]).boolValue()) aResult = aArgs[2];
  }
  else if (aFunc=="cyclic" && aArgs.size()==3) {
    // cyclic (x, a, b)    return x with wraparound into range a..b (not including b because it means the same thing as a)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    if (aArgs[2].notValue()) return errorInArg(aArgs[2], aResult); // return error/null from argument
    double o = aArgs[1].numValue();
    double x0 = aArgs[0].numValue()-o; // make null based
    double r = aArgs[2].numValue()-o; // wrap range
    if (x0>=r) x0 -= int(x0/r)*r;
    else if (x0<0) x0 += (int(-x0/r)+1)*r;
    aResult = x0+o;
  }
  else if (aFunc=="string" && aArgs.size()==1) {
    // string(anything)
    if (aArgs[0].isNull())
      aResult.setString("undefined"); // make it visible
    else
      aResult.setString(aArgs[0].stringValue()); // force convert to string, including nulls and errors
  }
  else if (aFunc=="number" && aArgs.size()==1) {
    // number(anything)
    aResult.setNumber(aArgs[0].numValue()); // force convert to numeric
  }
  else if (aFunc=="copy" && aArgs.size()==1) {
    // copy(anything) // make a value copy, including json object references
    #if EXPRESSION_JSON_SUPPORT
    if (aArgs[0].isJson()) {
      // need to make a value copy of the JsonObject itself
      aResult.setJson(JsonObjectPtr(new JsonObject(*(aArgs[0].jsonValue()))));
    }
    else
    #endif
    {
      aResult = aArgs[0]; // just copy the ExpressionValue
    }
  }
  #if EXPRESSION_JSON_SUPPORT
  else if (aFunc=="json" && aArgs.size()==1) {
    // json(anything)
    aResult.setJson(aArgs[0].jsonValue());
  }
  else if (aFunc=="setfield" && aArgs.size()==3) {
    // bool setfield(var, fieldname, value)
    if (!aArgs[0].isJson()) aResult.setNull(); // not JSON, cannot set value
    else {
      aResult.setJson(aArgs[0].jsonValue());
      if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
      aResult.jsonValue()->add(aArgs[1].stringValue().c_str(), aArgs[2].jsonValue());
    }
  }
  else if (aFunc=="setelement" && aArgs.size()>=2 && aArgs.size()<=3) {
    // bool setelement(var, index, value) // set
    // bool setelement(var, value) // append
    if (!aArgs[0].isJson()) aResult.setNull(); // not JSON, cannot set value
    else {
      aResult.setJson(aArgs[0].jsonValue());
      if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
      if (aArgs.size()==2) {
        // append
        aResult.jsonValue()->arrayAppend(aArgs[1].jsonValue());
      }
      else {
        aResult.jsonValue()->arrayPut(aArgs[1].intValue(), aArgs[2].jsonValue());
      }
    }
  }
  #endif // EXPRESSION_JSON_SUPPORT
  else if (aFunc=="strlen" && aArgs.size()==1) {
    // strlen(string)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    aResult.setNumber(aArgs[0].stringValue().size()); // length of string
  }
  else if (aFunc=="substr" && aArgs.size()>=2 && aArgs.size()<=3) {
    // substr(string, from)
    // substr(string, from, count)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    string s = aArgs[0].stringValue();
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    size_t start = aArgs[1].intValue();
    if (start>s.size()) start = s.size();
    size_t count = string::npos; // to the end
    if (aArgs.size()>=3 && aArgs[2].isValue()) {
      count = aArgs[2].intValue();
    }
    aResult.setString(s.substr(start, count));
  }
  else if (aFunc=="find" && aArgs.size()>=2 && aArgs.size()<=3) {
    // find(haystack, needle)
    // find(haystack, needle, from)
    string haystack = aArgs[0].stringValue(); // haystack can be anything, including invalid
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    string needle = aArgs[1].stringValue();
    size_t start = 0;
    if (aArgs.size()>=3) {
      start = aArgs[2].intValue();
      if (start>haystack.size()) start = haystack.size();
    }
    size_t p = string::npos;
    if (aArgs[0].isValue()) {
      p = haystack.find(needle, start);
    }
    if (p!=string::npos)
      aResult.setNumber(p);
    else
      aResult.setNull(); // not found
  }
  else if (aFunc=="format" && aArgs.size()==2) {
    // format(formatstring, number)
    // only % + - 0..9 . d, x, and f supported
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    string fmt = aArgs[0].stringValue();
    if (
      fmt.size()<2 ||
      fmt[0]!='%' ||
      fmt.substr(1,fmt.size()-2).find_first_not_of("+-0123456789.")!=string::npos || // excluding last digit
      fmt.find_first_not_of("duxXeEgGf", fmt.size()-1)!=string::npos // which must be d,x or f
    ) {
      abortWithSyntaxError("invalid format string, only basic %%duxXeEgGf specs allowed");
    }
    if (fmt.find_first_of("duxX", fmt.size()-1)!=string::npos)
      aResult.setString(string_format(fmt.c_str(), aArgs[1].intValue())); // int format
    else
      aResult.setString(string_format(fmt.c_str(), aArgs[1].numValue())); // double format
  }
  else if (aFunc=="throw" && aArgs.size()==1) {
    // throw(value)       - throw a expression user error with the string value of value as errormessage
    // throw(errvalue)    - (re-)throw with the error of the value passed
    if (aArgs[0].isError()) return throwError(aArgs[0].error()); // just pass as is
    else return throwError(ExpressionError::User, "%s", aArgs[0].stringValue().c_str());
  }
  else if (aFunc=="error" && aArgs.size()==1) {
    // error(value)       - create a user error value with the string value of value as errormessage, in all cases, even if value is already an error
    aResult.setError(ExpressionError::User, "%s", aArgs[0].stringValue().c_str());
    return true;
  }
  else if (aFunc=="error" && aArgs.size()==0) {
    // error()            - within a catch context only: the error thrown
    StackList::iterator spos = stack.end();
    while (spos!=stack.begin()) {
      spos--;
      if (spos->state==s_catchStatement) {
        // here the error is stored
        aResult = spos->res;
        return true;
      }
    }
    // try to use error() not within catch
    return abortWithSyntaxError("error() can only be called from within catch statements");
  }
  else if (!synchronous && oneTimeResultHandler && aFunc=="earlyresult" && aArgs.size()==1) {
    // send the one time result now, but keep script running
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    ELOG("earlyresult sends '%s' to caller, script continues running", aResult.stringValue().c_str());
    runCallBack(aArgs[0]);
  }
  else if (aFunc=="errordomain" && aArgs.size()==1) {
    // errordomain(errvalue)
    ErrorPtr err = aArgs[0].err;
    if (Error::isOK(err)) aResult.setNull(); // no error, no domain
    aResult.setString(err->getErrorDomain());
  }
  else if (aFunc=="errorcode") {
    // errorcode(errvalue)
    ErrorPtr err = aArgs[0].err;
    if (Error::isOK(err)) aResult.setNull(); // no error, no code
    aResult.setNumber(err->getErrorCode());
  }
  else if (aFunc=="errormessage" && aArgs.size()==1) {
    // errormessage(value)
    ErrorPtr err = aArgs[0].err;
    if (Error::isOK(err)) aResult.setNull(); // no error, no message
    aResult.setString(err->getErrorMessage());
  }
  else if (synchronous && aFunc=="eval" && aArgs.size()==1) {
    // eval(string)    have string evaluated as expression
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    // TODO: for now we use a adhoc evaluation with access only to vars, but no functions.
    //   later, when we have subroutine mechanisms, we'd be able to run the eval within the same context
    aResult = p44::evaluateExpression(
      aArgs[0].stringValue().c_str(),
      boost::bind(&EvaluationContext::valueLookup, this, _1, _2),
      NULL, // no functions from within function for now
      evalLogLevel
    );
    if (aResult.notValue()) {
      ELOG("eval(\"%s\") returns error '%s' in expression: '%s'", aArgs[0].stringValue().c_str(), aResult.err->text(), getCode());
      // do not cause syntax error, only invalid result, but with error message included
      aResult.setNull(string_format("eval() error: %s -> undefined", aResult.err->text()).c_str());
    }
  }
  else if (aFunc=="is_weekday" && aArgs.size()>0) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    // check if any of the weekdays match
    int weekday = loctim.tm_wday; // 0..6, 0=sunday
    ExpressionValue newRes(0);
    size_t refpos = aArgs.getPos(0); // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
    for (int i = 0; i<aArgs.size(); i++) {
      if (aArgs[i].notValue()) return errorInArg(aArgs[i], aResult); // return error/null from argument
      int w = (int)aArgs[i].numValue();
      if (w==7) w=0; // treat both 0 and 7 as sunday
      if (w==weekday) {
        // today is one of the days listed
        newRes.setNumber(1);
        break;
      }
    }
    // freeze until next check: next day 0:00:00
    loctim.tm_mday++;
    loctim.tm_hour = 0;
    loctim.tm_min = 0;
    loctim.tm_sec = 0;
    ExpressionValue res = newRes;
    FrozenResult* frozenP = getFrozen(res,refpos);
    newFreeze(frozenP, newRes, refpos, MainLoop::localTimeToMainLoopTime(loctim));
    aResult = res; // freeze time over, use actual, newly calculated result
  }
  else if ((aFunc=="after_time" || aFunc=="is_time") && aArgs.size()>=1) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    ExpressionValue newSecs;
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    size_t refpos = aArgs.getPos(0); // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
    if (aArgs.size()==2) {
      // legacy spec
      if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
      newSecs.setNumber(((int32_t)aArgs[0].numValue() * 60 + (int32_t)aArgs[1].numValue()) * 60);
    }
    else {
      // specification in seconds, usually using time literal
      newSecs.setNumber((int32_t)(aArgs[0].numValue()));
    }
    ExpressionValue secs = newSecs;
    FrozenResult* frozenP = getFrozen(secs, refpos);
    int32_t daySecs = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec;
    bool met = daySecs>=secs.numValue();
    // next check at specified time, today if not yet met, tomorrow if already met for today
    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)secs.numValue();
    ELOG("is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim)).c_str());
    bool res = met;
    // limit to a few secs around target if it's is_time
    if (aFunc=="is_time" && met && daySecs<secs.numValue()+IS_TIME_TOLERANCE_SECONDS) {
      // freeze again for a bit
      newFreeze(frozenP, secs, refpos, MainLoop::localTimeToMainLoopTime(loctim)+IS_TIME_TOLERANCE_SECONDS*Second);
    }
    else {
      loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)newSecs.numValue();
      if (met) {
        loctim.tm_mday++; // already met today, check again tomorrow
        if (aFunc=="is_time") res = false;
      }
      newFreeze(frozenP, newSecs, refpos, MainLoop::localTimeToMainLoopTime(loctim));
    }
    aResult = res;
  }
  else if (aFunc=="between_dates" || aFunc=="between_yeardays") {
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    struct tm loctim; MainLoop::getLocalTime(loctim);
    int smaller = (int)(aArgs[0].numValue());
    int larger = (int)(aArgs[1].numValue());
    int currentYday = loctim.tm_yday;
    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = 0;
    loctim.tm_mon = 0;
    bool lastBeforeFirst = smaller>larger;
    if (lastBeforeFirst) swap(larger, smaller);
    if (currentYday<smaller) loctim.tm_mday = 1+smaller;
    else if (currentYday<=larger) loctim.tm_mday = 1+larger;
    else { loctim.tm_mday = smaller; loctim.tm_year += 1; } // check one day too early, to make sure no day is skipped in a leap year to non leap year transition
    updateNextEval(loctim);
    aResult.setBool((currentYday>=smaller && currentYday<=larger)!=lastBeforeFirst);
  }
  else if (aFunc=="sunrise" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    else aResult.setNumber(sunrise(time(NULL), *geolocationP, false)*3600);
  }
  else if (aFunc=="dawn" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    else aResult.setNumber(sunrise(time(NULL), *geolocationP, true)*3600);
  }
  else if (aFunc=="sunset" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    else aResult.setNumber(sunset(time(NULL), *geolocationP, false)*3600);
  }
  else if (aFunc=="dusk" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    else aResult.setNumber(sunset(time(NULL), *geolocationP, true)*3600);
  }
  else {
    double fracSecs;
    struct tm loctim; MainLoop::getLocalTime(loctim, &fracSecs);
    if (aFunc=="timeofday" && aArgs.size()==0) {
      aResult.setNumber(((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs);
    }
    else if (aFunc=="hour" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_hour);
    }
    else if (aFunc=="minute" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_min);
    }
    else if (aFunc=="second" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_sec);
    }
    else if (aFunc=="year" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_year+1900);
    }
    else if (aFunc=="month" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_mon+1);
    }
    else if (aFunc=="day" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_mday);
    }
    else if (aFunc=="weekday" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_wday);
    }
    else if (aFunc=="yearday" && aArgs.size()==0) {
      aResult.setNumber(loctim.tm_yday);
    }
    else {
      return false; // not found
    }
  }
  return true;
}


bool EvaluationContext::evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
{
  aNotYielded = true; // by default, so we can use "return errorInArg()" style exits
  if (aFunc=="delay" && aArgs.size()==1) {
    if (aArgs[0].notValue()) return true; // no value specified, consider executed
    MLMicroSeconds delay = aArgs[0].numValue()*Second;
    execTicket.executeOnce(boost::bind(&EvaluationContext::continueEvaluation, this), delay);
    aNotYielded = false; // yielded execution
  }
  else {
    return false; // no such async function
  }
  return true; // function found, aNotYielded must be set correctly!
}




#if EXPRESSION_SCRIPT_SUPPORT


// MARK: - ScriptGlobals

ScriptGlobals& ScriptGlobals::sharedScriptGlobals()
{
  if (!scriptGlobals) {
    scriptGlobals = new ScriptGlobals;
  }
  return *scriptGlobals;
}

bool ScriptGlobals::exists()
{
  return scriptGlobals!=NULL;
}

void ScriptGlobals::registerFunctionHandler(FunctionLookupCB aFunctionLookupHandler)
{
  globalFunctionCallbacks.push_back(aFunctionLookupHandler);
}


// MARK: - ScriptExecutionContext

ScriptExecutionContext::ScriptExecutionContext(const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP)
{
}


ScriptExecutionContext::~ScriptExecutionContext()
{
}


void ScriptExecutionContext::releaseState()
{
  ELOG_DBG("All variables released now for expression: '%s'", getCode());
  variables.clear();
}


bool ScriptExecutionContext::startEvaluation()
{
  return startEvaluationWith(s_body);
}

bool ScriptExecutionContext::execute(bool aAsynchronously, EvaluationResultCB aOneTimeEvaluationResultHandler)
{
  synchronous = !aAsynchronously;
  callBack = true; // always call back!
  if (aOneTimeEvaluationResultHandler) {
    oneTimeResultHandler = true;
    evaluationResultHandler = aOneTimeEvaluationResultHandler;
  }
  bool notYielded = startEvaluation();
  while (notYielded && isEvaluating()) notYielded = continueEvaluation();
  return notYielded;
}


bool ScriptExecutionContext::chainContext(ScriptExecutionContext& aTargetContext, EvaluationResultCB aChainResultHandler)
{
  skipNonCode();
  if (sp().skipping) {
    if (aChainResultHandler) aChainResultHandler(ExpressionValue(), *this);
    return true; // not yielded, done
  }
  else {
    ELOG("chainContext: new context will execute rest of code: %s", tail(pos));
    aTargetContext.setEvalLogLevel(evalLogLevel); // inherit log level
    aTargetContext.setCode(tail(pos)); // pass tail
    pos = codeString.size(); // skip tail
    aTargetContext.abort(false); // abort previously running script
    return aTargetContext.execute(true, aChainResultHandler);
  }
}


bool ScriptExecutionContext::resumeEvaluation()
{
  bool ret = false;
  switch (sp().state) {
    case s_body:
    //case s_functionbody:
    case s_block:
    case s_oneStatement:
    case s_noStatement:
    case s_returnValue:
      ret = resumeStatements(); // list of statements or single statement
      break;
    case s_ifCondition:
    case s_ifTrueStatement:
    case s_elseStatement:
      ret = resumeIfElse(); // a if-elseif-else statement chain
      break;
    case s_tryStatement:
    case s_catchStatement:
      ret = resumeTryCatch(); // a try/catch statement chain
      break;
    case s_whileCondition:
    case s_whileStatement:
      ret = resumeWhile(); // a while loop statement
      break;
    case s_assignToVar:
      ret = resumeAssignment();
      break;
    default:
      ret = inherited::resumeEvaluation();
      break;
  }
  return ret;
}


bool ScriptExecutionContext::resumeStatements()
{
  // at a statement boundary, within a body/block/functionbody
  if (sp().state==s_noStatement) {
    // no more statements may follow in this level, a single terminator is allowed (but not required)
    if (currentchar()==';') pos++; // just consume it
    return pop();
  }
  if (sp().state==s_returnValue) {
    sp().state = s_complete;
    return true;
  }
  if (sp().state==s_oneStatement) {
    // only one statement allowed, next call must pop level ANYWAY (but might do so before because encountering a separator)
    sp().state = s_noStatement;
  }
  // - could be start of a new block
  if (currentchar()==0) {
    // end of code
    if (sp().state==s_body) {
      sp().state = s_complete;
      return true;
    }
    else {
      // unexpected end of code
      return abortWithSyntaxError("Unexpected end of code");
    }
  }
  if (currentchar()=='{') {
    // start new block
    pos++;
    return push(s_block);
  }
  if (sp().state==s_block && currentchar()=='}') {
    // end block
    pos++;
    return pop();
  }
  if (currentchar()==';') {
    if (sp().state==s_noStatement) return true; // the separator alone comprises a statement, so we're done
    pos++; // normal statement separator, consume it
    skipNonCode();
  }
  // at the beginning of a statement which is not beginning of a new block
  // - could be language keyword, variable assignment
  size_t kwsz; const char *kw = getIdentifier(kwsz);
  if (kw) {
    if (strucmp(kw, "if", kwsz)==0) {
      pos += kwsz;
      skipNonCode();
      if (currentchar()!='(') return abortWithSyntaxError("missing '(' after 'if'");
      pos++;
      push(s_ifCondition);
      return push(s_newExpression);
    }
    if (strucmp(kw, "else", kwsz)==0) {
      // just check to give sensible error message
      return abortWithSyntaxError("else without preceeding if");
    }
    if (strucmp(kw, "while", kwsz)==0) {
      pos += kwsz;
      skipNonCode();
      if (currentchar()!='(') return abortWithSyntaxError("missing '(' after 'while'");
      pos++;
      push(s_whileCondition);
      sp().pos = pos; // save position of the condition, we will jump here later
      return push(s_newExpression);
    }
    if (strucmp(kw, "break", kwsz)==0) {
      pos += kwsz;
      if (!sp().skipping) {
        if (!popToLast(s_whileStatement)) return abortWithSyntaxError("break must be within while statement");
        pos = sp().pos; // restore position saved
        newstate(s_whileCondition); // switch back to condition
        sp().skipping = true; // will avoid assignment of the condition, just skipping it and the while statement
        sp().flowDecision = false; // must make flow decision false to exit the loop
        return push(s_newExpression);
      }
      return true; // skipping, just consume keyword and ignore
    }
    if (strucmp(kw, "continue", kwsz)==0) {
      pos += kwsz;
      if (!sp().skipping) {
        if (!popToLast(s_whileStatement)) return abortWithSyntaxError("continue must be within while statement");
        pos = sp().pos; // restore position saved
        newstate(s_whileCondition); // switch back to condition
        return push(s_newExpression);
      }
      return true; // skipping, just consume keyword and ignore
    }
    if (strucmp(kw, "return", kwsz)==0) {
      pos += kwsz;
      sp().res.setNull(); // default to no result
      skipNonCode();
      if (currentchar() && currentchar()!=';') {
        // switch frame to last thing that will happen: getting the return value
        if (!sp().skipping) sp().state = s_returnValue;
        return push(s_newExpression);
      }
      if (sp().skipping) return true; // skipping, just consume keyword and ignore
      return newstate(s_complete); // not skipping, actually terminate
    }
    if (strucmp(kw, "try", kwsz)==0) {
      pos += kwsz;
      push(s_tryStatement);
      sp().flowDecision = false; // nothing caught so far
      return push(s_oneStatement);
    }
    if (strucmp(kw, "catch", kwsz)==0) {
      // just check to give sensible error message
      return abortWithSyntaxError("catch without preceeding try");
    }
    // variable handling
    bool vardef = false;
    bool let = false;
    bool glob = false;
    bool newVar = false;
    string varName;
    size_t apos = pos + kwsz; // potential assignment location
    if (strucmp(kw, "var", kwsz)==0) {
      vardef = true;
    }
    else if (strucmp(kw, "glob", kwsz)==0) {
      vardef = true;
      glob = true;
    }
    else if (strucmp(kw, "let", kwsz)==0) {
      let = true;
    }
    if (vardef || let) {
      // explicit assignment statement keyword
      pos = apos;
      skipNonCode();
      // variable name follows
      size_t vsz; const char *vn = getIdentifier(vsz);
      if (!vn)
        return abortWithSyntaxError("missing variable name after '%.*s'", (int)kwsz, kw);
      varName = lowerCase(vn, vsz);
      pos += vsz;
      apos = pos;
      if (vardef) {
        // is a definition
        VariablesMap* varsP = glob ? &ScriptGlobals::sharedScriptGlobals().globalVariables : &variables;
        if (varsP->find(varName)==varsP->end()) {
          // does not yet exist, create it with null value
          ExpressionValue null;
          (*varsP)[varName] = null;
          ELOG_DBG("Defined %s variable %.*s", glob ? "GLOBAL" : "LOCAL", (int)vsz,vn);
          newVar = true;
        }
      }
    }
    else {
      // keyword itself is the variable name
      varName.assign(kw, kwsz);
    }
    skipNonCode(apos);
    Operations op = parseOperator(apos);
    // Note: for the unambiguous "var", "global" and "let" cases, allow the equal operator for assignment
    if (op==op_assign || op==op_assignOrEq ||((vardef || let) && op==op_equal)) {
      // definitely: this is an assignment
      pos = apos;
      if (!glob || newVar) {
        // assign globals only if not already existing (initial value)
        // Note: local variables will be redefined each time we pass the "var" statement
        push(s_assignToVar);
        sp().identifier = varName; // new frame needs the name to assign value later
        return push(s_newExpression); // but first, evaluate the expression
      }
      else {
        // do not initialize again, skip the expression
        push(s_newExpression);
        sp().skipping = true;
      }
    }
    else if (let) {
      // let is not allowed w/o assignment
      return abortWithSyntaxError("let must always assign a value");
    }
    else if (vardef) {
      // declaration only
      pos = apos;
      return true;
    }
  }
  // no special language construct, statement just evaluates an expression
  return push(s_newExpression);
}


bool ScriptExecutionContext::resumeAssignment()
{
  // assign expression result to variable
  // - first search local ones
  bool glob = false;
  VariablesMap* varsP = &variables;
  VariablesMap::iterator vpos = varsP->find(sp().identifier);
  if (vpos==varsP->end()) {
    varsP = &ScriptGlobals::sharedScriptGlobals().globalVariables;
    vpos = varsP->find(sp().identifier);
    if (vpos==varsP->end()) {
      return abortWithSyntaxError("variable '%s' is not declared - use: var name := expression", sp().identifier.c_str());
    }
    glob = true;
  }
  if (!sp().skipping) {
    // assign variable
    ELOG_DBG("Assigned %s variable: %s := %s", glob ? "global" : "local", sp().identifier.c_str(), sp().res.stringValue().c_str());
    vpos->second = sp().res;
    return popAndPassResult(sp().res);
  }
  return pop();
}


bool ScriptExecutionContext::resumeTryCatch()
{
  if (sp().state==s_tryStatement) {
    // try statement is executed
    // - check for "catch" following
    size_t kwsz; const char *kw = getIdentifier(kwsz);
    if (kw && strucmp(kw, "catch", kwsz)==0) {
      pos += kwsz;
      sp().state = s_catchStatement;
      // - flowdecision is set only if there was an error -> skip catch only if it is not set!
      return push(s_oneStatement, !sp().flowDecision);
    }
    else {
      return abortWithSyntaxError("missing 'catch' after 'try'");
    }
  }
  if (sp().state==s_catchStatement) {
    // catch statement is executed
    return pop(); // end try/catch
  }
  return true;
}


bool ScriptExecutionContext::resumeIfElse()
{
  if (sp().state==s_ifCondition) {
    // if condition is evaluated
    if (currentchar()!=')')  return abortWithSyntaxError("missing ')' after if condition");
    pos++;
    sp().state = s_ifTrueStatement;
    sp().flowDecision = sp().res.boolValue();
    return push(s_oneStatement, !sp().flowDecision);
  }
  if (sp().state==s_ifTrueStatement) {
    // if statement (or block of statements) is executed
    // - check for "else" following
    size_t kwsz; const char *kw = getIdentifier(kwsz);
    if (kw && strucmp(kw, "else", kwsz)==0) {
      pos += kwsz;
      skipNonCode();
      // there might be another if following right away
      kw = getIdentifier(kwsz);
      if (kw && strucmp(kw, "if", kwsz)==0) {
        pos += kwsz;
        skipNonCode();
        if (currentchar()!='(') return abortWithSyntaxError("missing '(' after 'else if'");
        pos++;
        // chained if: when preceeding "if" did execute (or would have if not already skipping), rest of if/elseif...else chain will be skipped
        if (sp().flowDecision) sp().skipping = true;
        push(s_ifCondition);
        return push(s_newExpression);
      }
      else {
        // last else in chain
        sp().state = s_elseStatement;
        return push(s_oneStatement, sp().flowDecision);
      }
    }
    else {
      // if without else
      return pop(); // end if/then/else
    }
  }
  if (sp().state==s_elseStatement) {
    // last else in chain, no if following
    return pop();
  }
  return true;
}


bool ScriptExecutionContext::resumeWhile()
{
  if (sp().state==s_whileCondition) {
    // while condition is evaluated
    if (currentchar()!=')')  return abortWithSyntaxError("missing ')' after while condition");
    pos++;
    newstate(s_whileStatement);
    if (!sp().skipping) sp().flowDecision = sp().res.boolValue(); // do not change flow decision when skipping (=break)
    return push(s_oneStatement, !sp().flowDecision);
  }
  if (sp().state==s_whileStatement) {
    // while statement (or block of statements) is executed
    if (sp().flowDecision==false) {
      return pop(); // end while
    }
    // not skipping, means we need to loop
    pos = sp().pos; // restore position saved - that of the condition expression
    newstate(s_whileCondition); // switch back to condition
    return push(s_newExpression);
  }
  return true;
}


bool ScriptExecutionContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  if (variableLookup(&variables, aName, aResult)) return true;
  // none found locally. Let base class check for globals
  return inherited::valueLookup(aName, aResult);
}


bool ScriptExecutionContext::evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="log" && aArgs.size()>=1 && aArgs.size()<=2) {
    // log (logmessage)
    // log (loglevel, logmessage)
    int loglevel = LOG_INFO;
    int ai = 0;
    if (aArgs.size()>1) {
      if (aArgs[ai].notValue()) return errorInArg(aArgs[ai], aResult);
      loglevel = aArgs[ai].intValue();
      ai++;
    }
    if (aArgs[ai].notValue()) return errorInArg(aArgs[ai], aResult);
    LOG(loglevel, "Script log: %s", aArgs[ai].stringValue().c_str());
  }
  else if (aFunc=="loglevel" && aArgs.size()==1) {
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult);
    int newLevel = aArgs[0].intValue();
    if (newLevel>=0 && newLevel<=7) {
      int oldLevel = LOGLEVEL;
      SETLOGLEVEL(newLevel);
      LOG(newLevel, "\n\n========== script changed log level from %d to %d ===============", oldLevel, newLevel);
    }
  }
  else if (aFunc=="scriptloglevel" && aArgs.size()==1) {
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult);
    int newLevel = aArgs[0].intValue();
    if (newLevel>=0 && newLevel<=7) {
      evalLogLevel = newLevel;
    }
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  }
  // procedure with no return value of itself
  aResult.setNull();
  return true;
}


#endif // EXPRESSION_SCRIPT_SUPPORT



// MARK: - TimedEvaluationContext

TimedEvaluationContext::TimedEvaluationContext(const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP)
{
}


TimedEvaluationContext::~TimedEvaluationContext()
{

}


void TimedEvaluationContext::releaseState()
{
  ELOG_DBG("All frozen state is released now for expression: '%s'", getCode());
  frozenResults.clear(); // changing expression unfreezes everything
}


void TimedEvaluationContext::scheduleReEvaluation(MLMicroSeconds aAtTime)
{
  nextEvaluation = aAtTime;
  if (nextEvaluation!=Never) {
    reEvaluationTicket.executeOnceAt(boost::bind(&TimedEvaluationContext::timedEvaluationHandler, this, _1, _2), nextEvaluation);
  }
  else {
    reEvaluationTicket.cancel();
  }
}


void TimedEvaluationContext::timedEvaluationHandler(MLTimer &aTimer, MLMicroSeconds aNow)
{
  // trigger another evaluation
  ELOG("Timed re-evaluation of expression starting now: '%s'", getCode());
  triggerEvaluation(evalmode_timed);
}


void TimedEvaluationContext::scheduleLatestEvaluation(MLMicroSeconds aAtTime)
{
  if (updateNextEval(aAtTime)) {
    scheduleReEvaluation(nextEvaluation);
  }
}


TimedEvaluationContext::FrozenResult* TimedEvaluationContext::getFrozen(ExpressionValue &aResult, size_t aRefPos)
{
  FrozenResultsMap::iterator frozenVal = frozenResults.find(aRefPos);
  FrozenResult* frozenResultP = NULL;
  if (frozenVal!=frozenResults.end()) {
    frozenResultP = &(frozenVal->second);
    // there is a frozen result for this position in the expression
    ELOG_DBG("- frozen result (%s) for actual result (%s) at char pos %zu exists - will expire %s",
      frozenResultP->frozenResult.stringValue().c_str(),
      aResult.stringValue().c_str(),
      aRefPos,
      frozenResultP->frozen() ? MainLoop::string_mltime(frozenResultP->frozenUntil).c_str() : "NOW"
    );
    aResult = frozenVal->second.frozenResult;
    if (!frozenResultP->frozen()) frozenVal->second.frozenUntil = Never; // mark expired
  }
  return frozenResultP;
}


bool TimedEvaluationContext::FrozenResult::frozen()
{
  return frozenUntil==Infinite || (frozenUntil!=Never && frozenUntil>MainLoop::now());
}


TimedEvaluationContext::FrozenResult* TimedEvaluationContext::newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, size_t aRefPos, MLMicroSeconds aFreezeUntil, bool aUpdate)
{
  if (!aExistingFreeze) {
    // nothing frozen yet, freeze it now
    FrozenResult newFreeze;
    newFreeze.frozenResult = aNewResult; // full copy, including pos
    newFreeze.frozenUntil = aFreezeUntil;
    frozenResults[aRefPos] = newFreeze;
    ELOG_DBG("- new result (%s) frozen for pos %zu until %s", aNewResult.stringValue().c_str(), aRefPos, MainLoop::string_mltime(newFreeze.frozenUntil).c_str());
    return &frozenResults[aRefPos];
  }
  else if (!aExistingFreeze->frozen() || aUpdate || aFreezeUntil==Never) {
    ELOG_DBG("- existing freeze updated to value %s and to expire %s",
      aNewResult.stringValue().c_str(),
      aFreezeUntil==Never ? "IMMEDIATELY" : MainLoop::string_mltime(aFreezeUntil).c_str()
    );
    aExistingFreeze->frozenResult = aNewResult;
    aExistingFreeze->frozenUntil = aFreezeUntil;
  }
  else {
    ELOG_DBG("- no freeze created/updated");
  }
  return aExistingFreeze;
}


bool TimedEvaluationContext::unfreeze(size_t aAtPos)
{
  FrozenResultsMap::iterator frozenVal = frozenResults.find(aAtPos);
  if (frozenVal!=frozenResults.end()) {
    frozenResults.erase(frozenVal);
    return true;
  }
  return false;
}



#define MIN_RETRIGGER_SECONDS 10 ///< how soon testlater() is allowed to re-trigger

// special functions only available in timed evaluations
bool TimedEvaluationContext::evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="testlater" && aArgs.size()>=2 && aArgs.size()<=3) {
    // testlater(seconds, timedtest [, retrigger])   return "invalid" now, re-evaluate after given seconds and return value of test then. If repeat is true then, the timer will be re-scheduled
    bool retrigger = false;
    if (aArgs.size()>=3) retrigger = aArgs[2].isValue() && aArgs[2].boolValue();
    ExpressionValue secs = aArgs[0];
    if (retrigger && secs.numValue()<MIN_RETRIGGER_SECONDS) {
      // prevent too frequent re-triggering that could eat up too much cpu
      LOG(LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", secs.numValue(), (double)MIN_RETRIGGER_SECONDS);
      secs.setNumber(MIN_RETRIGGER_SECONDS);
    }
    ExpressionValue currentSecs = secs;
    size_t refPos = aArgs.getPos(0);
    FrozenResult* frozenP = getFrozen(currentSecs, refPos);
    bool evalNow = frozenP && !frozenP->frozen();
    if (evalMode!=evalmode_timed) {
      if (evalMode!=evalmode_initial || retrigger) {
        // evaluating non-timed, non-initial (or retriggering) means "not yet ready" and must start or extend freeze period
        newFreeze(frozenP, secs, refPos, MainLoop::now()+secs.numValue()*Second, true);
      }
      evalNow = false; // never evaluate on non-timed run
    }
    else {
      // evaluating timed after frozen period means "now is later" and if retrigger is set, must start a new freeze
      if (frozenP && retrigger) {
        newFreeze(frozenP, secs, refPos, MainLoop::now()+secs.numValue()*Second);
      }
    }
    if (evalNow) {
      // evaluation runs because freeze is over, return test result
      aResult.setNumber(aArgs[1].numValue());
    }
    else {
      // still frozen, return undefined
      aResult.setNull("testlater() not yet ready");
    }
  }
  else if (aFunc=="every" && aArgs.size()>=1 && aArgs.size()<=2) {
    // every(interval [, syncoffset])
    // returns true once every interval
    // Note: first true is returned at first evaluation or, if syncoffset is set,
    //   at next integer number of intervals calculated from beginning of the day + syncoffset
    double syncoffset = -1;
    if (aArgs.size()>=2) {
      syncoffset = aArgs[2].numValue();
    }
    ExpressionValue secs = aArgs[0];
    if (secs.numValue()<MIN_RETRIGGER_SECONDS) {
      // prevent too frequent re-triggering that could eat up too much cpu
      LOG(LOG_WARNING, "every() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", secs.numValue(), (double)MIN_RETRIGGER_SECONDS);
      secs.setNumber(MIN_RETRIGGER_SECONDS);
    }
    ExpressionValue currentSecs = secs;
    size_t refPos = aArgs.getPos(0);
    FrozenResult* frozenP = getFrozen(currentSecs, refPos);
    bool trigger = frozenP && !frozenP->frozen();
    if (trigger || evalMode==evalmode_initial) {
      // setup new interval
      double interval = secs.numValue();
      if (syncoffset<0) {
        // no sync
        // - interval starts from now
        newFreeze(frozenP, secs, refPos, MainLoop::now()+secs.numValue()*Second, true);
        trigger = true; // fire even in initial evaluation
      }
      else {
        // synchronize with real time
        double fracSecs;
        struct tm loctim; MainLoop::getLocalTime(loctim, &fracSecs);
        double secondOfDay = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs; // second of day right now
        double untilNext = syncoffset+(floor((secondOfDay-syncoffset)/interval)+1)*interval - secondOfDay; // time to next repetition
        newFreeze(frozenP, secs, refPos, MainLoop::now()+untilNext*Second, true);
      }
    }
    aResult.setBool(trigger);
  }
  else if (aFunc=="initial" && aArgs.size()==0) {
    // initial()  returns true if this is a "initial" run of the evaluator, meaning after startup or expression changes
    aResult.setNumber(evalMode==evalmode_initial);
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  }
  return true;
}




// MARK: - ad hoc expression evaluation

/// helper class for ad-hoc expression evaluation
class AdHocEvaluationContext : public EvaluationContext
{
  typedef EvaluationContext inherited;
  ValueLookupCB valueLookUp;
  FunctionLookupCB functionLookUp;

public:
  AdHocEvaluationContext(ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB) :
    inherited(NULL),
    valueLookUp(aValueLookupCB),
    functionLookUp(aFunctionLookpCB)
  {
  };

  virtual ~AdHocEvaluationContext() {};

  ExpressionValue evaluateExpression(const string &aExpression)
  {
    setCode(aExpression);
    return evaluateSynchronously(evalmode_initial);
  };

protected:

  virtual bool valueLookup(const string &aName, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (valueLookUp && valueLookUp(aName, aResult)) return true;
    return inherited::valueLookup(aName, aResult);
  };

  virtual bool evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (functionLookUp && functionLookUp(this, aFunc, aArgs, aResult)) return true;
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  };

};
typedef boost::intrusive_ptr<AdHocEvaluationContext> AdHocEvaluationContextPtr;


ExpressionValue p44::evaluateExpression(const string &aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, int aLogLevel)
{
  AdHocEvaluationContext ctx(aValueLookupCB, aFunctionLookpCB);
  ctx.isMemberVariable();
  ctx.setEvalLogLevel(aLogLevel);
  return ctx.evaluateExpression(aExpression);
}



// MARK: - placeholder expression substitution - @{expression}


ErrorPtr p44::substituteExpressionPlaceholders(string &aString, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, string aNullText)
{
  ErrorPtr err;
  size_t p = 0;
  AdHocEvaluationContextPtr ctx; // create it lazily when actually required, prevent allocation when aString has no substitutions at all

  // Syntax of placeholders:
  //   @{expression}
  while ((p = aString.find("@{",p))!=string::npos) {
    size_t e = aString.find("}",p+2);
    if (e==string::npos) {
      // syntactically incorrect, no closing "}"
      err = ExpressionError::err(ExpressionError::Syntax, "unterminated placeholder: %s", aString.c_str()+p);
      break;
    }
    string expr = aString.substr(p+2,e-2-p);
    // evaluate expression
    if (!ctx) ctx = AdHocEvaluationContextPtr(new AdHocEvaluationContext(aValueLookupCB, aFunctionLookpCB));
    ExpressionValue result = ctx->evaluateExpression(expr);
    string rep;
    if (result.isValue()) {
      rep = result.stringValue();
    }
    else {
      rep = aNullText;
      if (Error::isOK(err)) err = result.error(); // only report first error
    }
    // replace, even if rep is empty
    aString.replace(p, e-p+1, rep);
    p+=rep.size();
  }
  return err;
}


#if EXPRESSION_LEGACY_PLACEHOLDERS

// MARK: - legacy @{placeholder} substitution

ErrorPtr p44::substitutePlaceholders(string &aString, StringValueLookupCB aValueLookupCB)
{
  ErrorPtr err;
  size_t p = 0;
  // Syntax of placeholders:
  //   @{var[*ff][+|-oo][%frac]}
  //   ff is an optional float factor to scale the channel value, or 'B' to output JSON-compatible boolean true or false
  //   oo is an float offset to apply
  //   frac are number of fractional digits to use in output
  while ((p = aString.find("@{",p))!=string::npos) {
    size_t e = aString.find("}",p+2);
    if (e==string::npos) {
      // syntactically incorrect, no closing "}"
      err = ExpressionError::err(ExpressionError::Syntax, "unterminated placeholder: %s", aString.c_str()+p);
      break;
    }
    string v = aString.substr(p+2,e-2-p);
    // process operations
    double chfactor = 1;
    double choffset = 0;
    int numFracDigits = 0;
    bool boolFmt = false;
    bool calc = false;
    size_t varend = string::npos;
    size_t i = 0;
    while (true) {
      i = v.find_first_of("*+-%",i);
      if (varend==string::npos) {
        varend = i==string::npos ? v.size() : i;
      }
      if (i==string::npos) break; // no more factors, offsets or format specs
      // factor and/or offset
      if (v[i]=='%') {
        // format, check special cases
        if (v[i+1]=='B') {
          // binary true/false
          boolFmt = true;
          i+=2;
          continue;
        }
      }
      calc = true;
      double dd;
      if (sscanf(v.c_str()+i+1, "%lf", &dd)==1) {
        switch (v[i]) {
          case '*' : chfactor *= dd; break;
          case '+' : choffset += dd; break;
          case '-' : choffset -= dd; break;
          case '%' : numFracDigits = dd; break;
        }
      }
      i++;
    }
    // process variable
    string rep = v.substr(0, varend);
    if (aValueLookupCB) {
      // if no replacement is found, original text is used
      err = aValueLookupCB(rep, rep);
      if (Error::notOK(err))
        break; // abort
    }
    // apply calculations if any
    if (calc) {
      // parse as double
      double dv;
      if (sscanf(rep.c_str(), "%lf", &dv)==1) {
        // got double value, apply calculations
        dv = dv * chfactor + choffset;
        // render back to string
        if (boolFmt) {
          rep = dv>0 ? "true" : "false";
        }
        else {
          rep = string_format("%.*lf", numFracDigits, dv);
        }
      }
    }
    // replace, even if rep is empty
    aString.replace(p, e-p+1, rep);
    p+=rep.size();
  }
  return err;
}

#endif // EXPRESSION_LEGACY_PLACEHOLDERS

#endif // ENABLE_EXPRESSIONS

