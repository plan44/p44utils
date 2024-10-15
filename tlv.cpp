//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "tlv.hpp"

using namespace p44;

// TODO: implementation is not optimized
//   in particular the forwarding chain to nested readers/writers is not
//   efficient for highly nested documents (could be solved by maintaining
//   a pointer to the current sub-reader in the root.

// MARK: - TLVWriter

TLVWriter::TLVWriter() :
  mNestedWriter(nullptr),
  mStartedTag(tlv_invalid),
  mTagCount(0)
{
}


TLVWriter::~TLVWriter()
{
  if (mNestedWriter) delete mNestedWriter;
  mNestedWriter = nullptr;
}



string TLVWriter::int_data(uint64_t aData, bool aSigned)
{
  string data;
  int shift = 56;
  bool started = false;
  while (shift>0) {
    uint64_t mask = aSigned ? 0x1FFll<<(shift-1) : 0xFFll<<shift;
    if (started || (aData & mask)!=0 || (aSigned && (aData & mask)!=mask)) {
      data += (uint8_t)((aData>>shift) & 0xFF);
      started = true;
    }
    shift-=8;
  }
  // always put last byte
  data += (uint8_t)(aData>>shift);
  return data;
}


void TLVWriter::start_tag(TLVTag aTag)
{
  if (mNestedWriter) {
    mNestedWriter->start_tag(aTag);
    return;
  }
  mStartedTag = aTag;
}


void TLVWriter::finish_tag(const string& aData)
{
  assert(mStartedTag!=tlv_invalid);
  if (mNestedWriter) {
    mNestedWriter->finish_tag(aData);
    return;
  }
  finish_my_tag(aData);
}


void TLVWriter::finish_my_tag(const string& aData)
{
  string len = int_data(aData.size(), false);
  assert(len.size()<=4);
  TLVTag tag = (mStartedTag & tlv_tagmask) | ((len.size()-1) & tlv_sizemask);
  mTLV += tag;
  mTLV += len;
  mTLV += aData;
  mStartedTag = tlv_invalid;
  mTagCount++;
}


void TLVWriter::put_tag(TLVTag aTag, const string& aData)
{
  start_tag(aTag);
  finish_tag(aData);
}


void TLVWriter::put_id_unsigned(uint32_t aId)
{
  put_tag(tlv_id_unsigned, unsigned_data(aId));
}


void TLVWriter::put_id_string(const string& aId)
{
  put_tag(tlv_id_string, aId);
}

void TLVWriter::put_id_string(const char* aId)
{
  string s = aId;
  put_id_string(s);
}


void TLVWriter::put_unsigned(uint64_t aUnsigned)
{
  put_tag(tlv_unsigned, unsigned_data(aUnsigned));
}


void TLVWriter::put_signed(int64_t aSigned)
{
  put_tag(tlv_signed, signed_data(aSigned));
}


void TLVWriter::put_string(const string& aString)
{
  put_tag(tlv_string, aString);
}


void TLVWriter::put_string(const char* aString)
{
  string s = aString;
  put_tag(tlv_string, s);
}


void TLVWriter::put_blob(const string& aData)
{
  put_tag(tlv_blob, aData);
}


void TLVWriter::put_blob(const void* aData, size_t aSize)
{
  string s;
  s.assign((const char *)aData, aSize);
  put_tag(tlv_blob, s);
}


void TLVWriter::start_container(TLVTag aTag)
{
  if (mNestedWriter) {
    mNestedWriter->start_container(aTag);
    return;
  }
  start_tag(aTag);
  mNestedWriter = new TLVWriter;
}


void TLVWriter::end_container()
{
  if (mNestedWriter && mNestedWriter->tag_started()) {
    mNestedWriter->end_container();
    return;
  }
  assert(mNestedWriter);
  string data;
  if (mStartedTag==tlv_counted_container) {
    // prepend count
    TLVWriter wr;
    wr.put_unsigned(mNestedWriter->count());
    data = wr.data();
    data += mNestedWriter->data();
  }
  else {
    data = mNestedWriter->data();
  }
  delete mNestedWriter;
  mNestedWriter = nullptr;
  finish_my_tag(data);
}


void TLVWriter::start_container()
{
  start_container(tlv_container);
}


