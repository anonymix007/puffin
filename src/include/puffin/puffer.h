// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INCLUDE_PUFFIN_PUFFER_H_
#define SRC_INCLUDE_PUFFIN_PUFFER_H_

#include <memory>
#include <vector>

#include "puffin/common.h"
#include "puffin/stream.h"

namespace puffin {

class BitReaderInterface;
class PuffWriterInterface;
class HuffmanTable;

class PUFFIN_EXPORT Puffer {
 public:
  Puffer();
  ~Puffer();

  // Creates a puffed buffer from a deflate buffer.
  //
  // If |deflates| is not null, it will be populated with the location of the
  // subblocks in the input data. In addition, the uncompressed deflate blocks
  // will be ignored and will not be added to the |deflates|. For this case to
  // happen correctly, the |pw| should write into an empty/null buffer,
  // otherwise the created puff stream, will not match the deflate stream.
  bool PuffDeflate(BitReaderInterface* br,
                   PuffWriterInterface* pw,
                   std::vector<BitExtent>* deflates) const;

 private:
  std::unique_ptr<HuffmanTable> dyn_ht_;
  std::unique_ptr<HuffmanTable> fix_ht_;

  DISALLOW_COPY_AND_ASSIGN(Puffer);
};

}  // namespace puffin

#endif  // SRC_INCLUDE_PUFFIN_PUFFER_H_
