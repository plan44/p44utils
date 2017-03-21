//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
      err = TextError::err("unterminated placeholder: %s", aString.c_str()+p);
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
      aValueLookupCB(rep, rep);
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




// MARK: ===== numeric term evaluation


ErrorPtr evaluateTerm(const char * &aText, double &aValue, DoubleValueLookupCB aValueLookupCB)
{
  const char *a = aText;
  // a simple term can be
  // - a variable reference or
  // - a literal number
  // Note: a parantesized expression can also be a term, but this is parsed by the caller, not here
  while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
  // extract var name or number
  double v = 0;
  const char *e = aText;
  while (*e && (isalnum(*e) || *e=='.' || *e=='_')) e++;
  if (e==aText) {
    return TextError::err("missing term");
  }
  // must be simple term
  string term;
  term.assign(aText, e-aText);
  aText = e; // advance cursor
  // skip trailing whitespace
  while (*aText==' ' || *aText=='\t') aText++; // skip whitespace
  // decode term
  if (isalpha(term[0])) {
    // must be a variable
    ErrorPtr err;
    if (aValueLookupCB) {
      err = aValueLookupCB(term, v);
    }
    else {
      // undefined
      err = TextError::err("missing variable lookup func");
    }
    if (!Error::isOK(err)) return err;
  }
  else {
    // must be a numeric literal
    if (sscanf(term.c_str(), "%lf", &v)!=1) {
      return TextError::err("'%s' is not a valid number", term.c_str());
    }
  }
  // valid term
  FOCUSLOG("Term '%.*s' evaluation result: %lf", (int)(aText-a), a, v);
  aValue = v;
  return ErrorPtr();
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




ErrorPtr evaluateExpressionPrivate(const char * &aText, double &aValue, int aPrecedence, DoubleValueLookupCB aValueLookupCB)
{
  const char *a = aText;
  ErrorPtr err;
  double result = 0;
  // check for optional unary op
  Operations unaryop = parseOperator(aText);
  if (unaryop!=op_none) {
    if (unaryop!=op_subtract && unaryop!=op_not) {
      return TextError::err("invalid unary operator");
    }
  }
  // evaluate term
  // - check for paranthesis term
  if (*aText=='(') {
    // term is expression in paranthesis
    aText++;
    ErrorPtr err = evaluateExpressionPrivate(aText, result, 0, aValueLookupCB);
    if (!Error::isOK(err)) return err;
    if (*aText!=')') {
      return TextError::err("Missing ')'");
    }
    aText++;
  }
  else {
    // must be simple term
    err = evaluateTerm(aText, result, aValueLookupCB);
    if (!Error::isOK(err)) return err;
  }
  // apply unary ops if any
  switch (unaryop) {
    case op_not : result = result > 0 ? 0 : 1; break;
    case op_subtract : result = -result; break;
    default: break;
  }
  while (*aText) {
    // now check for operator and precedence
    const char *optext = aText;
    Operations binaryop = parseOperator(optext);
    int precedence = binaryop & opmask_precedence;
    // end parsing here if end of text reached or operator has a lower or same precedence as the passed in precedence
    if (*optext==0 || *optext==')' || precedence<=aPrecedence) {
      // what we have so far is the result
      break;
    }
    // must parse right side of operator as subexpression
    aText = optext; // advance past operator
    double rightside;
    err = evaluateExpressionPrivate(aText, rightside, precedence, aValueLookupCB);
    if (!Error::isOK(err)) return err;
    // apply the operation between leftside and rightside
    switch (binaryop) {
      case op_not: {
        return TextError::err("NOT operator not allowed here");
      }
      case op_divide:
        if (rightside==0) return TextError::err("division by zero");
        result = result/rightside;
        break;
      case op_multiply: result = result*rightside; break;
      case op_add: result = result+rightside; break;
      case op_subtract: result = result-rightside; break;
      case op_equal: result = result==rightside; break;
      case op_notequal: result = result!=rightside; break;
      case op_less: result = result < rightside; break;
      case op_greater: result = result > rightside; break;
      case op_leq: result = result <= rightside; break;
      case op_geq: result = result >= rightside; break;
      case op_and: result = result && rightside; break;
      case op_or: result = result || rightside; break;
      default: break;
    }
    FOCUSLOG("Intermediate expression '%.*s' evaluation result: %lf", (int)(aText-a), a, result);
  }
  // done
  aValue = result;
  return ErrorPtr();
}


ErrorPtr p44::evaluateExpression(const string &aExpression, double &aResult, DoubleValueLookupCB aValueLookupCB)
{
  const char *p = aExpression.c_str();
  return evaluateExpressionPrivate(p, aResult, 0, aValueLookupCB);
}

