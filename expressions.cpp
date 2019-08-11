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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "expressions.hpp"
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
  pos = aVal.pos;
  numVal = aVal.numVal;
  err = aVal.err;
  clrStr();
  if (aVal.strValP) {
    strValP = new string(*aVal.strValP);
  }
  return *this;
}


void ExpressionValue::clrStr()
{
  if (strValP) delete strValP;
  strValP = NULL;
}

ExpressionValue::~ExpressionValue()
{
  clrStr();
}


ExpressionValue ExpressionValue::withError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)
{
  err = new ExpressionError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  return *this;
}


ExpressionValue ExpressionValue::withSyntaxError(const char *aFmt, ...)
{
  err = new ExpressionError(ExpressionError::Syntax);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  return *this;
}


ExpressionValue ExpressionValue::errValue(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)
{
  ExpressionValue ev;
  ev.err = new ExpressionError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  ev.err->setFormattedMessage(aFmt, args);
  va_end(args);
  return ev;
}


string ExpressionValue::stringValue() const
{
  if (isOk()) {
    if (isString()) return *strValP;
    else return string_format("%lg", numVal);
  }
  else {
    return "";
  }
}


double ExpressionValue::numValue() const
{
  if (!isOk()) return 0;
  if (!isString()) return numVal;
  ExpressionValue v(0);
  size_t lpos = 0;
  EvaluationContext::parseNumericLiteral(v, strValP->c_str(), lpos);
  return v.numVal;
}


bool ExpressionValue::operator<(const ExpressionValue& aRightSide) const
{
  if (notOk() || aRightSide.notOk()) return false; // nulls and errors are not orderable
  if (isString()) return *strValP < aRightSide.stringValue();
  return numVal < aRightSide.numValue();
}


bool ExpressionValue::operator!=(const ExpressionValue& aRightSide) const
{
  return !operator==(aRightSide);
}



