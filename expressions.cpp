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
#define FOCUSLOGLEVEL 6

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
  if (aVal.strValP)
    setString(*aVal.strValP);
  else
    clrStr();
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
  EvaluationContext::evaluateNumericLiteral(v, *strValP);
  return v.numVal;
}


bool ExpressionValue::operator<(const ExpressionValue& aRightSide) const
{
  if (notOk() || aRightSide.notOk()) return false; // nulls and errors are not orderable
  if (isString()) return *strValP < aRightSide.stringValue();
  return numVal < aRightSide.numValue();
}

bool ExpressionValue::operator==(const ExpressionValue& aRightSide) const
{
  if (notOk() || aRightSide.notOk()) {
    if (
      Error::isError(aRightSide.err, ExpressionError::domain(), ExpressionError::Null) &&
      Error::isError(err, ExpressionError::domain(), ExpressionError::Null)
    ) {
      // special case: both sides NULL counts as equal
      return true;
    }
    // otherwise, nulls and errors are not comparable
    return false;
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


void EvaluationContext::skipWhiteSpace(const char *aExpr, size_t& aPos)
{
  while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++;
}


bool EvaluationContext::skipIdentifier(const char *aExpr, size_t& aPos)
{
  if (!isalpha(aExpr[aPos])) return false; // must start with alpha
  aPos++;
  while (aExpr[aPos] && (isalnum(aExpr[aPos]) || aExpr[aPos]=='_')) aPos++;
  return true; // non-empty identifier
}



EvaluationContext::EvaluationContext(const GeoLocation* aGeoLocationP) :
  geolocationP(aGeoLocationP),
  evaluating(false),
  nextEvaluation(Never)
{
}


EvaluationContext::~EvaluationContext()
{
}


void EvaluationContext::setEvaluationResultHandler(EvaluationResultCB aEvaluationResultHandler)
{
  evaluationResultHandler = aEvaluationResultHandler;
}


bool EvaluationContext::setExpression(const string aExpression)
{
  if (aExpression!=expression) {
    releaseState(); // changing expression unfreezes everything
    expression = aExpression;
    return true;
  }
  return false;
}


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


ExpressionValue EvaluationContext::evaluateNow(EvalMode aEvalMode, bool aScheduleReEval)
{
  nextEvaluation = Never;
  size_t pos = 0;
  return evaluateExpressionPrivate(expression.c_str(), pos, 0, NULL, false, aEvalMode);
}


ErrorPtr EvaluationContext::triggerEvaluation(EvalMode aEvalMode)
{
  ErrorPtr err;
  if (evaluating) {
    LOG(LOG_WARNING, "Apparently cyclic reference in evaluation of expression -> not retriggering: %s", expression.c_str());
    err = Error::err<ExpressionError>(ExpressionError::CyclicReference, "cyclic reference in expression");
  }
  else {
    evaluating = true;
    ExpressionValue res = evaluateNow(aEvalMode, true);
    if (evaluationResultHandler) {
      // this is where cyclic references could cause re-evaluation, which is protected by evaluating==true
      err = evaluationResultHandler(res, *this);
    }
    else {
      // no result handler, pass on error from evaluation
      LOG(LOG_WARNING, "triggerEvaluation() with no result handler for expression: %s", expression.c_str());
      if (res.notOk()) err = res.err;
    }
    evaluating = false;
  }
  return err;
}

// no variables in base class
ExpressionValue EvaluationContext::valueLookup(const string &aName)
{
  // no variables by default
  return ExpressionValue::errValue(ExpressionError::NotFound, "no variable named '%s'", aName.c_str());
}

static const char * const monthNames[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
static const char * const weekdayNames[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

void EvaluationContext::evaluateNumericLiteral(ExpressionValue &res, const string &term)
{
  double v;
  int i;
  if (sscanf(term.c_str(), "%lf%n", &v, &i)!=1) {
    res.withSyntaxError("'%s' is not a valid number, time or date", term.c_str());
    return;
  }
  else {
    // check for time/date literals
    // - time literals (returned in seconds) are in the form h:m or h:m:s, where all parts are allowed to be fractional
    // - month/day literals (returned in yeardays) are in the form dd.monthname or dd.mm. (mid the closing dot)
    if (term.size()>i) {
      if (term[i]==':') {
        // we have 'v:', could be time
        double t;
        int j;
        if (sscanf(term.c_str()+i+1, "%lf%n", &t, &j)!=1) {
          res.withSyntaxError("'%s' is not a valid time specification (hh:mm or hh:mm:ss)", term.c_str());
          return;
        }
        else {
          // we have v:t, take these as hours and minutes
          v = (v*60+t)*60; // in seconds
          j += i+1;
          if (term.size()>j && term[j]==':') {
            // apparently we also have seconds
            if (sscanf(term.c_str()+j+1, "%lf", &t)!=1) {
              res.withSyntaxError("'%s' time specification has invalid seconds (hh:mm:ss)", term.c_str());
              return;
            }
            v += t; // add the seconds
          }
        }
      }
      else {
        int m; int d = -1;
        if (term[i-1]=='.' && isalpha(term[i])) {
          // could be dd.monthname
          string mn = lowerCase(term.substr(i));
          for (m=0; m<12; m++) {
            if (mn==monthNames[m]) {
              // valid monthname following number
              // v = day, m = month-1
              m += 1;
              d = v;
              break;
            }
          }
          if (d<0) {
            res.withSyntaxError("'%s' date specification is invalid (dd.monthname)", term.c_str());
            return;
          }
        }
        else if (term[i]=='.') {
          // must be dd.mm. (with mm. alone, sscanf would have eaten it)
          if (sscanf(term.c_str(), "%d.%d.", &d, &m)!=2) {
            res.withSyntaxError("'%s' date specification is invalid (dd.mm.)", term.c_str());
            return;
          }
        }
        else {
          res.withSyntaxError("unexpected chars in term: '%s'", term.c_str());
          return;
        }
        if (d>=0) {
          struct tm loctim; MainLoop::getLocalTime(loctim);
          loctim.tm_mon = m-1;
          loctim.tm_mday = d;
          mktime(&loctim);
          v = loctim.tm_yday;
        }
      }
    }
  }
  res.withNumber(v);
}


ExpressionValue EvaluationContext::evaluateTerm(const char *aExpr, size_t &aPos, EvalMode aEvalMode)
{
  ExpressionValue res;
  res.pos = aPos;
  // a simple term can be
  // - a variable reference or
  // - a literal number or timespec (h:m or h:m:s)
  // - a literal string (C-string like)
  // Note: a parantesized expression can also be a term, but this is parsed by the caller, not here
  skipWhiteSpace(aExpr, aPos);
  if (aExpr[aPos]=='"') {
    // string literal
    string str;
    aPos++;
    char c;
    while((c = aExpr[aPos])!='"') {
      if (c==0) return res.withSyntaxError("unterminated string, missing \".").withPos(aPos);
      if (c=='\\') {
        c = aExpr[++aPos];
        if (c==0) res.withSyntaxError("incomplete \\-escape").withPos(aPos);
        else if (c=='n') c='\n';
        else if (c=='r') c='\r';
        else if (c=='t') c='\t';
        else if (c=='x') {
          unsigned int h = 0;
          aPos++;
          if (sscanf(aExpr+aPos, "%02x", &h)==1) aPos++;
          c = (char)h;
        }
        // everything else
      }
      str += c;
      aPos++;
    }
    aPos++; // skip closing quote
    res.setString(str);
  }
  else {
    // extract var name or number
    size_t e = aPos;
    while (aExpr[e] && (isalnum(aExpr[e]) || aExpr[e]=='.' || aExpr[e]=='_' || aExpr[e]==':')) e++;
    if (e==aPos) {
      return res.withSyntaxError("missing term");
    }
    // must be simple term
    string term;
    term.assign(aExpr+aPos, e-aPos);
    aPos = e; // advance cursor
    skipWhiteSpace(aExpr, aPos); // skip trailing whitespace
    // decode term
    if (isalpha(term[0])) {
      ErrorPtr err;
      // must be a variable or function call
      if (aExpr[aPos]=='(') {
        // function call
        aPos++; // skip opening paranthesis
        // - collect arguments
        FunctionArgumentVector args;
        skipWhiteSpace(aExpr, aPos);
        while (aExpr[aPos]!=')') {
          if (args.size()>0) aPos++; // skip the separating comma
          ExpressionValue arg = evaluateExpressionPrivate(aExpr, aPos, 0, ",)", true, aEvalMode);
          if (!arg.valueOk()) return arg; // exit, except on null which is ok as a function argument
          args.push_back(arg);
        }
        aPos++; // skip closing paranthesis
        FOCUSLOG("Function '%s' called", term.c_str());
        for (FunctionArgumentVector::iterator pos = args.begin(); pos!=args.end(); ++pos) {
          FOCUSLOG("- argument at char pos=%zu: %s (err=%s)", pos->pos, pos->stringValue().c_str(), Error::text(pos->err));
        }
        // run function
        ExpressionValue fnres;
        if (aEvalMode!=evalmode_noexec) fnres = evaluateFunction(term, args, aEvalMode);
        res.withValue(fnres);
      }
      else {
        // check some reserved values
        if (term=="true" || term=="yes") {
          res.withNumber(1);
        }
        else if (term=="false" || term=="no") {
          res.withNumber(0);
        }
        else if (term=="null" || term=="undefined") {
          res.withError(ExpressionError::Null, "%s", term.c_str());
        }
        else if (aEvalMode!=evalmode_noexec) {
          // must be identifier representing a variable value
          res.withValue(valueLookup(term));
          if (res.notOk() && res.err->isError(ExpressionError::domain(), ExpressionError::NotFound)) {
            // also match some convenience pseudo-vars
            string dn = lowerCase(term);
            for (int w=0; w<7; w++) {
              if (dn==weekdayNames[w]) {
                res.withError(ErrorPtr()); // clear not-found error
                res.withNumber(w); // return numeric value of weekday
                break;
              }
            }
          }
        }
      }
    }
    else {
      // must be a numeric literal (can also be a time literal in hh:mm:ss or hh:mm form or a yearday in dd.monthname or dd.mm. form)
      evaluateNumericLiteral(res, term);
    }
  }
  // valid term
  if (res.isOk() && aEvalMode!=evalmode_noexec) {
    FOCUSLOG("Term '%.*s' evaluation result: %s", (int)(aPos-res.pos), aExpr+res.pos, res.stringValue().c_str());
  }
  else {
    FOCUSLOG("Term '%.*s' evaluation error: %s", (int)(aPos-res.pos), aExpr+res.pos, Error::text(res.err));
  }
  return res;
}


// operations with precedence
typedef enum {
  op_none     = 0x06,
  op_not      = 0x16,
  op_multiply = 0x25,
  op_divide   = 0x35,
  op_add      = 0x44,
  op_subtract = 0x54,
  op_equal    = 0x63,
  op_notequal = 0x73,
  op_less     = 0x83,
  op_greater  = 0x93,
  op_leq      = 0xA3,
  op_geq      = 0xB3,
  op_and      = 0xC2,
  op_or       = 0xD2,
  opmask_precedence = 0x0F
} Operations;


// a + 3 * 4

static Operations parseOperator(const char *aExpr, size_t &aPos)
{
  p44::ScriptExecutionContext::skipWhiteSpace(aExpr, aPos);
  // check for operator
  Operations op = op_none;
  switch (aExpr[aPos++]) {
    case '*': op = op_multiply; break;
    case '/': op = op_divide; break;
    case '+': op = op_add; break;
    case '-': op = op_subtract; break;
    case '&': op = op_and; break;
    case '|': op = op_or; break;
    case '=': op = op_equal; break;
    case '<': {
      if (aExpr[aPos]=='=') {
        aPos++; op = op_leq; break;
      }
      else if (aExpr[aPos]=='>') {
        aPos++; op = op_notequal; break;
      }
      op = op_less; break;
    }
    case '>': {
      if (aExpr[aPos]=='=') {
        aPos++; op = op_geq; break;
      }
      op = op_greater; break;
    }
    case '!': {
      if (aExpr[aPos]=='=') {
        aPos++; op = op_notequal; break;
      }
      op = op_not; break;
      break;
    }
    default: --aPos; // no expression char
  }
  p44::ScriptExecutionContext::skipWhiteSpace(aExpr, aPos);
  return op;
}


ExpressionValue EvaluationContext::evaluateExpressionPrivate(const char *aExpr, size_t &aPos, int aPrecedence, const char *aStopChars, bool aNeedStopChar, EvalMode aEvalMode)
{
  ExpressionValue res;
  res.pos = aPos;
  // check for optional unary op
  Operations unaryop = parseOperator(aExpr, aPos);
  if (unaryop!=op_none) {
    if (unaryop!=op_subtract && unaryop!=op_not) {
      return res.withSyntaxError("invalid unary operator");
    }
  }
  // evaluate term
  // - check for paranthesis term
  if (aExpr[aPos]=='(') {
    // term is expression in paranthesis
    aPos++;
    res = evaluateExpressionPrivate(aExpr, aPos, 0, ")", false, aEvalMode);
    if (res.syntaxOk()) return res;
    if (aExpr[aPos]!=')') {
      return res.withSyntaxError("Missing ')'").withPos(aPos);
    }
    aPos++;
  }
  else {
    // must be simple term
    res = evaluateTerm(aExpr, aPos, aEvalMode);
    if (Error::isError(res.err, ExpressionError::domain(), ExpressionError::Syntax)) return res;
  }
  // apply unary ops if any
  switch (unaryop) {
    case op_not : res.setNumber(res.numValue() > 0 ? 0 : 1); break;
    case op_subtract : res.setNumber(-res.numValue()); break;
    default: break;
  }
  while (aExpr[aPos]) {
    // now check for operator and precedence
    size_t opIdx = aPos;
    Operations binaryop = parseOperator(aExpr, opIdx);
    int precedence = binaryop & opmask_precedence;
    // end parsing here if end of text, stopchar or operator with a lower or same precedence as the passed in precedence is reached
    if ((aStopChars && strchr(aStopChars, aExpr[opIdx])) || precedence<=aPrecedence)
      break; // break because of stopchar or end of higher precedence subexpression
    if (aExpr[opIdx]==0) {
      // end of text
      if (aStopChars && aNeedStopChar) return res.withSyntaxError("expected one of %s", aStopChars);
      break;
    }
    // prevent loop
    if (binaryop==op_none) {
      return res.withSyntaxError("Invalid operator: '%s'", aExpr+opIdx).withPos(aPos);
    }
    // must parse right side of operator as subexpression
    aPos = opIdx; // advance past operator
    ExpressionValue rightside = evaluateExpressionPrivate(aExpr, aPos, precedence, aStopChars, aNeedStopChar, aEvalMode);
    if (aEvalMode!=evalmode_noexec) {
      // - equality comparison is the only thing that also inlcudes "undefined", so do it first
      if (binaryop==op_equal)
        res.setBool(res == rightside);
      else if (binaryop==op_notequal)
        res.setBool(!(res == rightside));
      else {
        if (rightside.notOk()) res=rightside;
        if (res.isOk()) {
          // apply the operation between leftside and rightside
          switch (binaryop) {
            case op_not: {
              return res.withSyntaxError("NOT operator not allowed here").withPos(aPos);
            }
            case op_divide: res.withValue(res / rightside); break;
            case op_multiply: res.withValue(res * rightside); break;
            case op_add: res.withValue(res + rightside); break;
            case op_subtract: res.withValue(res - rightside); break;
            case op_less: res.setBool(res < rightside); break;
            case op_greater: res.setBool(!(res < rightside) && !(res == rightside)); break;
            case op_leq: res.setBool((res < rightside) || (res == rightside)); break;
            case op_geq: res.setBool(!(res < rightside)); break;
            case op_and: res.withValue(res && rightside); break;
            case op_or: res.withValue(res || rightside); break;
            default: break;
          }
        }
      }
      if (res.isOk()) {
        FOCUSLOG("Intermediate expression '%.*s' evaluation result: %s", (int)(aPos-res.pos), aExpr+res.pos, res.stringValue().c_str());
      }
      else {
        FOCUSLOG("Intermediate expression '%.*s' evaluation result is INVALID", (int)(aPos-res.pos), aExpr+res.pos);
      }
    }
  } // while expression ongoing
  // done
  return res;
}


ExpressionValue TimedEvaluationContext::evaluateNow(EvalMode aEvalMode, bool aScheduleReEval)
{
  ExpressionValue res = inherited::evaluateNow(aEvalMode, aScheduleReEval);
  if (aEvalMode!=evalmode_noexec) {
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
      FOCUSLOG("Expression demands re-evaluation at %s: %s", MainLoop::string_mltime(nextEvaluation).c_str(), expression.c_str());
    }
    if (aScheduleReEval) {
      scheduleReEvaluation(nextEvaluation);
    }
  }
  return res;
}



// MARK: - standard functions available in every context

#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function

// standard functions available in every context
ExpressionValue EvaluationContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode)
{
  if (aFunc=="ifvalid" && aArgs.size()==2) {
    // ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
    return ExpressionValue(aArgs[0].isOk() ? aArgs[0] : aArgs[1]);
  }
  if (aFunc=="isvalid" && aArgs.size()==1) {
    // ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
    return ExpressionValue(aArgs[0].isOk() ? 1 : 0);
  }
  else if (aFunc=="if" && aArgs.size()==3) {
    // if (c, a, b)    if c evaluates to true, return a, otherwise b
    if (aArgs[0].notOk()) return aArgs[0]; // return error from condition
    return ExpressionValue(aArgs[0].boolValue() ? aArgs[1] : aArgs[2]);
  }
  else if (aFunc=="abs" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    return ExpressionValue(fabs(aArgs[0].numValue()));
  }
  else if (aFunc=="int" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    return ExpressionValue(fabs(aArgs[0].int64Value()));
  }
  else if (aFunc=="round" && (aArgs.size()>=1 || aArgs.size()<=2)) {
    // round (a)       round value to integer
    // round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    double precision = 1;
    if (aArgs.size()>=2) {
      if (aArgs[1].notOk()) return aArgs[0]; // return error from argument
      precision = aArgs[1].numValue();
    }
    return ExpressionValue(round(aArgs[0].numValue()/precision)*precision);
  }
  else if (aFunc=="random" && aArgs.size()==2) {
    // random (a,b)     random value from a up to and including b
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    if (aArgs[1].notOk()) return aArgs[1]; // return error from argument
    // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
    return ExpressionValue(aArgs[0].numValue() + (double)rand()*(aArgs[1].numValue()-aArgs[0].numValue())/((double)RAND_MAX));
  }
  else if (aFunc=="string" && aArgs.size()==1) {
    // string(anything)
    return ExpressionValue(aArgs[0].stringValue()); // force convert to string, including nulls and errors
  }
  else if (aFunc=="number" && aArgs.size()==1) {
    // number(anything)
    if (aArgs[0].notOk()) return aArgs[0]; // pass null and errors
    return ExpressionValue(aArgs[0].numValue()); // force convert to numeric
  }
  else if (aFunc=="strlen" && aArgs.size()==1) {
    // strlen(string)
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    return ExpressionValue(aArgs[0].stringValue().size()); // length of string
  }
  else if (aFunc=="substr" && aArgs.size()>=2 && aArgs.size()<=3) {
    // substr(string, from)
    // substr(string, from, count)
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    string s = aArgs[0].stringValue();
    if (aArgs[1].notOk()) return aArgs[1]; // return error from argument
    size_t start = aArgs[1].intValue();
    if (start>s.size()) start = s.size();
    size_t count = string::npos; // to the end
    if (aArgs.size()>=3) {
      if (aArgs[2].notOk()) return aArgs[0]; // return error from argument
      count = aArgs[2].intValue();
    }
    return ExpressionValue(s.substr(start, count));
  }
  else if (aFunc=="find" && aArgs.size()>=2 && aArgs.size()<=3) {
    // find(haystack, needle)
    // find(haystack, needle, from)
    string haystack = aArgs[0].stringValue(); // haystack can be anything, including invalid
    if (aArgs[1].notOk()) return aArgs[1]; // return error from argument
    string needle = aArgs[1].stringValue();
    size_t start = 0;
    if (aArgs.size()>=3) {
      start = aArgs[2].intValue();
      if (start>haystack.size()) start = haystack.size();
    }
    if (aArgs[0].isOk()) {
      size_t p = haystack.find(needle, start);
      if (p!=string::npos) return ExpressionValue(p);
    }
    return ExpressionValue::nullValue(); // not found
  }
  else if (aFunc=="format" && aArgs.size()==2) {
    // format(formatstring, number)
    // only % + - 0..9 . d, x, and f supported
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    string fmt = aArgs[0].stringValue();
    if (
      fmt.size()<2 ||
      fmt[0]!='%' ||
      fmt.substr(1,fmt.size()-2).find_first_not_of("+-0123456789.")!=string::npos || // excluding last digit
      fmt.find_first_not_of("duxXeEgGf", fmt.size()-1)!=string::npos // which must be d,x or f
    ) {
      return ExpressionValue::errValue(ExpressionError::Syntax, "invalid format string, only basic %%duxXeEgGf specs allowed").withPos(aArgs[0].pos);
    }
    if (fmt.find_first_of("duxX", fmt.size()-1)!=string::npos)
      return ExpressionValue(string_format(fmt.c_str(), aArgs[1].intValue())); // int format
    else
      return ExpressionValue(string_format(fmt.c_str(), aArgs[1].numValue())); // double format
  }
  else if (aFunc=="errormessage" && aArgs.size()==1) {
    // errormessage(value)
    ErrorPtr err = aArgs[0].err;
    if (Error::isOK(err)) return ExpressionValue::nullValue(); // no error, no message
    return ExpressionValue(err->getErrorMessage());
  }
  else if (aFunc=="errordescription" && aArgs.size()==1) {
    // errordescription(value)
    return ExpressionValue(aArgs[0].err->text());
  }
  else if (aFunc=="eval" && aArgs.size()==1) {
    // eval(string)    have string evaluated as expression
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    size_t pos = 0;
    ExpressionValue evalRes = evaluateExpressionPrivate(aArgs[0].stringValue().c_str(), pos, 0, NULL, false, aEvalMode);
    if (evalRes.notOk()) {
      FOCUSLOG("eval(\"%s\") returns error '%s' in expression: %s", aArgs[0].stringValue().c_str(), evalRes.err->text(), expression.c_str());
      // do not cause syntax error, only invalid result, but with error message included
      evalRes.withError(Error::err<ExpressionError>(ExpressionError::Null, "eval() error: %s -> undefined", evalRes.err->text()));
    }
    return evalRes;
  }
  else if (aFunc=="is_weekday" && aArgs.size()>0) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    // check if any of the weekdays match
    int weekday = loctim.tm_wday; // 0..6, 0=sunday
    ExpressionValue newRes(0);
    newRes.pos = aArgs[0].pos; // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
    for (int i = 0; i<aArgs.size(); i++) {
      if (aArgs[i].notOk()) return aArgs[i]; // return error from argument
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
    return res; // freeze time over, use actual, newly calculated result
  }
  else if ((aFunc=="after_time" || aFunc=="is_time") && aArgs.size()>=1) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    ExpressionValue newSecs;
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    newSecs.pos = aArgs[0].pos; // Note: we use pos of first argument for freezing the seconds
    if (aArgs.size()==2) {
      // legacy spec
      if (aArgs[1].notOk()) return aArgs[0]; // return error from argument
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
    FOCUSLOG("is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim)).c_str());
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
    return ExpressionValue(res);
  }
  else if (aFunc=="between_dates" || aFunc=="between_yeardays") {
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    if (aArgs[1].notOk()) return aArgs[1]; // return error from argument
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
    return ExpressionValue((currentYday>=smaller && currentYday<=larger)!=lastBeforeFirst);
  }
  else if (aFunc=="sunrise" && aArgs.size()==0) {
    if (!geolocationP) return ExpressionValue::nullValue();
    return ExpressionValue(sunrise(time(NULL), *geolocationP, false)*3600);
  }
  else if (aFunc=="dawn" && aArgs.size()==0) {
    if (!geolocationP) return ExpressionValue::nullValue();
    return ExpressionValue(sunrise(time(NULL), *geolocationP, true)*3600);
  }
  else if (aFunc=="sunset" && aArgs.size()==0) {
    if (!geolocationP) return ExpressionValue::nullValue();
    return ExpressionValue(sunset(time(NULL), *geolocationP, false)*3600);
  }
  else if (aFunc=="dusk" && aArgs.size()==0) {
    if (!geolocationP) return ExpressionValue::nullValue();
    return ExpressionValue(sunset(time(NULL), *geolocationP, true)*3600);
  }
  else {
    double fracSecs;
    struct tm loctim; MainLoop::getLocalTime(loctim, &fracSecs);
    if (aFunc=="timeofday" && aArgs.size()==0) {
      return ExpressionValue(((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs);
    }
    else if (aFunc=="hour" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_hour);
    }
    else if (aFunc=="minute" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_min);
    }
    else if (aFunc=="second" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_sec);
    }
    else if (aFunc=="year" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_year+1900);
    }
    else if (aFunc=="month" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_mon+1);
    }
    else if (aFunc=="day" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_mday);
    }
    else if (aFunc=="weekday" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_wday);
    }
    else if (aFunc=="yearday" && aArgs.size()==0) {
      return ExpressionValue(loctim.tm_yday);
    }
  }
  // no such function
  return ExpressionValue::errValue(ExpressionError::NotFound, "Unknown function '%s' with %lu arguments", aFunc.c_str(), aArgs.size());
}



