// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INCLUDE_PUFFIN_PUFFDIFF_H_
#define SRC_INCLUDE_PUFFIN_PUFFDIFF_H_

#include <string>
#include <vector>

#include "bsdiff/constants.h"

#include "puffin/common.h"
#include "puffin/stream.h"

namespace puffin {

enum class PatchAlgorithm {
  kBsdiff = 0,
  kZucchini = 1,
};

// Performs a diff operation between input deflate streams and creates a patch
// that is used in the client to recreate the |dst| from |src|.
// |src|          IN   Source deflate stream.
// |dst|          IN   Destination deflate stream.
// |src_deflates| IN   Deflate locations in |src|.
// |dst_deflates| IN   Deflate locations in |dst|.
// |compressors|  IN   Compressors to use in the underlying bsdiff, e.g.
// bz2,
//                     brotli.
// |patchAlgorithm|    IN   The patchAlgorithm used to create patches between
//                     uncompressed bytes, e.g. bsdiff, zucchini.
// |tmp_filepath| IN   A path to a temporary file. The caller has the
//                     responsibility of unlinking the file after the call to
//                     |PuffDiff| finishes.
// |puffin_patch| OUT  The patch that later can be used in |PuffPatch|.
bool PuffDiff(UniqueStreamPtr src,
              UniqueStreamPtr dst,
              const std::vector<BitExtent>& src_deflates,
              const std::vector<BitExtent>& dst_deflates,
              const std::vector<bsdiff::CompressorType>& compressors,
              PatchAlgorithm patchAlgorithm,
              const std::string& tmp_filepath,
              Buffer* patch);

// This function uses bsdiff as the patch algorithm.
bool PuffDiff(UniqueStreamPtr src,
              UniqueStreamPtr dst,
              const std::vector<BitExtent>& src_deflates,
              const std::vector<BitExtent>& dst_deflates,
              const std::vector<bsdiff::CompressorType>& compressors,
              const std::string& tmp_filepath,
              Buffer* patch);

// Similar to the function above, except that it accepts raw buffer rather than
// stream.
bool PuffDiff(const Buffer& src,
              const Buffer& dst,
              const std::vector<BitExtent>& src_deflates,
              const std::vector<BitExtent>& dst_deflates,
              const std::vector<bsdiff::CompressorType>& compressors,
              const std::string& tmp_filepath,
              Buffer* patch);

// The default puffdiff function that uses both bz2 and brotli to compress the
// patch data.
bool PuffDiff(const Buffer& src,
              const Buffer& dst,
              const std::vector<BitExtent>& src_deflates,
              const std::vector<BitExtent>& dst_deflates,
              const std::string& tmp_filepath,
              Buffer* patch);

}  // namespace puffin

#endif  // SRC_INCLUDE_PUFFIN_PUFFDIFF_H_
