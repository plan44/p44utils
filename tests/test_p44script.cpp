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

#include "p44script.hpp"
#include <stdlib.h>

#include "httpcomm.hpp"

#define LOGLEVELOFFSET 0

#define JSON_TEST_OBJ "{\"array\":[\"first\",2,3,\"fourth\",6.6],\"obj\":{\"objA\":\"A\",\"objB\":42,\"objC\":{\"objD\":\"D\",\"objE\":45}},\"string\":\"abc\",\"number\":42,\"bool\":true,\"bool2\":false,\"null\":null}"

using namespace p44;
using namespace p44::P44Script;


class TestLookup : public MemberLookup
{
public:
  virtual TypeInfo containsTypes() const P44_OVERRIDE { return value; }

  virtual ScriptObjPtr memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements) const P44_OVERRIDE
  {
    ScriptObjPtr result;
    if (strucmp(aName.c_str(),"ua")==0) result = new NumericValue(42);
    else if (strucmp(aName.c_str(),"almostua")==0) result = new NumericValue(42.7);
    else if (strucmp(aName.c_str(),"uatext")==0) result = new StringValue("fortyTwo");
    return result;
  };
};


class ScriptingCodeFixture
{
  ScriptMainContextPtr mainContext;
  TestLookup testLookup;
public:
  ScriptSource s;

  ScriptingCodeFixture() :
    s(scriptbody)
  {
    SETDAEMONMODE(false);
    SETLOGLEVEL(LOG_NOTICE);
    LOG(LOG_INFO, "\n+++++++ constructing ScriptingCodeFixture");
    testLookup.isMemberVariable();
    StandardScriptingDomain::sharedDomain().setLogLevelOffset(LOGLEVELOFFSET);
    mainContext = StandardScriptingDomain::sharedDomain().newContext();
    s.setSharedMainContext(mainContext);
    mainContext->registerMemberLookup(&testLookup);
    mainContext->domain()->setMemberByName("jstest", new JsonValue(JsonObject::objFromText(JSON_TEST_OBJ)));
  };
  virtual ~ScriptingCodeFixture()
  {
    LOG(LOG_INFO, "------- destructing ScriptingCodeFixture\n");
  }

};


class AsyncScriptingFixture
{

  ScriptSource s;
  ScriptMainContextPtr mainContext;
  ScriptObjPtr testResult;
  MLMicroSeconds tm;

public:

  AsyncScriptingFixture() :
    s(scriptbody)
  {
    SETDAEMONMODE(false);
    SETLOGLEVEL(LOG_NOTICE);
    LOG(LOG_INFO, "\n+++++++ constructing AsyncScriptingFixture");
    StandardScriptingDomain::sharedDomain().setLogLevelOffset(LOGLEVELOFFSET);
    #if ENABLE_HTTP_SCRIPT_FUNCS
    StandardScriptingDomain::sharedDomain().registerMemberLookup(new P44Script::HttpLookup);
    #endif
    mainContext = StandardScriptingDomain::sharedDomain().newContext();
    s.setSharedMainContext(mainContext);
  };

  virtual ~AsyncScriptingFixture()
  {
    LOG(LOG_INFO, "------- destructing AsyncScriptingFixture\n");
  }

  void resultCapture(ScriptObjPtr aResult)
  {
    testResult = aResult;
    MainLoop::currentMainLoop().terminate(EXIT_SUCCESS);
  }

  ScriptObjPtr scriptTest(EvaluationFlags aEvalFlags, const string aSource)
  {
    testResult.reset();
    s.setSource(aSource, aEvalFlags);
    EvaluationCB cb = boost::bind(&AsyncScriptingFixture::resultCapture, this, _1);
    // Note: as we share an eval context with all triggers and handlers, main script must be concurrent as well
    MainLoop::currentMainLoop().executeNow(boost::bind(&ScriptSource::run, &s, aEvalFlags|regular|concurrently, cb, /* 20*Second */ Infinite));
    tm = MainLoop::now();
    MainLoop::currentMainLoop().run(true);
    tm = MainLoop::now()-tm;
    return testResult;
  }

  double runningTime() { return (double)tm/Second; }

};




// MARK: CodeCursor tests

TEST_CASE("CodeCursor", "[scripting]" )
{
  SETDAEMONMODE(false);
  SETLOGLEVEL(LOG_NOTICE);

  SECTION("Cursor") {
    // basic
    SourceCursor cursor("test");
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
    // part of a string only
    SourceCursor cursor2("the part of buffer passed");
    SourceCursor cursor2start(cursor2);
    cursor2start.advance(4);
    SourceCursor cursor2end(cursor2start);
    cursor2end.advance(7); // only "part of" should be visible
    SourceCursor cursor2part(cursor2.source, cursor2start.pos, cursor2end.pos);
    // only "part of" should be visible
    REQUIRE(cursor2part.charsleft() == 7);
    REQUIRE(cursor2part.advance(5) == true);
    REQUIRE(cursor2part.c() == 'o');
    REQUIRE(cursor2part.next() == true);
    REQUIRE(cursor2part.nextIf('f') == true); // reaching end now
    REQUIRE(cursor2part.c() == 0);
    REQUIRE(cursor2part.next() == false); // cannot move further
  }

  SECTION("Identifiers") {
    // multi line + identifiers
    SourceCursor cursor3("multiple words /*   on\nmore */ than // one\nline: one.a2-a3_a4");
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
    REQUIRE(cursor3.checkForIdentifier("than") == true);
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
    REQUIRE(SourceCursor("42").parseNumericLiteral()->doubleValue() == 42);
    REQUIRE(SourceCursor("0x42").parseNumericLiteral()->doubleValue() == 0x42);
    REQUIRE(SourceCursor("42.42").parseNumericLiteral()->doubleValue() == 42.42);

    REQUIRE(SourceCursor("\"Hello\"").parseStringLiteral()->stringValue() == "Hello");
    REQUIRE(SourceCursor("\"He\\x65llo\"").parseStringLiteral()->stringValue() == "Heello");
    REQUIRE(SourceCursor("\"\\tHello\\nWorld, \\\"double quoted\\\"\"").parseStringLiteral()->stringValue() == "\tHello\nWorld, \"double quoted\""); // C string style
    REQUIRE(SourceCursor("'Hello\\nWorld, \"double quoted\" text'").parseStringLiteral()->stringValue() == "Hello\\nWorld, \"double quoted\" text"); // PHP single quoted style
    REQUIRE(SourceCursor("'Hello\\nWorld, ''single quoted'' text'").parseStringLiteral()->stringValue() == "Hello\\nWorld, 'single quoted' text"); // include single quotes in single quoted text by doubling them
    REQUIRE(SourceCursor("\"\"").parseStringLiteral()->stringValue() == ""); // empty string

    REQUIRE(SourceCursor("12:35").parseNumericLiteral()->doubleValue() == 45300);
    REQUIRE(SourceCursor("14:57:42").parseNumericLiteral()->doubleValue() == 53862);
    REQUIRE(SourceCursor("14:57:42.328").parseNumericLiteral()->doubleValue() == 53862.328);
    REQUIRE(SourceCursor("1.Jan").parseNumericLiteral()->doubleValue() == 0);
    REQUIRE(SourceCursor("1.1.").parseNumericLiteral()->doubleValue() == 0);
    REQUIRE(SourceCursor("19.Feb").parseNumericLiteral()->doubleValue() == 49);
    REQUIRE(SourceCursor("19.FEB").parseNumericLiteral()->doubleValue() == 49);
    REQUIRE(SourceCursor("19.2.").parseNumericLiteral()->doubleValue() == 49);

    REQUIRE(SourceCursor("{ 'type':'object', 'test':42 }").parseJSONLiteral()->stringValue() == "{\"type\":\"object\",\"test\":42}");
    REQUIRE(SourceCursor("[ 'first', 2, 3, 'fourth', 6.6 ]").parseJSONLiteral()->stringValue() == "[\"first\",2,3,\"fourth\",6.6]");
  }

}

