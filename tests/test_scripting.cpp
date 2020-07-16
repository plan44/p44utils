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


class TestLookup : public ClassLevelLookup
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

  ScriptingCodeFixture()
  {
    SETERRLEVEL(0, false); // everything to stdout, once
    LOG(LOG_ERR, "\n+++++++ constructing ScriptingCodeFixture");
    testLookup.isMemberVariable();
    StandardScriptingDomain::sharedDomain().setLogLevelOffset(LOGLEVELOFFSET);
    mainContext = StandardScriptingDomain::sharedDomain().newContext();
    s.setSharedMainContext(mainContext);
    mainContext->registerMemberLookup(&testLookup);
    mainContext->domain()->setMemberByName("jstest", new JsonValue(JsonObject::objFromText(JSON_TEST_OBJ)), global|create);
  };
  virtual ~ScriptingCodeFixture()
  {
    LOG(LOG_ERR, "------- destructing ScriptingCodeFixture\n");
  }

};




// MARK: CodeCursor tests

TEST_CASE("CodeCursor", "[scripting]" )
{
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
    REQUIRE(SourceCursor("42").parseNumericLiteral()->numValue() == 42);
    REQUIRE(SourceCursor("0x42").parseNumericLiteral()->numValue() == 0x42);
    REQUIRE(SourceCursor("42.42").parseNumericLiteral()->numValue() == 42.42);

    REQUIRE(SourceCursor("\"Hello\"").parseStringLiteral()->stringValue() == "Hello");
    REQUIRE(SourceCursor("\"He\\x65llo\"").parseStringLiteral()->stringValue() == "Heello");
    REQUIRE(SourceCursor("\"\\tHello\\nWorld, \\\"double quoted\\\"\"").parseStringLiteral()->stringValue() == "\tHello\nWorld, \"double quoted\""); // C string style
    REQUIRE(SourceCursor("'Hello\\nWorld, \"double quoted\" text'").parseStringLiteral()->stringValue() == "Hello\\nWorld, \"double quoted\" text"); // PHP single quoted style
    REQUIRE(SourceCursor("'Hello\\nWorld, ''single quoted'' text'").parseStringLiteral()->stringValue() == "Hello\\nWorld, 'single quoted' text"); // include single quotes in single quoted text by doubling them
    REQUIRE(SourceCursor("\"\"").parseStringLiteral()->stringValue() == ""); // empty string

    REQUIRE(SourceCursor("12:35").parseNumericLiteral()->numValue() == 45300);
    REQUIRE(SourceCursor("14:57:42").parseNumericLiteral()->numValue() == 53862);
    REQUIRE(SourceCursor("14:57:42.328").parseNumericLiteral()->numValue() == 53862.328);
    REQUIRE(SourceCursor("1.Jan").parseNumericLiteral()->numValue() == 0);
    REQUIRE(SourceCursor("1.1.").parseNumericLiteral()->numValue() == 0);
    REQUIRE(SourceCursor("19.Feb").parseNumericLiteral()->numValue() == 49);
    REQUIRE(SourceCursor("19.FEB").parseNumericLiteral()->numValue() == 49);
    REQUIRE(SourceCursor("19.2.").parseNumericLiteral()->numValue() == 49);

    REQUIRE(SourceCursor("{ 'type':'object', 'test':42 }").parseJSONLiteral()->stringValue() == "{\"type\":\"object\",\"test\":42}");
    REQUIRE(SourceCursor("[ 'first', 2, 3, 'fourth', 6.6 ]").parseJSONLiteral()->stringValue() == "[\"first\",2,3,\"fourth\",6.6]");
  }

}

// MARK: - debug test case

TEST_CASE_METHOD(ScriptingCodeFixture, "Focus", "[scripting],[DEBUG]" )
{
  REQUIRE(s.test(expression, "jstest['array',0]")->stringValue() == "first");
}

// MARK: - Literals

TEST_CASE_METHOD(ScriptingCodeFixture, "Literals", "[scripting],[FOCUS]" )
{
  SECTION("Literals") {
    REQUIRE(s.test(expression, "42")->numValue() == 42);
    REQUIRE(s.test(expression, "0x42")->numValue() == 0x42);
    REQUIRE(s.test(expression, "42.42")->numValue() == 42.42);

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
    REQUIRE(s.test(expression, "14:57:42.328")->numValue() == 53862.328);
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
    REQUIRE(s.test(expression, "42 // 43")->numValue() == 42);
    REQUIRE(s.test(expression, "/* 43 */ 42")->numValue() == 42);
    REQUIRE(s.test(expression, "/* 43 // 42")->undefined() == true);
  }
}


