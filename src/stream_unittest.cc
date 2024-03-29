// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <numeric>

#include "gtest/gtest.h"

#include "puffin/file_stream.h"
#include "puffin/memory_stream.h"
#include "puffin/src/extent_stream.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/puffin_stream.h"
#include "puffin/src/unittest_common.h"

using std::string;
using std::vector;

namespace puffin {

class StreamTest : public ::testing::Test {
 public:
  // |data| is the content of stream as a buffer.
  void TestRead(StreamInterface* stream, const Buffer& data) {
    // Test read
    Buffer buf(data.size(), 0);

    ASSERT_TRUE(stream->Seek(0));
    ASSERT_TRUE(stream->Read(buf.data(), buf.size()));
    for (size_t idx = 0; idx < buf.size(); idx++) {
      ASSERT_EQ(buf[idx], data[idx]);
    }

    // No reading out of data boundary.
    Buffer tmp(100);
    uint64_t size;
    ASSERT_TRUE(stream->GetSize(&size));
    ASSERT_TRUE(stream->Seek(size));
    ASSERT_TRUE(stream->Read(tmp.data(), 0));
    ASSERT_FALSE(stream->Read(tmp.data(), 1));
    ASSERT_FALSE(stream->Read(tmp.data(), 2));
    ASSERT_FALSE(stream->Read(tmp.data(), 3));
    ASSERT_FALSE(stream->Read(tmp.data(), 100));

    ASSERT_TRUE(stream->Seek(size - 1));
    ASSERT_TRUE(stream->Read(tmp.data(), 0));
    ASSERT_TRUE(stream->Read(tmp.data(), 1));

    ASSERT_TRUE(stream->Seek(size - 1));
    ASSERT_FALSE(stream->Read(tmp.data(), 2));
    ASSERT_FALSE(stream->Read(tmp.data(), 3));
    ASSERT_FALSE(stream->Read(tmp.data(), 100));

    // Read the entire buffer one byte at a time.
    ASSERT_TRUE(stream->Seek(0));
    for (size_t idx = 0; idx < size; idx++) {
      uint8_t u;
      ASSERT_TRUE(stream->Read(&u, 1));
      ASSERT_EQ(u, buf[idx]);
    }

    // Read the entire buffer one byte at a time and set offset for each read.
    for (size_t idx = 0; idx < size; idx++) {
      uint8_t u;
      ASSERT_TRUE(stream->Seek(idx));
      ASSERT_TRUE(stream->Read(&u, 1));
      ASSERT_EQ(u, buf[idx]);
    }

    // Read random lengths from random offsets.
    tmp.resize(buf.size());
    srand(time(nullptr));
    uint32_t rand_seed;
    for (size_t idx = 0; idx < 10000; idx++) {
      // zero to full size available.
      size_t size = rand_r(&rand_seed) % (buf.size() + 1);
      uint64_t max_start = buf.size() - size;
      uint64_t start = rand_r(&rand_seed) % (max_start + 1);
      ASSERT_TRUE(stream->Seek(start));
      ASSERT_TRUE(stream->Read(tmp.data(), size));
      for (size_t idx = 0; idx < size; idx++) {
        ASSERT_EQ(tmp[idx], buf[start + idx]);
      }
    }
  }

  void TestWriteBoundary(StreamInterface* stream) {
    Buffer buf(10);
    // Writing out of boundary is fine.
    uint64_t size;
    ASSERT_TRUE(stream->GetSize(&size));
    ASSERT_TRUE(stream->Seek(size));
    ASSERT_TRUE(stream->Write(buf.data(), 0));
    ASSERT_TRUE(stream->Write(buf.data(), 1));
    ASSERT_TRUE(stream->Write(buf.data(), 2));
    ASSERT_TRUE(stream->Write(buf.data(), 3));
    ASSERT_TRUE(stream->Write(buf.data(), 10));

    ASSERT_TRUE(stream->GetSize(&size));
    ASSERT_TRUE(stream->Seek(size - 1));
    ASSERT_TRUE(stream->Write(buf.data(), 0));
    ASSERT_TRUE(stream->Write(buf.data(), 1));

    ASSERT_TRUE(stream->GetSize(&size));
    ASSERT_TRUE(stream->Seek(size - 1));
    ASSERT_TRUE(stream->Write(buf.data(), 2));
    ASSERT_TRUE(stream->Write(buf.data(), 3));
    ASSERT_TRUE(stream->Write(buf.data(), 10));
  }