void TLVWriter::start_counted_container()
{
  start_container(tlv_counted_container);
}


TLVWriter* TLVWriter::current()
{
  if (mNestedWriter) return mNestedWriter->current();
  return this;
}


string TLVWriter::finalize()
{
  if (mNestedWriter) {
    mNestedWriter->finalize();
    delete mNestedWriter;
    mNestedWriter = nullptr;
  }
  return mTLV;
}



// MARK: - TLVReader

TLVReader::TLVReader(const string &aTLVString, size_t aPos, size_t aEndPos) :
  mTLV(aTLVString),
  mPos(aPos),
  mStartPos(aPos),
  mEndPos(aEndPos),
  mNestedReader(nullptr)
{
  if (mEndPos==string::npos) mEndPos = mTLV.size();
}


TLVReader::~TLVReader()
{
  reset();
}


TLVTag TLVReader::nextTag()
{
  if (mNestedReader) return mNestedReader->nextTag();
  if (mPos>=mEndPos) return tlv_invalid;
  return mTLV[mPos] & tlv_tagmask;
}


bool TLVReader::eot()
{
  return nextTag()==tlv_invalid;
}


void TLVReader::reset()
{
  if (mNestedReader) mNestedReader->reset();
  delete mNestedReader;
  mNestedReader = nullptr;
  rewind();
}


void TLVReader::rewind()
{
  if (mNestedReader) mNestedReader->rewind();
  mPos = mStartPos;
}


uint64_t TLVReader::get_int_bytes(size_t aStart, size_t aBytes, int aExtendSignTo)
{
  uint64_t d = 0;
  aExtendSignTo -= aBytes;
  if (aExtendSignTo>0 && (mTLV[aStart] & 0x80)!=0) {
    while (aExtendSignTo>0) {
      d = (d << 8) | 0xFF;
      aExtendSignTo--;
    }
  }
  while(aBytes>0) {
    if (aStart>=mEndPos) return 0; // safeguard
    d = d << 8;
    d |= (mTLV[aStart++] & 0xFF);
    aBytes--;
    aExtendSignTo--;
  }
  return d;
}


bool TLVReader::get_TL(size_t& aStart, size_t& aSize)
{
  if (mNestedReader) return mNestedReader->get_TL(aStart, aSize);
  TLVTag tag = mTLV[mPos++];
  size_t szSz = (tag & tlv_sizemask)+1;
  aSize = (size_t)get_int_bytes(mPos, szSz, 0);
  aStart = mPos+szSz;
  if (aStart+aSize>mEndPos) {
    aSize = 0; // safeguard
    return false;
  }
  mPos = aStart+aSize;
  return true;
}


bool TLVReader::get_TLV_string(string& aString)
{
  size_t start, size;
  if (!get_TL(start, size)) return false;
  aString = mTLV.substr(start, size);
  return true;
}


bool TLVReader::skip()
{
  size_t start, size;
  return get_TL(start, size);
}


TLVTag TLVReader::nextTag(string &aId)
{
  TLVTag tag = nextTag();
  if (tag==tlv_id_string) {
    get_TLV_string(aId);
    tag = nextTag();
  }
  return tag;
}


TLVTag TLVReader::nextTag(uint32_t &aId)
{
  TLVTag tag = nextTag();
  if (tag==tlv_id_unsigned) {
    size_t start, size;
    if (!get_TL(start, size)) return tlv_invalid;
    aId = (uint32_t)get_int_bytes(start, size, 0);
    tag = nextTag();
  }
  return tag;
}


TLVTag TLVReader::nextDataTag()
{
  while(true) {
    TLVTag tag = nextTag();
    if (tag!=tlv_id_string && tag!=tlv_id_unsigned) return tag;
    skip();
  }
}


bool TLVReader::nextIs(TLVTag aTag, const string aId)
{
  size_t oldpos = current()->pos();
  string id;
  TLVTag tag = nextTag(id);
  if ((aTag==tlv_any || tag==aTag) && id==aId) return true;
  current()->setPos(oldpos);
  return false;
}


bool TLVReader::nextIs(TLVTag aTag, uint32_t aId)
{
  size_t oldpos = current()->pos();
  uint32_t id;
  TLVTag tag = nextTag(id);
  if ((aTag==tlv_any || tag==aTag) && id==aId) return true;
  current()->setPos(oldpos);
  return false;
}