// MARK: - Lookups

TEST_CASE_METHOD(ScriptingCodeFixture, "lookups", "[scripting],[FOCUS]") {

  SECTION("Scalars") {
    REQUIRE(s.test(expression, "UA")->numValue() == 42);
    REQUIRE(s.test(expression, "dummy")->defined() == false); // unknown var is not a value
    REQUIRE(s.test(expression, "dummy")->isErr() == true); // ..and not value-ok
    REQUIRE(s.test(expression, "almostUA")->numValue() == 42.7);
    REQUIRE(s.test(expression, "UAtext")->stringValue() == "fortyTwo");
    REQUIRE(s.test(expression, "UAtext")->stringValue() == "fortyTwo");
    REQUIRE(s.test(expression, "UAtext")->stringValue() == "fortyTwo");
  }

  SECTION("Json") {
    // JSON tests, see JSON_TEST_OBJ
    REQUIRE(s.test(expression, "jstest")->stringValue() == JSON_TEST_OBJ);
    REQUIRE(s.test(expression, "jstest.string")->stringValue() == "abc");
    REQUIRE(s.test(expression, "jstest.number")->numValue() == 42);
    REQUIRE(s.test(expression, "jstest.bool")->boolValue() == true);
    REQUIRE(s.test(expression, "jstest.array[2]")->numValue() == 3);
    REQUIRE(s.test(expression, "jstest.array[0]")->stringValue() == "first");
    REQUIRE(s.test(expression, "jstest['array'][0]")->stringValue() == "first");
    REQUIRE(s.test(expression, "jstest['array',0]")->stringValue() == "first");
    REQUIRE(s.test(expression, "jstest.obj.objA")->stringValue() == "A");
    REQUIRE(s.test(expression, "jstest.obj.objB")->numValue() == 42);
    REQUIRE(s.test(expression, "jstest.obj['objB']")->numValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj'].objB")->numValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj','objB']")->numValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj']['objB']")->numValue() == 42);
    REQUIRE(s.test(expression, "jstest['obj'].objC.objD")->stringValue() == "D");
    REQUIRE(s.test(expression, "jstest['obj'].objC.objE")->numValue() == 45);
  }

}


// MARK: - Expressions

