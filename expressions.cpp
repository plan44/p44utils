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


// MARK: - expression value

string ExpressionValue::stringValue() const
{
  if (isOk()) {
    return string_format("%lg", v);
  }
  else {
    return "unknown";
  }
}



// MARK: - expression values and errors


ErrorPtr ExpressionError::err(ErrorCodes aErrCode, const char *aFmt, ...)
{
  Error *errP = new ExpressionError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args);
  va_end(args);
  return ErrorPtr(errP);
}


//ExpressionValue ExpressionError::errValue(ErrorCodes aErrCode, const char *aFmt, ...)
//{
//  Error *errP = new ExpressionError(aErrCode);
//  va_list args;
//  va_start(args, aFmt);
//  errP->setFormattedMessage(aFmt, args);
//  va_end(args);
//  return ExpressionValue(ErrorPtr(errP));
//}

ExpressionValue ExpressionValue::withError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)
{
  err = new ExpressionError(aErrCode);
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



// MARK: - EvaluationContext


EvaluationContext::EvaluationContext() :
  evalMode(evalmode_initial),
  evaluating(false)
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


ExpressionValue EvaluationContext::evaluateNow(EvalMode aEvalMode, bool aScheduleReEval)
{
  if (aEvalMode!=evalmode_current) evalMode = aEvalMode;
  size_t pos = 0;
  return evaluateExpressionPrivate(expression.c_str(), pos, 0);
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


// standard functions available in every context
ExpressionValue EvaluationContext::evaluateFunction(const string &aName, const FunctionArgumentVector &aArgs)
{
  if (aName=="ifvalid" && aArgs.size()==2) {
    // ifvalid(a, b)   if a is a valid value, return it, otherwise return the default as specified by b
    return ExpressionValue(aArgs[0].isOk() ? aArgs[0] : aArgs[1]);
  }
  else if (aName=="if" && aArgs.size()==3) {
    // if (c, a, b)    if c evaluates to true, return a, otherwise b
    if (aArgs[0].notOk()) return aArgs[0]; // return error from condition
    return ExpressionValue(aArgs[0].v!=0 ? aArgs[1] : aArgs[2]);
  }
  else if (aName=="abs" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    return ExpressionValue(fabs(aArgs[0].v));
  }
  else if (aName=="round" && (aArgs.size()==1 || aArgs.size()==2)) {
    // round (a)       round value to integer
    // round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    double precision = 1;
    if (aArgs.size()>=2) {
      if (aArgs[1].notOk()) return aArgs[0]; // return error from argument
      precision = aArgs[1].v;
    }
    return ExpressionValue(round(aArgs[0].v/precision)*precision);
  }
  else if (aName=="random" && aArgs.size()==2) {
    // random (a,b)     random value from a up to and including b
    if (aArgs[0].notOk()) return aArgs[0]; // return error from argument
    if (aArgs[1].notOk()) return aArgs[1]; // return error from argument
    // rand(): returns a pseudo-random integer value between ​0​ and RAND_MAX (0 and RAND_MAX included).
    return ExpressionValue(aArgs[0].v + (double)rand()*(aArgs[1].v-aArgs[0].v)/((double)RAND_MAX));
  }
  // no such function
  return ExpressionValue::errValue(ExpressionError::NotFound, "Unknown function '%s' with %lu arguments", aName.c_str(), aArgs.size());
}


// no variables in base class
ExpressionValue EvaluationContext::valueLookup(const string &aName)
{
  // no variables by default
  return ExpressionValue::errValue(ExpressionError::NotFound, "no variable named '%s'", aName.c_str());
}

static const char * const monthNames[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };

ExpressionValue EvaluationContext::evaluateTerm(const char *aExpr, size_t &aPos)
{
  ExpressionValue res;
  res.pos = aPos;
  // a simple term can be
  // - a variable reference or
  // - a literal number or timespec (h:m or h:m:s)
  // Note: a parantesized expression can also be a term, but this is parsed by the caller, not here
  while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++; // skip whitespace
  // extract var name or number
  size_t e = aPos;
  while (aExpr[e] && (isalnum(aExpr[e]) || aExpr[e]=='.' || aExpr[e]=='_' || aExpr[e]==':')) e++;
  if (e==aPos) {
    return res.withError(ExpressionError::Syntax, "missing term");
  }
  // must be simple term
  string term;
  term.assign(aExpr+aPos, e-aPos);
  aPos = e; // advance cursor
  // skip trailing whitespace
  while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++; // skip whitespace
  // decode term
  if (isalpha(term[0])) {
    ErrorPtr err;
    // must be a variable or function call
    if (aExpr[aPos]=='(') {
      // function call
      aPos++; // skip opening paranthesis
      // - collect arguments
      FunctionArgumentVector args;
      while (true) {
        while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++; // skip whitespace
        if (aExpr[aPos]==')')
          break; // no more arguments
        if (args.size()!=0) {
          if (aExpr[aPos]!=',')
            return res.withError(ExpressionError::Syntax, "missing comma or closing ')'");
          aPos++; // skip comma
        }
        ExpressionValue arg = evaluateExpressionPrivate(aExpr, aPos, 0);
        if (arg.notOk() && !arg.err->isError("ExpressionError", ExpressionError::Null))
          return arg; // exit, except on null which is ok as a function argument
        args.push_back(arg);
      }
      aPos++; // skip closing paranthesis
      FOCUSLOG("Function '%s' called", term.c_str());
      for (FunctionArgumentVector::iterator pos = args.begin(); pos!=args.end(); ++pos) {
        FOCUSLOG("- argument at char pos=%zu: %lf (err=%s)", pos->pos, pos->v, Error::text(pos->err));
      }
      // run function
      res.withValue(evaluateFunction(term, args));
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
      else {
        // must be identifier representing a variable value
        res.withValue(valueLookup(term));
      }
    }
  }
  else {
    // must be a numeric literal (can also be a time literal in hh:mm:ss or hh:mm form or a yearday in dd.monthname or dd.mm. form)
    double v;
    int i;
    if (sscanf(term.c_str(), "%lf%n", &v, &i)!=1) {
      return res.withError(ExpressionError::Syntax, "'%s' is not a valid number, time or date", term.c_str());
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
            return res.withError(ExpressionError::Syntax, "'%s' is not a valid time specification (hh:mm or hh:mm:ss)", term.c_str());
          }
          else {
            // we have v:t, take these as hours and minutes
            v = (v*60+t)*60; // in seconds
            j += i+1;
            if (term.size()>j && term[j]==':') {
              // apparently we also have seconds
              if (sscanf(term.c_str()+j+1, "%lf", &t)!=1) {
                return res.withError(ExpressionError::Syntax, "'%s' time specification has invalid seconds (hh:mm:ss)", term.c_str());
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
            if (d<0) return res.withError(ExpressionError::Syntax, "'%s' date specification is invalid (dd.monthname)", term.c_str());
          }
          else if (term[i]=='.') {
            // must be dd.mm. (with mm. alone, sscanf would have eaten it)
            if (sscanf(term.c_str(), "%d.%d.", &d, &m)!=2) {
              return res.withError(ExpressionError::Syntax, "'%s' date specification is invalid (dd.mm.)", term.c_str());
            }
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
  // valid term
  if (res.isOk()) {
    FOCUSLOG("Term '%.*s' evaluation result: %lf", (int)(aPos-res.pos), aExpr+res.pos, res.v);
  }
  else {
    FOCUSLOG("Term '%.*s' evaluation error: %s", (int)(aPos-res.pos), aExpr+res.pos, res.err->text());
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
  while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++; // skip whitespace
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
  while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++; // skip whitespace
  return op;
}


ExpressionValue EvaluationContext::evaluateExpressionPrivate(const char *aExpr, size_t &aPos, int aPrecedence)
{
  ExpressionValue res;
  res.pos = aPos;
  // check for optional unary op
  Operations unaryop = parseOperator(aExpr, aPos);
  if (unaryop!=op_none) {
    if (unaryop!=op_subtract && unaryop!=op_not) {
      return res.withError(ExpressionError::Syntax, "invalid unary operator");
    }
  }
  // evaluate term
  // - check for paranthesis term
  if (aExpr[aPos]=='(') {
    // term is expression in paranthesis
    aPos++;
    res = evaluateExpressionPrivate(aExpr, aPos, 0);
    if (Error::isError(res.err, ExpressionError::domain(), ExpressionError::Syntax)) return res;
    if (aExpr[aPos]!=')') {
      return res.withError(ExpressionError::Syntax, "Missing ')'").withPos(aPos);
    }
    aPos++;
  }
  else {
    // must be simple term
    res = evaluateTerm(aExpr, aPos);
    if (Error::isError(res.err, ExpressionError::domain(), ExpressionError::Syntax)) return res;
  }
  // apply unary ops if any
  switch (unaryop) {
    case op_not : res.v = res.v > 0 ? 0 : 1; break;
    case op_subtract : res.v = -res.v; break;
    default: break;
  }
  while (aExpr[aPos]) {
    // now check for operator and precedence
    size_t opIdx = aPos;
    Operations binaryop = parseOperator(aExpr, opIdx);
    int precedence = binaryop & opmask_precedence;
    // end parsing here if end of text, paranthesis or argument reached or operator has a lower or same precedence as the passed in precedence
    if (aExpr[opIdx]==0 || aExpr[opIdx]==')' || aExpr[opIdx]==',' || precedence<=aPrecedence) {
      // what we have so far is the result
      break;
    }
    // prevent loop
    if (binaryop==op_none) {
      return res.withError(ExpressionError::Syntax, "Invalid operator: '%s'", aExpr+opIdx).withPos(aPos);
    }
    // must parse right side of operator as subexpression
    aPos = opIdx; // advance past operator
    ExpressionValue rightside = evaluateExpressionPrivate(aExpr, aPos, precedence);
    if (rightside.notOk()) res=rightside;
    if (res.isOk()) {
      // apply the operation between leftside and rightside
      switch (binaryop) {
        case op_not: {
          return res.withError(ExpressionError::Syntax, "NOT operator not allowed here").withPos(aPos);
        }
        case op_divide:
          if (rightside.v==0) return res.withError(ExpressionError::DivisionByZero, "division by zero").withPos(aPos);
          res.v = res.v/rightside.v;
          break;
        case op_multiply: res.v = res.v*rightside.v; break;
        case op_add: res.v = res.v+rightside.v; break;
        case op_subtract: res.v = res.v-rightside.v; break;
        case op_equal: res.v = res.v==rightside.v; break;
        case op_notequal: res.v = res.v!=rightside.v; break;
        case op_less: res.v = res.v < rightside.v; break;
        case op_greater: res.v = res.v > rightside.v; break;
        case op_leq: res.v = res.v <= rightside.v; break;
        case op_geq: res.v = res.v >= rightside.v; break;
        case op_and: res.v = res.v && rightside.v; break;
        case op_or: res.v = res.v || rightside.v; break;
        default: break;
      }
      FOCUSLOG("Intermediate expression '%.*s' evaluation result: %lf", (int)(aPos-res.pos), aExpr+res.pos, res.v);
    }
    else {
      FOCUSLOG("Intermediate expression '%.*s' evaluation result is INVALID", (int)(aPos-res.pos), aExpr+res.pos);
    }
  }
  // done
  return res;
}


bool TimedEvaluationContext::updateNextEval(const MLMicroSeconds aLatestEval)
{
  if (aLatestEval==Never || aLatestEval==Infinite) return false; // no next evaluation needed, no need to update
  if (nextEvaluation==Never || aLatestEval<nextEvaluation) {
    // new time is more recent than previous, update
    nextEvaluation = aLatestEval;
    return true;
  }
  return false;
}


bool TimedEvaluationContext::updateNextEval(const struct tm& aLatestEvalTm)
{
  MLMicroSeconds latestEval = MainLoop::localTimeToMainLoopTime(aLatestEvalTm);
  return updateNextEval(latestEval);
}



ExpressionValue TimedEvaluationContext::evaluateNow(EvalMode aEvalMode, bool aScheduleReEval)
{
  nextEvaluation = Never;
  ExpressionValue res = inherited::evaluateNow(aEvalMode, aScheduleReEval);
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
  return res;
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
    valueLookUp(aValueLookupCB),
    functionLookUp(aFunctionLookpCB)
  {
  };

  virtual ~AdHocEvaluationContext() {};

  ExpressionValue evaluateExpression(const string &aExpression)
  {
    setExpression(aExpression);
    return evaluateNow();
  };

protected:

  virtual ExpressionValue valueLookup(const string &aName) P44_OVERRIDE
  {
    if (valueLookUp) return valueLookUp(aName);
    return inherited::valueLookup(aName);
  };

  virtual ExpressionValue evaluateFunction(const string &aFunctionName, const FunctionArgumentVector &aArguments) P44_OVERRIDE
  {
    if (functionLookUp) return functionLookUp(aFunctionName, aArguments);
    return inherited::evaluateFunction(aFunctionName, aArguments);
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



// MARK: - TimedEvaluationContext

TimedEvaluationContext::TimedEvaluationContext(const GeoLocation& aGeoLocation) :
  geolocation(aGeoLocation),
  nextEvaluation(Never)
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
#define IS_TIME_TOLERANCE_SECONDS 5 ///< matching window for is_time() function

ExpressionValue TimedEvaluationContext::evaluateFunction(const string &aFunctionName, const FunctionArgumentVector &aArguments)
{
  if (aFunctionName=="testlater" && aArguments.size()>=2 && aArguments.size()<=3) {
    // testlater(seconds, timedtest [, retrigger])   return "invalid" now, re-evaluate after given seconds and return value of test then. If repeat is true then, the timer will be re-scheduled
    bool retrigger = false;
    if (aArguments.size()>=3) retrigger = aArguments[2].isOk() && aArguments[2].v>0;
    ExpressionValue secs = aArguments[0];
    if (retrigger && secs.v<MIN_RETRIGGER_SECONDS) {
      // prevent too frequent re-triggering that could eat up too much cpu
      LOG(LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", secs.v, (double)MIN_RETRIGGER_SECONDS);
      secs.v = MIN_RETRIGGER_SECONDS;
    }
    ExpressionValue currentSecs = secs;
    FrozenResult* frozenP = getFrozen(currentSecs);
    if (evalMode!=evalmode_timed) {
      if (evalMode!=evalmode_initial) {
        // evaluating non-timed, non-initial means "not yet ready" and must start or extend freeze period
        newFreeze(frozenP, secs, MainLoop::now()+secs.v*Second, true);
      }
      frozenP = NULL;
    }
    else {
      // evaluating timed after frozen period means "now is later" and if retrigger is set, must start a new freeze
      if (frozenP && retrigger) {
        newFreeze(frozenP, secs, MainLoop::now()+secs.v*Second);
      }
    }
    if (frozenP && !frozenP->frozen()) {
      // evaluation runs because freeze is over, return test result
      return ExpressionValue(aArguments[1].v);
    }
    else {
      // still frozen, return undefined
      return ExpressionValue::errValue(ExpressionError::Null, "testlater() not yet ready");
    }
  }
  else if (aFunctionName=="initial" && aArguments.size()==0) {
    // initial()  returns true if this is a "initial" run of the evaluator, meaning after startup or expression changes
    return ExpressionValue(evalMode==evalmode_initial);
  }
  else if (aFunctionName=="is_weekday" && aArguments.size()>0) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    // check if any of the weekdays match
    int weekday = loctim.tm_wday; // 0..6, 0=sunday
    ExpressionValue newRes(0);
    newRes.pos = aArguments[0].pos; // Note: we use pos of first argument for freezing the function's result (no need to freeze every single weekday)
    for (int i = 0; i<aArguments.size(); i++) {
      int w = (int)aArguments[i].v;
      if (w==7) w=0; // treat both 0 and 7 as sunday
      if (w==weekday) {
        // today is one of the days listed
        newRes.v = 1;
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
  else if ((aFunctionName=="after_time" || aFunctionName=="is_time") && aArguments.size()>=1) {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    ExpressionValue newSecs;
    newSecs.pos = aArguments[0].pos; // Note: we use pos of first argument for freezing the seconds
    if (aArguments.size()==2) {
      // legacy spec
      newSecs.v = ((int32_t)aArguments[0].v * 60 + (int32_t)aArguments[1].v) * 60;
    }
    else {
      // specification in seconds, usually using time literal
      newSecs.v = (int32_t)(aArguments[0].v);
    }
    ExpressionValue secs = newSecs;
    FrozenResult* frozenP = getFrozen(secs);
    int32_t daySecs = ((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec;
    bool met = daySecs>=secs.v;
    // next check at specified time, today if not yet met, tomorrow if already met for today
    loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)secs.v;
    FOCUSLOG("is/after_time() reference time for current check is: %s", MainLoop::string_mltime(MainLoop::localTimeToMainLoopTime(loctim)).c_str());
    bool res = met;
    // limit to a few secs around target if it's is_time
    if (aFunctionName=="is_time" && met && daySecs<secs.v+IS_TIME_TOLERANCE_SECONDS) {
      // freeze again for a bit
      newFreeze(frozenP, secs, MainLoop::localTimeToMainLoopTime(loctim)+IS_TIME_TOLERANCE_SECONDS*Second);
    }
    else {
      loctim.tm_hour = 0; loctim.tm_min = 0; loctim.tm_sec = (int)newSecs.v;
      if (met) {
        loctim.tm_mday++; // already met today, check again tomorrow
        if (aFunctionName=="is_time") res = false;
      }
      newFreeze(frozenP, newSecs, MainLoop::localTimeToMainLoopTime(loctim));
    }
    return ExpressionValue(res);
  }
  else if (aFunctionName=="between_dates" || aFunctionName=="between_yeardays") {
    struct tm loctim; MainLoop::getLocalTime(loctim);
    int smaller = (int)(aArguments[0].v);
    int larger = (int)(aArguments[1].v);
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
  else if (aFunctionName=="sunrise") {
    return ExpressionValue(sunrise(time(NULL), geolocation, false)*3600);
  }
  else if (aFunctionName=="dawn") {
    return ExpressionValue(sunrise(time(NULL), geolocation, true)*3600);
  }
  else if (aFunctionName=="sunset") {
    return ExpressionValue(sunset(time(NULL), geolocation, false)*3600);
  }
  else if (aFunctionName=="dusk") {
    return ExpressionValue(sunset(time(NULL), geolocation, true)*3600);
  }
  else {
    double fracSecs;
    struct tm loctim; MainLoop::getLocalTime(loctim, &fracSecs);
    if (aFunctionName=="timeofday") {
      return ExpressionValue(((loctim.tm_hour*60)+loctim.tm_min)*60+loctim.tm_sec+fracSecs);
    }
    else if (aFunctionName=="hour") {
      return ExpressionValue(loctim.tm_hour);
    }
    else if (aFunctionName=="minute") {
      return ExpressionValue(loctim.tm_min);
    }
    else if (aFunctionName=="second") {
      return ExpressionValue(loctim.tm_sec);
    }
    else if (aFunctionName=="year") {
      return ExpressionValue(loctim.tm_year+1900);
    }
    else if (aFunctionName=="month") {
      return ExpressionValue(loctim.tm_mon+1);
    }
    else if (aFunctionName=="day") {
      return ExpressionValue(loctim.tm_mday);
    }
    else if (aFunctionName=="weekday") {
      return ExpressionValue(loctim.tm_wday);
    }
    else if (aFunctionName=="yearday") {
      return ExpressionValue(loctim.tm_yday);
    }
  }
  return inherited::evaluateFunction(aFunctionName, aArguments);
}
