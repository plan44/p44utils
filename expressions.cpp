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

string ExpressionValue::stringValue()
{
  if (isOk()) {
    return string_format("%lg", v);
  }
  else {
    return "unknown";
  }
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


ExpressionValue ExpressionError::errValue(ErrorCodes aErrCode, const char *aFmt, ...)
{
  Error *errP = new ExpressionError(aErrCode);
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args);
  va_end(args);
  return ExpressionValue(ErrorPtr(errP));
}



// MARK: - EvaluationContext


EvaluationContext::EvaluationContext() :
  evalMode(evalmode_initial),
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


void EvaluationContext::unfreezeAll()
{
  frozenResults.clear(); // changing expression unfreezes everything
}


bool EvaluationContext::setExpression(const string aExpression)
{
  if (aExpression!=expression) {
    unfreezeAll(); // changing expression unfreezes everything
    expression = aExpression;
    return true;
  }
  return false;
}


ExpressionValue EvaluationContext::evaluateNow(EvalMode aEvalMode, bool aScheduleReEval)
{
  if (aEvalMode!=evalmode_current)
    evalMode = aEvalMode;
  nextEvaluation = Never;
  size_t pos = 0;
  ExpressionValue res = evaluateExpressionPrivate(expression.c_str(), pos, 0);
  if (nextEvaluation!=Never) {
    FOCUSLOG("Expression demands re-evaluation at %s: %s", MainLoop::string_fmltime("%H:%M:%S", nextEvaluation).c_str(), expression.c_str());
  }
  if (aScheduleReEval) {
    scheduleReEvaluation(nextEvaluation);
  }
  return res;
}


void EvaluationContext::scheduleReEvaluation(MLMicroSeconds aAtTime)
{
  nextEvaluation = aAtTime;
  if (nextEvaluation!=Never) {
    reEvaluationTicket.executeOnceAt(boost::bind(&EvaluationContext::reEvaluationHandler, this, _1, _2), nextEvaluation);
  }
  else {
    reEvaluationTicket.cancel();
  }
}


void EvaluationContext::scheduleLatestEvaluation(MLMicroSeconds aAtTime)
{
  if (updateNextEval(aAtTime)) {
    scheduleReEvaluation(nextEvaluation);
  }
}




bool EvaluationContext::triggerEvaluation(EvalMode aEvalMode)
{
  if (evaluating) {
    LOG(LOG_WARNING, "Apparently cyclic reference in evaluation of expression -> not retriggering: %s", expression.c_str());
    return false;
  }
  else {
    evaluating = true;
    ExpressionValue res = evaluateNow(aEvalMode, true);
    if (evaluationResultHandler) {
      // this is where cyclic references could cause re-evaluation, which is protected by evaluating==true
      evaluationResultHandler(res, *this);
    }
    evaluating = false;
  }
  return true;
}


void EvaluationContext::reEvaluationHandler(MLTimer &aTimer, MLMicroSeconds aNow)
{
  // trigger another evaluation
  FOCUSLOG("Timed re-evaluation of expression starting now: %s", expression.c_str());
  triggerEvaluation(evalmode_timed);
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
    if (!aArgs[0].isOk()) return aArgs[0]; // return error from condition
    return ExpressionValue(aArgs[0].v!=0 ? aArgs[1] : aArgs[2]);
  }
  else if (aName=="abs" && aArgs.size()==1) {
    // abs (a)         absolute value of a
    if (!aArgs[0].isOk()) return aArgs[0]; // return error from argument
    return ExpressionValue(fabs(aArgs[0].v));
  }
  else if (aName=="round" && (aArgs.size()==1 || aArgs.size()==2)) {
    // round (a)       round value to integer
    // round (a, p)    round value to specified precision (1=integer, 0.5=halves, 100=hundreds, etc...)
    if (!aArgs[0].isOk()) return aArgs[0]; // return error from argument
    double precision = 1;
    if (aArgs.size()>=2) {
      if (!aArgs[1].isOk()) return aArgs[0]; // return error from argument
      precision = aArgs[1].v;
    }
    return ExpressionValue(round(aArgs[0].v/precision)*precision);
  }
  // no such function
  return ExpressionError::errValue(ExpressionError::NotFound, "Unknown function '%s' with %lu arguments", aName.c_str(), aArgs.size());
}