  void TestWrite(StreamInterface* write_stream, StreamInterface* read_stream) {
    uint64_t size;
    ASSERT_TRUE(read_stream->GetSize(&size));
    Buffer buf1(size);
    Buffer buf2(size);
    std::iota(buf1.begin(), buf1.end(), 0);

    // Make sure the write works.
    ASSERT_TRUE(write_stream->Seek(0));
    ASSERT_TRUE(write_stream->Write(buf1.data(), buf1.size()));
    ASSERT_TRUE(read_stream->Seek(0));
    ASSERT_TRUE(read_stream->Read(buf2.data(), buf2.size()));
    ASSERT_EQ(buf1, buf2);

    std::fill(buf2.begin(), buf2.end(), 0);

    // Write entire buffer one byte at a time. (all zeros).
    ASSERT_TRUE(write_stream->Seek(0));
    for (size_t idx = 0; idx < buf2.size(); idx++) {
      ASSERT_TRUE(write_stream->Write(&buf2[idx], 1));
    }

    ASSERT_TRUE(read_stream->Seek(0));
    ASSERT_TRUE(read_stream->Read(buf1.data(), buf1.size()));
    ASSERT_EQ(buf1, buf2);
  }

  // Call this at the end before |TestClose|.
  void TestSeek(StreamInterface* stream, bool seek_end_is_fine) {
    uint64_t size, offset;
    ASSERT_TRUE(stream->GetSize(&size));
    ASSERT_TRUE(stream->Seek(size));
    ASSERT_TRUE(stream->GetOffset(&offset));
    ASSERT_EQ(offset, size);
    ASSERT_TRUE(stream->Seek(10));
    ASSERT_TRUE(stream->GetOffset(&offset));
    ASSERT_EQ(offset, 10);
    ASSERT_TRUE(stream->Seek(0));
    ASSERT_TRUE(stream->GetOffset(&offset));
    ASSERT_EQ(offset, 0);
    // Test end of stream offset.
    ASSERT_EQ(stream->Seek(size + 1), seek_end_is_fine);
  }

