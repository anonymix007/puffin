// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "puffin/memory_stream.h"
#include "puffin/src/bit_reader.h"
#include "puffin/src/bit_writer.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/logging.h"
#include "puffin/src/puff_reader.h"
#include "puffin/src/puff_writer.h"
#include "puffin/src/puffin_stream.h"
#include "puffin/src/unittest_common.h"

using std::string;
using std::vector;

namespace puffin {

namespace {

// Uncompressed deflate block.
const Buffer kRawEmpty = {};
const Buffer kRaw1 = {0x01};
const Buffer kRaw2 = {0x01, 0x01};
const Buffer kRaw5 = {0x01, 0x02, 0x03, 0x04, 0x05};

}  // namespace

class PuffinTest : public ::testing::Test {
 public:
  // Utility for decompressing a puff stream.
  bool DecompressPuff(const uint8_t* puff_buf,
                      size_t* puff_size,
                      uint8_t* out_buf,
                      size_t* out_size) {
    BufferPuffReader puff_reader(static_cast<const uint8_t*>(puff_buf),
                                 *puff_size);
    auto start = static_cast<uint8_t*>(out_buf);

    PuffData pd;
    while (puff_reader.BytesLeft() != 0) {
      TEST_AND_RETURN_FALSE(puff_reader.GetNext(&pd));
      switch (pd.type) {
        case PuffData::Type::kLiteral:
          *start = pd.byte;
          start++;
          FALLTHROUGH_INTENDED;

        case PuffData::Type::kLiterals:
          pd.read_fn(start, pd.length);
          start += pd.length;
          break;

        case PuffData::Type::kLenDist: {
          while (pd.length-- > 0) {
            *start = *(start - pd.distance);
            start++;
          }
          break;
        }

        case PuffData::Type::kBlockMetadata:
          break;

        case PuffData::Type::kEndOfBlock:
          break;

        default:
          LOG(ERROR) << "Invalid block data type";
          break;
      }
    }
    *out_size = start - static_cast<uint8_t*>(out_buf);
    *puff_size = *puff_size - puff_reader.BytesLeft();
    return true;
  }

  bool PuffDeflate(const uint8_t* comp_buf,
                   size_t comp_size,
                   uint8_t* puff_buf,
                   size_t puff_size) const {
    BufferBitReader bit_reader(comp_buf, comp_size);
    BufferPuffWriter puff_writer(puff_buf, puff_size);

    TEST_AND_RETURN_FALSE(
        puffer_.PuffDeflate(&bit_reader, &puff_writer, nullptr));
    TEST_AND_RETURN_FALSE(comp_size == bit_reader.Offset());
    TEST_AND_RETURN_FALSE(puff_size == puff_writer.Size());
    return true;
  }

  bool HuffDeflate(const uint8_t* puff_buf,
                   size_t puff_size,
                   uint8_t* comp_buf,
                   size_t comp_size) const {
    BufferPuffReader puff_reader(puff_buf, puff_size);
    BufferBitWriter bit_writer(comp_buf, comp_size);

    TEST_AND_RETURN_FALSE(huffer_.HuffDeflate(&puff_reader, &bit_writer));
    TEST_AND_RETURN_FALSE(comp_size == bit_writer.Size());
    TEST_AND_RETURN_FALSE(puff_reader.BytesLeft() == 0);
    return true;
  }

  // Puffs |compressed| into |out_puff| and checks its equality with
  // |expected_puff|.
  void TestPuffDeflate(const Buffer& compressed,
                       const Buffer& expected_puff,
                       Buffer* out_puff) {
    out_puff->resize(expected_puff.size());
    auto comp_size = compressed.size();
    auto puff_size = out_puff->size();
    ASSERT_TRUE(
        PuffDeflate(compressed.data(), comp_size, out_puff->data(), puff_size));
    ASSERT_EQ(puff_size, expected_puff.size());
    out_puff->resize(puff_size);
    ASSERT_EQ(expected_puff, *out_puff);
  }

  // Should fail when trying to puff |compressed|.
  void FailPuffDeflate(const Buffer& compressed, Buffer* out_puff) {
    out_puff->resize(compressed.size() * 2 + 10);
    auto comp_size = compressed.size();
    auto puff_size = out_puff->size();
    ASSERT_FALSE(
        PuffDeflate(compressed.data(), comp_size, out_puff->data(), puff_size));
  }

  // Huffs |puffed| into |out_huff| and checks its equality with
  // |expected_huff|.|
  void TestHuffDeflate(const Buffer& puffed,
                       const Buffer& expected_huff,
                       Buffer* out_huff) {
    out_huff->resize(expected_huff.size());
    auto huff_size = out_huff->size();
    auto puffed_size = puffed.size();
    ASSERT_TRUE(
        HuffDeflate(puffed.data(), puffed_size, out_huff->data(), huff_size));
    ASSERT_EQ(expected_huff, *out_huff);
  }

  // Should fail while huffing |puffed|
  void FailHuffDeflate(const Buffer& puffed, Buffer* out_compress) {
    out_compress->resize(puffed.size());
    auto comp_size = out_compress->size();
    auto puff_size = puffed.size();
    ASSERT_TRUE(
        HuffDeflate(puffed.data(), puff_size, out_compress->data(), comp_size));
  }

