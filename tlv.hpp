//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__tlv__
#define __p44utils__tlv__

#include "p44utils_common.hpp"

using namespace std;

namespace p44 {

  enum {
    tlv_invalid = 0x03, // unsiged needing 32bit length -> does not exist
    // Tag IDs
    tlv_unsigned = 0x00,
    tlv_signed = 0x04,
    tlv_string = 0x08,
    tlv_blob = 0x0C,
    tlv_id_unsigned = 0x10,
    tlv_id_string = 0x14,
    tlv_container = 0x18,
    tlv_counted_container = 0x1C,
    // masks
    tlv_tagmask = 0xFC,
    tlv_sizemask = 0x03,
  };
  typedef uint8_t TLVTag;


  class TLVWriter {

    string mTLV;
    TLVWriter* mNestedWriter;
    TLVTag mStartedTag;
    size_t mTagCount;

  public:
    TLVWriter();
    virtual ~TLVWriter();

    void put_id_unsigned(uint32_t aId);
    void put_id_string(const string& aId);
    void put_id_string(const char* aId);

    void put_unsigned(uint64_t aUnsigned);
    void put_signed(int64_t aSigned);

    void put_string(const string& aString);
    void put_string(const char* aString);
    void put_blob(const string& aData);
    void put_blob(const void* aData, size_t aSize);

    void start_container();
    void start_counted_container();
    void end_container();

    string finalize();

  protected:

    void put_tag(TLVTag aTag, const string& aData);
    void start_tag(TLVTag aTag);
    bool tag_started() { return mStartedTag!=tlv_invalid; };
    void finish_tag(const string& aData);
    void finish_my_tag(const string& aData);
    void start_container(TLVTag aTag);


    string int_data(uint64_t aData, bool aSigned);
    string unsigned_data(uint64_t aData) { return int_data(aData, false); };
    string signed_data(int64_t aData) { return int_data((uint64_t)aData, true); };

    const string& data() { return mTLV; }
    size_t count() { return mTagCount; }

  };


  class TLVReader {

    const string& mTLV;
    size_t mPos;
    size_t mEndPos;
    TLVReader* mNestedReader;

  public:
    TLVReader(const string &aTLVString, size_t aPos = 0, size_t aEndPos = string::npos);
    virtual ~TLVReader();

    TLVTag nextTag();
    TLVTag nextTag(string &aId);
    TLVTag nextTag(uint32_t &aId);

    bool nextIs(TLVTag aTag, const string aId);
    bool nextIs(TLVTag aTag, uint32_t aId);

    bool skip();

    template<typename T> bool read_unsigned(T& aUnsigned)
    {
      if (nextTag()!=tlv_unsigned) return false;
      size_t start, size;
      if (!get_TL(start, size)) return false;
      aUnsigned = static_cast<T>(get_int_bytes(start, size, 0));
      return true;
    }

    template<typename T> bool read_signed(T& aSigned)
    {
      if (nextTag()!=tlv_signed) return false;
      size_t start, size;
      if (!get_TL(start, size)) return false;
      aSigned = static_cast<T>(get_int_bytes(start, size, sizeof(T)));
      return true;
    }

    bool read_string(string& aString);
    bool read_blob(string& aBlob);
    bool read_blob(void* aBuffer, size_t aBufSiz);

    bool open_container();
    bool open_counted_container(size_t& aCount);
    bool close_container();

    string dump(int aIndent = 0);

  protected:

    uint64_t get_int_bytes(size_t aStart, size_t aBytes, int aExtendSignTo);

    bool get_TL(size_t& aStart, size_t& aSize);
    bool get_TLV_string(string& aString);
    size_t pos() { return mPos; };
  };




}

#endif
