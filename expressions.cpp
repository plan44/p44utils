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


// MARK: ===== placeholder substitution

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
      if (!Error::isOK(err))
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


// MARK: ===== expression value

string ExpressionValue::stringValue()
{
  if (isOk()) {
    return string_format("%lg", v);
  }
  else {
    return "unknown";
  }
}



// MARK: ===== expression error


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



// MARK: ===== numeric term evaluation

ExpressionValue evaluateExpressionPrivate(const char * &aText, int aPrecedence, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB);


ExpressionValue evaluateBuiltinFunction(const string &aName, const FunctionArgumentVector &aArgs)
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



ExpressionValue evaluateTerm(const char * &aText, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB)
{
  const char *a = aText;
  // a simple term can be
  // - a variable reference or
  // - a literal number or timespec (h:m or h:m:s)
  // Note: a parantesized expression can also be a term, but this is parsed by the caller, not here
  while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
  // extract var name or number
  ExpressionValue res;
  const char *e = aText;
  while (*e && (isalnum(*e) || *e=='.' || *e=='_' || *e==':')) e++;
  if (e==aText) {
    return ExpressionError::errValue(ExpressionError::Syntax, "missing term");
  }
  // must be simple term
  string term;
  term.assign(aText, e-aText);
  aText = e; // advance cursor
  // skip trailing whitespace
  while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
  // decode term
  if (isalpha(term[0])) {
    ErrorPtr err;
    // must be a variable or function call
    if (*aText=='(') {
      // function call
      aText++; // skip opening paranthesis
      // - collect arguments
      FunctionArgumentVector args;
      while (true) {
        while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
        if (*aText==')')
          break; // no more arguments
        if (args.size()!=0) {
          if (*aText!=',')
            return ExpressionError::errValue(ExpressionError::Syntax, "missing comma or closing ')'");
          aText++; // skip comma
        }
        ExpressionValue arg = evaluateExpressionPrivate(aText, 0, aValueLookupCB, aFunctionLookpCB);
        if (!arg.isOk() && !arg.err->isError("ExpressionError", ExpressionError::Null))
          return arg; // exit, except on null which is ok as a function argument
        args.push_back(arg);
      }
      aText++; // skip closing paranthesis
      FOCUSLOG("Function '%s' called", term.c_str());
      for (FunctionArgumentVector::iterator pos = args.begin(); pos!=args.end(); ++pos) {
        FOCUSLOG("- argument: %lf (err=%s)", pos->v, Error::text(pos->err).c_str());
      }
      // run function
      bool foundFunc = false;
      if (aFunctionLookpCB) {
        res = aFunctionLookpCB(term, args);
        foundFunc = res.isOk() || !res.err->isError("ExpressionError", ExpressionError::NotFound);
      }
      if (!foundFunc) {
        res = evaluateBuiltinFunction(term, args);
      }
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
      else if (aValueLookupCB) {
        res = aValueLookupCB(term);
      }
      else {
        // undefined
        res = ExpressionError::errValue(ExpressionError::NotFound, "no variables");
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
    FOCUSLOG("Term '%.*s' evaluation result: %lf", (int)(aText-a), a, res.v);
  }
  else {
    FOCUSLOG("Term '%.*s' evaluation error: %s", (int)(aText-a), a, res.err->description().c_str());
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

static Operations parseOperator(const char * &aText)
{
  while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
  // check for operator
  Operations op = op_none;
  switch (*aText++) {
    case '*': op = op_multiply; break;
    case '/': op = op_divide; break;
    case '+': op = op_add; break;
    case '-': op = op_subtract; break;
    case '&': op = op_and; break;
    case '|': op = op_or; break;
    case '=': op = op_equal; break;
    case '<': {
      if (*aText=='=') {
        aText++; op = op_leq; break;
      }
      else if (*aText=='>') {
        aText++; op = op_notequal; break;
      }
      op = op_less; break;
    }
    case '>': {
      if (*aText=='=') {
        aText++; op = op_geq; break;
      }
      op = op_greater; break;
    }
    case '!': {
      if (*aText=='=') {
        aText++; op = op_notequal; break;
      }
      op = op_not; break;
      break;
    }
    default: --aText; // no expression char
  }
  while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
  return op;
}




ExpressionValue evaluateExpressionPrivate(const char * &aText, int aPrecedence, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB)
{
  const char *a = aText;
  ExpressionValue res;
  // check for optional unary op
  Operations unaryop = parseOperator(aText);
  if (unaryop!=op_none) {
    if (unaryop!=op_subtract && unaryop!=op_not) {
      return ExpressionError::errValue(ExpressionError::Syntax, "invalid unary operator");
    }
  }
  // evaluate term
  // - check for paranthesis term
  if (*aText=='(') {
    // term is expression in paranthesis
    aText++;
    res = evaluateExpressionPrivate(aText, 0, aValueLookupCB, aFunctionLookpCB);
    if (Error::isError(res.err, ExpressionError::domain(), ExpressionError::Syntax)) return res;
    if (*aText!=')') {
      return ExpressionValue(ExpressionError::err(ExpressionError::Syntax, "Missing ')'"));
    }
    aText++;
  }
  else {
    // must be simple term
    res = evaluateTerm(aText, aValueLookupCB, aFunctionLookpCB);
    if (Error::isError(res.err, ExpressionError::domain(), ExpressionError::Syntax)) return res;
  }
  // apply unary ops if any
  switch (unaryop) {
    case op_not : res.v = res.v > 0 ? 0 : 1; break;
    case op_subtract : res.v = -res.v; break;
    default: break;
  }
  while (*aText) {
    // now check for operator and precedence
    const char *optext = aText;
    Operations binaryop = parseOperator(optext);
    int precedence = binaryop & opmask_precedence;
    // end parsing here if end of text, paranthesis or argument reached or operator has a lower or same precedence as the passed in precedence
    if (*optext==0 || *optext==')' || *optext==',' || precedence<=aPrecedence) {
      // what we have so far is the result
      break;
    }
    // prevent loop
    if (binaryop==op_none) {
      return ExpressionError::err(ExpressionError::Syntax, "Invalid operator: '%s'", optext);
    }
    // must parse right side of operator as subexpression
    aText = optext; // advance past operator
    ExpressionValue rightside = evaluateExpressionPrivate(aText, precedence, aValueLookupCB, aFunctionLookpCB);
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
      FOCUSLOG("Intermediate expression '%.*s' evaluation result: %lf", (int)(aText-a), a, res.v);
    }
    else {
      FOCUSLOG("Intermediate expression '%.*s' evaluation result is INVALID", (int)(aText-a), a);
    }
  }
  // done
  return res;
}


ExpressionValue p44::evaluateExpression(const string &aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB)
{
  const char *p = aExpression.c_str();
  return evaluateExpressionPrivate(p, 0, aValueLookupCB, aFunctionLookpCB);
}


ErrorPtr p44::substituteExpressionPlaceholders(string &aString, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, string aNullText)
{
  ErrorPtr err;
  size_t p = 0;
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
    ExpressionValue result = evaluateExpression(expr, aValueLookupCB, aFunctionLookpCB);
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