  // Decompresses from |puffed| into |uncompress| and checks its equality with
  // |original|.
  void Decompress(const Buffer& puffed,
                  const Buffer& original,
                  Buffer* uncompress) {
    uncompress->resize(original.size());
    auto uncomp_size = uncompress->size();
    auto puffed_size = puffed.size();
    ASSERT_TRUE(DecompressPuff(puffed.data(), &puffed_size, uncompress->data(),
                               &uncomp_size));
    ASSERT_EQ(puffed_size, puffed.size());
    ASSERT_EQ(uncomp_size, original.size());
    uncompress->resize(uncomp_size);
    ASSERT_EQ(original, *uncompress);
  }

  void CheckSample(const Buffer original,
                   const Buffer compressed,
                   const Buffer puffed) {
    Buffer puff, uncompress, huff;
    TestPuffDeflate(compressed, puffed, &puff);
    TestHuffDeflate(puffed, compressed, &huff);
    Decompress(puffed, original, &uncompress);
  }

  void CheckBitExtentsPuffAndHuff(const Buffer& deflate_buffer,
                                  const vector<BitExtent>& deflate_extents,
                                  const Buffer& puff_buffer,
                                  const vector<ByteExtent>& puff_extents) {
    auto puffer = std::make_shared<Puffer>();
    auto deflate_stream = MemoryStream::CreateForRead(deflate_buffer);
    ASSERT_TRUE(deflate_stream->Seek(0));
    vector<ByteExtent> out_puff_extents;
    uint64_t puff_size;
    ASSERT_TRUE(FindPuffLocations(deflate_stream, deflate_extents,
                                  &out_puff_extents, &puff_size));
    EXPECT_EQ(puff_size, puff_buffer.size());
    EXPECT_EQ(out_puff_extents, puff_extents);

    auto src_puffin_stream =
        PuffinStream::CreateForPuff(std::move(deflate_stream), puffer,
                                    puff_size, deflate_extents, puff_extents);

    Buffer out_puff_buffer(puff_buffer.size());
    ASSERT_TRUE(src_puffin_stream->Read(out_puff_buffer.data(),
                                        out_puff_buffer.size()));
    EXPECT_EQ(out_puff_buffer, puff_buffer);

    auto huffer = std::make_shared<Huffer>();
    Buffer out_deflate_buffer;
    deflate_stream = MemoryStream::CreateForWrite(&out_deflate_buffer);

    src_puffin_stream =
        PuffinStream::CreateForHuff(std::move(deflate_stream), huffer,
                                    puff_size, deflate_extents, puff_extents);

    ASSERT_TRUE(
        src_puffin_stream->Write(puff_buffer.data(), puff_buffer.size()));
    EXPECT_EQ(out_deflate_buffer, deflate_buffer);
  }