// MARK: - debug test case

/*
TEST_CASE_METHOD(AsyncScriptingFixture, "Debugging single case/assertion", "[DEBUG]" )
{
  SETLOGLEVEL(LOG_DEBUG);
  SETDELTATIME(true);

#define TEST_URL "plan44.ch/testing/httptest.php"
#define DATA_IN_7SEC_TEST_URL "plan44.ch/testing/httptest.php?delay=7"
//  REQUIRE(scriptTest(sourcecode, "glob res='not completed'; log(4, 'will take 3 secs'); concurrent as http { try { res=geturl('http://" DATA_IN_7SEC_TEST_URL "', 5) } catch as err { res = err } } delay(3); abort(http); return res")->stringValue() == "ok");
  REQUIRE(scriptTest(sourcecode, "glob res='not completed'; log(4, 'will take 3 secs'); concurrent as http { res=geturl('http://" DATA_IN_7SEC_TEST_URL "', 5) } delay(3); abort(http); return res")->stringValue() == "not completed");
}
*/

/*
TEST_CASE_METHOD(ScriptingCodeFixture, "Debugging single case/assertion", "[DEBUG]" )
{
  SETLOGLEVEL(LOG_DEBUG);
  SETDELTATIME(true);

  REQUIRE(s.test(expression, "jstest.array[2]")->doubleValue() == 3);
}
*/


// MARK: - Literals

TEST_CASE_METHOD(ScriptingCodeFixture, "Literals", "[scripting]" )
{
  SECTION("Literals") {
    REQUIRE(s.test(expression, "42")->doubleValue() == 42);
    REQUIRE(s.test(expression, "0x42")->doubleValue() == 0x42);
    REQUIRE(s.test(expression, "42.42")->doubleValue() == 42.42);

    REQUIRE(s.test(expression, "\"Hello\"")->stringValue() == "Hello");
    REQUIRE(s.test(expression, "\"He\\x65llo\"")->stringValue() == "Heello");
    REQUIRE(s.test(expression, "\"\\tHello\\nWorld, \\\"double quoted\\\"\"")->stringValue() == "\tHello\nWorld, \"double quoted\""); // C string style
    REQUIRE(s.test(expression, "'Hello\\nWorld, \"double quoted\" text'")->stringValue() == "Hello\\nWorld, \"double quoted\" text"); // PHP single quoted style
    REQUIRE(s.test(expression, "'Hello\\nWorld, ''single quoted'' text'")->stringValue() == "Hello\\nWorld, 'single quoted' text"); // include single quotes in single quoted text by doubling them
    REQUIRE(s.test(expression, "\"\"")->stringValue() == ""); // empty string

    REQUIRE(s.test(expression, "true")->intValue() == 1);
    REQUIRE(s.test(expression, "TRUE")->intValue() == 1);
    REQUIRE(s.test(expression, "yes")->intValue() == 1);
    REQUIRE(s.test(expression, "YES")->intValue() == 1);
    REQUIRE(s.test(expression, "false")->intValue() == 0);
    REQUIRE(s.test(expression, "FALSE")->intValue() == 0);
    REQUIRE(s.test(expression, "no")->intValue() == 0);
    REQUIRE(s.test(expression, "NO")->intValue() == 0);
    REQUIRE(s.test(expression, "undefined")->hasType(null) == true);
    REQUIRE(s.test(expression, "UNDEFINED")->hasType(null) == true);
    REQUIRE(s.test(expression, "null")->hasType(null) == true);
    REQUIRE(s.test(expression, "NULL")->hasType(null) == true);

    REQUIRE(s.test(expression, "12:35")->intValue() == 45300);
    REQUIRE(s.test(expression, "14:57:42")->intValue() == 53862);
    REQUIRE(s.test(expression, "14:57:42.328")->doubleValue() == 53862.328);
    REQUIRE(s.test(expression, "1.Jan")->intValue() == 0);
    REQUIRE(s.test(expression, "1.1.")->intValue() == 0);
    REQUIRE(s.test(expression, "19.Feb")->intValue() == 49);
    REQUIRE(s.test(expression, "19.FEB")->intValue() == 49);
    REQUIRE(s.test(expression, "19.2.")->intValue() == 49);
    REQUIRE(s.test(expression, "Mon")->intValue() == 1);
    REQUIRE(s.test(expression, "Sun")->intValue() == 0);
    REQUIRE(s.test(expression, "SUN")->intValue() == 0);
    REQUIRE(s.test(expression, "thu")->intValue() == 4);

    REQUIRE(s.test(expression, "{ 'type':'object', 'test':42 }")->stringValue() == "{\"type\":\"object\",\"test\":42}");
    REQUIRE(s.test(expression, "[ 'first', 2, 3, 'fourth', 6.6 ]")->stringValue() == "[\"first\",2,3,\"fourth\",6.6]");
  }


  SECTION("Whitespace and comments") {
    REQUIRE(s.test(expression, "42 // 43")->doubleValue() == 42);
    REQUIRE(s.test(expression, "/* 43 */ 42")->doubleValue() == 42);
    REQUIRE(s.test(expression, "/* 43 // 42")->undefined() == true);
  }
}


// MARK: - Lookups