// MARK: - ScriptExecutionContext

#if EXPRESSION_SCRIPT_SUPPORT

ScriptExecutionContext::ScriptExecutionContext(const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP)
{
}


ScriptExecutionContext::~ScriptExecutionContext()
{
}


void ScriptExecutionContext::clearVariables()
{
  variables.clear();
}


ExpressionValue ScriptExecutionContext::runAsScript()
{
  size_t pos = 0;
  ExpressionValue res;
  while (pos<expression.size()) {
    res = runStatementPrivate(expression.c_str(), pos, evalmode_script, false);
    if (!res.valueOk()) break;
  }
  return res;
}


ExpressionValue ScriptExecutionContext::valueLookup(const string &aName)
{
  VariablesMap::iterator pos = variables.find(aName);
  if (pos!=variables.end()) {
    return pos->second;
  }
  return inherited::valueLookup(aName);
}


ExpressionValue ScriptExecutionContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode)
{
  if (aFunc=="log" && aArgs.size()>=1 && aArgs.size()<=2) {
    // log (logmessage)
    // log (loglevel, logmessage)
    int loglevel = LOG_INFO;
    int ai = 0;
    if (aArgs.size()>1) {
      if (aArgs[ai].notOk()) return aArgs[ai];
      loglevel = aArgs[ai].intValue();
      ai++;
    }
    if (aArgs[ai].notOk()) return aArgs[ai];
    LOG(loglevel, "Script log: %s", aArgs[ai].stringValue().c_str());
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aEvalMode);
  }
  // procedure with no return value of itself
  return ExpressionValue::nullValue();
}




