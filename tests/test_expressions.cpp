//
//  Copyright (c) 2016-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "catch.hpp"

#include "expressions.hpp"
#include <stdlib.h>

using namespace p44;

class ExpressionFixture : public EvaluationContext
{
  typedef EvaluationContext inherited;
public:
  ExpressionFixture() :
    inherited(NULL)
  {
    evalLogLevel = 0;
  };

  bool valueLookup(const string &aName, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (aName=="ua") aResult.setNumber(42);
    else if (aName=="almostua") aResult.setNumber(42.7);
    else if (aName=="uatext") aResult.setString("fortyTwo");
    else return inherited::valueLookup(aName, aResult);
    return true;
  }

  ExpressionValue runExpression(const string &aExpression)
  {
    setCode(aExpression);
    return evaluateSynchronously(evalmode_initial);
  }
};


class ScriptFixture : public ScriptExecutionContext
{
  typedef ScriptExecutionContext inherited;
public:
  ScriptFixture() :
    inherited(NULL)
  {
    evalLogLevel = 0;
  };

  bool valueLookup(const string &aName, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (aName=="ua") aResult.setNumber(42);
    else if (aName=="almostua") aResult.setNumber(42.7);
    else if (aName=="uatext") aResult.setString("fortyTwo");
    else return inherited::valueLookup(aName, aResult);
    return true;
  }

  ExpressionValue runScript(const string &aScript)
  {
    setCode(aScript);
    return evaluateSynchronously(evalmode_script);
  }
};


//TEST_CASE_METHOD(ScriptFixture, "Focus", "[expressions]" )
//{
//  setEvalLogLevel(7);
//  REQUIRE(runScript("var cond = 2; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';").stringValue() == "two");
//}


TEST_CASE("ExpressionValue", "[expressions]" )
{

  SECTION("Default Value") {
    REQUIRE(ExpressionValue().isNull()); // default expression is NULL
    REQUIRE(ExpressionValue().isString() == false);
    REQUIRE(ExpressionValue().isValue() == false); // FIXME: for now, is notOK because NULL is an error
    REQUIRE(ExpressionValue().isOK() == true);
    REQUIRE(ExpressionValue().syntaxOk() == true);
    REQUIRE(ExpressionValue().boolValue() == false);
    REQUIRE(ExpressionValue().boolValue() == false);
  }

  SECTION("Numbers") {
    REQUIRE(ExpressionValue(42).numValue() == 42);
    REQUIRE(ExpressionValue(42.78).numValue() == 42.78);
    REQUIRE(ExpressionValue(42.78).intValue() == 42);
    REQUIRE(ExpressionValue(42.78).intValue() == 42);
    REQUIRE(ExpressionValue(42.78).boolValue() == true);
    REQUIRE(ExpressionValue(-42.78).boolValue() == true);
    REQUIRE(ExpressionValue(0).boolValue() == false);
    ExpressionValue e; e.setBool(true);
    REQUIRE(e.numValue() == 1);
    ExpressionValue e2; e2.setBool(false);
    REQUIRE(e2.numValue() == 0);
  }

  SECTION("Strings") {
    REQUIRE(ExpressionValue(42).stringValue() == "42");
    REQUIRE(ExpressionValue("UA").stringValue() == "UA");
  }

  SECTION("Operators") {
    REQUIRE((ExpressionValue("UA") == ExpressionValue("UA")).boolValue() == true);
    REQUIRE((ExpressionValue("UA") < ExpressionValue("ua")).boolValue() == true);
    REQUIRE((ExpressionValue("UA")+ExpressionValue("ua")).stringValue() == "UAua");
    REQUIRE((ExpressionValue(42.7)+ExpressionValue(42)).numValue() == 42.7+42.0);
    REQUIRE((ExpressionValue(42.7)-ExpressionValue(24)).numValue() == 42.7-24.0);
    REQUIRE((ExpressionValue(42.7)*ExpressionValue(42)).numValue() == 42.7*42.0);
    REQUIRE((ExpressionValue(42.7)/ExpressionValue(24)).numValue() == 42.7/24.0);
  }

}