// no variables in base class
ExpressionValue EvaluationContext::valueLookup(const string &aName)
{
  // no variables by default
  return ExpressionError::errValue(ExpressionError::NotFound, "no variable named '%s'", aName.c_str());
}


ExpressionValue EvaluationContext::evaluateTerm(const char *aExpr, size_t &aPos)
{
  size_t a = aPos;
  // a simple term can be
  // - a variable reference or
  // - a literal number or timespec (h:m or h:m:s)
  // Note: a parantesized expression can also be a term, but this is parsed by the caller, not here
  while (aExpr[aPos]==' ' || aExpr[aPos]=='\t') aPos++; // skip whitespace
  // extract var name or number
  ExpressionValue res;
  size_t e = aPos;
  while (aExpr[e] && (isalnum(aExpr[e]) || aExpr[e]=='.' || aExpr[e]=='_' || aExpr[e]==':')) e++;
  if (e==aPos) {
    return ExpressionError::errValue(ExpressionError::Syntax, "missing term");
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
            return ExpressionError::errValue(ExpressionError::Syntax, "missing comma or closing ')'");
          aPos++; // skip comma
        }
        ExpressionValue arg = evaluateExpressionPrivate(aExpr, aPos, 0);
        if (!arg.isOk() && !arg.err->isError("ExpressionError", ExpressionError::Null))
          return arg; // exit, except on null which is ok as a function argument
        args.push_back(arg);
      }
      aPos++; // skip closing paranthesis
      FOCUSLOG("Function '%s' called", term.c_str());
      for (FunctionArgumentVector::iterator pos = args.begin(); pos!=args.end(); ++pos) {
        FOCUSLOG("- argument: %lf (err=%s)", pos->v, Error::text(pos->err));
      }
      // run function
      res = evaluateFunction(term, args);
    }
    else {
      // check some reserved values
      if (term=="true" || term=="yes") {
        res = ExpressionValue(1);
      }
      else if (term=="false" || term=="no") {
        res = ExpressionValue(0);
      }
      else if (term=="null" || term=="undefined") {
        res = ExpressionError::errValue(ExpressionError::Null, "%s", term.c_str());
      }
      else {
        // must be identifier representing a variable value
        res = valueLookup(term);
      }
    }
  }
  else {
    // must be a numeric literal (can also be a time literal in hh:mm:ss or hh:mm form)
    double v;
    int i;
    if (sscanf(term.c_str(), "%lf%n", &v, &i)!=1) {
      return ExpressionError::errValue(ExpressionError::Syntax, "'%s' is not a valid number", term.c_str());
    }
    else {
      // TODO: date literals in form yyyy_mm_dd or just mm_dd
      // check for time literals (returned as seconds)
      // - these are in the form h:m or h:m:s, where all parts are allowed to be fractional
      if (term.size()>i && term[i]==':') {
        // we have 'v:'
        double t;
        int j;
        if (sscanf(term.c_str()+i+1, "%lf%n", &t, &j)!=1) {
          return ExpressionError::errValue(ExpressionError::Syntax, "'%s' is not a valid time specification (hh:mm or hh:mm:ss)", term.c_str());
        }
        else {
          // we have v:t, take these as hours and minutes
          v = (v*60+t)*60; // in seconds
          j += i+1;
          if (term.size()>j && term[j]==':') {
            // apparently we also have seconds
            if (sscanf(term.c_str()+j+1, "%lf", &t)!=1) {
              return ExpressionError::errValue(ExpressionError::Syntax, "'%s' time specification has invalid seconds (hh:mm:ss)", term.c_str());
            }
            v += t; // add the seconds
          }
        }
      }
    }
    res = ExpressionValue(v);
  }
  // valid term
  if (res.isOk()) {
    FOCUSLOG("Term '%.*s' evaluation result: %lf", (int)(aPos-a), aExpr+a, res.v);
  }
  else {
    FOCUSLOG("Term '%.*s' evaluation error: %s", (int)(aPos-a), aExpr+a, res.err->text());
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


bool EvaluationContext::updateNextEval(const MLMicroSeconds aLatestEval)
{
  if (aLatestEval==Never) return false; // no next evaluation needed, no need to update
  if (nextEvaluation==Never || aLatestEval<nextEvaluation) {
    // new time is more recent than previous, update
    nextEvaluation = aLatestEval;
    return true;
  }
  return false;
}




ExpressionValue EvaluationContext::evaluateExpressionPrivate(const char *aExpr, size_t &aPos, int aPrecedence)
{
  ExpressionValue res;
  size_t a = aPos;
  // check for optional unary op
  Operations unaryop = parseOperator(aExpr, aPos);
  if (unaryop!=op_none) {
    if (unaryop!=op_subtract && unaryop!=op_not) {
      return ExpressionError::errValue(ExpressionError::Syntax, "invalid unary operator");
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
      return ExpressionValue(ExpressionError::err(ExpressionError::Syntax, "Missing ')'"));
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
      return ExpressionError::err(ExpressionError::Syntax, "Invalid operator: '%s'", aExpr+opIdx);
    }
    // must parse right side of operator as subexpression
    aPos = opIdx; // advance past operator
    ExpressionValue rightside = evaluateExpressionPrivate(aExpr, aPos, precedence);
    if (!rightside.isOk()) res=rightside;
    if (res.isOk()) {
      // apply the operation between leftside and rightside
      switch (binaryop) {
        case op_not: {
          return ExpressionError::err(ExpressionError::Syntax, "NOT operator not allowed here");
        }
        case op_divide:
          if (rightside.v==0) return ExpressionError::errValue(ExpressionError::DivisionByZero, "division by zero");
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
      FOCUSLOG("Intermediate expression '%.*s' evaluation result: %lf", (int)(aPos-a), aExpr+a, res.v);
    }
    else {
      FOCUSLOG("Intermediate expression '%.*s' evaluation result is INVALID", (int)(aPos-a), aExpr+a);
    }
  }
  // done
  return res;
}


bool EvaluationContext::checkFrozen(ExpressionValue &aResult, size_t aAtPos, MLMicroSeconds aFreezeUntil)
{
  // TODO: implement
  return false;
}


bool EvaluationContext::unfreeze(size_t aAtPos)
{
  // TODO: implement
  return false;
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
    if (valueLookUp) return valueLookUp(aName, nextEvaluation);
    return inherited::valueLookup(aName);
  };

  virtual ExpressionValue evaluateFunction(const string &aFunctionName, const FunctionArgumentVector &aArguments) P44_OVERRIDE
  {
    if (functionLookUp) return functionLookUp(aFunctionName, aArguments, nextEvaluation);
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

TimedEvaluationContext::TimedEvaluationContext()
{

}


TimedEvaluationContext::~TimedEvaluationContext()
{

}


#define MIN_RETRIGGER_SECONDS 10

ExpressionValue TimedEvaluationContext::evaluateFunction(const string &aFunctionName, const FunctionArgumentVector &aArguments)
{
  if (aFunctionName=="testlater" && aArguments.size()>=2 && aArguments.size()<=3) {
    // testlater(seconds, timedtest [, retrigger])   return "invalid" now, re-evaluate after given seconds and return value of test then. If repeat is true then, the timer will be re-scheduled
    bool retrigger = false;
    if (aArguments.size()>=3) retrigger = aArguments[2].isOk() && aArguments[2].v>0;
    if (evalMode!=evalmode_timed || retrigger) {
      // (re-)setup timer
      double secs = aArguments[0].v;
      if (retrigger && secs<MIN_RETRIGGER_SECONDS) {
        // prevent too frequent re-triggering that could eat up too much cpu
        LOG(LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", secs, (double)MIN_RETRIGGER_SECONDS);
        secs = MIN_RETRIGGER_SECONDS;
      }
      updateNextEval(MainLoop::now()+secs*Second);
    }
    if (evalMode==evalmode_timed) {
      // evaluation runs because timer has expired, return test result
      return ExpressionValue(aArguments[1].v);
    }
    else {
      // timer not yet expired, return undefined
      return ExpressionError::errValue(ExpressionError::Null, "testlater() not yet ready");
    }
  }
  else if (aFunctionName=="initial" && aArguments.size()==0) {
    // initial()  returns true if this is a "initial" run of the evaluator, meaning after startup or expression changes
    return ExpressionValue(evalMode==evalmode_initial);
  }
  else {
    return inhertited::evaluateFunction(aFunctionName, aArguments);
  }
}
