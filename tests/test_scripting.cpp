//
//  Copyright (c) 2016-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "scripting.hpp"
#include <stdlib.h>

#define LOGLEVELOFFSET 0

#define JSON_TEST_OBJ "{\"array\":[\"first\",2,3,\"fourth\",6.6],\"obj\":{\"objA\":\"A\",\"objB\":42,\"objC\":{\"objD\":\"D\",\"objE\":45}},\"string\":\"abc\",\"number\":42,\"bool\":true}"

using namespace p44;
using namespace p44::Script;

// MARK: CodeCursor tests

double numParse(const string input) {
  CodeCursor c(input);
  double num;
  c.parseNumericLiteral(num);
  return num;
}

string strParse(const string input) {
  CodeCursor c(input);
  string str;
  c.parseStringLiteral(str);
  return str;
}

string jsonParse(const string input) {
  CodeCursor c(input);
  JsonObjectPtr o;
  c.parseJSONLiteral(o);
  return o->json_str();
}


TEST_CASE("CodeCursor", "[scripting],[FOCUS]" )
{
  SECTION("Cursor") {
    // basic
    CodeCursor cursor("test");
    REQUIRE(cursor.charsleft() == 4);
    REQUIRE(cursor.lineno() == 0); // first line
    REQUIRE(cursor.charpos() == 0); // first char
    REQUIRE(cursor.c() == 't');
    REQUIRE(cursor.c(1) == 'e');
    REQUIRE(cursor.c(4) == 0); // at end
    REQUIRE(cursor.c(5) == 0); // beyond end, still 0
    REQUIRE(cursor.next() == true);
    REQUIRE(cursor.c() == 'e');
    REQUIRE(cursor.advance(2) == true);
    REQUIRE(cursor.c() == 't');
    REQUIRE(cursor.charpos() == 3);
    REQUIRE(cursor.advance(2) == false); // cannot advance 2 chars, only 1
    // end of buffer
    CodeCursor cursor2("part of buffer passed", 7); // only "part of" should be visible
    REQUIRE(cursor2.charsleft() == 7);
    cursor2.advance(5);
    REQUIRE(cursor2.c() == 'o');
    REQUIRE(cursor2.next() == true);
    REQUIRE(cursor2.c() == 'f');
    REQUIRE(cursor2.next() == true); // reaching end now
    REQUIRE(cursor2.c() == 0);
    REQUIRE(cursor2.next() == false); // cannot move further
  }

  SECTION("Identifiers") {
    // multi line + identifiers
    CodeCursor cursor3("multiple words /*   on\nmore */ than // one\nline: one.a2-a3_a4");
    string i;
    // "multiple"
    REQUIRE(cursor3.parseIdentifier(i) == true);
    REQUIRE(cursor3.lineno() == 0);
    REQUIRE(i == "multiple");
    REQUIRE(cursor3.charpos() == 8);
    // at space
    REQUIRE(cursor3.parseIdentifier(i) == false);
    cursor3.skipNonCode();
    // "words"
    size_t l;
    REQUIRE(cursor3.parseIdentifier(i,&l) == true);
    REQUIRE(i == "words");
    REQUIRE(l == 5);
    REQUIRE(cursor3.charpos() == 9);
    REQUIRE(cursor3.advance(l) == true);
    REQUIRE(cursor3.lineno() == 0);
    REQUIRE(cursor3.charpos() == 14);
    // skip 2-line comment
    cursor3.skipNonCode();
    REQUIRE(cursor3.lineno() == 1);
    // "than"
    REQUIRE(cursor3.parseIdentifier(i) == true);
    REQUIRE(i == "than");
    REQUIRE(cursor3.lineno() == 1);
    REQUIRE(cursor3.charpos() == 12);
    // skip EOL comment
    cursor3.skipNonCode();
    REQUIRE(cursor3.lineno() == 2);
    REQUIRE(cursor3.charpos() == 0);
    // "line"
    REQUIRE(cursor3.parseIdentifier(i) == true);
    REQUIRE(i == "line");
    REQUIRE(cursor3.lineno() == 2);
    REQUIRE(cursor3.charpos() == 4);
    // identifier and dots
    REQUIRE(cursor3.nextIf(':') == true);
    cursor3.skipNonCode();
    // "one"
    REQUIRE(cursor3.parseIdentifier(i) == true);
    REQUIRE(i == "one");
    REQUIRE(cursor3.nextIf('.') == true);
    // "a2"
    REQUIRE(cursor3.parseIdentifier(i) == true);
    REQUIRE(i == "a2");
    REQUIRE(cursor3.nextIf('+') == false);
    REQUIRE(cursor3.nextIf('-') == true);
    // "a3_a4"
    REQUIRE(cursor3.parseIdentifier(i) == true);
    REQUIRE(i == "a3_a4");
    // nothing more
    REQUIRE(cursor3.EOT() == true);
    REQUIRE(cursor3.next() == false);
    REQUIRE(cursor3.EOT() == true);
  }

  SECTION("Literals") {
    REQUIRE(numParse("42") == 42);
    REQUIRE(numParse("0x42") == 0x42);
    REQUIRE(numParse("42.42") == 42.42);

    REQUIRE(strParse("\"Hello\"") == "Hello");
    REQUIRE(strParse("\"He\\x65llo\"") == "Heello");
    REQUIRE(strParse("\"\\tHello\\nWorld, \\\"double quoted\\\"\"") == "\tHello\nWorld, \"double quoted\""); // C string style
    REQUIRE(strParse("'Hello\\nWorld, \"double quoted\" text'") == "Hello\\nWorld, \"double quoted\" text"); // PHP single quoted style
    REQUIRE(strParse("'Hello\\nWorld, ''single quoted'' text'") == "Hello\\nWorld, 'single quoted' text"); // include single quotes in single quoted text by doubling them
    REQUIRE(strParse("\"\"") == ""); // empty string

    REQUIRE(numParse("12:35") == 45300);
    REQUIRE(numParse("14:57:42") == 53862);
    REQUIRE(numParse("14:57:42.328") == 53862.328);
    REQUIRE(numParse("1.Jan") == 0);
    REQUIRE(numParse("1.1.") == 0);
    REQUIRE(numParse("19.Feb") == 49);
    REQUIRE(numParse("19.FEB") == 49);
    REQUIRE(numParse("19.2.") == 49);

    REQUIRE(jsonParse("{ 'type':'object', 'test':42 }") == "{\"type\":\"object\",\"test\":42}");
    REQUIRE(jsonParse("[ 'first', 2, 3, 'fourth', 6.6 ]") == "[\"first\",2,3,\"fourth\",6.6]");
  }

}