TEST_CASE_METHOD(ExpressionFixture, "Expressions", "[expressions]" )
{

  SECTION("Literals") {
    REQUIRE(runExpression("42").numValue() == 42);
    REQUIRE(runExpression("0x42").numValue() == 0x42);
    REQUIRE(runExpression("42.42").numValue() == 42.42);

    REQUIRE(runExpression("\"Hello\"").stringValue() == "Hello");
    REQUIRE(runExpression("\"He\\x65llo\"").stringValue() == "Heello");
    REQUIRE(runExpression("\"\\tHello\\nWorld, \\\"double quoted\\\"\"").stringValue() == "\tHello\nWorld, \"double quoted\""); // C string style
    REQUIRE(runExpression("'Hello\\nWorld, \"double quoted\" text'").stringValue() == "Hello\\nWorld, \"double quoted\" text"); // PHP single quoted style
    REQUIRE(runExpression("'Hello\\nWorld, ''single quoted'' text'").stringValue() == "Hello\\nWorld, 'single quoted' text"); // include single quotes in single quoted text by doubling them
    REQUIRE(runExpression("\"\"").stringValue() == ""); // empty string

    REQUIRE(runExpression("true").intValue() == 1);
    REQUIRE(runExpression("TRUE").intValue() == 1);
    REQUIRE(runExpression("yes").intValue() == 1);
    REQUIRE(runExpression("YES").intValue() == 1);
    REQUIRE(runExpression("false").intValue() == 0);
    REQUIRE(runExpression("FALSE").intValue() == 0);
    REQUIRE(runExpression("no").intValue() == 0);
    REQUIRE(runExpression("NO").intValue() == 0);
    REQUIRE(runExpression("undefined").isNull() == true);
    REQUIRE(runExpression("UNDEFINED").isNull() == true);
    REQUIRE(runExpression("null").isNull() == true);
    REQUIRE(runExpression("NULL").isNull() == true);

    REQUIRE(runExpression("12:35").intValue() == 45300);
    REQUIRE(runExpression("14:57:42").intValue() == 53862);
    REQUIRE(runExpression("14:57:42.328").numValue() == 53862.328);
    REQUIRE(runExpression("1.Jan").intValue() == 0);
    REQUIRE(runExpression("1.1.").intValue() == 0);
    REQUIRE(runExpression("19.Feb").intValue() == 49);
    REQUIRE(runExpression("19.FEB").intValue() == 49);
    REQUIRE(runExpression("19.2.").intValue() == 49);
    REQUIRE(runExpression("Mon").intValue() == 1);
    REQUIRE(runExpression("Sun").intValue() == 0);
    REQUIRE(runExpression("SUN").intValue() == 0);
    REQUIRE(runExpression("thu").intValue() == 4);
  }

  SECTION("Value Lookup") {
    REQUIRE(runExpression("UA").numValue() == 42);
    REQUIRE(runExpression("dummy").isNull() == false); // unknown var should not be Null..
    REQUIRE(runExpression("dummy").isValue() == false); // ..but not ok
    REQUIRE(runExpression("dummy").isOK() == false); // ..and not value-ok
    REQUIRE(runExpression("almostUA").numValue() == 42.7);
    REQUIRE(runExpression("UAtext").stringValue() == "fortyTwo");
    REQUIRE(runExpression("UAtext").stringValue() == "fortyTwo");
    REQUIRE(runExpression("UAtext").stringValue() == "fortyTwo");
  }

  SECTION("Operations") {
    REQUIRE(runExpression("-42.42").numValue() == -42.42); // unary minus
    REQUIRE(runExpression("!true").numValue() == 0); // unary not
    REQUIRE(runExpression("\"UA\"").stringValue() == "UA");
    REQUIRE(runExpression("\"ABC\" < \"abc\"").boolValue() == true);
    REQUIRE(runExpression("42.7+42").numValue() == 42.7+42.0);
    REQUIRE(runExpression("42.7-24").numValue() == 42.7-24.0);
    REQUIRE(runExpression("42.7*42").numValue() == 42.7*42.0);
    REQUIRE(runExpression("42.7/24").numValue() == 42.7/24.0);
    REQUIRE(runExpression("78/0").isValue() == false); // division by zero
    REQUIRE(runExpression("\"ABC\" + \"abc\"").stringValue() == "ABCabc");
    REQUIRE(runExpression("\"empty\"+\"\"").stringValue() == "empty");
    REQUIRE(runExpression("\"\"+\"empty\"").stringValue() == "empty");
    REQUIRE(runExpression("1==true").boolValue() == true);
    REQUIRE(runExpression("1==yes").boolValue() == true);
    REQUIRE(runExpression("0==false").boolValue() == true);
    REQUIRE(runExpression("0==no").boolValue() == true);
    REQUIRE(runExpression("undefined").boolValue() == false);
    REQUIRE(runExpression("undefined!=undefined").boolValue() == false); // undefined is equal only to itself
    REQUIRE(runExpression("undefined==undefined").boolValue() == true); // undefined is equal only to itself
    REQUIRE(runExpression("undefined==42").boolValue() == false); // undefined is not equal to anything else
    REQUIRE(runExpression("42==undefined").boolValue() == false); // undefined is not equal to anything else
    REQUIRE(runExpression("undefined!=42").boolValue() == true); // undefined is not equal to anything else
    REQUIRE(runExpression("42!=undefined").boolValue() == true); // undefined is not equal to anything else
    REQUIRE(runExpression("null==undefined").boolValue() == true); // undefined and null are the same
    REQUIRE(runExpression("42<>78").boolValue() == true);
    REQUIRE(runExpression("42=42").isValue() == (EXPRESSION_OPERATOR_MODE!=EXPRESSION_OPERATOR_MODE_C));
    REQUIRE(runExpression("42=42").boolValue() == (EXPRESSION_OPERATOR_MODE!=EXPRESSION_OPERATOR_MODE_C));
    REQUIRE(runExpression("7<28").boolValue() == true);
    REQUIRE(runExpression("7>28").boolValue() == false);
    REQUIRE(runExpression("28>28").boolValue() == false);
    REQUIRE(runExpression("28>=28").boolValue() == true);
    REQUIRE(runExpression("7<7").boolValue() == false);
    REQUIRE(runExpression("7<=7").boolValue() == true);
    REQUIRE(runExpression("7==7").boolValue() == true);
    REQUIRE(runExpression("7!=7").boolValue() == false);
    REQUIRE(runExpression("78==\"78\"").boolValue() == true);
    REQUIRE(runExpression("78==\"78.00\"").boolValue() == true); // numeric comparison, right side is forced to number
    REQUIRE(runExpression("\"78\"==\"78.00\"").boolValue() == false); // string comparison, right side is compared as-is
    REQUIRE(runExpression("78.00==\"78\"").boolValue() == true); // numeric comparison, right side is forced to number
  }

  SECTION("Operator precedence") {
    REQUIRE(runExpression("12*3+7").numValue() == 12*3+7);
    REQUIRE(runExpression("12*(3+7)").numValue() == 12*(3+7));
    REQUIRE(runExpression("12/3-7").numValue() == 12/3-7);
    REQUIRE(runExpression("12/(3-7)").numValue() == 12/(3-7));
  }

  SECTION("functions") {
    REQUIRE(runExpression("ifvalid(undefined,42)").numValue() == 42);
    REQUIRE(runExpression("ifvalid(33,42)").numValue() == 33);
    REQUIRE(runExpression("isvalid(undefined)").boolValue() == false);
    REQUIRE(runExpression("isvalid(1234)").boolValue() == true);
    REQUIRE(runExpression("if(true, 'TRUE', 'FALSE')").stringValue() == "TRUE");
    REQUIRE(runExpression("if(false, 'TRUE', 'FALSE')").stringValue() == "FALSE");
    REQUIRE(runExpression("abs(33)").numValue() == 33);
    REQUIRE(runExpression("abs(-33)").numValue() == 33);
    REQUIRE(runExpression("abs(0)").numValue() == 0);
    REQUIRE(runExpression("int(33)").numValue() == 33);
    REQUIRE(runExpression("int(33.3)").numValue() == 33);
    REQUIRE(runExpression("int(33.6)").numValue() == 33);
    REQUIRE(runExpression("int(-33.3)").numValue() == -33);
    REQUIRE(runExpression("int(-33.6)").numValue() == -33);
    REQUIRE(runExpression("round(33)").numValue() == 33);
    REQUIRE(runExpression("round(33.3)").numValue() == 33);
    REQUIRE(runExpression("round(33.6)").numValue() == 34);
    REQUIRE(runExpression("round(-33.6)").numValue() == -34);
    REQUIRE(runExpression("round(33.3, 0.5)").numValue() == 33.5);
    REQUIRE(runExpression("round(33.6, 0.5)").numValue() == 33.5);
    REQUIRE(runExpression("random(0,10)").numValue() < 10);
    REQUIRE(runExpression("random(0,10) != random(0,10)").boolValue() == true);
    REQUIRE(runExpression("string(33)").stringValue() == "33");
    REQUIRE(runExpression("number('33')").numValue() == 33);
    REQUIRE(runExpression("number('0x33')").numValue() == 0x33);
    REQUIRE(runExpression("number('33 gugus')").numValue() == 33); // best effort, ignore trailing garbage
    REQUIRE(runExpression("number('gugus 33')").numValue() == 0); // best effort, nothing readable
    REQUIRE(runExpression("strlen('gugus')").numValue() == 5);
    REQUIRE(runExpression("substr('gugus',3)").stringValue() == "us");
    REQUIRE(runExpression("substr('gugus',3,1)").stringValue() == "u");
    REQUIRE(runExpression("substr('gugus',7,1)").stringValue() == "");
    REQUIRE(runExpression("find('gugus dada', 'ad')").numValue() == 7);
    REQUIRE(runExpression("find('gugus dada', 'blubb')").isNull() == true);
    REQUIRE(runExpression("find('gugus dada', 'gu', 1)").numValue() == 2);
    REQUIRE(runExpression("format('%04d', 33.7)").stringValue() == "0033");
    REQUIRE(runExpression("format('%4d', 33.7)").stringValue() == "  33");
    REQUIRE(runExpression("format('%.1f', 33.7)").stringValue() == "33.7");
    REQUIRE(runExpression("format('%08X', 0x24F5E21)").stringValue() == "024F5E21");
    REQUIRE(runExpression("eval('333*777')").numValue() == 333*777);
    // special cases
    REQUIRE(runExpression("hour()").numValue() > 0);
    // should be case insensitive
    REQUIRE(runExpression("HOUR()").numValue() > 0);
    REQUIRE(runExpression("IF(TRUE, 'TRUE', 'FALSE')").stringValue() == "TRUE");
  }


  SECTION("AdHoc expression evaluation") {
    REQUIRE(p44::evaluateExpression("42", boost::bind(&ExpressionFixture::valueLookup, this, _1, _2), NULL).numValue() == 42 );
  }

}


