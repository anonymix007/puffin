// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/unittest_common.h"

#include <unistd.h>

using std::string;
using std::vector;

namespace puffin {

bool MakeTempFile(string* filename, int* fd) {
#ifdef __ANDROID__
  char tmp_template[] = "/data/local/tmp/puffin-XXXXXX";
#else
  char tmp_template[] = "/tmp/puffin-XXXXXX";
#endif  // __ANDROID__
  int mkstemp_fd = mkstemp(tmp_template);
  TEST_AND_RETURN_FALSE(mkstemp_fd >= 0);
  if (filename) {
    *filename = tmp_template;
  }
  if (fd) {
    *fd = mkstemp_fd;
  } else {
    close(mkstemp_fd);
  }
  return true;
}

// clang-format off
const Buffer kDeflatesSample1 = {
    /* raw   0 */ 0x11, 0x22,
    /* def   2 */ 0x63, 0x64, 0x62, 0x66, 0x61, 0x05, 0x00,
    /* raw   9 */ 0x33,
    /* def  10 */ 0x03, 0x00,
    /* raw  12 */
    /* def  12 */ 0x63, 0x04, 0x00,
    /* raw  15 */ 0x44, 0x55
};
const Buffer kPuffsSample1 = {
    /* raw   0 */ 0x11, 0x22,
    /* puff  2 */ 0x00, 0x00, 0xA0, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF,
                  0x81,
    /* raw  13 */ 0x00, 0x33,
    /* puff 15 */ 0x00, 0x00, 0xA0, 0xFF, 0x81,
    /* raw  20 */ 0x00,
    /* puff 21 */ 0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81,
    /* raw  28 */ 0x00, 0x44, 0x55
};
const vector<ByteExtent> kDeflateExtentsSample1 = {
  {2, 7}, {10, 2}, {12, 3}};
const vector<BitExtent> kSubblockDeflateExtentsSample1 = {
  {16, 50}, {80, 10}, {96, 18}};
const vector<ByteExtent> kPuffExtentsSample1 = {{2, 11}, {15, 5}, {21, 7}};

const Buffer kDeflatesSample2 = {
    /* def  0  */ 0x63, 0x64, 0x62, 0x66, 0x61, 0x05, 0x00,
    /* raw  7  */ 0x33, 0x66,
    /* def  9  */ 0x01, 0x05, 0x00, 0xFA, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05,
    /* def  19 */ 0x63, 0x04, 0x00
};
const Buffer kPuffsSample2 = {
    /* puff  0 */ 0x00, 0x00, 0xA0, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF,
                  0x81,
    /* raw  11 */ 0x00, 0x33, 0x66,
    /* puff 14 */ 0x00, 0x00, 0x80, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF,
                  0x81,
    /* puff 25 */ 0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81,
    /* raw  32 */ 0x00,
};
const vector<ByteExtent> kDeflateExtentsSample2 = {
  {0, 7}, {9, 10}, {19, 3}};
const vector<BitExtent> kSubblockDeflateExtentsSample2 = {
  {0, 50}, {72, 80}, {152, 18}};
const vector<ByteExtent> kPuffExtentsSample2 = {
  {0, 11}, {14, 11}, {25, 7}};
// clang-format on

// This data is taken from the failed instances described in crbug.com/915559.
const Buffer kProblematicCache = {
    0x51, 0x74, 0x97, 0x71, 0x51, 0x6e, 0x6d, 0x1b, 0x87, 0x4f, 0x5b,
    0xb1, 0xbb, 0xb6, 0xdd, 0xdd, 0xdd, 0x89, 0x89, 0xa2, 0x88, 0x9d,
    0x18, 0x4c, 0x1a, 0x8c, 0x8a, 0x1d, 0xa8, 0xd8, 0x89, 0xdd, 0xdd,
    0x81, 0x89, 0x62, 0x77, 0xb7, 0x32, 0x81, 0x31, 0x98, 0x88, 0x5d,
    0x83, 0xbd, 0xff, 0xf3, 0xe1, 0xf8, 0x9d, 0xd7, 0xba, 0xd6, 0x9a,
    0x7b, 0x86, 0x99, 0x3b, 0xf7, 0xbb, 0xdf, 0xfd, 0x90, 0xf0, 0x45,
    0x0b, 0xb4, 0x44, 0x2b, 0xb4, 0x46, 0x1b, 0xb4, 0xc5, 0xff};
const vector<BitExtent> kProblematicCacheDeflateExtents = {{2, 606}};
const vector<BitExtent> kProblematicCachePuffExtents = {{1, 185}};

}  // namespace puffin