TEST_CASE_METHOD(ScriptingCodeFixture, "lookups", "[scripting]") {

  SECTION("Scalars") {
    REQUIRE(s.test(expression, "UA")->doubleValue() == 42);
    REQUIRE(s.test(expression, "dummy")->defined() == false); // unknown var is not a value
    REQUIRE(s.test(expression, "dummy")->isErr() == true); // ..and not value-ok
    REQUIRE(s.test(expression, "almostUA")->doubleValue() == 42.7);
    REQUIRE(s.test(expression, "UAtext")->stringValue() == "fortyTwo");
    REQUIRE(s.test(expression, "UAtext")->stringValue() == "fortyTwo");
    REQUIRE(s.test(expression, "UAtext")->stringValue() == "fortyTwo");
  }

  SECTION("Json") {
    // JSON access tests, see JSON_TEST_OBJ
    REQUIRE(s.test(expression, "jstest")->stringValue() == JSON_TEST_OBJ);
    REQUIRE(s.test(expression, "jstest.string")->stringValue() == "abc");
    REQUIRE(s.test(expression, "jstest.number")->doubleValue() == 42);
    REQUIRE(s.test(expression, "jstest.bool")->boolValue() == true);
    REQUIRE(s.test(expression, "jstest.array[2]")->doubleValue() == 3);
    REQUIRE(s.test(expression, "jstest.array[0]")->stringValue() == "first");
    REQUIRE(s.test(expression, "jstest['array'][0]")->stringValue() == "first");
    REQUIRE(s.test(expression, "jstest['array',0]")->stringValue() == "first");
    REQUIRE(s.test(expression, "jstest.obj.objA")->stringValue() == "A");
    REQUIRE(s.test(expression, "jstest.obj.objB")->doubleValue() == 42);
    REQUIRE(s.test(expression, "(jstest.obj).objB")->doubleValue() == 42); // submember of subexpression must work as well
    REQUIRE(s.test(expression, "jstest.obj['objB']")->doubleValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj'].objB")->doubleValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj','objB']")->doubleValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj']['objB']")->doubleValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj'].objC.objD")->stringValue() == "D");
    REQUIRE(s.test(expression, "jstest['obj'].objC.objE")->doubleValue() == 45);
    // JSON boolean interpretation (JavaScriptish...)
    REQUIRE(s.test(expression, "{}")->boolValue() == true); // empty object must be true
    REQUIRE(s.test(expression, "[]")->boolValue() == true); // empty array must be true
    REQUIRE(s.test(expression, "{ 'a':2 }")->boolValue() == true); // object must be true
    REQUIRE(s.test(expression, "[1,2]")->boolValue() == true); // array must be true
    REQUIRE(s.test(expression, "jstest.bool2")->boolValue() == false);
    REQUIRE(s.test(expression, "jstest.null")->boolValue() == false);
    REQUIRE(s.test(expression, "jstest.null")->defined() == false);
  }

}


// MARK: - Expressions