TEST_CASE_METHOD(ScriptFixture, "Scripts", "[expressions]" )
{

  SECTION("return values") {
    REQUIRE(runScript("78.42").numValue() == 78.42); // last expression returns
    REQUIRE(runScript("78.42; return").isNull() == true); // explicit no-result
    REQUIRE(runScript("78.42; return null").isNull() == true); // explicit no-result
    REQUIRE(runScript("return 78.42").numValue() == 78.42); // same effect
    REQUIRE(runScript("return 78.42; 999").numValue() == 78.42); // same effect, return exits early
    REQUIRE(runScript("return 78.42; return 999").numValue() == 78.42); // first return counts
    REQUIRE(runScript("return; 999").isNull() == true); // explicit no-result
  }

  SECTION("variables") {
    REQUIRE(runScript("x = 78.42").isValue() == false); // cannot just assign
    REQUIRE(runScript("let x = 78.42").isValue() == false); // must be defined first
    REQUIRE(runScript("let x").isValue() == false); // let is not a declaration
    REQUIRE(runScript("var x = 78.42").isValue() == true); // should be ok
    REQUIRE(runScript("var x = 78.42").numValue() == 78.42); // last expression returns, even if in assignment
    REQUIRE(runScript("var x; let x = 1234").isValue() == true);
    REQUIRE(runScript("var x; let x = 1234").numValue() == 1234);
    REQUIRE(runScript("var x = 4321; X = 1234; return X").numValue() == 1234); // case insensitivity
    REQUIRE(runScript("var x = 4321; x = x + 1234; return x").numValue() == 1234+4321); // case insensitivity
  }

  SECTION("control flow") {
    REQUIRE(runScript("var cond = 1; var res = 'none'; var cond = 1; if (cond==1) res='one' else res='NOT one'; return res").stringValue() == "one");
    REQUIRE(runScript("var cond = 2; var res = 'none'; var cond = 2; if (cond==1) res='one' else res='NOT one'; return res").stringValue() == "NOT one");
    // without statement separators (JavaScript style)
    REQUIRE(runScript("var cond = 1; var res = 'none'; var cond = 1; if (cond==1) res='one' else if (cond==2) res='two' else res='not 1 or 2'; return res").stringValue() == "one");
    REQUIRE(runScript("var cond = 2; var res = 'none'; var cond = 2; if (cond==1) res='one' else if (cond==2) res='two' else res='not 1 or 2'; return res").stringValue() == "two");
    REQUIRE(runScript("var cond = 5; var res = 'none'; var cond = 5; if (cond==1) res='one' else if (cond==2) res='two' else res='not 1 or 2'; return res").stringValue() == "not 1 or 2");
    // with statement separators
    REQUIRE(runScript("var cond = 1; var res = 'none'; var cond = 1; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2'; return res").stringValue() == "one");
    REQUIRE(runScript("var cond = 2; var res = 'none'; var cond = 2; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2'; return res").stringValue() == "two");
    REQUIRE(runScript("var cond = 5; var res = 'none'; var cond = 5; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2'; return res").stringValue() == "not 1 or 2");
    // with skipped return statements
    REQUIRE(runScript("var cond = 1; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';").stringValue() == "one");
    REQUIRE(runScript("var cond = 2; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';").stringValue() == "two");
    REQUIRE(runScript("var cond = 5; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';").stringValue() == "not 1 or 2");
    // special cases
    REQUIRE(runScript("var cond = 2; var res = 'none'; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2' return res").stringValue() == "two");
    // blocks
    REQUIRE(runScript("var cond = 1; var res = 'none'; var res2 = 'none'; if (cond==1) res='one'; res2='two'; return string(res) + ',' + res2").stringValue() == "one,two");
    REQUIRE(runScript("var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) res='one'; res2='two'; return string(res) + ',' + res2").stringValue() == "none,two");
    REQUIRE(runScript("var cond = 1; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two' }; return string(res) + ',' + res2").stringValue() == "one,two");
    REQUIRE(runScript("var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two' }; return string(res) + ',' + res2").stringValue() == "none,none");
    // blocks with delimiter variations
    REQUIRE(runScript("var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two'; }; return string(res) + ',' + res2").stringValue() == "none,none");
    REQUIRE(runScript("var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two'; } return string(res) + ',' + res2").stringValue() == "none,none");
    // while, continue, break
    REQUIRE(runScript("var count = 0; while (count<5) count = count+1; return count").numValue() == 5);
    REQUIRE(runScript("var res = ''; var count = 0; while (count<5) { count = count+1; res = res+string(count); } return res").stringValue() == "12345");
    REQUIRE(runScript("var res = ''; var count = 0; while (count<5) { count = count+1; if (count==3) continue; res = res+string(count); } return res").stringValue() == "1245");
    REQUIRE(runScript("var res = ''; var count = 0; while (count<5) { count = count+1; if (count==3) break; res = res+string(count); } return res").stringValue() == "12");
    // skipping execution of chained expressions
    REQUIRE(runScript("if (false) return string(\"A\" + \"X\" + \"B\")").isNull() == true);
    REQUIRE(runScript("if (false) return string(\"A\" + string(\"\") + \"B\")").isNull() == true);
  }

}