#if 0

class ScriptingExpressionFixture : public EvaluationContext
{
  typedef EvaluationContext inherited;
public:
  ScriptingExpressionFixture() :
    inherited(NULL)
  {
    setLogLevelOffset(LOGLEVELOFFSET);
    syncExecLimit = Infinite;
  };

  bool valueLookup(const string &aName, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (strucmp(aName.c_str(),"ua")==0) aResult.setNumber(42);
    else if (strucmp(aName.c_str(),"almostua")==0) aResult.setNumber(42.7);
    else if (strucmp(aName.c_str(),"uatext")==0) aResult.setString("fortyTwo");
    else return inherited::valueLookup(aName, aResult);
    return true;
  }

  ExpressionValue runExpression(const string &aExpression)
  {
    setCode(aExpression);
    // set global jstest to JSON_TEST_OBJ
    ExpressionValue v;
    v.setJson(JsonObject::objFromText(JSON_TEST_OBJ));
    ScriptGlobals::sharedScriptGlobals().globalVariables["jstest"] = v;
    // evaluate
    return evaluateSynchronously(evalmode_initial);
  }
};


class ScriptingCodeFixture : public ScriptExecutionContext
{
  typedef ScriptExecutionContext inherited;
public:
  ScriptingCodeFixture() :
    inherited(NULL)
  {
    setLogLevelOffset(LOGLEVELOFFSET);
    syncExecLimit = Infinite;
  };