  void TestClose(StreamInterface* stream) { ASSERT_TRUE(stream->Close()); }
};

TEST_F(StreamTest, MemoryStreamTest) {
  Buffer buf(105);
  std::iota(buf.begin(), buf.end(), 0);

  auto read_stream = MemoryStream::CreateForRead(buf);
  TestRead(read_stream.get(), buf);
  TestSeek(read_stream.get(), false);

  auto write_stream = MemoryStream::CreateForWrite(&buf);
  TestWrite(write_stream.get(), read_stream.get());
  TestWriteBoundary(write_stream.get());
  TestSeek(write_stream.get(), false);

  TestClose(read_stream.get());
  TestClose(write_stream.get());
}

TEST_F(StreamTest, FileStreamTest) {
  string filepath;
  ASSERT_TRUE(MakeTempFile(&filepath, nullptr));
  ScopedPathUnlinker scoped_unlinker(filepath);
  ASSERT_FALSE(FileStream::Open(filepath, false, false));

  auto stream = FileStream::Open(filepath, true, true);
  ASSERT_TRUE(stream.get() != nullptr);
  // Doesn't matter if it is not initialized. I will be overridden.
  Buffer buf(105);
  std::iota(buf.begin(), buf.end(), 0);

  ASSERT_TRUE(stream->Write(buf.data(), buf.size()));

  TestRead(stream.get(), buf);
  TestWrite(stream.get(), stream.get());
  TestWriteBoundary(stream.get());
  TestSeek(stream.get(), true);
  TestClose(stream.get());
}

TEST_F(StreamTest, PuffinStreamTest) {
  auto puffer = std::make_shared<Puffer>();
  auto read_stream = PuffinStream::CreateForPuff(
      MemoryStream::CreateForRead(kDeflatesSample1), puffer,
      kPuffsSample1.size(), kSubblockDeflateExtentsSample1,
      kPuffExtentsSample1);
  TestRead(read_stream.get(), kPuffsSample1);
  TestSeek(read_stream.get(), false);
  TestClose(read_stream.get());

  // Test the stream with puff cache.
  read_stream = PuffinStream::CreateForPuff(
      MemoryStream::CreateForRead(kDeflatesSample1), puffer,
      kPuffsSample1.size(), kSubblockDeflateExtentsSample1, kPuffExtentsSample1,
      8 /* max_cache_size */);
  TestRead(read_stream.get(), kPuffsSample1);
  TestSeek(read_stream.get(), false);
  TestClose(read_stream.get());

  Buffer buf(kDeflatesSample1.size());
  auto huffer = std::make_shared<Huffer>();
  auto write_stream = PuffinStream::CreateForHuff(
      MemoryStream::CreateForWrite(&buf), huffer, kPuffsSample1.size(),
      kSubblockDeflateExtentsSample1, kPuffExtentsSample1);

  ASSERT_TRUE(write_stream->Seek(0));
  for (size_t idx = 0; idx < kPuffsSample1.size(); idx++) {
    ASSERT_TRUE(write_stream->Write(&kPuffsSample1[idx], 1));
  }
  // Make sure the write works
  ASSERT_EQ(buf, kDeflatesSample1);

  std::fill(buf.begin(), buf.end(), 0);
  ASSERT_TRUE(write_stream->Seek(0));
  ASSERT_TRUE(write_stream->Write(kPuffsSample1.data(), kPuffsSample1.size()));
  // Check its correctness.
  ASSERT_EQ(buf, kDeflatesSample1);

  // Write entire buffer one byte at a time. (all zeros).
  std::fill(buf.begin(), buf.end(), 0);
  ASSERT_TRUE(write_stream->Seek(0));
  for (const auto& byte : kPuffsSample1) {
    ASSERT_TRUE(write_stream->Write(&byte, 1));
  }
  // Check its correctness.
  ASSERT_EQ(buf, kDeflatesSample1);

  // No TestSeek is needed as PuffinStream is not supposed to seek to anywhere
  // except 0.
  TestClose(write_stream.get());
}

TEST_F(StreamTest, ExtentStreamTest) {
  Buffer buf(100);
  std::iota(buf.begin(), buf.end(), 0);

  vector<ByteExtent> extents = {{10, 10}, {25, 0}, {30, 10}};
  Buffer data = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                 30, 31, 32, 33, 34, 35, 36, 37, 38, 39};

  auto read_stream =
      ExtentStream::CreateForRead(MemoryStream::CreateForRead(buf), extents);
  TestSeek(read_stream.get(), false);
  TestRead(read_stream.get(), data);
  TestClose(read_stream.get());

  auto buf2 = buf;
  std::fill(data.begin(), data.end(), 3);
  for (const auto& extent : extents) {
    std::fill(buf.begin() + extent.offset,
              buf.begin() + (extent.offset + extent.length), 3);
  }
  auto write_stream = ExtentStream::CreateForWrite(
      MemoryStream::CreateForWrite(&buf2), extents);
  ASSERT_TRUE(write_stream->Seek(0));
  ASSERT_TRUE(write_stream->Write(data.data(), data.size()));
  EXPECT_EQ(buf2, buf);

  TestSeek(write_stream.get(), false);
  TestClose(write_stream.get());
}

}  // namespace puffin