TEST_CASE_METHOD(ScriptingCodeFixture, "expressions", "[scripting],[FOCUS]") {

  SECTION("Operations") {
    REQUIRE(s.test(expression, "-42.42")->numValue() == -42.42); // unary minus
    REQUIRE(s.test(expression, "!true")->numValue() == 0); // unary not
    REQUIRE(s.test(expression, "\"UA\"")->stringValue() == "UA");
    REQUIRE(s.test(expression, "42.7+42")->numValue() == 42.7+42.0);
    REQUIRE(s.test(expression, "42.7-24")->numValue() == 42.7-24.0);
    REQUIRE(s.test(expression, "42.7*42")->numValue() == 42.7*42.0);
    REQUIRE(s.test(expression, "42.7/24")->numValue() == 42.7/24.0);
    REQUIRE(s.test(expression, "5%2")->numValue() == 1);
    REQUIRE(s.test(expression, "5%2.5")->numValue() == 0);
    REQUIRE(s.test(expression, "5%1.5")->numValue() == 0.5);
    REQUIRE(s.test(expression, "5.5%2")->numValue() == 1.5);
    REQUIRE(s.test(expression, "78%9")->numValue() == 6.0);
    REQUIRE(s.test(expression, "77.77%9")->numValue() == Approx(5.77));
    REQUIRE(s.test(expression, "78/0")->isErr() == true); // division by zero
    REQUIRE(s.test(expression, "\"ABC\" + \"abc\"")->stringValue() == "ABCabc");
    REQUIRE(s.test(expression, "\"empty\"+\"\"")->stringValue() == "empty");
    REQUIRE(s.test(expression, "\"\"+\"empty\"")->stringValue() == "empty");
    REQUIRE(s.test(expression, "1==true")->boolValue() == true);
    REQUIRE(s.test(expression, "1==yes")->boolValue() == true);
    REQUIRE(s.test(expression, "0==false")->boolValue() == true);
    REQUIRE(s.test(expression, "0==no")->boolValue() == true);
    REQUIRE(s.test(expression, "undefined")->boolValue() == false);
    REQUIRE(s.test(expression, "undefined!=undefined")->boolValue() == false);
    REQUIRE(s.test(expression, "undefined==undefined")->boolValue() == false);
    REQUIRE(s.test(expression, "undefined==42")->boolValue() == false);
    REQUIRE(s.test(expression, "42==undefined")->boolValue() == false);
    REQUIRE(s.test(expression, "undefined!=42")->boolValue() == false);
    REQUIRE(s.test(expression, "42!=undefined")->boolValue() == false);
    REQUIRE(s.test(expression, "42>undefined")->undefined() == true);
    REQUIRE(s.test(expression, "42<undefined")->undefined() == true);
    REQUIRE(s.test(expression, "undefined<42")->undefined() == true);
    REQUIRE(s.test(expression, "undefined>42")->undefined() == true);
    REQUIRE(s.test(expression, "!undefined")->undefined() == true);
    REQUIRE(s.test(expression, "-undefined")->undefined() == true);
    REQUIRE(s.test(expression, "42<>78")->boolValue() == true);
    REQUIRE(s.test(expression, "42=42")->defined() == (SCRIPT_OPERATOR_MODE!=SCRIPT_OPERATOR_MODE_C));
    REQUIRE(s.test(expression, "42=42")->boolValue() == (SCRIPT_OPERATOR_MODE!=SCRIPT_OPERATOR_MODE_C));
    // Comparisons
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
    REQUIRE(s.test(expression, "12*3+7")->numValue() == 12*3+7);
    REQUIRE(s.test(expression, "12*(3+7)")->numValue() == 12*(3+7));
    REQUIRE(s.test(expression, "12/3-7")->numValue() == 12/3-7);
    REQUIRE(s.test(expression, "12/(3-7)")->numValue() == 12/(3-7));
  }

  SECTION("functions") {
    // testing
    REQUIRE(s.test(expression, "ifvalid(undefined,42)")->numValue() == 42);
    REQUIRE(s.test(expression, "ifvalid(33,42)")->numValue() == 33);
    REQUIRE(s.test(expression, "isvalid(undefined)")->boolValue() == false);
    REQUIRE(s.test(expression, "isvalid(undefined)")->undefined() == false);
    REQUIRE(s.test(expression, "isvalid(1234)")->boolValue() == true);
    REQUIRE(s.test(expression, "isvalid(0)")->boolValue() == true);
    REQUIRE(s.test(expression, "if(true, 'TRUE', 'FALSE')")->stringValue() == "TRUE");
    REQUIRE(s.test(expression, "if(false, 'TRUE', 'FALSE')")->stringValue() == "FALSE");
    // numbers
    REQUIRE(s.test(expression, "number(undefined)")->numValue() == 0);
    REQUIRE(s.test(expression, "number(undefined)")->undefined() == false);
    REQUIRE(s.test(expression, "number(0)")->boolValue() == false);
    REQUIRE(s.test(expression, "abs(33)")->numValue() == 33);
    REQUIRE(s.test(expression, "abs(undefined)")->undefined() == true);
    REQUIRE(s.test(expression, "abs(-33)")->numValue() == 33);
    REQUIRE(s.test(expression, "abs(0)")->numValue() == 0);
    REQUIRE(s.test(expression, "int(33)")->numValue() == 33);
    REQUIRE(s.test(expression, "int(33.3)")->numValue() == 33);
    REQUIRE(s.test(expression, "int(33.6)")->numValue() == 33);
    REQUIRE(s.test(expression, "int(-33.3)")->numValue() == -33);
    REQUIRE(s.test(expression, "int(-33.6)")->numValue() == -33);
    REQUIRE(s.test(expression, "round(33)")->numValue() == 33);
    REQUIRE(s.test(expression, "round(33.3)")->numValue() == 33);
    REQUIRE(s.test(expression, "round(33.6)")->numValue() == 34);
    REQUIRE(s.test(expression, "round(-33.6)")->numValue() == -34);
    REQUIRE(s.test(expression, "round(33.3, 0.5)")->numValue() == 33.5);
    REQUIRE(s.test(expression, "round(33.6, 0.5)")->numValue() == 33.5);
    REQUIRE(s.test(expression, "frac(33)")->numValue() == 0);
    REQUIRE(s.test(expression, "frac(-33)")->numValue() == 0);
    REQUIRE(s.test(expression, "frac(33.6)")->numValue() == Approx(0.6));
    REQUIRE(s.test(expression, "frac(-33.6)")->numValue() == Approx(-0.6));
    REQUIRE(s.test(expression, "random(0,10)")->numValue() < 10);
    REQUIRE(s.test(expression, "random(0,10) != random(0,10)")->boolValue() == true);
    REQUIRE(s.test(expression, "number('33')")->numValue() == 33);
    REQUIRE(s.test(expression, "number('0x33')")->numValue() == 0x33);
    REQUIRE(s.test(expression, "number('33 gugus')")->numValue() == 33); // best effort, ignore trailing garbage
    REQUIRE(s.test(expression, "number('gugus 33')")->numValue() == 0); // best effort, nothing readable
    REQUIRE(s.test(expression, "min(42,78)")->numValue() == 42);
    REQUIRE(s.test(expression, "min(78,42)")->numValue() == 42);
    REQUIRE(s.test(expression, "max(42,78)")->numValue() == 78);
    REQUIRE(s.test(expression, "max(78,42)")->numValue() == 78);
    REQUIRE(s.test(expression, "limited(15,10,20)")->numValue() == 15);
    REQUIRE(s.test(expression, "limited(2,10,20)")->numValue() == 10);
    REQUIRE(s.test(expression, "limited(42,10,20)")->numValue() == 20);
    REQUIRE(s.test(expression, "cyclic(15,10,20)")->numValue() == 15);
    REQUIRE(s.test(expression, "cyclic(2,10,20)")->numValue() == 12);
    REQUIRE(s.test(expression, "cyclic(-18,10,20)")->numValue() == 12);
    REQUIRE(s.test(expression, "cyclic(22,10,20)")->numValue() == 12);
    REQUIRE(s.test(expression, "cyclic(42,10,20)")->numValue() == 12);
    REQUIRE(s.test(expression, "cyclic(-10.8,1,2)")->numValue() == Approx(1.2));
    REQUIRE(s.test(expression, "cyclic(-1.8,1,2)")->numValue() == Approx(1.2));
    REQUIRE(s.test(expression, "cyclic(2.2,1,2)")->numValue() == Approx(1.2));
    REQUIRE(s.test(expression, "cyclic(4.2,1,2)")->numValue() == Approx(1.2));
    REQUIRE(s.test(expression, "epochtime()")->numValue() == Approx((double)MainLoop::unixtime()/Day));
    // strings
    REQUIRE(s.test(expression, "string(33)")->stringValue() == "33");
    REQUIRE(s.test(expression, "string(undefined)")->stringValue() == "undefined");
    REQUIRE(s.test(expression, "strlen('gugus')")->numValue() == 5);
    REQUIRE(s.test(expression, "substr('gugus',3)")->stringValue() == "us");
    REQUIRE(s.test(expression, "substr('gugus',3,1)")->stringValue() == "u");
    REQUIRE(s.test(expression, "substr('gugus',7,1)")->stringValue() == "");
    REQUIRE(s.test(expression, "find('gugus dada', 'ad')")->numValue() == 7);
    REQUIRE(s.test(expression, "find('gugus dada', 'blubb')")->undefined() == true);
    REQUIRE(s.test(expression, "find('gugus dada', 'gu', 1)")->numValue() == 2);
    REQUIRE(s.test(expression, "format('%04d', 33.7)")->stringValue() == "0033");
    REQUIRE(s.test(expression, "format('%4d', 33.7)")->stringValue() == "  33");
    REQUIRE(s.test(expression, "format('%.1f', 33.7)")->stringValue() == "33.7");
    REQUIRE(s.test(expression, "format('%08X', 0x24F5E21)")->stringValue() == "024F5E21");
    // divs
    REQUIRE(s.test(expression, "eval('333*777')")->numValue() == 333*777);
    // error handling
    REQUIRE(s.test(expression, "error('testerror')")->stringValue() == string_format("testerror (ScriptError::User[%d])", ScriptError::User));
    REQUIRE(s.test(expression, "errordomain(error('testerror'))")->stringValue() == "ScriptError");
    REQUIRE(s.test(expression, "errorcode(error('testerror'))")->numValue() == ScriptError::User);
    REQUIRE(s.test(expression, "errormessage(error('testerror'))")->stringValue() == "testerror");
    // separate terms ARE a syntax error in a expression! (not in a script, see below)
    REQUIRE(s.test(expression, "42 43 44")->stringValue().find(string_format("(ScriptError::Syntax[%d])", ScriptError::Syntax)) != string::npos);
    // special cases
    REQUIRE(s.test(expression, "hour()")->numValue() > 0);
    // should be case insensitive
    REQUIRE(s.test(expression, "HOUR()")->numValue() > 0);
    REQUIRE(s.test(expression, "IF(TRUE, 'TRUE', 'FALSE')")->stringValue() == "TRUE");
  }
}

#if 0

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