bool ExpressionValue::operator==(const ExpressionValue& aRightSide) const
{
  if (notOk() || aRightSide.notOk()) {
    // - notOk()'s loosely count as "undefined" (even if not specifically ExpressionError::Null)
    // - do not compare with other side's value, but its ok status
    return notOk() == aRightSide.notOk();
  }
  if (isString()) return *strValP == aRightSide.stringValue();
  else return numVal == aRightSide.numValue();
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
  if (aRightSide.numValue()==0) return ExpressionValue::errValue(ExpressionError::DivisionByZero, "division by zero").withPos(aRightSide.pos);
  return numValue() / aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator&&(const ExpressionValue& aRightSide) const
{
  return numValue() && aRightSide.numValue();
}

ExpressionValue ExpressionValue::operator||(const ExpressionValue& aRightSide) const
{
  return numValue() || aRightSide.numValue();
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

#define ELOGGING (evalLogLevel!=0)
#define ELOG(...) { if (ELOGGING) LOG(evalLogLevel,##__VA_ARGS__) }

EvaluationContext::EvaluationContext(const GeoLocation* aGeoLocationP) :
  geolocationP(aGeoLocationP),
  runningSince(Never),
  nextEvaluation(Never),
  synchronous(true),
  evalLogLevel(FOCUSLOGGING ? FOCUSLOGLEVEL : 0) // default to focus level
{
}


EvaluationContext::~EvaluationContext()
{
}


void EvaluationContext::setEvaluationResultHandler(EvaluationResultCB aEvaluationResultHandler)
{
  evaluationResultHandler = aEvaluationResultHandler;
}


bool EvaluationContext::setCode(const string aCode)
{
  if (aCode!=codeString) {
    releaseState(); // changing expression unfreezes everything
    codeString = aCode;
    return true;
  }
  return false;
}


void EvaluationContext::skipWhiteSpace(size_t& aPos)
{
  while (code(aPos)==' ' || code(aPos)=='\t') aPos++;
}


void EvaluationContext::skipWhiteSpace()
{
  while (currentchar()==' ' || currentchar()=='\t') pos++;
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


bool EvaluationContext::newstate(EvalState aNewState)
{
  sp().state = aNewState;
  return true; // not yielded
}


bool EvaluationContext::push(EvalState aNewState, bool aStartSkipping)
{
  skipWhiteSpace();
  stack.push_back(StackFrame(aNewState, aStartSkipping || sp().skipping, sp().precedence));
  return true; // not yielded
}


bool EvaluationContext::pop()
{
  skipWhiteSpace();
  if (stack.size()>1) {
    // regular pop
    stack.pop_back();
    return true; // not yielded
  }
  // trying to pop last entry - switch to complete/abort first
  if (isEvaluating()) {
    return newstate(sp().res.valueOk() ? s_complete : s_abort);
  }
  return true; // not yielded
}


bool EvaluationContext::popAndPassResult(ExpressionValue aResult)
{
  pop();
  if (stack.empty())
    finalResult = aResult;
  else
    sp().res = aResult;
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



bool EvaluationContext::abortWithError(ErrorPtr aError)
{
  sp().res.err = aError;
  sp().res.pos = pos;
  return newstate(s_abort);
}


bool EvaluationContext::errorInArg(ExpressionValue aArg, const char* aExtraPrefix)
{
  ErrorPtr err;
  if (aArg.isOk()) {
    err = ExpressionError::err(ExpressionError::Syntax, "unspecific");
  }
  else if (aExtraPrefix) {
    err->prefixMessage("%s", aExtraPrefix);
  }
  else {
    err->prefixMessage("Function argument error: ");
  }
  abortWithError(err);
  return true; // function known
}





bool EvaluationContext::abortWithError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)
{
  ErrorPtr err = Error::err<ExpressionError>(aErrCode);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  return abortWithError(err);
}


bool EvaluationContext::abortWithSyntaxError(const char *aFmt, ...)
{
  ErrorPtr err = Error::err<ExpressionError>(ExpressionError::Syntax);
  va_list args;
  va_start(args, aFmt);
  err->setFormattedMessage(aFmt, args);
  va_end(args);
  return abortWithError(err);
}


bool EvaluationContext::startEvaluationWith(EvalState aState)
{
  if (runningSince!=Never) {
    LOG(LOG_WARNING, "Already evaluating (since %s) -> cannot start again: %s", MainLoop::string_mltime(runningSince).c_str(), getCode());
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
  if (runningSince==Never) {
    LOG(LOG_CRIT, "EvaluationContext: continueEvaluation() -> implementation error!");
  }
  while(isEvaluating()) {
    if (!resumeEvaluation()) return false; // execution yielded
  }
  return true; // ran to end without yielding
}



ExpressionValue EvaluationContext::evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval)
{
  synchronous = true; // force synchronous operation, disable functionality that would need yielding execution
  evalMode = aEvalMode;
  if (runningSince!=Never) {
    LOG(LOG_WARNING, "Another evaluation is running (since %s) -> cannot start synchronous evaluation: %s", MainLoop::string_mltime(runningSince).c_str(), getCode());
    return ExpressionValue::errValue(ExpressionError::Busy, "Evaluation busy since %s -> cannot start now", MainLoop::string_mltime(runningSince).c_str());
  }
  bool notYielded = startEvaluation();
  if (notYielded && continueEvaluation()) {
    // has run to end
    return finalResult;
  }
  // FATAL ERROR: has yielded execution
  LOG(LOG_CRIT, "EvaluationContext: state machine has yielded execution while synchronous is set -> implementation error!");
  return ExpressionValue::errValue(ExpressionError::Busy, "state machine has yielded execution while synchronous is set -> implementation error!");
}


ErrorPtr EvaluationContext::triggerEvaluation(EvalMode aEvalMode)
{
  ErrorPtr err;
  ExpressionValue res = evaluateSynchronously(aEvalMode, true);
  if (evaluationResultHandler) {
    // this is where cyclic references could cause re-evaluation, which is protected by evaluating==true
    err = evaluationResultHandler(res, *this);
  }
  else {
    // no result handler, pass on error from evaluation
    LOG(LOG_WARNING, "triggerEvaluation() with no result handler for expression: %s", getCode());
    if (res.notOk()) err = res.err;
  }
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

#define OLDEXPRESSIONS 0

bool EvaluationContext::resumeEvaluation()
{
  if (runningSince==Never) {
    LOG(LOG_ERR, "resumeEvaluation() while not started");
    return false; // DO NOT CALL AGAIN!
  }
  switch (sp().state) {
    // completion states
    case s_complete:
      ELOG("Evaluation: execution completed");
      return newstate(s_finalize);
    case s_abort:
      ELOG("Evaluation: execution aborted");
      return newstate(s_finalize);
    case s_finalize:
      runningSince = Never;
      finalResult = sp().res;
      if (ELOGGING) {
        string errInd;
        if (!finalResult.syntaxOk()) {
          errInd = "\n                ";
          errInd.append(finalResult.pos, '-');
          errInd += '^';
        }
        ELOG("- code        = %s%s", getCode(), errInd.c_str());
        ELOG("- finalResult = %s - err = %s", finalResult.stringValue().c_str(), Error::text(finalResult.err));
      }
      stack.clear();
      return true;
    // expression evaluation states
    case s_newExpression:
    case s_expression:
    case s_exprFirstTerm:
    case s_exprLeftSide:
    case s_exprRightSide:
      #if !OLDEXPRESSIONS
      return resumeExpression();
      #else
      // FIXME: quick hack to test basics: use old uninteruptable evaluation
      sp().res = evaluateExpressionPrivate(codeString.c_str(), pos, 0, ";}", false, evalMode);
      sp().state = s_result;
      // %%% for now, fall through to result
      goto label_result;
      #endif
    // grouped expression
    case s_groupedExpression:
      return resumeGroupedExpression();
    case s_simpleTerm:
    case s_funcArg:
    case s_funcExec:
    // FIXME: add term subprocessing states
      return resumeTerm();
    // end of expressions, groups, terms
    #if OLDEXPRESSIONS
    label_result:
    #endif
    case s_result:
      if (!sp().res.syntaxOk()) {
        return abortWithError(sp().res.err);
      }
      // successful expression result
      return popAndPassResult(sp().res);
    default:
      break;
  }
  return abortWithError(TextError::err("resumed in invalid state %d", sp().state));
}


// no variables in base class
bool EvaluationContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  // no variables by default
  return false;
}


static const char * const monthNames[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
static const char * const weekdayNames[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

void EvaluationContext::parseNumericLiteral(ExpressionValue &aResult, const char* aCode, size_t& aPos)
{
  double v;
  int i;
  if (sscanf(aCode+aPos, "%lf%n", &v, &i)!=1) {
    aResult.withSyntaxError("'invalid number, time or date");
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
          aResult.withSyntaxError("invalid time specification - use hh:mm or hh:mm:ss");
          return;
        }
        else {
          aPos += i+1;
          // we have v:t, take these as hours and minutes
          v = (v*60+t)*60; // in seconds
          if (aCode[aPos]==':') {
            // apparently we also have seconds
            if (sscanf(aCode+aPos+1, "%lf", &t)!=1) {
              aResult.withSyntaxError("Time specification has invalid seconds - use hh:mm:ss");
              return;
            }
            v += t; // add the seconds
          }
        }
      }
      else {
        int m = -1; int d = -1;
        if (aCode[aPos-1]=='.' && isalpha(aCode[aPos])) {
          // could be dd.monthname
          for (m=0; m<12; m++) {
            if (strncasecmp(monthNames[m], aCode+aPos, 3)==0) {
              // valid monthname following number
              // v = day, m = month-1
              m += 1;
              d = v;
              break;
            }
          }
          aPos += 3;
          if (d<0) {
            aResult.withSyntaxError("Invalid date specification - use dd.monthname");
            return;
          }
        }
        else if (aCode[aPos]=='.') {
          // must be dd.mm. (with mm. alone, sscanf would have eaten it)
          aPos = b;
          int l;
          if (sscanf(aCode+aPos, "%d.%d.%n", &d, &m, &l)!=2) {
            aResult.withSyntaxError("Invalid date specification - use dd.mm.");
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
  aResult.withNumber(v);
}


EvaluationContext::Operations EvaluationContext::parseOperator(size_t &aPos)
{
  skipWhiteSpace(aPos);
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
    case '&': op = op_and; break;
    case '|': op = op_or; break;
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
  skipWhiteSpace(aPos);
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
    sp().val.pos = pos; // remember start of the expression
    // - check for optional unary op
    Operations unaryop = parseOperator(pos);
    sp().op = unaryop; // store for later
    if (unaryop!=op_none && unaryop!=op_subtract && unaryop!=op_not) {
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
    // assign to val, applying unary op
    switch (unaryop) {
      case op_not : sp().res.setBool(!sp().res.boolValue()); break;
      case op_subtract : sp().res.setNumber(-sp().res.numValue()); break;
      default: break;
    }
    return newstate(s_exprLeftSide);
  }
  if (sp().state==s_exprLeftSide) {
    // res now has the left side value
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
      // - equality comparison is the only thing that also inlcudes "undefined", so do it first
      if (binaryop==op_equal || binaryop==op_assignOrEq)
        sp().val.setBool(sp().val == sp().res);
      else if (binaryop==op_notequal)
        sp().val.setBool(sp().val != sp().res);
      else if (sp().res.isOk()) {
        // apply the operation between leftside and rightside
        ExpressionValue opRes;
        switch (binaryop) {
          case op_not: {
            return abortWithSyntaxError("NOT operator not allowed here");
          }
          case op_divide: opRes.withValue(sp().val / sp().res); break;
          case op_multiply: opRes.withValue(sp().val * sp().res); break;
          case op_add: opRes.withValue(sp().val + sp().res); break;
          case op_subtract: opRes.withValue(sp().val - sp().res); break;
          case op_less: opRes.setBool(sp().val < sp().res); break;
          case op_greater: opRes.setBool(!(sp().val < sp().res) && !(sp().val == sp().res)); break;
          case op_leq: opRes.setBool((sp().val < sp().res) || (sp().val == sp().res)); break;
          case op_geq: opRes.setBool(!(sp().val < sp().res)); break;
          case op_and: opRes.withValue(sp().val && sp().res); break;
          case op_or: opRes.withValue(sp().val || sp().res); break;
          default: break;
        }
        sp().val = opRes;
      }
      // duplicate into res in case evaluation ends
      sp().res = sp().val;
      if (sp().res.isOk()) {
        ELOG("Intermediate expression '%.*s' evaluation result: %s", (int)(pos-sp().val.pos), tail(sp().val.pos), sp().res.stringValue().c_str());
      }
      else {
        ELOG("Intermediate expression '%.*s' evaluation result is INVALID", (int)(pos-sp().val.pos), tail(sp().val.pos));
      }
      return newstate(s_exprLeftSide); // back to leftside, more chained operators might follow
    }
    return newstate(s_result); // end of this expression
  }
  return abortWithError(TextError::err("expression resumed in invalid state %d", sp().state));
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
    else {
      // identifier (variable, function)
      size_t idsz; const char *id = getIdentifier(idsz);
      if (!id) {
        // we can get here depending on how statement delimiters are used, and should not try to parse a numeric, then
        if (currentchar()!='}' && currentchar()!=';') {
          // checking for statement separating chars is safe, there's no way one of these could appear at the beginning of a term
          parseNumericLiteral(sp().res, getCode(), pos);
        }
        return newstate(s_result);
      }
      else {
        // identifier, examine
        const char *idpos = id;
        pos += idsz;
        // - check for subfields
        while (currentchar()=='.') {
          pos++; idsz++;
          size_t s; id = getIdentifier(s);
          if (id) pos += s;
          idsz += s;
        }
        sp().identifier.assign(lowerCase(idpos, idsz)); // save the name
        skipWhiteSpace();
        if (currentchar()=='(') {
          // function call
          pos++; // skip opening paranthesis
          sp().args.clear();
          skipWhiteSpace();
          if (currentchar()!=')') {
            // start scanning argument
            newstate(s_funcArg);
            return push(s_newExpression);
          }
          // function w/o arguments, directly go to execute
          return newstate(s_funcExec);
        } // function call
        else {
          // plain identifier
          if (strncasecmp(idpos, "true", idsz)==0 || strncasecmp(idpos, "yes", idsz)==0) {
            sp().res.withNumber(1);
          }
          else if (strncasecmp(idpos, "false", idsz)==0 || strncasecmp(idpos, "no", idsz)==0) {
            sp().res.setNumber(0);
          }
          else if (strncasecmp(idpos, "null", idsz)==0 || strncasecmp(idpos, "undefined", idsz)==0) {
            sp().res.withError(ExpressionError::Null, "%.*s", (int)idsz, idpos);
          }
          else if (!sp().skipping) {
            // must be identifier representing a variable value
            if (!valueLookup(sp().identifier, sp().res)) {
              // also match some convenience pseudo-vars
              bool pseudovar = false;
              if (idsz==3) {
                // Optimisation, all weekdays have 3 chars
                for (int w=0; w<7; w++) {
                  if (strncasecmp(weekdayNames[w], idpos, idsz)==0) {
                    sp().res.withError(ErrorPtr()); // clear not-found error
                    sp().res.withNumber(w); // return numeric value of weekday
                    pseudovar = true;
                    break;
                  }
                }
              }
              if (!pseudovar) {
                abortWithError(ExpressionError::NotFound, "no variable named '%s'", sp().identifier.c_str());
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
    sp().args.push_back(sp().res);
    skipWhiteSpace();
    if (currentchar()==',') {
      // more arguments
      pos++; // consume comma
      return push(s_newExpression);
    }
    else if (currentchar()==')') {
      pos++; // consume closing ')'
      return newstate(s_funcExec);
    }
    return abortWithSyntaxError("missing closing ) in function call");
  }
  else if (sp().state==s_funcExec) {
    ELOG("Calling Function '%s'", sp().identifier.c_str());
    for (FunctionArgumentVector::iterator pos = sp().args.begin(); pos!=sp().args.end(); ++pos) {
      ELOG("- argument at char pos=%zu: %s (err=%s)", pos->pos, pos->stringValue().c_str(), Error::text(pos->err));
    }
    // run function
    newstate(s_result); // expecting result from function
    if (!sp().skipping) {
      // - try synchronous functions first
      if (evaluateFunction(sp().identifier, sp().args, sp().res)) {
        return true; // not yielded
      }
      // - must be async
      bool notYielded = true; // default to not yielded, especially for errorInArg()
      if (!evaluateAsyncFunction(sp().identifier, sp().args, notYielded)) {
        return abortWithSyntaxError("Unknown function '%s' with %lu arguments", sp().identifier.c_str(), sp().args.size());
      }
      return notYielded;
    }
    return true; // not executed -> not yielded
  }
  return abortWithError(TextError::err("resumed term in invalid state %d", sp().state));
}


ExpressionValue TimedEvaluationContext::evaluateSynchronously(EvalMode aEvalMode, bool aScheduleReEval)
{
  ExpressionValue res = inherited::evaluateSynchronously(aEvalMode, aScheduleReEval);
  // take unfreeze time of frozen results into account for next evaluation
  FrozenResultsMap::iterator pos = frozenResults.begin();
  while (pos!=frozenResults.end()) {
    if (pos->second.frozenUntil==Never) {
      // already detected expired -> erase (Note: just expired ones in terms of now() MUST wait until checked in next evaluation!)
      pos = frozenResults.erase(pos);
      continue;
    }
    updateNextEval(pos->second.frozenUntil);
    pos++;
  }
  if (nextEvaluation!=Never) {
    ELOG("Expression demands re-evaluation at %s: %s", MainLoop::string_mltime(nextEvaluation).c_str(), getCode());
  }
  if (aScheduleReEval) {
    scheduleReEvaluation(nextEvaluation);
  }
  return res;
}


// MARK: - standard functions available in every context

#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function

// standard functions available in every context
bool EvaluationContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="ifvalid" && aArgs.size()==2) {
    // ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
    aResult = aArgs[0].isOk() ? aArgs[0] : aArgs[1];
  }
  else if (aFunc=="isvalid" && aArgs.size()==1) {
    // ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
    aResult.setNumber(aArgs[0].isOk() ? 1 : 0);
  }
  else if (aFunc=="if" && aArgs.size()==3) {
    // if (c, a, b)    if c evaluates to true, return a, otherwise b
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from condition
    aResult = aArgs[0].boolValue() ? aArgs[1] : aArgs[2];
  }
  else if (aFunc=="abs" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    aResult.setNumber(fabs(aArgs[0].numValue()));
  }
  else if (aFunc=="int" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    aResult.setNumber(int(aArgs[0].int64Value()));
  }
  else if (aFunc=="round" && (aArgs.size()>=1 || aArgs.size()<=2)) {
    // round (a)       round value to integer
    // round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    double precision = 1;
    if (aArgs.size()>=2) {
      if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
      precision = aArgs[1].numValue();
    }
    aResult.setNumber(round(aArgs[0].numValue()/precision)*precision);
  }
  else if (aFunc=="random" && aArgs.size()==2) {
    // random (a,b)     random value from a up to and including b
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
    // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
    aResult.setNumber(aArgs[0].numValue() + (double)rand()*(aArgs[1].numValue()-aArgs[0].numValue())/((double)RAND_MAX));
  }
  else if (aFunc=="string" && aArgs.size()==1) {
    // string(anything)
    aResult.setString(aArgs[0].stringValue()); // force convert to string, including nulls and errors
  }
  else if (aFunc=="number" && aArgs.size()==1) {
    // number(anything)
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // pass null and errors
    aResult.setNumber(aArgs[0].numValue()); // force convert to numeric
  }
  else if (aFunc=="strlen" && aArgs.size()==1) {
    // strlen(string)
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    aResult.setNumber(aArgs[0].stringValue().size()); // length of string
  }
  else if (aFunc=="substr" && aArgs.size()>=2 && aArgs.size()<=3) {
    // substr(string, from)
    // substr(string, from, count)
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    string s = aArgs[0].stringValue();
    if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
    size_t start = aArgs[1].intValue();
    if (start>s.size()) start = s.size();
    size_t count = string::npos; // to the end
    if (aArgs.size()>=3) {
      if (aArgs[2].notOk()) return errorInArg(aArgs[2]); // return error from argument
      count = aArgs[2].intValue();
    }
    aResult.setString(s.substr(start, count));
  }
  else if (aFunc=="find" && aArgs.size()>=2 && aArgs.size()<=3) {
    // find(haystack, needle)
    // find(haystack, needle, from)
    string haystack = aArgs[0].stringValue(); // haystack can be anything, including invalid
    if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
    string needle = aArgs[1].stringValue();
    size_t start = 0;
    if (aArgs.size()>=3) {
      start = aArgs[2].intValue();
      if (start>haystack.size()) start = haystack.size();
    }
    size_t p = string::npos;
    if (aArgs[0].isOk()) {
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
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
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
  else if (aFunc=="errormessage" && aArgs.size()==1) {
    // errormessage(value)
    ErrorPtr err = aArgs[0].err;
    if (Error::isOK(err)) aResult.setNull(); // no error, no message
    aResult.setString(err->getErrorMessage());
  }
  else if (aFunc=="errordescription" && aArgs.size()==1) {
    // errordescription(value)
    aResult.setString(aArgs[0].err->text());
  }
  else if (synchronous && aFunc=="eval" && aArgs.size()==1) {
    // eval(string)    have string evaluated as expression
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    // TODO: for now we use a adhoc evaluation with access only to vars, but no functions.
    //   later, when we have subroutine mechanisms, we'd be able to run the eval within the same context
    aResult = p44::evaluateExpression(
      aArgs[0].stringValue().c_str(),
      boost::bind(&EvaluationContext::valueLookup, this, _1, _2),
      NULL, // no functions from within function for now
      evalLogLevel
    );
    if (aResult.notOk()) {
      FOCUSLOG("eval(\"%s\") returns error '%s' in expression: %s", aArgs[0].stringValue().c_str(), aResult.err->text(), getCode());
      // do not cause syntax error, only invalid result, but with error message included
      aResult.withError(Error::err<ExpressionError>(ExpressionError::Null, "eval() error: %s -> undefined", aResult.err->text()));
    }
  }
  else if (aFunc=="is_weekday" && aArgs.size()>0) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    // check if any of the weekdays match
    int weekday = loctim.tm_wday; // 0..6, 0=sunday
    ExpressionValue newRes(0);
    newRes.pos = aArgs[0].pos; // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
    for (int i = 0; i<aArgs.size(); i++) {
      if (aArgs[i].notOk()) return errorInArg(aArgs[i]); // return error from argument
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
    FrozenResult* frozenP = getFrozen(res);
    newFreeze(frozenP, newRes, MainLoop::localTimeToMainLoopTime(loctim));
    aResult = res; // freeze time over, use actual, newly calculated result
  }
  else if ((aFunc=="after_time" || aFunc=="is_time") && aArgs.size()>=1) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    ExpressionValue newSecs;
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    newSecs.pos = aArgs[0].pos; // Note: we use pos of first argument for freezing the seconds
    if (aArgs.size()==2) {
      // legacy spec
      if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
      newSecs.setNumber(((int32_t)aArgs[0].numValue() * 60 + (int32_t)aArgs[1].numValue()) * 60);
    }
    else {
      // specification in seconds, usually using time literal
      newSecs.setNumber((int32_t)(aArgs[0].numValue()));
    }
    ExpressionValue secs = newSecs;
    FrozenResult* frozenP = getFrozen(secs);
    int32_t daySecs = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec;
    bool met = daySecs>=secs.numValue();
    // next check at specified time, today if not yet met, tomorrow if already met for today
    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)secs.numValue();
    ELOG("is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim)).c_str());
    bool res = met;
    // limit to a few secs around target if it's is_time
    if (aFunc=="is_time" && met && daySecs<secs.numValue()+IS_TIME_TOLERANCE_SECONDS) {
      // freeze again for a bit
      newFreeze(frozenP, secs, MainLoop::localTimeToMainLoopTime(loctim)+IS_TIME_TOLERANCE_SECONDS*Second);
    }
    else {
      loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)newSecs.numValue();
      if (met) {
        loctim.tm_mday++; // already met today, check again tomorrow
        if (aFunc=="is_time") res = false;
      }
      newFreeze(frozenP, newSecs, MainLoop::localTimeToMainLoopTime(loctim));
    }
    aResult = res;
  }
  else if (aFunc=="between_dates" || aFunc=="between_yeardays") {
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
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
    aResult.setNumber(sunrise(time(NULL), *geolocationP, false)*3600);
  }
  else if (aFunc=="dawn" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    aResult.setNumber(sunrise(time(NULL), *geolocationP, true)*3600);
  }
  else if (aFunc=="sunset" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    aResult.setNumber(sunset(time(NULL), *geolocationP, false)*3600);
  }
  else if (aFunc=="dusk" && aArgs.size()==0) {
    if (!geolocationP) aResult.setNull();
    aResult.setNumber(sunset(time(NULL), *geolocationP, true)*3600);
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


bool EvaluationContext::evaluateAsyncFunction(const string &aFunc, const FunctionArgumentVector &aArgs, bool &aNotYielded)
{
  aNotYielded = true; // by default, so we can use "return errorInArg()" style exits
  if (aFunc=="delay" && aArgs.size()==1) {
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]);
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

// MARK: - ScriptExecutionContext

ScriptExecutionContext::ScriptExecutionContext(const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP)
{
}


ScriptExecutionContext::~ScriptExecutionContext()
{
}


bool ScriptExecutionContext::startEvaluation()
{
  return startEvaluationWith(s_body);
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
    skipWhiteSpace();
  }
  // at the beginning of a statement which is not beginning of a new block
  // - could be language keyword, variable assignment
  size_t kwsz; const char *kw = getIdentifier(kwsz);
  if (kw) {
    if (strncasecmp("if", kw, kwsz)==0) {
      pos += kwsz;
      skipWhiteSpace();
      if (currentchar()!='(') return abortWithSyntaxError("missing '(' after 'if'");
      pos++;
      push(s_ifCondition);
      return push(s_newExpression);
    }
    if (strncasecmp("else", kw, kwsz)==0) {
      // just check to give sensible error message
      return abortWithSyntaxError("else without preceeding if");
    }
    if (strncasecmp("while", kw, kwsz)==0) {
      pos += kwsz;
      skipWhiteSpace();
      if (currentchar()!='(') return abortWithSyntaxError("missing '(' after 'while'");
      pos++;
      push(s_whileCondition);
      sp().pos = pos; // save position of the condition, we will jump here later
      return push(s_newExpression);
    }
    if (strncasecmp("break", kw, kwsz)==0) {
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
    if (strncasecmp("continue", kw, kwsz)==0) {
      pos += kwsz;
      if (!sp().skipping) {
        if (!popToLast(s_whileStatement)) return abortWithSyntaxError("continue must be within while statement");
        pos = sp().pos; // restore position saved
        newstate(s_whileCondition); // switch back to condition
        return push(s_newExpression);
      }
      return true; // skipping, just consume keyword and ignore
    }
    if (strncasecmp("return", kw, kwsz)==0) {
      pos += kwsz;
      sp().res.setNull(); // default to no result
      skipWhiteSpace();
      if (currentchar() && currentchar()!=';') {
        // switch frame to last thing that will happen: getting the return value
        sp().state = s_returnValue;
        return push(s_newExpression);
      }
      return newstate(s_complete);
    }
    // variable handling
    bool vardef = false;
    bool glob = false;
    bool let = false;
    string varName;
    size_t apos = pos + kwsz; // potential assignment location
    if (strncasecmp("var", kw, kwsz)==0) {
      vardef = true;
    }
    else if (strncasecmp("let", kw, kwsz)==0) {
      let = true;
    }
    else if (strncasecmp("glob", kw, kwsz)==0) {
      vardef = true;
      glob = true;
    }
    if (vardef || let) {
      // explicit assignment statement keyword
      pos = apos;
      skipWhiteSpace();
      // variable name follows
      size_t vsz; const char *vn = getIdentifier(vsz);
      if (!vn) return abortWithSyntaxError("missing variable name after '%.*s'", (int)kwsz, kw);
      varName = lowerCase(vn, vsz);
      pos += vsz;
      apos = pos;
      if (vardef) {
        // is a definition
        if (!glob || variables.find(varName)==variables.end()) {
          variables[varName] = ExpressionValue::nullValue();
          ELOG("Defined %svariable %.*s", glob ? "global" : " ", (int)vsz,vn);
        }
      }
    }
    else {
      // keyword itself is the variable name
      varName.assign(kw, kwsz);
    }
    skipWhiteSpace(apos);
    Operations op = parseOperator(apos);
    // Note: for the unambiguous "var", "global" and "let" cases, allow the equal operator for assignment
    if (op==op_assign || op==op_assignOrEq ||((vardef || let) && op==op_equal)) {
      // definitely: this is an assignment
      pos = apos;
      push(s_assignToVar);
      sp().identifier = varName; // new frame needs the name to assign value later
      return push(s_newExpression); // but first, evaluate the expression
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
  VariablesMap::iterator vpos = variables.find(lowerCase(sp().identifier));
  if (vpos==variables.end()) return abortWithError(ExpressionError::NotFound, "variable '%s' is not declared - use: var name := expression", sp().identifier.c_str());
  if (!sp().skipping) {
    // assign variable
    ELOG("Assigned: %s := %s", sp().identifier.c_str(), sp().res.stringValue().c_str());
    vpos->second = sp().res;
    return popAndPassResult(sp().res);
  }
  return pop();
}


bool ScriptExecutionContext::resumeIfElse()
{
  if (sp().state==s_ifCondition) {
    // if condition is evaluated
    if (currentchar()!=')')  return abortWithSyntaxError("missing ) after if condition");
    pos++;
    sp().state = s_ifTrueStatement;
    sp().flowDecision = sp().res.boolValue();
    return push(s_oneStatement, !sp().flowDecision);
  }
  if (sp().state==s_ifTrueStatement) {
    // if statement (or block of statements) is executed
    // - check for "else" following
    size_t kwsz; const char *kw = getIdentifier(kwsz);
    if (kw && strncasecmp("else", kw, kwsz)==0) {
      pos += kwsz;
      skipWhiteSpace();
      // there might be another if following right away
      kw = getIdentifier(kwsz);
      if (kw && strncasecmp("if", kw, kwsz)==0) {
        pos += kwsz;
        skipWhiteSpace();
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
    if (currentchar()!=')')  return abortWithSyntaxError("missing ) after while condition");
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
  VariablesMap::iterator pos = variables.find(lowerCase(aName));
  if (pos!=variables.end()) {
    aResult = pos->second;
    return true;
  }
  return inherited::valueLookup(aName, aResult);
}


bool ScriptExecutionContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="log" && aArgs.size()>=1 && aArgs.size()<=2) {
    // log (logmessage)
    // log (loglevel, logmessage)
    int loglevel = LOG_INFO;
    int ai = 0;
    if (aArgs.size()>1) {
      if (aArgs[ai].notOk()) return errorInArg(aArgs[ai]);
      loglevel = aArgs[ai].intValue();
      ai++;
    }
    if (aArgs[ai].notOk()) return errorInArg(aArgs[ai]);
    LOG(loglevel, "Script log: %s", aArgs[ai].stringValue().c_str());
  }
  else if (aFunc=="setloglevel" && aArgs.size()==1) {
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]);
    int newLevel = aArgs[0].intValue();
    if (newLevel>=0 && newLevel<=7) {
      int oldLevel = LOGLEVEL;
      SETLOGLEVEL(newLevel);
      LOG(newLevel, "\n\n========== script changed log level from %d to %d ===============", oldLevel, newLevel);
    }
  }
  else if (aFunc=="setscriptlog" && aArgs.size()==1) {
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]);
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
  ELOG("All frozen state is released now for expression: %s", getCode());
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
  ELOG("Timed re-evaluation of expression starting now: %s", getCode());
  triggerEvaluation(evalmode_timed);
}


void TimedEvaluationContext::scheduleLatestEvaluation(MLMicroSeconds aAtTime)
{
  if (updateNextEval(aAtTime)) {
    scheduleReEvaluation(nextEvaluation);
  }
}


TimedEvaluationContext::FrozenResult* TimedEvaluationContext::getFrozen(ExpressionValue &aResult)
{
  FrozenResultsMap::iterator frozenVal = frozenResults.find(aResult.pos);
  FrozenResult* frozenResultP = NULL;
  if (frozenVal!=frozenResults.end()) {
    frozenResultP = &(frozenVal->second);
    // there is a frozen result for this position in the expression
    ELOG("- frozen result (%s) for actual result (%s) at char pos %zu exists - will expire %s",
      frozenResultP->frozenResult.stringValue().c_str(),
      aResult.stringValue().c_str(),
      aResult.pos,
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


TimedEvaluationContext::FrozenResult* TimedEvaluationContext::newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, MLMicroSeconds aFreezeUntil, bool aUpdate)
{
  if (!aExistingFreeze) {
    // nothing frozen yet, freeze it now
    FrozenResult newFreeze;
    newFreeze.frozenResult = aNewResult; // full copy, including pos
    newFreeze.frozenUntil = aFreezeUntil;
    frozenResults[aNewResult.pos] = newFreeze;
    ELOG("- new result (%s) frozen for pos %zu until %s", aNewResult.stringValue().c_str(), aNewResult.pos, MainLoop::string_mltime(newFreeze.frozenUntil).c_str());
    return &frozenResults[aNewResult.pos];
  }
  else if (!aExistingFreeze->frozen() || aUpdate || aFreezeUntil==Never) {
    ELOG("- existing freeze updated to value %s and to expire %s",
      aNewResult.stringValue().c_str(),
      aFreezeUntil==Never ? "IMMEDIATELY" : MainLoop::string_mltime(aFreezeUntil).c_str()
    );
    aExistingFreeze->frozenResult.withValue(aNewResult);
    aExistingFreeze->frozenUntil = aFreezeUntil;
  }
  else {
    ELOG("- no freeze created/updated");
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
bool TimedEvaluationContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="testlater" && aArgs.size()>=2 && aArgs.size()<=3) {
    // testlater(seconds, timedtest [, retrigger])   return "invalid" now, re-evaluate after given seconds and return value of test then. If repeat is true then, the timer will be re-scheduled
    bool retrigger = false;
    if (aArgs.size()>=3) retrigger = aArgs[2].isOk() && aArgs[2].numValue()>0;
    ExpressionValue secs = aArgs[0];
    if (retrigger && secs.numValue()<MIN_RETRIGGER_SECONDS) {
      // prevent too frequent re-triggering that could eat up too much cpu
      LOG(LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", secs.numValue(), (double)MIN_RETRIGGER_SECONDS);
      secs.setNumber(MIN_RETRIGGER_SECONDS);
    }
    ExpressionValue currentSecs = secs;
    FrozenResult* frozenP = getFrozen(currentSecs);
    if (evalMode!=evalmode_timed) {
      if (evalMode!=evalmode_initial) {
        // evaluating non-timed, non-initial means "not yet ready" and must start or extend freeze period
        newFreeze(frozenP, secs, MainLoop::now()+secs.numValue()*Second, true);
      }
      frozenP = NULL;
    }
    else {
      // evaluating timed after frozen period means "now is later" and if retrigger is set, must start a new freeze
      if (frozenP && retrigger) {
        newFreeze(frozenP, secs, MainLoop::now()+secs.numValue()*Second);
      }
    }
    if (frozenP && !frozenP->frozen()) {
      // evaluation runs because freeze is over, return test result
      aResult.setNumber(aArgs[1].numValue());
    }
    else {
      // still frozen, return undefined
      aResult.withError(ExpressionError::Null, "testlater() not yet ready");
    }
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
    if (valueLookUp && valueLookUp(lowerCase(aName), aResult)) return true;
    return inherited::valueLookup(aName, aResult);
  };

  virtual bool evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (functionLookUp && functionLookUp(aFunc, aArgs, aResult)) return true;
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
    if (result.isOk()) {
      rep = result.stringValue();
    }
    else {
      rep = aNullText;
      if (Error::isOK(err)) err = result.err; // only report first error
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

