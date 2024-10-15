//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "catch_amalgamated.hpp"

#include "tlv.hpp"

using namespace p44;

TEST_CASE( "TLV simple tags", "[tlv]" ) {

  TLVWriter writer;
  // IDs
  writer.put_id_string("tlvtest");
  writer.put_string("value");
  writer.put_id_unsigned(42);
  writer.put_unsigned(42);
  // unsigneds of 1,2,3, 4 bytes
  writer.put_unsigned(88);
  writer.put_unsigned(288); // 2 bytes
  writer.put_unsigned(78888); // 3 bytes
  writer.put_unsigned(16777888); // 4 bytes
  // signeds of 1,2,3,4 bytes
  writer.put_signed(88);
  writer.put_signed(288); // 2 bytes
  writer.put_signed(78888); // 3 bytes
  writer.put_signed(16777888); // 4 bytes
  writer.put_signed(-88);
  writer.put_signed(-288); // 2 bytes
  writer.put_signed(-78888); // 3 bytes
  writer.put_signed(-16777888); // 4 bytes
  // string
  writer.put_string("anything");
  // blob
  writer.put_blob("BLOB_but_no_more", 4); // only first 4!
  // uncounted countainer
  writer.start_container();
  writer.put_unsigned(499);
  writer.put_unsigned(49999);
  writer.put_signed(-1);
  writer.put_signed(0);
  // - nested counted container
  writer.start_counted_container();
  writer.put_unsigned(1);
  writer.put_unsigned(22);
  writer.put_unsigned(333);
  writer.end_container(); // counted
  writer.end_container(); // uncounted
  // seek target
  writer.put_id_string("seektarget");
  writer.put_unsigned(424242);
  // done

  string tlv = writer.finalize();
  TLVReader reader(tlv);

  SECTION("simple tags") {
    uint64_t ui;
    string s;
    int64_t si;
    string b;
    size_t n;

    // tag ids
    REQUIRE( (reader.nextIs(tlv_string, "tlvtest")) == true );
    REQUIRE( (reader.read_string(s) && s=="value") == true );
    REQUIRE( (reader.nextIs(tlv_unsigned, 42)) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==42) == true );
    // unsigned numbers
    REQUIRE( (reader.read_unsigned(ui) && ui==88) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==288) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==78888) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==16777888) == true );
    // signed positive numbers
    REQUIRE( (reader.read_signed(si) && si==88) == true );
    REQUIRE( (reader.read_signed(si) && si==288) == true );
    REQUIRE( (reader.read_signed(si) && si==78888) == true );
    REQUIRE( (reader.read_signed(si) && si==16777888) == true );
    // signed negative numbers
    REQUIRE( (reader.read_signed(si) && si==-88) == true );
    REQUIRE( (reader.read_signed(si) && si==-288) == true );
    REQUIRE( (reader.read_signed(si) && si==-78888) == true );
    REQUIRE( (reader.read_signed(si) && si==-16777888) == true );
    // string
    REQUIRE( (reader.read_string(s) && s=="anything") == true );
    // BLOB
    REQUIRE( (reader.read_blob(b) && b=="BLOB") == true );
    // uncounted container
    REQUIRE( (reader.open_container()) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==499) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==49999) == true );
    REQUIRE( (reader.read_signed(si) && si==-1) == true );
    REQUIRE( (reader.read_signed(si) && si==0) == true );
    // - nested counted container
    REQUIRE( (reader.open_counted_container(n) && n==3) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==1) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==22) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==333) == true );
    REQUIRE( (reader.close_container()) == true );
    REQUIRE( (reader.close_container()) == true );
    // skipping a id
    REQUIRE( (reader.read_unsigned(ui) && ui==424242) == true );
    // end
    REQUIRE( (reader.eot()) == true );
    // rewind
    reader.rewind();
    REQUIRE( (reader.nextIs(tlv_string, "gugus")) == false ); // must not consume tag
    REQUIRE( (reader.nextIs(tlv_string, "tlvtest")) == true );
    // check eot
    REQUIRE( (reader.eot()) == false );
    // seek (over containers)
    REQUIRE( (reader.seekNext(tlv_any, "seektarget")==tlv_unsigned) == true );
    REQUIRE( (reader.read_unsigned(ui) && ui==424242) == true );
    // end
    REQUIRE( (reader.eot()) == true );
  }
}