bool TLVReader::read_string(string& aString)
{
  if (nextDataTag()!=tlv_string) return false;
  return get_TLV_string(aString);
}


bool TLVReader::read_blob(string& aBlob)
{
  if (nextDataTag()!=tlv_blob) return false;
  return get_TLV_string(aBlob);
  return true;
}


bool TLVReader::read_blob(void* aBuffer, size_t aBufSiz)
{
  if (nextDataTag()!=tlv_blob) return false;
  size_t start, size;
  if (!get_TL(start, size)) return false;
  memcpy(aBuffer, mTLV.c_str()+mPos, size>aBufSiz ? aBufSiz : size);
  return size<=aBufSiz;
}


bool TLVReader::open_container()
{
  if (mNestedReader) return mNestedReader->open_container();
  if (nextDataTag()!=tlv_container) return false;
  size_t start, size;
  get_TL(start, size);
  mNestedReader = new TLVReader(mTLV, start, start+size);
  return true;
}


bool TLVReader::open_counted_container(size_t& aCount)
{
  if (mNestedReader) return mNestedReader->open_counted_container(aCount);
  if (nextDataTag()!=tlv_counted_container) return false;
  size_t start, size;
  get_TL(start, size);
  mNestedReader = new TLVReader(mTLV, start, start+size);
  if (!mNestedReader->read_unsigned(aCount)) {
    delete mNestedReader;
    mNestedReader = nullptr;
    return false;
  }
  return true;
}


bool TLVReader::close_container()
{
  if (!mNestedReader) return false; // I am the leaf
  if (mNestedReader->close_container()) return true; // nested was not the leaf
  // nested is the leaf or in error, close it
  //size_t nestedPos = mNestedReader->pos();
  delete mNestedReader;
  mNestedReader = nullptr;
  // nestedPos==mPos; // content fully read?
  return true; // closed
}


TLVReader* TLVReader::current()
{
  if (mNestedReader) return mNestedReader->current();
  return this;
}


string TLVReader::dump(int aIndent)
{
  string out;
  TLVTag tag = nextTag();
  while(tag!=tlv_invalid) {
    string name;
    string value;
    if (tag==tlv_id_unsigned) {
      uint32_t id;
      tag = nextTag(id);
      name = string_format("0x%x:", id);
    }
    else if (tag==tlv_id_string) {
      tag = nextTag(name);
      name += ":";
    }
    if (tag==tlv_invalid) break;
    switch (tag) {
      case tlv_unsigned: {
        name += "unsigned";
        uint64_t u;
        if (read_unsigned(u)) {
          value = string_format("%llu", u);
        }
        break;
      }
      case tlv_signed: {
        name += "signed";
        int64_t s;
        if (read_signed(s)) {
          value = string_format("%lld", s);
        }
        break;
      }
      case tlv_string: {
        name += "string";
        string s;
        read_string(s);
        value = cstringQuote(s);
        break;
      }
      case tlv_blob: {
        name += "blob";
        string b;
        if (read_blob(b)) {
          value = binaryToHexString(b,':');
        }
        break;
      }
      case tlv_container: {
        name += "container";
        if (open_container()) {
          value = "{\n" + mNestedReader->dump(aIndent+2);
          value.append(aIndent, ' ');
          value += "}";
          close_container();
        }
        else {
          string_format_append(out, "ERROR at offset 0x%zx: invalid %s\n", mPos, name.c_str());
          return out;
        }
        break;
      }
      case tlv_counted_container: {
        name += "container";
        size_t n;
        if (open_counted_container(n)) {
          name += string_format("[%zu]", n);
          value = "[\n" + mNestedReader->dump(aIndent+2);
          value.append(aIndent, ' ');
          value += "]";
          close_container();
        }
        else {
          string_format_append(out, "ERROR at offset 0x%zx: invalid counted %s\n", mPos, name.c_str());
          return out;
        }
        break;
      }
      default: {
        string_format_append(out, "ERROR at offset 0x%zx: invalid tag 0x%02X\n", mPos, tag);
        return out;
      }
    }
    out.append(aIndent, ' ');
    string_format_append(out, "%s = %s\n", name.c_str(), value.c_str());
    tag = nextTag();
  }
  return out;
}