  bool valueLookup(const string &aName, ExpressionValue &aResult) P44_OVERRIDE
  {
    if (strucmp(aName.c_str(),"ua")==0) aResult.setNumber(42);
    else if (strucmp(aName.c_str(),"almostua")==0) aResult.setNumber(42.7);
    else if (strucmp(aName.c_str(),"uatext")==0) aResult.setString("fortyTwo");
    else return inherited::valueLookup(aName, aResult);
    return true;
  }

  ExpressionValue runScript(const string &aScript)
  {
    setCode(aScript);
    // set global jstest to JSON_TEST_OBJ
    ExpressionValue v;
    v.setJson(JsonObject::objFromText(JSON_TEST_OBJ));
    ScriptGlobals::sharedScriptGlobals().globalVariables["jstest"] = v;
    return evaluateSynchronously(evalmode_script);
  }
};


TEST_CASE_METHOD(ScriptingExpressionFixture, "Focus1", "[FOCUS]" )
{
  setLogLevelOffset(2);
  //REQUIRE(runExpression("42").numValue() == 42);
}


TEST_CASE_METHOD(ScriptingCodeFixture, "Focus2", "[FOCUS]" )
{
  setLogLevelOffset(2);
  //REQUIRE(runScript("fortytwo = 42; return fortytwo").numValue() == 42);
  //REQUIRE(runScript("var js = " JSON_TEST_OBJ "; return js").stringValue() == JSON_TEST_OBJ);
  //REQUIRE(runScript("var js = " JSON_TEST_OBJ "; return js.obj").stringValue() == "{\"objA\":\"A\",\"objB\":42,\"objC\":{\"objD\":\"D\",\"objE\":45}}");
  //REQUIRE(runScript("var js = " JSON_TEST_OBJ "; setfield(js.obj, 'objF', 46); return js.obj.objF").numValue() == 46);
}



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