 protected:
  Puffer puffer_;
  Huffer huffer_;
};

// Tests a simple buffer with uncompressed deflate block.
TEST_F(PuffinTest, UncompressedTest) {
  const Buffer kDeflate = {0x01, 0x05, 0x00, 0xFA, 0xFF,
                           0x01, 0x02, 0x03, 0x04, 0x05};
  const Buffer kPuff = {0x00, 0x00, 0x80, 0x04, 0x01, 0x02,
                        0x03, 0x04, 0x05, 0xFF, 0x81};
  CheckSample(kRaw5, kDeflate, kPuff);
}

// Tests a simple buffer with uncompressed deflate block with length zero.
TEST_F(PuffinTest, ZeroLengthUncompressedTest) {
  const Buffer kDeflate = {0x01, 0x00, 0x00, 0xFF, 0xFF};
  const Buffer kPuff = {0x00, 0x00, 0x80, 0xFF, 0x81};
  CheckSample(kRawEmpty, kDeflate, kPuff);
}

// Tests a Fixed Huffman table compressed buffer with only one literal.
TEST_F(PuffinTest, OneLiteralFixedHuffmanTableTest) {
  const Buffer kDeflate = {0x63, 0x04, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81};
  CheckSample(kRaw1, kDeflate, kPuff);
}

// Tests deflate of an empty buffer.
TEST_F(PuffinTest, EmptyTest) {
  const Buffer kDeflate = {0x03, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0xA0, 0xFF, 0x81};
  CheckSample(kRawEmpty, kDeflate, kPuff);
}

// Tests a simple buffer with compress deflate block using fixed Huffman table.
TEST_F(PuffinTest, FixedHuffmanTableCompressedTest) {
  const Buffer kDeflate = {0x63, 0x64, 0x62, 0x66, 0x61, 0x05, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0xA0, 0x04, 0x01, 0x02,
                        0x03, 0x04, 0x05, 0xFF, 0x81};
  CheckSample(kRaw5, kDeflate, kPuff);
}

// Tests that uncompressed deflate blocks are not ignored when the output
// deflate location pointer is null.
TEST_F(PuffinTest, NoIgnoreUncompressedBlocksTest) {
  const Buffer kDeflate = {0x01, 0x05, 0x00, 0xFA, 0xFF,
                           0x01, 0x02, 0x03, 0x04, 0x05};
  BufferBitReader bit_reader(kDeflate.data(), kDeflate.size());
  Buffer puff_buffer(11);  // Same size as |uncomp_puff| below.
  BufferPuffWriter puff_writer(puff_buffer.data(), puff_buffer.size());
  vector<BitExtent> deflates;
  EXPECT_TRUE(puffer_.PuffDeflate(&bit_reader, &puff_writer, nullptr));
  const Buffer kPuff = {0x00, 0x00, 0x80, 0x04, 0x01, 0x02,
                        0x03, 0x04, 0x05, 0xFF, 0x81};
  EXPECT_EQ(puff_writer.Size(), kPuff.size());
  EXPECT_EQ(puff_buffer, kPuff);
}

// Tests that uncompressed deflate blocks are ignored when the output
// deflate location pointer is valid.
TEST_F(PuffinTest, IgnoreUncompressedBlocksTest) {
  const Buffer kDeflate = {0x01, 0x05, 0x00, 0xFA, 0xFF,
                           0x01, 0x02, 0x03, 0x04, 0x05};
  BufferBitReader bit_reader(kDeflate.data(), kDeflate.size());
  BufferPuffWriter puff_writer(nullptr, 0);
  vector<BitExtent> deflates;
  EXPECT_TRUE(puffer_.PuffDeflate(&bit_reader, &puff_writer, &deflates));
  EXPECT_TRUE(deflates.empty());
}

namespace {
// It is actuall the content of the copyright header.
const Buffer kDynamicHTRaw = {
    0x0A, 0x2F, 0x2F, 0x0A, 0x2F, 0x2F, 0x20, 0x43, 0x6F, 0x70, 0x79, 0x72,
    0x69, 0x67, 0x68, 0x74, 0x20, 0x28, 0x43, 0x29, 0x20, 0x32, 0x30, 0x31,
    0x37, 0x20, 0x54, 0x68, 0x65, 0x20, 0x41, 0x6E, 0x64, 0x72, 0x6F, 0x69,
    0x64, 0x20, 0x4F, 0x70, 0x65, 0x6E, 0x20, 0x53, 0x6F, 0x75, 0x72, 0x63,
    0x65, 0x20, 0x50, 0x72, 0x6F, 0x6A, 0x65, 0x63, 0x74, 0x0A, 0x2F, 0x2F,
    0x0A, 0x2F, 0x2F, 0x20, 0x4C, 0x69, 0x63, 0x65, 0x6E, 0x73, 0x65, 0x64,
    0x20, 0x75, 0x6E, 0x64, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41,
    0x70, 0x61, 0x63, 0x68, 0x65, 0x20, 0x4C, 0x69, 0x63, 0x65, 0x6E, 0x73,
    0x65, 0x2C, 0x20, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x32,
    0x2E, 0x30, 0x20, 0x28, 0x74, 0x68, 0x65, 0x20, 0x22, 0x4C, 0x69, 0x63,
    0x65, 0x6E, 0x73, 0x65, 0x22, 0x29, 0x3B, 0x0A, 0x2F, 0x2F, 0x20, 0x79,
    0x6F, 0x75, 0x20, 0x6D, 0x61, 0x79, 0x20, 0x6E, 0x6F, 0x74, 0x20, 0x75,
    0x73, 0x65, 0x20, 0x74, 0x68, 0x69, 0x73, 0x20, 0x66, 0x69, 0x6C, 0x65,
    0x20, 0x65, 0x78, 0x63, 0x65, 0x70, 0x74, 0x20, 0x69, 0x6E, 0x20, 0x63,
    0x6F, 0x6D, 0x70, 0x6C, 0x69, 0x61, 0x6E, 0x63, 0x65, 0x20, 0x77, 0x69,
    0x74, 0x68, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4C, 0x69, 0x63, 0x65, 0x6E,
    0x73, 0x65, 0x2E, 0x0A, 0x2F, 0x2F, 0x20, 0x59, 0x6F, 0x75, 0x20, 0x6D,
    0x61, 0x79, 0x20, 0x6F, 0x62, 0x74, 0x61, 0x69, 0x6E, 0x20, 0x61, 0x20,
    0x63, 0x6F, 0x70, 0x79, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20,
    0x4C, 0x69, 0x63, 0x65, 0x6E, 0x73, 0x65, 0x20, 0x61, 0x74, 0x0A, 0x2F,
    0x2F, 0x0A, 0x2F, 0x2F, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x68, 0x74,
    0x74, 0x70, 0x3A, 0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x61, 0x70, 0x61,
    0x63, 0x68, 0x65, 0x2E, 0x6F, 0x72, 0x67, 0x2F, 0x6C, 0x69, 0x63, 0x65,
    0x6E, 0x73, 0x65, 0x73, 0x2F, 0x4C, 0x49, 0x43, 0x45, 0x4E, 0x53, 0x45,
    0x2D, 0x32, 0x2E, 0x30, 0x0A, 0x2F, 0x2F, 0x0A, 0x2F, 0x2F, 0x20, 0x55,
    0x6E, 0x6C, 0x65, 0x73, 0x73, 0x20, 0x72, 0x65, 0x71, 0x75, 0x69, 0x72,
    0x65, 0x64, 0x20, 0x62, 0x79, 0x20, 0x61, 0x70, 0x70, 0x6C, 0x69, 0x63,
    0x61, 0x62, 0x6C, 0x65, 0x20, 0x6C, 0x61, 0x77, 0x20, 0x6F, 0x72, 0x20,
    0x61, 0x67, 0x72, 0x65, 0x65, 0x64, 0x20, 0x74, 0x6F, 0x20, 0x69, 0x6E,
    0x20, 0x77, 0x72, 0x69, 0x74, 0x69, 0x6E, 0x67, 0x2C, 0x20, 0x73, 0x6F,
    0x66, 0x74, 0x77, 0x61, 0x72, 0x65, 0x0A, 0x2F, 0x2F, 0x20, 0x64, 0x69,
    0x73, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x64, 0x20, 0x75, 0x6E,
    0x64, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4C, 0x69, 0x63, 0x65,
    0x6E, 0x73, 0x65, 0x20, 0x69, 0x73, 0x20, 0x64, 0x69, 0x73, 0x74, 0x72,
    0x69, 0x62, 0x75, 0x74, 0x65, 0x64, 0x20, 0x6F, 0x6E, 0x20, 0x61, 0x6E,
    0x20, 0x22, 0x41, 0x53, 0x20, 0x49, 0x53, 0x22, 0x20, 0x42, 0x41, 0x53,
    0x49, 0x53, 0x2C, 0x0A, 0x2F, 0x2F, 0x20, 0x57, 0x49, 0x54, 0x48, 0x4F,
    0x55, 0x54, 0x20, 0x57, 0x41, 0x52, 0x52, 0x41, 0x4E, 0x54, 0x49, 0x45,
    0x53, 0x20, 0x4F, 0x52, 0x20, 0x43, 0x4F, 0x4E, 0x44, 0x49, 0x54, 0x49,
    0x4F, 0x4E, 0x53, 0x20, 0x4F, 0x46, 0x20, 0x41, 0x4E, 0x59, 0x20, 0x4B,
    0x49, 0x4E, 0x44, 0x2C, 0x20, 0x65, 0x69, 0x74, 0x68, 0x65, 0x72, 0x20,
    0x65, 0x78, 0x70, 0x72, 0x65, 0x73, 0x73, 0x20, 0x6F, 0x72, 0x20, 0x69,
    0x6D, 0x70, 0x6C, 0x69, 0x65, 0x64, 0x2E, 0x0A, 0x2F, 0x2F, 0x20, 0x53,
    0x65, 0x65, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4C, 0x69, 0x63, 0x65, 0x6E,
    0x73, 0x65, 0x20, 0x66, 0x6F, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x73,
    0x70, 0x65, 0x63, 0x69, 0x66, 0x69, 0x63, 0x20, 0x6C, 0x61, 0x6E, 0x67,
    0x75, 0x61, 0x67, 0x65, 0x20, 0x67, 0x6F, 0x76, 0x65, 0x72, 0x6E, 0x69,
    0x6E, 0x67, 0x20, 0x70, 0x65, 0x72, 0x6D, 0x69, 0x73, 0x73, 0x69, 0x6F,
    0x6E, 0x73, 0x20, 0x61, 0x6E, 0x64, 0x0A, 0x2F, 0x2F, 0x20, 0x6C, 0x69,
    0x6D, 0x69, 0x74, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x73, 0x20, 0x75, 0x6E,
    0x64, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4C, 0x69, 0x63, 0x65,
    0x6E, 0x73, 0x65, 0x2E, 0x0A};

// Dynamic huffman compressed deflate.
const Buffer kDynamicHTDeflate = {
    0x65, 0x91, 0x41, 0x6F, 0x9C, 0x30, 0x10, 0x85, 0xEF, 0xFB, 0x2B, 0x9E,
    0xF6, 0x94, 0x48, 0x5B, 0x48, 0x73, 0xA9, 0xD4, 0x9E, 0xE8, 0x66, 0xAB,
    0xA0, 0x46, 0x50, 0x2D, 0xA4, 0x51, 0x8E, 0x5E, 0x18, 0xD8, 0x89, 0x58,
    0xDB, 0xB5, 0x4D, 0xC9, 0xFE, 0xFB, 0x8E, 0x59, 0x22, 0x25, 0xAA, 0x2F,
    0xC8, 0xCC, 0xCC, 0x9B, 0xEF, 0x3D, 0xAF, 0xD2, 0x74, 0x95, 0xA6, 0xD8,
    0x1A, 0x7B, 0x76, 0xDC, 0x1F, 0x03, 0xAE, 0xB6, 0xD7, 0xB8, 0xBD, 0xF9,
    0xFC, 0x05, 0xF5, 0x91, 0x90, 0xE9, 0xD6, 0x19, 0x6E, 0x51, 0x5A, 0xD2,
    0xA8, 0xCC, 0xE8, 0x1A, 0xC2, 0x2F, 0x67, 0x5E, 0xA8, 0x09, 0xAB, 0xCB,
    0xE0, 0x03, 0x37, 0xA4, 0x3D, 0xB5, 0x18, 0x75, 0x4B, 0x0E, 0x21, 0x0E,
    0x59, 0xD5, 0xC8, 0x67, 0xA9, 0x6C, 0xF0, 0x9B, 0x9C, 0x67, 0xA3, 0x71,
    0x9B, 0xDC, 0xE0, 0x2A, 0x36, 0xAC, 0x97, 0xD2, 0xFA, 0xFA, 0x5B, 0x94,
    0x38, 0x9B, 0x11, 0x27, 0x75, 0x86, 0x36, 0x01, 0xA3, 0x27, 0xD1, 0x60,
    0x8F, 0x8E, 0x07, 0x02, 0xBD, 0x36, 0x64, 0x03, 0x58, 0xA3, 0x31, 0x27,
    0x3B, 0xB0, 0xD2, 0xB2, 0x7F, 0xE2, 0x70, 0x9C, 0xF7, 0x2C, 0x2A, 0x49,
    0xD4, 0x78, 0x5E, 0x34, 0xCC, 0x21, 0x28, 0x69, 0x57, 0x32, 0x60, 0xE5,
    0xD6, 0xBD, 0x6F, 0x84, 0x7A, 0x83, 0x9E, 0xCF, 0x31, 0x04, 0xFB, 0x35,
    0x4D, 0xA7, 0x69, 0x4A, 0xD4, 0x4C, 0x9C, 0x18, 0xD7, 0xA7, 0xC3, 0xA5,
    0xD7, 0xA7, 0x0F, 0xF9, 0x76, 0x57, 0x54, 0xBB, 0x4F, 0x42, 0xBD, 0x4C,
    0x3D, 0xEA, 0x81, 0xBC, 0x87, 0xA3, 0x3F, 0x23, 0x3B, 0x71, 0x7C, 0x38,
    0x43, 0x59, 0xA1, 0x6A, 0xD4, 0x41, 0x58, 0x07, 0x35, 0xC1, 0x38, 0xA8,
    0xDE, 0x91, 0xD4, 0x82, 0x89, 0xD4, 0x93, 0xE3, 0xC0, 0xBA, 0xDF, 0xC0,
    0x9B, 0x2E, 0x4C, 0xCA, 0x51, 0x94, 0x69, 0xD9, 0x07, 0xC7, 0x87, 0x31,
    0x7C, 0x08, 0xED, 0x8D, 0x51, 0xAC, 0xBF, 0x6F, 0x90, 0xD8, 0x94, 0xC6,
    0x3A, 0xAB, 0x90, 0x57, 0x6B, 0x7C, 0xCF, 0xAA, 0xBC, 0xDA, 0x44, 0x91,
    0xA7, 0xBC, 0xBE, 0x2F, 0x1F, 0x6B, 0x3C, 0x65, 0xFB, 0x7D, 0x56, 0xD4,
    0xF9, 0xAE, 0x42, 0xB9, 0xC7, 0xB6, 0x2C, 0xEE, 0xF2, 0x3A, 0x2F, 0x0B,
    0xB9, 0xFD, 0x40, 0x56, 0x3C, 0xE3, 0x67, 0x5E, 0xDC, 0x6D, 0x40, 0x12,
    0x99, 0xEC, 0xA1, 0x57, 0xEB, 0xA2, 0x03, 0xC1, 0xE4, 0x18, 0x27, 0xB5,
    0x73, 0x76, 0x15, 0xD1, 0x07, 0x84, 0xCE, 0x5C, 0x90, 0xBC, 0xA5, 0x86,
    0x3B, 0x6E, 0xC4, 0x9A, 0xEE, 0x47, 0xD5, 0x13, 0x7A, 0xF3, 0x97, 0x9C,
    0x16, 0x47, 0xB0, 0xE4, 0x4E, 0xEC, 0xE3, 0xB3, 0x7A, 0x01, 0x6C, 0xA3,
    0xCC, 0xC0, 0x27, 0x0E, 0x2A, 0xCC, 0xBF, 0xFE, 0xF3, 0x95, 0xAC, 0xFE,
    0x01};

const Buffer kDynamicHTPuff = {
    0x00, 0x74, 0xC0, 0x0C, 0x11, 0x0C, 0x04, 0x63, 0x34, 0x32, 0x03, 0x04,
    0x05, 0x06, 0x1B, 0x07, 0x26, 0x03, 0x00, 0x07, 0x16, 0x08, 0x08, 0x00,
    0x00, 0x07, 0x09, 0x06, 0x06, 0x08, 0x09, 0x08, 0x15, 0x09, 0x00, 0x00,
    0x09, 0x09, 0x16, 0x06, 0x09, 0x07, 0x08, 0x07, 0x09, 0x00, 0x08, 0x06,
    0x00, 0x09, 0x08, 0x00, 0x06, 0x06, 0x09, 0x00, 0x07, 0x06, 0x06, 0x08,
    0x09, 0x08, 0x00, 0x08, 0x18, 0x05, 0x07, 0x06, 0x06, 0x04, 0x06, 0x06,
    0x07, 0x04, 0x08, 0x00, 0x06, 0x07, 0x05, 0x05, 0x05, 0x09, 0x05, 0x05,
    0x05, 0x06, 0x09, 0x06, 0x08, 0x07, 0x97, 0x09, 0x04, 0x05, 0x06, 0x07,
    0x06, 0x08, 0x00, 0x00, 0x08, 0x08, 0x00, 0x09, 0x05, 0x15, 0x06, 0x00,
    0x05, 0x06, 0x04, 0x04, 0x04, 0x03, 0x04, 0x02, 0x03, 0x03, 0x05, 0x39,
    0x0A, 0x2F, 0x2F, 0x0A, 0x2F, 0x2F, 0x20, 0x43, 0x6F, 0x70, 0x79, 0x72,
    0x69, 0x67, 0x68, 0x74, 0x20, 0x28, 0x43, 0x29, 0x20, 0x32, 0x30, 0x31,
    0x37, 0x20, 0x54, 0x68, 0x65, 0x20, 0x41, 0x6E, 0x64, 0x72, 0x6F, 0x69,
    0x64, 0x20, 0x4F, 0x70, 0x65, 0x6E, 0x20, 0x53, 0x6F, 0x75, 0x72, 0x63,
    0x65, 0x20, 0x50, 0x72, 0x6F, 0x6A, 0x65, 0x63, 0x74, 0x0A, 0x83, 0x00,
    0x38, 0x0F, 0x4C, 0x69, 0x63, 0x65, 0x6E, 0x73, 0x65, 0x64, 0x20, 0x75,
    0x6E, 0x64, 0x65, 0x72, 0x20, 0x74, 0x81, 0x00, 0x34, 0x02, 0x70, 0x61,
    0x63, 0x80, 0x00, 0x06, 0x84, 0x00, 0x19, 0x0E, 0x2C, 0x20, 0x56, 0x65,
    0x72, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x32, 0x2E, 0x30, 0x20, 0x28, 0x81,
    0x00, 0x20, 0x00, 0x22, 0x84, 0x00, 0x1A, 0x02, 0x22, 0x29, 0x3B, 0x81,
    0x00, 0x42, 0x0E, 0x79, 0x6F, 0x75, 0x20, 0x6D, 0x61, 0x79, 0x20, 0x6E,
    0x6F, 0x74, 0x20, 0x75, 0x73, 0x65, 0x80, 0x00, 0x43, 0x19, 0x69, 0x73,
    0x20, 0x66, 0x69, 0x6C, 0x65, 0x20, 0x65, 0x78, 0x63, 0x65, 0x70, 0x74,
    0x20, 0x69, 0x6E, 0x20, 0x63, 0x6F, 0x6D, 0x70, 0x6C, 0x69, 0x61, 0x6E,
    0x80, 0x00, 0x7F, 0x03, 0x77, 0x69, 0x74, 0x68, 0x82, 0x00, 0x67, 0x84,
    0x00, 0x45, 0x00, 0x2E, 0x81, 0x00, 0x43, 0x00, 0x59, 0x84, 0x00, 0x43,
    0x03, 0x6F, 0x62, 0x74, 0x61, 0x80, 0x00, 0x2E, 0x00, 0x61, 0x80, 0x00,
    0x30, 0x00, 0x70, 0x80, 0x00, 0x0D, 0x00, 0x66, 0x89, 0x00, 0x28, 0x01,
    0x20, 0x61, 0x85, 0x00, 0xB4, 0x82, 0x00, 0x00, 0x0B, 0x68, 0x74, 0x74,
    0x70, 0x3A, 0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x61, 0x82, 0x00, 0xB1,
    0x05, 0x2E, 0x6F, 0x72, 0x67, 0x2F, 0x6C, 0x83, 0x00, 0x2B, 0x09, 0x73,
    0x2F, 0x4C, 0x49, 0x43, 0x45, 0x4E, 0x53, 0x45, 0x2D, 0x80, 0x00, 0xB5,
    0x84, 0x00, 0x35, 0x0C, 0x55, 0x6E, 0x6C, 0x65, 0x73, 0x73, 0x20, 0x72,
    0x65, 0x71, 0x75, 0x69, 0x72, 0x80, 0x00, 0xF1, 0x04, 0x62, 0x79, 0x20,
    0x61, 0x70, 0x80, 0x00, 0x95, 0x02, 0x63, 0x61, 0x62, 0x80, 0x00, 0xAB,
    0x0A, 0x6C, 0x61, 0x77, 0x20, 0x6F, 0x72, 0x20, 0x61, 0x67, 0x72, 0x65,
    0x80, 0x00, 0x1B, 0x01, 0x74, 0x6F, 0x81, 0x00, 0xB5, 0x10, 0x77, 0x72,
    0x69, 0x74, 0x69, 0x6E, 0x67, 0x2C, 0x20, 0x73, 0x6F, 0x66, 0x74, 0x77,
    0x61, 0x72, 0x65, 0x81, 0x00, 0x46, 0x08, 0x64, 0x69, 0x73, 0x74, 0x72,
    0x69, 0x62, 0x75, 0x74, 0x8A, 0x01, 0x34, 0x85, 0x00, 0xA3, 0x80, 0x00,
    0xFA, 0x89, 0x00, 0x20, 0x80, 0x01, 0x36, 0x10, 0x61, 0x6E, 0x20, 0x22,
    0x41, 0x53, 0x20, 0x49, 0x53, 0x22, 0x20, 0x42, 0x41, 0x53, 0x49, 0x53,
    0x2C, 0x81, 0x00, 0x44, 0x1E, 0x57, 0x49, 0x54, 0x48, 0x4F, 0x55, 0x54,
    0x20, 0x57, 0x41, 0x52, 0x52, 0x41, 0x4E, 0x54, 0x49, 0x45, 0x53, 0x20,
    0x4F, 0x52, 0x20, 0x43, 0x4F, 0x4E, 0x44, 0x49, 0x54, 0x49, 0x4F, 0x4E,
    0x80, 0x00, 0x0D, 0x0C, 0x46, 0x20, 0x41, 0x4E, 0x59, 0x20, 0x4B, 0x49,
    0x4E, 0x44, 0x2C, 0x20, 0x65, 0x80, 0x01, 0x32, 0x80, 0x00, 0x67, 0x03,
    0x65, 0x78, 0x70, 0x72, 0x81, 0x00, 0xC1, 0x80, 0x00, 0xA6, 0x00, 0x69,
    0x81, 0x01, 0x4E, 0x01, 0x65, 0x64, 0x82, 0x01, 0x3B, 0x02, 0x53, 0x65,
    0x65, 0x8A, 0x00, 0x82, 0x01, 0x66, 0x6F, 0x83, 0x00, 0x92, 0x07, 0x73,
    0x70, 0x65, 0x63, 0x69, 0x66, 0x69, 0x63, 0x80, 0x00, 0xDA, 0x0C, 0x6E,
    0x67, 0x75, 0x61, 0x67, 0x65, 0x20, 0x67, 0x6F, 0x76, 0x65, 0x72, 0x6E,
    0x80, 0x00, 0xD1, 0x06, 0x20, 0x70, 0x65, 0x72, 0x6D, 0x69, 0x73, 0x81,
    0x01, 0xD6, 0x00, 0x73, 0x80, 0x00, 0xA0, 0x00, 0x64, 0x81, 0x00, 0x46,
    0x06, 0x6C, 0x69, 0x6D, 0x69, 0x74, 0x61, 0x74, 0x82, 0x00, 0x12, 0x8E,
    0x00, 0xD7, 0x01, 0x2E, 0x0A, 0xFF, 0x81};
}  // namespace

// Tests a compressed deflate block using dynamic Huffman table.
TEST_F(PuffinTest, DynamicHuffmanTableTest) {
  CheckSample(kDynamicHTRaw, kDynamicHTDeflate, kDynamicHTPuff);
}

// Tests an uncompressed deflate block with invalid LEN/NLEN.
TEST_F(PuffinTest, PuffInvalidUncompressedLengthDeflateTest) {
  const Buffer kDeflate = {0x01, 0x05, 0x00, 0xFF, 0xFF,
                           0x01, 0x02, 0x03, 0x04, 0x05};
  Buffer puffed;
  FailPuffDeflate(kDeflate, &puffed);
}

// Tests puffing a block with invalid block header.
TEST_F(PuffinTest, PuffInvalidBlockHeaderDeflateTest) {
  const Buffer kDeflate = {0x07};
  Buffer puffed;
  FailPuffDeflate(kDeflate, &puffed);
}

// Tests puffing a block with final block bit unset so it returns false.
TEST_F(PuffinTest, PuffDeflateNoFinalBlockBitTest) {
  const Buffer kDeflate = {0x62, 0x04, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81};
  CheckSample(kRaw1, kDeflate, kPuff);
}

// Tests two deflate buffers concatenated, neither have their final bit set.  It
// is a valid deflate and puff buffer.
TEST_F(PuffinTest, MultipleDeflateBufferNoFinabBitsTest) {
  const Buffer kDeflate = {0x62, 0x04, 0x88, 0x11, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,
                        0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81};
  CheckSample(kRaw2, kDeflate, kPuff);
}

// Tests two deflate buffers concatenated, the first one has final bit set,
// second one not. It is a valid deflate and puff buffer.
TEST_F(PuffinTest, MultipleDeflateBufferOneFinalBitTest) {
  const Buffer kDeflate = {0x63, 0x04, 0x88, 0x11, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81,
                        0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81};
  CheckSample(kRaw2, kDeflate, kPuff);
}

// Tests two deflate buffers concatenated, both have final bits set. It is a
// valid deflate and puff buffer.
TEST_F(PuffinTest, MultipleDeflateBufferBothFinalBitTest) {
  const Buffer kDeflate = {0x63, 0x04, 0x8C, 0x11, 0x00};
  const Buffer kPuff = {0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81,
                        0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81};
  CheckSample(kRaw2, kDeflate, kPuff);
}

// When locating deflates, the puffer has to end when it hit a final block. Test
// that with two deflate buffers concatenated and both have final bits set.
TEST_F(PuffinTest, EndOnFinalBitTest) {
  const Buffer kDeflate = {0x63, 0x04, 0x8C, 0x11, 0x00};
  BufferBitReader bit_reader(kDeflate.data(), kDeflate.size());
  BufferPuffWriter puff_writer(nullptr, 0);
  vector<BitExtent> deflates;
  EXPECT_TRUE(puffer_.PuffDeflate(&bit_reader, &puff_writer, &deflates));
  const vector<BitExtent> kExpectedDeflates = {{0, 18}};
  EXPECT_EQ(deflates, kExpectedDeflates);
  EXPECT_EQ(bit_reader.Offset(), 3);
}

// TODO(ahassani): Add unittests for Failhuff too.

namespace {
// The following is a sequence of bits starting from the top right and ends in
// bottom left. It represents the bits in |kGapDeflates|. Bits inside the
// brackets (including bits exactly under brackets) represent a deflate stream.
//
//       }   {                  } {                  }{                  }
// 11000101 10000000 10001100 01010000 00010001 10001000 00000100 01100010
//   0xC5     0x80     0x8C     0x50     0x11     0x88     0x04     0x62
//
//      }         {                  } {                  }   {
// 10001011 11111100 00000100 01100010 00000001 00011000 10111000 00001000
//   0x8B     0xFC     0x04     0x62     0x01     0x18     0xB8     0x08
//
//      }   {                  }         {                  }{
// 10001011 00000001 00011000 10111111 11000000 01000110 00100000 00010001
//   0x8B     0x01     0x18     0xBF     0xC0     0x46     0x20     0x11
//
//       {                  }          {                  }  {
// 11111100 00000100 01100010 11111111 00000001 00011000 10110000 00010001
//   0xFC     0x04     0x62     0xFF     0x01     0x18     0xB0     0x11
//
const Buffer kGapDeflates = {0x62, 0x04, 0x88, 0x11, 0x50, 0x8C, 0x80, 0xC5,
                             0x08, 0xB8, 0x18, 0x01, 0x62, 0x04, 0xFC, 0x8B,
                             0x11, 0x20, 0x46, 0xC0, 0xBF, 0x18, 0x01, 0x8B,
                             0x11, 0xB0, 0x18, 0x01, 0xFF, 0x62, 0x04, 0xFC};

const Buffer kGapPuffs = {0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  0
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  7
                          0x01,                                      // raw   14
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  15
                          0x01, 0x01,                                // raw   22
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  24
                          0x07,                                      // raw   31
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  32
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  39
                          0x3F, 0x03,                                // raw   46
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  48
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  55
                          0x03, 0x3F,                                // raw   62
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  64
                          0x03,                                      // raw   71
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  72
                          0x03,                                      // raw   79
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  80
                          0xFF,                                      // raw   87
                          0x00, 0x00, 0x20, 0x00, 0x01, 0xFF, 0x81,  // puff  88
                          0x3F};                                     // raw   95

// The fifth deflate (and its puff in kGapPuffExtents) is for zero length
// deflate corner case.
const vector<BitExtent> kGapSubblockDeflateExtents = {
    {0, 18},   {18, 18},  {37, 18},  {57, 18},  {75, 0},   {78, 18}, {96, 18},
    {122, 18}, {140, 18}, {166, 18}, {186, 18}, {206, 18}, {232, 18}};

const vector<ByteExtent> kGapPuffExtents = {
    {0, 7},  {7, 7},  {15, 7}, {24, 7}, {31, 0}, {32, 7}, {39, 7},
    {48, 7}, {55, 7}, {64, 7}, {72, 7}, {80, 7}, {88, 7}};
}  // namespace

TEST_F(PuffinTest, BitExtentPuffAndHuffTest) {
  CheckBitExtentsPuffAndHuff(kGapDeflates, kGapSubblockDeflateExtents,
                             kGapPuffs, kGapPuffExtents);
}

TEST_F(PuffinTest, ExcludeBadDistanceCaches) {
  BufferBitReader br(kProblematicCache.data(), kProblematicCache.size());
  BufferPuffWriter pw(nullptr, 0);

  // The first two bits of this data should be ignored.
  br.CacheBits(2);
  br.DropBits(2);

  vector<BitExtent> deflates, empty;
  Puffer puffer(true);
  EXPECT_TRUE(puffer.PuffDeflate(&br, &pw, &deflates));
  EXPECT_EQ(deflates, empty);
}

TEST_F(PuffinTest, NoExcludeBadDistanceCaches) {
  BufferBitReader br(kProblematicCache.data(), kProblematicCache.size());
  BufferPuffWriter pw(nullptr, 0);

  // The first two bits of this data should be ignored.
  br.CacheBits(2);
  br.DropBits(2);

  vector<BitExtent> deflates;
  Puffer puffer;  // The default value for excluding bad distance cache should
                  // be false.
  EXPECT_TRUE(puffer.PuffDeflate(&br, &pw, &deflates));
  EXPECT_EQ(deflates, kProblematicCacheDeflateExtents);
}

}  // namespace puffin