// TODO: refactor for interruptable
// - ALL FUNCTIONS WITHOUT PARAMETERS!!! (except for the context itself)
// - Execution State instead
//   - the finalize callback
//   - WHAT ARE NOW BYREF PARAMS:
//     - pos    : current scanning position
//   - stack of stackframes containing:
//     - WHAT ARE NOW BYVAL PARAMS AND ARE ACTUALLY CHANGED LOCALLY, AND: LOCAL VARS
//       - res
//       - evalMode : current evaluation mode
//       -

// TODO: PRINCIPLES
// - INSTEAD OF RECURSION, we have a loop and create stack entries
// - evaluation methods can NOT have local vars in blocks that also call other evaluation methods
// - essentially only 2 functions
//   - initEvaluation(can have params, for example the callback)
//   - bool continueEvaluation()
//     - if returns true, caller is responsible for calling again
//     - otherwise, a callback will call it again and caller MUST NOT call it!
//   - a convenience function to repeatedly call continueEvaluation() until it does not return true.
//     - this could determine max execution time and pause via mainloop when uninterrupted time is too long



// TODO: HOWTO




ExpressionValue ScriptExecutionContext::runStatementPrivate(const char *aScript, size_t &aPos, EvalMode aEvalMode, bool aInBlock)
{
  ExpressionValue res;
  ExpressionValue flowDecision = false;
  enum {
    stmt_single,
    stmt_if,
    stmt_else,
    stmt_chainif
  } stmtMode = stmt_single;
  while (aScript[aPos]) {
    skipWhiteSpace(aScript, aPos);
    // beginning of statement segment
    res.withPos(aPos);
    if (aScript[aPos]=='{') {
      // block containing multiple statements
      while (aScript[aPos]) {
        res = runStatementPrivate(aScript, aPos, aEvalMode, true);
        if (!res.syntaxOk()) return res;
        if (aScript[aPos]=='}') {
          aPos++;
          break;
        }
      }
    }
    else {
      // single statement
      // - check for keywords
      bool languageconstruct = false;
      size_t kpos = aPos;
      if (skipIdentifier(aScript, kpos)) {
        string keyword;
        keyword.assign(aScript+aPos, kpos-aPos);
        skipWhiteSpace(aScript, kpos);
        // could be a language keyword
        languageconstruct = true;
        if (keyword=="if") {
          aPos = kpos;
          // if (expression) statement [else statement]
          skipWhiteSpace(aScript, aPos);
          if (aScript[aPos]!='(') return res.withSyntaxError("missing ( after if");
          aPos++; // skip opening (
          flowDecision = evaluateExpressionPrivate(aScript, aPos, 0, ")", true, aEvalMode);
          if (!flowDecision.syntaxOk()) return flowDecision;
          aPos++; // skip closing )
          // run statement after if
          stmtMode = stmtMode==stmt_else ? stmt_chainif : stmt_if;
          res = runStatementPrivate(aScript, aPos, !flowDecision.boolValue() ? evalmode_noexec : aEvalMode, false);
        }
        else if (stmtMode==stmt_else || keyword=="else") {
          aPos = kpos;
          // ...else [if (expression)] statement
          if (stmtMode!=stmt_if) return res.withSyntaxError("else without preceeding if");
          // run statement after else
          stmtMode = stmt_else;
          flowDecision.boolValue();
          return res.withSyntaxError("%%%% NOT IMPLEMENTED");
        }
        else if (keyword=="return") {
          aPos = kpos;
          if (aScript[kpos] && strchr(";}", aScript[kpos])==NULL) {
            // return is followed by an expression
            res = evaluateExpressionPrivate(aScript, aPos, 0, ";}", false, aEvalMode);
          }
          // anyway, executions ends here
          // FIXME: does not work yet to exit all levels!! -> need stmtmode_return for that...
          return res;
        }
        else {
          // could be a variable assignment or declaration
          bool isVarDef = false;
          bool isGlobal = keyword=="global";
          if (keyword=="var" || isGlobal) {
            skipWhiteSpace(aScript, kpos);
            size_t vpos = kpos;
            if (!skipIdentifier(aScript, vpos)) return res.withSyntaxError("missing variable name after '%s'", keyword.c_str());
            keyword.assign(aScript+kpos, vpos-kpos);
            kpos = vpos;
            if (!isGlobal || variables.find(keyword)==variables.end()) {
              variables[keyword] = ExpressionValue::nullValue();
              FOCUSLOG("Defined %s variable %s", isGlobal ? "permanent" : "temporary", keyword.c_str());
            }
            isVarDef = true;
          }
          skipWhiteSpace(aScript, kpos);
          if (aScript[kpos]==':' && aScript[kpos+1]=='=') {
            // assignment
            aPos = kpos+2;
            VariablesMap::iterator pos = variables.find(keyword);
            if (pos==variables.end()) return res.withError(ExpressionError::NotFound, "variable '%s' is not declared, use: var name := expression", keyword.c_str());
            res = evaluateExpressionPrivate(aScript, aPos, 0, ";", false, aEvalMode);
            if (!res.valueOk()) return res;
            if (aEvalMode!=evalmode_noexec) {
              // assign variable
              FOCUSLOG("Assigned: %s := %s", keyword.c_str(), res.stringValue().c_str());
              pos->second = res;
            }
          }
          else {
            // not an assignment..
            if (!isVarDef) languageconstruct = false; // ..and not a vardef, either -> no language construct
            else aPos = kpos; // ..but it IS a the variable def language construct
          }
        }
      }
      if (!languageconstruct) {
        // not a language construct statement, just an expression to run
        res = evaluateExpressionPrivate(aScript, aPos, 0, aInBlock ? ";}" : ";", false, aEvalMode);
      }
    }
    // end of statement segment
    skipWhiteSpace(aScript, aPos);
    if (aScript[aPos]==';') {
      aPos++; // may be terminated by a semicolon
    }
    // FIXME: handle else and chained ifs
    break;
  }
  return res;
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
  FOCUSLOG("All frozen state is released now for expression: %s", expression.c_str());
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
  FOCUSLOG("Timed re-evaluation of expression starting now: %s", expression.c_str());
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
    FOCUSLOG("- frozen result (%s) for actual result (%s) at char pos %zu exists - will expire %s",
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
    FOCUSLOG("- new result (%s) frozen for pos %zu until %s", aNewResult.stringValue().c_str(), aNewResult.pos, MainLoop::string_mltime(newFreeze.frozenUntil).c_str());
    return &frozenResults[aNewResult.pos];
  }
  else if (!aExistingFreeze->frozen() || aUpdate || aFreezeUntil==Never) {
    FOCUSLOG("- existing freeze updated to value %s and to expire %s",
      aNewResult.stringValue().c_str(),
      aFreezeUntil==Never ? "IMMEDIATELY" : MainLoop::string_mltime(aFreezeUntil).c_str()
    );
    aExistingFreeze->frozenResult.withValue(aNewResult);
    aExistingFreeze->frozenUntil = aFreezeUntil;
  }
  else {
    FOCUSLOG("- no freeze created/updated");
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
ExpressionValue TimedEvaluationContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode)
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
    if (aEvalMode!=evalmode_timed) {
      if (aEvalMode!=evalmode_initial) {
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
      return ExpressionValue(aArgs[1].numValue());
    }
    else {
      // still frozen, return undefined
      return ExpressionValue::errValue(ExpressionError::Null, "testlater() not yet ready");
    }
  }
  else if (aFunc=="initial" && aArgs.size()==0) {
    // initial()  returns true if this is a "initial" run of the evaluator, meaning after startup or expression changes
    return ExpressionValue(aEvalMode==evalmode_initial);
  }
  return inherited::evaluateFunction(aFunc, aArgs, aEvalMode);
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
    setExpression(aExpression);
    return evaluateNow(evalmode_initial);
  };

protected:

  virtual ExpressionValue valueLookup(const string &aName) P44_OVERRIDE
  {
    if (valueLookUp) return valueLookUp(aName);
    return inherited::valueLookup(aName);
  };

  virtual ExpressionValue evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode) P44_OVERRIDE
  {
    if (functionLookUp) return functionLookUp(aFunc, aArgs);
    return inherited::evaluateFunction(aFunc, aArgs, aEvalMode);
  };

};
typedef boost::intrusive_ptr<AdHocEvaluationContext> AdHocEvaluationContextPtr;


ExpressionValue p44::evaluateExpression(const string &aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB)
{
  AdHocEvaluationContext ctx(aValueLookupCB, aFunctionLookpCB);
  ctx.isMemberVariable();
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