TEST_CASE_METHOD(ScriptingExpressionFixture, "Expressions", "[expressions]" )
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

    REQUIRE(runExpression("{ 'type':'object', 'test':42 }").stringValue() == "{\"type\":\"object\",\"test\":42}");
    REQUIRE(runExpression("[ 'first', 2, 3, 'fourth', 6.6 ]").stringValue() == "[\"first\",2,3,\"fourth\",6.6]");

  }

  SECTION("Whitespace and comments") {
    REQUIRE(runExpression("42 // 43").numValue() == 42);
    REQUIRE(runExpression("/* 43 */ 42").numValue() == 42);
    REQUIRE(runExpression("/* 43 // 42").isNull() == true);
  }


  SECTION("Value Lookup") {
    REQUIRE(runExpression("UA").numValue() == 42);
    REQUIRE(runExpression("dummy").isValue() == false); // unknown var is not a value
    REQUIRE(runExpression("dummy").isOK() == false); // ..and not value-ok
    REQUIRE(runExpression("almostUA").numValue() == 42.7);
    REQUIRE(runExpression("UAtext").stringValue() == "fortyTwo");
    REQUIRE(runExpression("UAtext").stringValue() == "fortyTwo");
    REQUIRE(runExpression("UAtext").stringValue() == "fortyTwo");
    // JSON tests, see JSON_TEST_OBJ
    REQUIRE(runExpression("jstest").stringValue() == JSON_TEST_OBJ);
    REQUIRE(runExpression("jstest.string").stringValue() == "abc");
    REQUIRE(runExpression("jstest.number").numValue() == 42);
    REQUIRE(runExpression("jstest.bool").boolValue() == true);
    REQUIRE(runExpression("jstest.array[2]").numValue() == 3);
    REQUIRE(runExpression("jstest.array[0]").stringValue() == "first");
    REQUIRE(runExpression("jstest['array'][0]").stringValue() == "first");
    REQUIRE(runExpression("jstest['array',0]").stringValue() == "first");
    REQUIRE(runExpression("jstest.obj.objA").stringValue() == "A");
    REQUIRE(runExpression("jstest.obj.objB").numValue() == 42);
    REQUIRE(runExpression("jstest.obj['objB']").numValue() == 42);
    REQUIRE(runExpression("jstest['obj'].objB").numValue() == 42);
    REQUIRE(runExpression("jstest['obj','objB']").numValue() == 42);
    REQUIRE(runExpression("jstest['obj']['objB']").numValue() == 42);
    REQUIRE(runExpression("jstest['obj'].objC.objD").stringValue() == "D");
    REQUIRE(runExpression("jstest['obj'].objC.objE").numValue() == 45);
  }

  SECTION("Operations") {
    REQUIRE(runExpression("-42.42").numValue() == -42.42); // unary minus
    REQUIRE(runExpression("!true").numValue() == 0); // unary not
    REQUIRE(runExpression("\"UA\"").stringValue() == "UA");
    REQUIRE(runExpression("42.7+42").numValue() == 42.7+42.0);
    REQUIRE(runExpression("42.7-24").numValue() == 42.7-24.0);
    REQUIRE(runExpression("42.7*42").numValue() == 42.7*42.0);
    REQUIRE(runExpression("42.7/24").numValue() == 42.7/24.0);
    REQUIRE(runExpression("5%2").numValue() == 1);
    REQUIRE(runExpression("5%2.5").numValue() == 0);
    REQUIRE(runExpression("5%1.5").numValue() == 0.5);
    REQUIRE(runExpression("5.5%2").numValue() == 1.5);
    REQUIRE(runExpression("78%9").numValue() == 6.0);
    REQUIRE(runExpression("77.77%9").numValue() == Approx(5.77));
    REQUIRE(runExpression("78/0").isOK() == false); // division by zero
    REQUIRE(runExpression("\"ABC\" + \"abc\"").stringValue() == "ABCabc");
    REQUIRE(runExpression("\"empty\"+\"\"").stringValue() == "empty");
    REQUIRE(runExpression("\"\"+\"empty\"").stringValue() == "empty");
    REQUIRE(runExpression("1==true").boolValue() == true);
    REQUIRE(runExpression("1==yes").boolValue() == true);
    REQUIRE(runExpression("0==false").boolValue() == true);
    REQUIRE(runExpression("0==no").boolValue() == true);
    REQUIRE(runExpression("undefined").boolValue() == false);
    REQUIRE(runExpression("undefined!=undefined").boolValue() == false);
    REQUIRE(runExpression("undefined==undefined").boolValue() == false);
    REQUIRE(runExpression("undefined==42").boolValue() == false);
    REQUIRE(runExpression("42==undefined").boolValue() == false);
    REQUIRE(runExpression("undefined!=42").boolValue() == false);
    REQUIRE(runExpression("42!=undefined").boolValue() == false);
    REQUIRE(runExpression("42>undefined").isNull() == true);
    REQUIRE(runExpression("42<undefined").isNull() == true);
    REQUIRE(runExpression("undefined<42").isNull() == true);
    REQUIRE(runExpression("undefined>42").isNull() == true);
    REQUIRE(runExpression("!undefined").isNull() == true);
    REQUIRE(runExpression("-undefined").isNull() == true);
    REQUIRE(runExpression("42<>78").boolValue() == true);
    REQUIRE(runExpression("42=42").isValue() == (EXPRESSION_OPERATOR_MODE!=EXPRESSION_OPERATOR_MODE_C));
    REQUIRE(runExpression("42=42").boolValue() == (EXPRESSION_OPERATOR_MODE!=EXPRESSION_OPERATOR_MODE_C));
    // Comparisons
    REQUIRE(runExpression("7<8").boolValue() == true);
    REQUIRE(runExpression("7<7").boolValue() == false);
    REQUIRE(runExpression("8<7").boolValue() == false);
    REQUIRE(runExpression("7<=8").boolValue() == true);
    REQUIRE(runExpression("7<=7").boolValue() == true);
    REQUIRE(runExpression("8<=7").boolValue() == false);
    REQUIRE(runExpression("8>7").boolValue() == true);
    REQUIRE(runExpression("7>7").boolValue() == false);
    REQUIRE(runExpression("7>8").boolValue() == false);
    REQUIRE(runExpression("8>=7").boolValue() == true);
    REQUIRE(runExpression("7>=7").boolValue() == true);
    REQUIRE(runExpression("7>=8").boolValue() == false);
    REQUIRE(runExpression("7==7").boolValue() == true);
    REQUIRE(runExpression("7!=7").boolValue() == false);
    REQUIRE(runExpression("7==8").boolValue() == false);
    REQUIRE(runExpression("7!=8").boolValue() == true);
    // String comparisons
    REQUIRE(runExpression("\"ABC\" < \"abc\"").boolValue() == true);
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
    // testing
    REQUIRE(runExpression("ifvalid(undefined,42)").numValue() == 42);
    REQUIRE(runExpression("ifvalid(33,42)").numValue() == 33);
    REQUIRE(runExpression("isvalid(undefined)").boolValue() == false);
    REQUIRE(runExpression("isvalid(undefined)").isNull() == false);
    REQUIRE(runExpression("isvalid(1234)").boolValue() == true);
    REQUIRE(runExpression("isvalid(0)").boolValue() == true);
    REQUIRE(runExpression("if(true, 'TRUE', 'FALSE')").stringValue() == "TRUE");
    REQUIRE(runExpression("if(false, 'TRUE', 'FALSE')").stringValue() == "FALSE");
    // numbers
    REQUIRE(runExpression("number(undefined)").numValue() == 0);
    REQUIRE(runExpression("number(undefined)").isNull() == false);
    REQUIRE(runExpression("number(0)").boolValue() == false);
    REQUIRE(runExpression("abs(33)").numValue() == 33);
    REQUIRE(runExpression("abs(undefined)").isNull() == true);
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
    REQUIRE(runExpression("frac(33)").numValue() == 0);
    REQUIRE(runExpression("frac(-33)").numValue() == 0);
    REQUIRE(runExpression("frac(33.6)").numValue() == Approx(0.6));
    REQUIRE(runExpression("frac(-33.6)").numValue() == Approx(-0.6));
    REQUIRE(runExpression("random(0,10)").numValue() < 10);
    REQUIRE(runExpression("random(0,10) != random(0,10)").boolValue() == true);
    REQUIRE(runExpression("number('33')").numValue() == 33);
    REQUIRE(runExpression("number('0x33')").numValue() == 0x33);
    REQUIRE(runExpression("number('33 gugus')").numValue() == 33); // best effort, ignore trailing garbage
    REQUIRE(runExpression("number('gugus 33')").numValue() == 0); // best effort, nothing readable
    REQUIRE(runExpression("min(42,78)").numValue() == 42);
    REQUIRE(runExpression("min(78,42)").numValue() == 42);
    REQUIRE(runExpression("max(42,78)").numValue() == 78);
    REQUIRE(runExpression("max(78,42)").numValue() == 78);
    REQUIRE(runExpression("limited(15,10,20)").numValue() == 15);
    REQUIRE(runExpression("limited(2,10,20)").numValue() == 10);
    REQUIRE(runExpression("limited(42,10,20)").numValue() == 20);
    REQUIRE(runExpression("cyclic(15,10,20)").numValue() == 15);
    REQUIRE(runExpression("cyclic(2,10,20)").numValue() == 12);
    REQUIRE(runExpression("cyclic(-18,10,20)").numValue() == 12);
    REQUIRE(runExpression("cyclic(22,10,20)").numValue() == 12);
    REQUIRE(runExpression("cyclic(42,10,20)").numValue() == 12);
    REQUIRE(runExpression("cyclic(-10.8,1,2)").numValue() == Approx(1.2));
    REQUIRE(runExpression("cyclic(-1.8,1,2)").numValue() == Approx(1.2));
    REQUIRE(runExpression("cyclic(2.2,1,2)").numValue() == Approx(1.2));
    REQUIRE(runExpression("cyclic(4.2,1,2)").numValue() == Approx(1.2));
    REQUIRE(runExpression("epochtime()").numValue() == (double)(time(NULL))/24/60/60);
    // strings
    REQUIRE(runExpression("string(33)").stringValue() == "33");
    REQUIRE(runExpression("string(undefined)").stringValue() == "undefined");
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
    // divs
    REQUIRE(runExpression("eval('333*777')").numValue() == 333*777);
    // error handling
    REQUIRE(runExpression("error('testerror')").stringValue() == string_format("testerror (ExpressionError:%d)", ExpressionError::User));
    REQUIRE(runExpression("errordomain(error('testerror'))").stringValue() == "ExpressionError");
    REQUIRE(runExpression("errorcode(error('testerror'))").numValue() == ExpressionError::User);
    REQUIRE(runExpression("errormessage(error('testerror')))").stringValue() == "testerror");
    // separate terrms ARE a syntax error in a expression! (not in a script, see below)
    REQUIRE(runExpression("42 43 44").stringValue().find(string_format("(ExpressionError:%d)", ExpressionError::Syntax)) != string::npos);
    // special cases
    REQUIRE(runExpression("hour()").numValue() > 0);
    // should be case insensitive
    REQUIRE(runExpression("HOUR()").numValue() > 0);
    REQUIRE(runExpression("IF(TRUE, 'TRUE', 'FALSE')").stringValue() == "TRUE");
  }


  SECTION("AdHoc expression evaluation") {
    REQUIRE(p44::evaluateExpression("42", boost::bind(&ScriptingExpressionFixture::valueLookup, this, _1, _2), NULL).numValue() == 42 );
  }

}