TEST_CASE_METHOD(ScriptingCodeFixture, "expressions", "[scripting]") {

  SECTION("Operations") {
    REQUIRE(s.test(expression, "-42.42")->doubleValue() == -42.42); // unary minus
    REQUIRE(s.test(expression, "!true")->doubleValue() == 0); // unary not
    REQUIRE(s.test(expression, "\"UA\"")->stringValue() == "UA");
    REQUIRE(s.test(expression, "42.7+42")->doubleValue() == 42.7+42.0);
    REQUIRE(s.test(expression, "42.7-24")->doubleValue() == 42.7-24.0);
    REQUIRE(s.test(expression, "42.7*42")->doubleValue() == 42.7*42.0);
    REQUIRE(s.test(expression, "42.7/24")->doubleValue() == 42.7/24.0);
    REQUIRE(s.test(expression, "5%2")->doubleValue() == 1);
    REQUIRE(s.test(expression, "5%2.5")->doubleValue() == 0);
    REQUIRE(s.test(expression, "5%1.5")->doubleValue() == 0.5);
    REQUIRE(s.test(expression, "5.5%2")->doubleValue() == 1.5);
    REQUIRE(s.test(expression, "78%9")->doubleValue() == 6.0);
    REQUIRE(s.test(expression, "77.77%9")->doubleValue() == Approx(5.77));
    REQUIRE(s.test(expression, "78/0")->isErr() == true); // division by zero
    REQUIRE(s.test(expression, "\"ABC\" + \"abc\"")->stringValue() == "ABCabc");
    REQUIRE(s.test(expression, "\"empty\"+\"\"")->stringValue() == "empty");
    REQUIRE(s.test(expression, "\"\"+\"empty\"")->stringValue() == "empty");
    REQUIRE(s.test(expression, "1==true")->boolValue() == true);
    REQUIRE(s.test(expression, "1==yes")->boolValue() == true);
    REQUIRE(s.test(expression, "0==false")->boolValue() == true);
    REQUIRE(s.test(expression, "0==no")->boolValue() == true);
    REQUIRE(s.test(expression, "undefined")->boolValue() == false);
    // Comparisons
    REQUIRE(s.test(expression, "undefined!=undefined")->boolValue() == false); // == is now evaluated between nulls
    REQUIRE(s.test(expression, "undefined!=undefined")->defined() == true); // ..so result is defined
    REQUIRE(s.test(expression, "undefined==undefined")->boolValue() == true); // == is now evaluated between nulls
    REQUIRE(s.test(expression, "undefined==undefined")->defined() == true); // ..so result is defined
    REQUIRE(s.test(expression, "42==undefined")->boolValue() == false); // == is now evaluated between nulls
    REQUIRE(s.test(expression, "42!=undefined")->boolValue() == true); // != is now evaluated between nulls
    REQUIRE(s.test(expression, "undefined==42")->boolValue() == false); // == is now evaluated between nulls
    REQUIRE(s.test(expression, "undefined!=42")->boolValue() == true); // != is now evaluated between nulls
    REQUIRE(s.test(expression, "42>undefined")->undefined() == true);
    REQUIRE(s.test(expression, "42<undefined")->undefined() == true);
    REQUIRE(s.test(expression, "undefined<42")->undefined() == true);
    REQUIRE(s.test(expression, "undefined>42")->undefined() == true);
    REQUIRE(s.test(expression, "!undefined")->undefined() == true);
    REQUIRE(s.test(expression, "-undefined")->undefined() == true);
    REQUIRE(s.test(expression, "0==undefined")->boolValue() == false); // zero is not NULL
    REQUIRE(s.test(expression, "0!=undefined")->boolValue() == true); // zero is not NULL
    REQUIRE(s.test(expression, "undefined==0")->boolValue() == false); // zero is not NULL
    REQUIRE(s.test(expression, "undefined!=0")->boolValue() == true); // zero is not NULL
    REQUIRE(s.test(expression, "42<>78")->boolValue() == true);
    REQUIRE(s.test(expression, "42=42")->defined() == (SCRIPT_OPERATOR_MODE!=SCRIPT_OPERATOR_MODE_C));
    REQUIRE(s.test(expression, "42=42")->boolValue() == (SCRIPT_OPERATOR_MODE!=SCRIPT_OPERATOR_MODE_C));
    REQUIRE(s.test(expression, "7<8")->boolValue() == true);
    REQUIRE(s.test(expression, "7<7")->boolValue() == false);
    REQUIRE(s.test(expression, "8<7")->boolValue() == false);
    REQUIRE(s.test(expression, "7<=8")->boolValue() == true);
    REQUIRE(s.test(expression, "7<=7")->boolValue() == true);
    REQUIRE(s.test(expression, "8<=7")->boolValue() == false);
    REQUIRE(s.test(expression, "8>7")->boolValue() == true);
    REQUIRE(s.test(expression, "7>7")->boolValue() == false);
    REQUIRE(s.test(expression, "7>8")->boolValue() == false);
    REQUIRE(s.test(expression, "8>=7")->boolValue() == true);
    REQUIRE(s.test(expression, "7>=7")->boolValue() == true);
    REQUIRE(s.test(expression, "7>=8")->boolValue() == false);
    REQUIRE(s.test(expression, "7==7")->boolValue() == true);
    REQUIRE(s.test(expression, "7!=7")->boolValue() == false);
    REQUIRE(s.test(expression, "7==8")->boolValue() == false);
    REQUIRE(s.test(expression, "7!=8")->boolValue() == true);
    // String comparisons
    REQUIRE(s.test(expression, "\"ABC\" < \"abc\"")->boolValue() == true);
    REQUIRE(s.test(expression, "78==\"78\"")->boolValue() == true);
    REQUIRE(s.test(expression, "78==\"78.00\"")->boolValue() == true); // numeric comparison, right side is forced to number
    REQUIRE(s.test(expression, "\"78\"==\"78.00\"")->boolValue() == false); // string comparison, right side is compared as-is
    REQUIRE(s.test(expression, "78.00==\"78\"")->boolValue() == true); // numeric comparison, right side is forced to number
  }

  SECTION("Operator precedence") {
    REQUIRE(s.test(expression, "12*3+7")->doubleValue() == 12*3+7);
    REQUIRE(s.test(expression, "12*(3+7)")->doubleValue() == 12*(3+7));
    REQUIRE(s.test(expression, "12/3-7")->doubleValue() == 12/3-7);
    REQUIRE(s.test(expression, "12/(3-7)")->doubleValue() == 12/(3-7));
  }

  SECTION("functions") {
    // testing
    REQUIRE(s.test(expression, "ifvalid(undefined,42)")->doubleValue() == 42);
    REQUIRE(s.test(expression, "ifvalid(33,42)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "isvalid(undefined)")->boolValue() == false);
    REQUIRE(s.test(expression, "isvalid(undefined)")->undefined() == false);
    REQUIRE(s.test(expression, "isvalid(1234)")->boolValue() == true);
    REQUIRE(s.test(expression, "isvalid(0)")->boolValue() == true);
    REQUIRE(s.test(expression, "if(true, 'TRUE', 'FALSE')")->stringValue() == "TRUE");
    REQUIRE(s.test(expression, "if(false, 'TRUE', 'FALSE')")->stringValue() == "FALSE");
    // numbers
    REQUIRE(s.test(expression, "number(undefined)")->doubleValue() == 0);
    REQUIRE(s.test(expression, "number(undefined)")->undefined() == false);
    REQUIRE(s.test(expression, "number(0)")->boolValue() == false);
    REQUIRE(s.test(expression, "abs(33)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "abs(undefined)")->undefined() == true);
    REQUIRE(s.test(expression, "abs(-33)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "abs(0)")->doubleValue() == 0);
    REQUIRE(s.test(expression, "int(33)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "int(33.3)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "int(33.6)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "int(-33.3)")->doubleValue() == -33);
    REQUIRE(s.test(expression, "int(-33.6)")->doubleValue() == -33);
    REQUIRE(s.test(expression, "round(33)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "round(33.3)")->doubleValue() == 33);
    REQUIRE(s.test(expression, "round(33.6)")->doubleValue() == 34);
    REQUIRE(s.test(expression, "round(-33.6)")->doubleValue() == -34);
    REQUIRE(s.test(expression, "round(33.3, 0.5)")->doubleValue() == 33.5);
    REQUIRE(s.test(expression, "round(33.6, 0.5)")->doubleValue() == 33.5);
    REQUIRE(s.test(expression, "frac(33)")->doubleValue() == 0);
    REQUIRE(s.test(expression, "frac(-33)")->doubleValue() == 0);
    REQUIRE(s.test(expression, "frac(33.6)")->doubleValue() == Approx(0.6));
    REQUIRE(s.test(expression, "frac(-33.6)")->doubleValue() == Approx(-0.6));
    REQUIRE(s.test(expression, "random(0,10)")->doubleValue() < 10);
    REQUIRE(s.test(expression, "random(0,10) != random(0,10)")->boolValue() == true);
    REQUIRE(s.test(expression, "number('33')")->doubleValue() == 33);
    REQUIRE(s.test(expression, "number('0x33')")->doubleValue() == 0x33);
    REQUIRE(s.test(expression, "number('33 gugus')")->doubleValue() == 33); // best effort, ignore trailing garbage
    REQUIRE(s.test(expression, "number('gugus 33')")->doubleValue() == 0); // best effort, nothing readable
    REQUIRE(s.test(expression, "min(42,78)")->doubleValue() == 42);
    REQUIRE(s.test(expression, "min(78,42)")->doubleValue() == 42);
    REQUIRE(s.test(expression, "max(42,78)")->doubleValue() == 78);
    REQUIRE(s.test(expression, "max(78,42)")->doubleValue() == 78);
    REQUIRE(s.test(expression, "limited(15,10,20)")->doubleValue() == 15);
    REQUIRE(s.test(expression, "limited(2,10,20)")->doubleValue() == 10);
    REQUIRE(s.test(expression, "limited(42,10,20)")->doubleValue() == 20);
    REQUIRE(s.test(expression, "cyclic(15,10,20)")->doubleValue() == 15);
    REQUIRE(s.test(expression, "cyclic(2,10,20)")->doubleValue() == 12);
    REQUIRE(s.test(expression, "cyclic(-18,10,20)")->doubleValue() == 12);
    REQUIRE(s.test(expression, "cyclic(22,10,20)")->doubleValue() == 12);
    REQUIRE(s.test(expression, "cyclic(42,10,20)")->doubleValue() == 12);
    REQUIRE(s.test(expression, "cyclic(-10.8,1,2)")->doubleValue() == Approx(1.2));
    REQUIRE(s.test(expression, "cyclic(-1.8,1,2)")->doubleValue() == Approx(1.2));
    REQUIRE(s.test(expression, "cyclic(2.2,1,2)")->doubleValue() == Approx(1.2));
    REQUIRE(s.test(expression, "cyclic(4.2,1,2)")->doubleValue() == Approx(1.2));
    REQUIRE(s.test(expression, "epochdays()")->int64Value() == floor(MainLoop::unixtime()/Day));
    REQUIRE(s.test(expression, "epochtime()")->doubleValue() == Approx((double)MainLoop::unixtime()/Second));
    REQUIRE(s.test(expression, "hour(23:42)")->doubleValue() == 23);
    REQUIRE(s.test(expression, "minute(23:42)")->doubleValue() == 42);
    REQUIRE(s.test(expression, "formattime(23:42)")->stringValue() == "23:42:00");
    REQUIRE(s.test(expression, "formattime()==formattime(epochtime())")->boolValue() == true);
    // strings
    REQUIRE(s.test(expression, "string(33)")->stringValue() == "33");
    REQUIRE(s.test(expression, "string(undefined)")->stringValue() == "undefined");
    REQUIRE(s.test(expression, "strlen('gugus')")->doubleValue() == 5);
    REQUIRE(s.test(expression, "substr('gugus',3)")->stringValue() == "us");
    REQUIRE(s.test(expression, "substr('gugus',3,1)")->stringValue() == "u");
    REQUIRE(s.test(expression, "substr('gugus',7,1)")->stringValue() == "");
    REQUIRE(s.test(expression, "find('gugus dada', 'ad')")->doubleValue() == 7);
    REQUIRE(s.test(expression, "find('gugus dada', 'blubb')")->undefined() == true);
    REQUIRE(s.test(expression, "find('gugus dada', 'gu', 1)")->doubleValue() == 2);
    REQUIRE(s.test(expression, "format('%04d', 33.7)")->stringValue() == "0033");
    REQUIRE(s.test(expression, "format('%4d', 33.7)")->stringValue() == "  33");
    REQUIRE(s.test(expression, "format('%.1f', 33.7)")->stringValue() == "33.7");
    REQUIRE(s.test(expression, "format('%08X', 0x24F5E21)")->stringValue() == "024F5E21");
    REQUIRE(s.test(expression, "format('%X', 0xABCDEF24F5E21)")->stringValue() == "ABCDEF24F5E21");
    REQUIRE(s.test(expression, "format('%15s', 'hello world')")->stringValue() == "    hello world");
    REQUIRE(s.test(expression, "format('%.5s', 'hello world')")->stringValue() == "hello");
    REQUIRE(s.test(expression, "format('full format with decimal %04d%% and float %.3f and string %s in one call', 42, 78.787878, 'UA')")->stringValue() == "full format with decimal 0042% and float 78.788 and string UA in one call");

    // divs
    REQUIRE(s.test(expression, "eval('333*777')")->isErr() == true); // eval is async, s.test is synchronous!
    // error handling
    REQUIRE(s.test(expression, "error('testerror')")->stringValue() == string_format("testerror (ScriptError::User[%d])", ScriptError::User));
    REQUIRE(s.test(expression, "errordomain(error('testerror'))")->stringValue() == "ScriptError");
    REQUIRE(s.test(expression, "errorcode(error('testerror'))")->doubleValue() == ScriptError::User);
    REQUIRE(s.test(expression, "errormessage(error('testerror'))")->stringValue() == "testerror");
    // separate terms ARE a syntax error in a expression! (not in a script, see below)
    REQUIRE(s.test(expression, "42 43 44")->stringValue().find(string_format("(ScriptError::Syntax[%d])", ScriptError::Syntax)) != string::npos);
    // should be case insensitive
    REQUIRE(s.test(expression, "IF(TRUE, 'TRUE', 'FALSE')")->stringValue() == "TRUE");
  }
}

TEST_CASE_METHOD(ScriptingCodeFixture, "statements", "[scripting]" )
{

  SECTION("return values") {
    REQUIRE(s.test(scriptbody, "78.42")->doubleValue() == 78.42); // last expression returns
    REQUIRE(s.test(scriptbody, "78.42; return")->undefined() == true); // explicit no-result
    REQUIRE(s.test(scriptbody, "78.42; return null")->undefined() == true); // explicit no-result
    REQUIRE(s.test(scriptbody, "return 78.42")->doubleValue() == 78.42); // same effect
    REQUIRE(s.test(scriptbody, "return 78.42; 999")->doubleValue() == 78.42); // same effect, return exits early
    REQUIRE(s.test(scriptbody, "return 78.42; return 999")->doubleValue() == 78.42); // first return counts
    REQUIRE(s.test(scriptbody, "return; 999")->undefined() == true); // explicit no-result
  }

  SECTION("variables") {
    REQUIRE(s.test(scriptbody, "x = 78.42")->isErr() == true); // cannot just assign
    REQUIRE(s.test(scriptbody, "let x = 78.42")->isErr() == true); // must be defined first
    REQUIRE(s.test(scriptbody, "let x")->isErr() == true); // let is not a declaration
    REQUIRE(s.test(scriptbody, "var x = 78.42")->doubleValue() == 78.42); // assignment returns value
    REQUIRE(s.test(scriptbody, "var x; x = 78.42")->doubleValue() == 78.42); // last expression returns, even if in assignment
    REQUIRE(s.test(scriptbody, "var x; let x = 1234")->doubleValue() == 1234);
    REQUIRE(s.test(scriptbody, "var x = 4321; X = 1234; return X")->doubleValue() == 1234); // case insensitivity
    REQUIRE(s.test(scriptbody, "var x = 4321; x = x + 1234; return x")->doubleValue() == 1234+4321); // case insensitivity
    REQUIRE(s.test(scriptbody, "var x = 1; var x = 2; return x")->doubleValue() == 2); // locals initialized whenerver encountered (now! was different before)
    REQUIRE(s.test(scriptbody, "glob g = 1; return g")->isErr() == true); // globals cannot be initialized in a script BODY (ANY MORE, they could, ONCE, in old ScriptContext)
    REQUIRE(s.test(sourcecode, "glob g = 1; return g")->doubleValue() == 1); // ..however, in the declaration part, initialisation IS possible
    REQUIRE(s.test(scriptbody, "glob g; g = 4; return g")->doubleValue() == 4); // normal assignment is possible, however
    #if SCRIPT_OPERATOR_MODE==SCRIPT_OPERATOR_MODE_FLEXIBLE
    REQUIRE(s.test(scriptbody, "var h; var i = 8; h = 3 + (i = 8)")->doubleValue() == 4); // inner "=" is treated as comparison
    #elif SCRIPT_OPERATOR_MODE==SCRIPT_OPERATOR_MODE_C
    REQUIRE(s.test(scriptbody, "var h; var i = 8; h = 3 + (i = 8)")->isErr() == true); // no nested assignment allowed
    #elif SCRIPT_OPERATOR_MODE==SCRIPT_OPERATOR_MODE_PASCAL
    REQUIRE(s.test(scriptbody, "var h; var i := 8; h := 3 + (i := 8)")->isErr() == true); // no nested assignment allowed
    REQUIRE(s.test(scriptbody, "glob j; j = 44; return j")->numValue() == 44);
    REQUIRE(s.test(scriptbody, "glob j; return j")->numValue() == 44); // should still be there
    #endif
    // scope and unset
    REQUIRE(s.test(scriptbody|keepvars, "glob k; k=42; return k")->doubleValue() == 42);
    REQUIRE(s.test(scriptbody|keepvars, "k")->doubleValue() == 42); // must stay
    REQUIRE(s.test(scriptbody|keepvars, "var k = 43")->doubleValue() == 43); // hide global k with a local k
    REQUIRE(s.test(scriptbody|keepvars, "k")->doubleValue() == 43); // must stay
    REQUIRE(s.test(scriptbody|keepvars, "unset k = 47")->isErr() == true); // unset cannot have an initializer
    REQUIRE(s.test(scriptbody|keepvars, "k")->doubleValue() == 43); // global still shadowed
    REQUIRE(s.test(scriptbody|keepvars, "unset k")->isErr() == false); // should work
    REQUIRE(s.test(scriptbody|keepvars, "k")->doubleValue() == 42); // again global
    REQUIRE(s.test(scriptbody|keepvars, "unset k")->isErr() == false); // should work, deleting global
    REQUIRE(s.test(scriptbody|keepvars, "k")->isErr() == true); // deleted
    REQUIRE(s.test(scriptbody|keepvars, "unset k")->isErr() == false); // unsetting nonexisting variable should still not throw an error
  }

  // "{\"array\":[\"first\",2,3,\"fourth\",6.6],\"obj\":{\"objA\":\"A\",\"objB\":42,\"objC\":{\"objD\":\"D\",\"objE\":45}},\"string\":\"abc\",\"number\":42,\"bool\":true}"
  SECTION("json manipulation") {
    REQUIRE(s.test(scriptbody, "var js = " JSON_TEST_OBJ "; js.obj.objF = 46; log(6,js); return js.obj.objF")->doubleValue() == 46);
    REQUIRE(s.test(scriptbody, "var js = " JSON_TEST_OBJ "; js.obj['objA'] = 'AA'; log(6,js); return js.obj.objA")->stringValue() == "AA");
    REQUIRE(s.test(scriptbody, "var js = " JSON_TEST_OBJ "; js.array[5] = 'AA'; log(6,js); return js.array[5]")->stringValue() == "AA");
    REQUIRE(s.test(scriptbody, "var js = " JSON_TEST_OBJ "; js.array[0] = 'modified'; log(6,js); return js.array[0]")->stringValue() == "modified");
    // test if json assignment really copies var, such that modifications to the members of the copied object does NOT affect the original val
    REQUIRE(s.test(scriptbody, "var js = " JSON_TEST_OBJ "; var js2 = js; js2.array[0] = 'first MODIFIED'; log(6,js); return js.array[0]")->stringValue() == "first");
  }

  SECTION("json leaf values") {
    REQUIRE(s.test(scriptbody, "var j = { 'text':'hello' }; j.text")->stringValue() == "hello");
    REQUIRE(s.test(scriptbody, "var j = { 'text':'hello' }; j.text=='hello'")->boolValue() == true);
    REQUIRE(s.test(scriptbody, "var j = { 'text':'hello' }; j.text+' world'")->stringValue() == "hello world"); // calculatioValue() of json text field must be string that can be appended to
    REQUIRE(s.test(scriptbody, "var j = { 'number':42 }; j.number")->doubleValue() == 42.0);
    REQUIRE(s.test(scriptbody, "var j = { 'number':42 }; j.number==42")->boolValue() == true);
    REQUIRE(s.test(scriptbody, "var j = { 'number':42 }; j.number+2")->doubleValue() == 44.0); // calculatioValue() of json numeric field must be number that can be added to
  }

  SECTION("control flow") {
    REQUIRE(s.test(scriptbody, "var cond = 1; var res = 'none'; var cond = 1; if (cond==1) res='one' else res='NOT one'; return res")->stringValue() == "one");
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var cond = 2; if (cond==1) res='one' else res='NOT one'; return res")->stringValue() == "NOT one");
    // without statement separators (JavaScript style)
    REQUIRE(s.test(scriptbody, "var cond = 1; var res = 'none'; var cond = 1; if (cond==1) res='one' else if (cond==2) res='two' else res='not 1 or 2'; return res")->stringValue() == "one");
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var cond = 2; if (cond==1) res='one' else if (cond==2) res='two' else res='not 1 or 2'; return res")->stringValue() == "two");
    REQUIRE(s.test(scriptbody, "var cond = 5; var res = 'none'; var cond = 5; if (cond==1) res='one' else if (cond==2) res='two' else res='not 1 or 2'; return res")->stringValue() == "not 1 or 2");
    // with statement separators
    REQUIRE(s.test(scriptbody, "var cond = 1; var res = 'none'; var cond = 1; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2'; return res")->stringValue() == "one");
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var cond = 2; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2'; return res")->stringValue() == "two");
    REQUIRE(s.test(scriptbody, "var cond = 5; var res = 'none'; var cond = 5; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2'; return res")->stringValue() == "not 1 or 2");
    // with skipped return statements
    REQUIRE(s.test(scriptbody, "var cond = 1; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';")->stringValue() == "one");
    REQUIRE(s.test(scriptbody, "var cond = 2; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';")->stringValue() == "two");
    REQUIRE(s.test(scriptbody, "var cond = 5; if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2';")->stringValue() == "not 1 or 2");
    // nested, inner if/elseif/else must be entirely skipped
    REQUIRE(s.test(scriptbody, "var cond = 1; if (false) { if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2'; } return 'skipped'")->stringValue() == "skipped");
    REQUIRE(s.test(scriptbody, "var cond = 2; if (false) { if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2'; } return 'skipped'")->stringValue() == "skipped");
    REQUIRE(s.test(scriptbody, "var cond = 5; if (false) { if (cond==1) return 'one'; else if (cond==2) return 'two'; else return 'not 1 or 2'; } return 'skipped'")->stringValue() == "skipped");
    // special cases
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; if (cond==1) res='one'; else if (cond==2) res='two'; else res='not 1 or 2' return res")->stringValue() == "two");
    // blocks
    REQUIRE(s.test(scriptbody, "var cond = 1; var res = 'none'; var res2 = 'none'; if (cond==1) res='one'; res2='two'; return string(res) + ',' + res2")->stringValue() == "one,two");
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) res='one'; res2='two'; return string(res) + ',' + res2")->stringValue() == "none,two");
    REQUIRE(s.test(scriptbody, "var cond = 1; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two' }; return string(res) + ',' + res2")->stringValue() == "one,two");
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two' }; return string(res) + ',' + res2")->stringValue() == "none,none");
    // blocks with delimiter variations
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two'; }; return string(res) + ',' + res2")->stringValue() == "none,none");
    REQUIRE(s.test(scriptbody, "var cond = 2; var res = 'none'; var res2 = 'none'; if (cond==1) { res='one'; res2='two'; } return string(res) + ',' + res2")->stringValue() == "none,none");
    // while, continue, break
    REQUIRE(s.test(scriptbody, "var count = 0; while (count<5) count = count+1; return count")->doubleValue() == 5);
    REQUIRE(s.test(scriptbody, "var res = ''; var count = 0; while (count<5) { count = count+1; res = res+string(count); } return res")->stringValue() == "12345");
    REQUIRE(s.test(scriptbody, "var res = ''; var count = 0; while (count<5) { count = count+1; if (count==3) continue; res = res+string(count); } return res")->stringValue() == "1245");
    REQUIRE(s.test(scriptbody, "var res = ''; var count = 0; while (count<5) { count = count+1; if (count==3) break; res = res+string(count); } return res")->stringValue() == "12");
    // skipping execution of chained expressions
    REQUIRE(s.test(scriptbody, "if (false) return string(\"A\" + \"X\" + \"B\")")->undefined() == true);
    REQUIRE(s.test(scriptbody, "if (false) return string(\"A\" + string(\"\") + \"B\")")->undefined() == true);
    // throw/try/catch
    REQUIRE(s.test(scriptbody, "throw('test error')")->isErr() == true);
    REQUIRE(Error::isError(s.test(scriptbody, "throw('test error')")->errorValue(), ScriptError::domain(), ScriptError::User) == true);
    REQUIRE(strcmp(s.test(scriptbody, "throw('test error')")->errorValue()->getErrorMessage(), "test error") == 0);
    REQUIRE(Error::isError(s.test(scriptbody, "try var zerodiv = 7/0; catch as error return error; return 'ok'")->errorValue(), ScriptError::domain(), ScriptError::DivisionByZero) == true);
    REQUIRE(Error::isError(s.test(scriptbody, "try var zerodiv = 7/0; catch as error { return error; } return 'ok'")->errorValue(), ScriptError::domain(), ScriptError::DivisionByZero) == true);
    REQUIRE(s.test(scriptbody, "try var zerodiv = 7/0; catch return 'not allowed'; return 'ok'")->stringValue() == "not allowed");
    REQUIRE(s.test(scriptbody, "try var zerodiv = 7/1; catch return 'error'; return zerodiv")->doubleValue() == 7);
    REQUIRE(s.test(scriptbody, "try { var zerodiv = 42; zerodiv = 7/0 } catch { log(6,'CAUGHT!') }; return zerodiv")->doubleValue() == 42);
    REQUIRE(s.test(scriptbody, "try { var zerodiv = 42; zerodiv = 7/0; zerodiv = 66 } catch { log(6,'CAUGHT!') }; return zerodiv")->doubleValue() == 42);
    REQUIRE(s.test(scriptbody, "try { var zerodiv = 42; zerodiv = 7/1; zerodiv = 66 } catch { log(6,'CAUGHT!') }; return zerodiv")->doubleValue() == 66);
    // Syntax errors
    REQUIRE(Error::isError(s.test(scriptbody, "78/9#")->errorValue(), ScriptError::domain(), ScriptError::Syntax) == true);
    REQUIRE(Error::isError(s.test(scriptbody, "78/#9")->errorValue(), ScriptError::domain(), ScriptError::Syntax) == true);
    // Not Syntax error in a script, the three numbers are separate statements, the last one is returned
    REQUIRE(s.test(scriptbody, "42 43 44")->intValue() == 44);
  }

  SECTION("custom functions") {
    // Simple function w/o args
    REQUIRE(s.test(sourcecode|floatingGlobs, "function f42() { return 42; }")->isErr() == false);
    REQUIRE(s.test(scriptbody, "f42()")->doubleValue() == 42);
    REQUIRE(s.test(scriptbody, "f42(7)")->isErr() == true); // no args expected
    // Simple function with one arg
    REQUIRE(s.test(sourcecode|floatingGlobs, "function f42p(a) { return 42+a; }")->isErr() == false);
    REQUIRE(s.test(scriptbody, "f42p()")->isErr() == true); // needs a arg
    REQUIRE(s.test(scriptbody, "f42p(null)")->isErr() == false); // arg may be explicit null
    REQUIRE(s.test(scriptbody, "f42p(null)")->undefined() == true); // null in calculation results in null
    REQUIRE(s.test(scriptbody, "f42p(8)")->doubleValue() == 50);
    REQUIRE(s.test(scriptbody, "f42p(41,4)")->isErr() == true); // too many args
    // Simple function with more than one arg
    REQUIRE(s.test(sourcecode|floatingGlobs, "function f42pp(a,b) { return 42+a+b; }")->isErr() == false);
    REQUIRE(s.test(scriptbody, "f42pp()")->isErr() == true); // needs a arg
    REQUIRE(s.test(scriptbody, "f42pp(1)")->isErr() == true); // needs two args
    REQUIRE(s.test(scriptbody, "f42pp(1,2)")->doubleValue() == 45);
    // variadic function
    REQUIRE(s.test(sourcecode|floatingGlobs, "function m(...) { return 1+ifvalid(arg1,0)+ifvalid(arg2,0)+ifvalid(arg3,0); } return m")->stringValue() == "function");
    REQUIRE(s.test(scriptbody, "m")->stringValue() == "function");
    REQUIRE(s.test(scriptbody, "m()")->doubleValue() == 1);
    REQUIRE(s.test(scriptbody, "m(1,2,3)")->doubleValue() == 7);
    REQUIRE(s.test(scriptbody, "m(22,33)")->doubleValue() == 56);
    // function with one required and some more optional params
    REQUIRE(s.test(sourcecode|floatingGlobs, "function m2(a,...) { return a+ifvalid(arg2,0)+ifvalid(arg3,0)+ifvalid(arg4,0); } return m2")->stringValue() == "function");
    REQUIRE(s.test(scriptbody, "m2")->stringValue() == "function");
    REQUIRE(s.test(scriptbody, "m2()")->isErr() == true);
    REQUIRE(s.test(scriptbody, "m2(42)")->doubleValue() == 42);
    REQUIRE(s.test(scriptbody, "m2(42,3)")->doubleValue() == 45);
    REQUIRE(s.test(scriptbody, "m2(42,1,2)")->doubleValue() == 45);
    REQUIRE(s.test(scriptbody, "m2(42,1,1,1)")->doubleValue() == 45);
    REQUIRE(s.test(scriptbody, "m2(42,1,1,1,error('dummy'),'test',77.77)")->doubleValue() == 45);
    // unsetting functions
    REQUIRE(s.test(scriptbody, "unset m")->isErr() == false);
    REQUIRE(s.test(scriptbody, "m")->isErr() == true); // should be gone
    REQUIRE(s.test(scriptbody, "undeclare()")->isErr() == true); // works only in floatingGlobs mode
    REQUIRE(s.test(scriptbody|floatingGlobs, "undeclare()")->isErr() == false);
    REQUIRE(s.test(scriptbody, "m2")->isErr() == true); // should be gone
    REQUIRE(s.test(scriptbody, "f42")->isErr() == true); // should be gone
    REQUIRE(s.test(scriptbody, "f42p")->isErr() == true); // should be gone
    REQUIRE(s.test(scriptbody, "f42pp")->isErr() == true); // should be gone
  }

}

TEST_CASE_METHOD(AsyncScriptingFixture, "async", "[scripting]") {

  SECTION("fixtureTest") {
    REQUIRE(scriptTest(scriptbody, "42")->doubleValue() == 42);
  }

  SECTION("eval") {
    REQUIRE(scriptTest(expression, "eval('333*777')")->doubleValue() == 333*777); // eval is marked async
  }

  SECTION("delay") {
    REQUIRE(scriptTest(scriptbody, "delay(2)")->isErr() == false); // no error
    REQUIRE(runningTime() ==  Approx(2).epsilon(0.01));
  }

  SECTION("concurrency") {
    REQUIRE(scriptTest(scriptbody, "var res=''; log(4, 'will take 2 secs'); concurrent as test { delay(2); res = res + '2sec' } delay(1); res = res+'1sec'; await(test); res")->stringValue() == "1sec2sec");
    REQUIRE(runningTime() ==  Approx(2).epsilon(0.05));
    REQUIRE(scriptTest(scriptbody, "var res=''; log(4, 'will take 3 secs'); concurrent as test { delay(3); res = res + '3sec' } concurrent as test2 { delay(2); res = res + '2sec' } delay(1); res = res+'1sec'; await(test); res")->stringValue() == "1sec2sec3sec");
    REQUIRE(runningTime() ==  Approx(3).epsilon(0.05));
    REQUIRE(scriptTest(scriptbody, "var res=''; log(4, 'will take 3 secs'); concurrent as test { delay(3); res = res + '3sec' } concurrent as test2 { delay(2); res = res + '2sec' } delay(1); res = res+'1sec'; abort(test2) await(test); res")->stringValue() == "1sec3sec");
    REQUIRE(runningTime() ==  Approx(3).epsilon(0.05));
    // assignment of thread variables
    // - thread must be assigned by reference to a new variable
    REQUIRE(scriptTest(scriptbody, "var res=''; concurrent as test { delay(0.5); res = 'done' } var test2 = test; abort(test2); await(test); res")->stringValue() == "");
    REQUIRE(runningTime() < 0.4);
    // - "as" clause must assign to existing global if one exists
    REQUIRE(scriptTest(scriptbody, "glob th; var res=''; concurrent as th { delay(0.5); res = 'done' } var th='notThread'; unset th; abort(th); delay(1); res")->stringValue() == "");
    REQUIRE(runningTime() < 0.4);
  }

  SECTION("event handlers") {
    // Note: might fail when execution is sluggish, because order of events might be affected then:  5/7  1  10/7  2  15/7  20/7  3  25/7  4  30/7   4.5  Seconds
    REQUIRE(scriptTest(sourcecode, "glob res='decl'; on(every(1) & !initial()) { res = res + 'Ping' } on(every(5/7) & !initial()) { res = res + 'Pong' } res='init'; log(4, 'will take 4.5 secs'); delay(4.5); res")->stringValue() == "initPongPingPongPingPongPongPingPongPingPong");
    REQUIRE(runningTime() ==  Approx(4.5).epsilon(0.05));
}

}

#if ENABLE_HTTP_SCRIPT_FUNCS

#define TEST_URL "plan44.ch/testing/httptest.php"
#define DATA_IN_7SEC_TEST_URL "plan44.ch/testing/httptest.php?delay=7"

TEST_CASE_METHOD(AsyncScriptingFixture, "http scripting", "[scripting],[FOCUS]") {

  SECTION("geturl") {
    REQUIRE(scriptTest(sourcecode, "find(geturl('http://" TEST_URL "'), 'Document OK')")->intValue() > 0);
    REQUIRE(scriptTest(sourcecode, "find(geturl('https://" TEST_URL "'), 'Document OK')")->intValue() > 0);
    REQUIRE(scriptTest(sourcecode, "log(4, 'will take 5 secs'); geturl('http://" DATA_IN_7SEC_TEST_URL "', 5)")->isErr() == true);
    REQUIRE(runningTime() ==  Approx(5).epsilon(0.05));
    REQUIRE(scriptTest(sourcecode, "glob res='not completed'; log(4, 'will take 3 secs'); concurrent as http { res=geturl('http://" DATA_IN_7SEC_TEST_URL "', 5) } delay(3); abort(http); return res")->stringValue() == "not completed");
    REQUIRE(runningTime() ==  Approx(3).epsilon(0.05));
  }
  SECTION("posturl") {
    REQUIRE(scriptTest(sourcecode, "find(posturl('http://" TEST_URL "', 'Gugus'), 'POST data=\"Gugus\"')")->intValue() > 0);
    REQUIRE(scriptTest(sourcecode, "find(posturl('http://" TEST_URL "', 10, 'Gugus'), 'POST data=\"Gugus\"')")->intValue() > 0);
    REQUIRE(scriptTest(sourcecode, "find(posturl('https://" TEST_URL "', 'Gugus'), 'POST data=\"Gugus\"')")->intValue() > 0);
  }

  SECTION("puturl") {
    REQUIRE(scriptTest(sourcecode, "find(puturl('http://" TEST_URL "', 'Gugus'), 'PUT data=\"Gugus\"')")->intValue() > 0);
    REQUIRE(scriptTest(sourcecode, "find(puturl('http://" TEST_URL "', 10, 'Gugus'), 'PUT data=\"Gugus\"')")->intValue() > 0);
    REQUIRE(scriptTest(sourcecode, "find(puturl('https://" TEST_URL "', 'Gugus'), 'PUT data=\"Gugus\"')")->intValue() > 0);
  }

}

#endif // ENABLE_HTTP_SCRIPT_FUNCS