TEST_CASE_METHOD(ScriptingCodeFixture, "Scripts", "[expressions]" )
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
    REQUIRE(runScript("var x; x = 78.42").numValue() == 78.42); // last expression returns, even if in assignment
    REQUIRE(runScript("var x; let x = 1234").isValue() == true);
    REQUIRE(runScript("var x; let x = 1234").numValue() == 1234);
    REQUIRE(runScript("var x = 4321; X = 1234; return X").numValue() == 1234); // case insensitivity
    REQUIRE(runScript("var x = 4321; x = x + 1234; return x").numValue() == 1234+4321); // case insensitivity
    REQUIRE(runScript("var x = 1; var x = 2; return x").numValue() == 2); // locals initialized whenerver encountered (now! was different before)
    REQUIRE(runScript("glob g = 1; glob g = 2; return g").numValue() == 1); // however globals are initialized once and then never again
    REQUIRE(runScript("glob g = 3; g = 4; return g").numValue() == 4); // normal assignment is possible, however
  }

  SECTION("json manipulation") {
    REQUIRE(runScript("var js = " JSON_TEST_OBJ "; setfield(js.obj, 'objF', 46); log(5,js); return js.obj.objF").numValue() == 46);
    REQUIRE(runScript("var js = " JSON_TEST_OBJ "; setfield(js.obj, 'objA', 'AA'); log(5,js); return js.obj.objA").stringValue() == "AA");
    REQUIRE(runScript("var js = " JSON_TEST_OBJ "; setelement(js.array, 'AA'); log(5,js); return js.array[5]").stringValue() == "AA");
    REQUIRE(runScript("var js = " JSON_TEST_OBJ "; setelement(js.array, 0, 'modified'); log(5,js); return js.array[0]").stringValue() == "modified");
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
    // throw/try/catch
    REQUIRE(runScript("throw('test error')").isOK() == false);
    REQUIRE(Error::isError(runScript("throw('test error')").error(), ExpressionError::domain(), ExpressionError::User) == true);
    REQUIRE(strcmp(runScript("throw('test error')").error()->getErrorMessage(), "test error") == 0);
    REQUIRE(Error::isError(runScript("try var zerodiv = 7/0; catch return error()").error(), ExpressionError::domain(), ExpressionError::DivisionByZero) == true);
    REQUIRE(runScript("try var zerodiv = 7/0; catch return 'not allowed'").stringValue() == "not allowed");
    REQUIRE(runScript("try var zerodiv = 7/1; catch return error(); return zerodiv").numValue() == 7);
    REQUIRE(runScript("try { var zerodiv = 42; zerodiv = 7/0 } catch { log('CAUGHT!') }; return zerodiv").numValue() == 42);
    // Syntax errors
    REQUIRE(runScript("78/9#").stringValue() == string_format("invalid number, time or date (ExpressionError:%d)", ExpressionError::Syntax));
    REQUIRE(runScript("78/#9").stringValue() == string_format("invalid number, time or date (ExpressionError:%d)", ExpressionError::Syntax));
    // Not Syntax error in a script, the three numbers are separate statements, the last one is returned
    REQUIRE(runScript("42 43 44").intValue() == 44);
  }

}

#endif // 0
