// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "riegeli/bytes/limiting_reader.h"

#include <stddef.h>
#include <limits>
#include <memory>

#include "absl/base/optimization.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/object.h"
#include "riegeli/bytes/backward_writer.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/writer.h"

namespace riegeli {

void LimitingReaderBase::Done() {
  if (ABSL_PREDICT_TRUE(healthy())) {
    Reader* const src = src_reader();
    SyncBuffer(src);
  }
  Reader::Done();
}

bool LimitingReaderBase::PullSlow() {
  RIEGELI_ASSERT_EQ(available(), 0u)
      << "Failed precondition of Reader::PullSlow(): "
         "data available, use Pull() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Reader* const src = src_reader();
  SyncBuffer(src);
  if (ABSL_PREDICT_FALSE(limit_pos_ == size_limit_)) return false;
  const bool ok = src->Pull();
  MakeBuffer(src);
  return ok;
}

bool LimitingReaderBase::ReadSlow(char* dest, size_t length) {
  RIEGELI_ASSERT_GT(length, available())
      << "Failed precondition of Reader::ReadSlow(char*): "
         "length too small, use Read(char*) instead";
  return ReadInternal(dest, length);
}

bool LimitingReaderBase::ReadSlow(Chain* dest, size_t length) {
  RIEGELI_ASSERT_GT(length, UnsignedMin(available(), kMaxBytesToCopy()))
      << "Failed precondition of Reader::ReadSlow(Chain*): "
         "length too small, use Read(Chain*) instead";
  RIEGELI_ASSERT_LE(length, std::numeric_limits<size_t>::max() - dest->size())
      << "Failed precondition of Reader::ReadSlow(Chain*): "
         "Chain size overflow";
  return ReadInternal(dest, length);
}

template <typename Dest>
inline bool LimitingReaderBase::ReadInternal(Dest* dest, size_t length) {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Reader* const src = src_reader();
  SyncBuffer(src);
  RIEGELI_ASSERT_LE(pos(), size_limit_)
      << "Failed invariant of LimitingReaderBase: position exceeds size limit";
  const size_t length_to_read = UnsignedMin(length, size_limit_ - pos());
  const bool ok = src->Read(dest, length_to_read);
  MakeBuffer(src);
  return ok && length_to_read == length;
}

bool LimitingReaderBase::CopyToSlow(Writer* dest, Position length) {
  RIEGELI_ASSERT_GT(length, UnsignedMin(available(), kMaxBytesToCopy()))
      << "Failed precondition of Reader::CopyToSlow(Writer*): "
         "length too small, use CopyTo(Writer*) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Reader* const src = src_reader();
  SyncBuffer(src);
  RIEGELI_ASSERT_LE(pos(), size_limit_)
      << "Failed invariant of LimitingReaderBase: position exceeds size limit";
  const Position length_to_copy = UnsignedMin(length, size_limit_ - pos());
  const bool ok = src->CopyTo(dest, length_to_copy);
  MakeBuffer(src);
  return ok && length_to_copy == length;
}

bool LimitingReaderBase::CopyToSlow(BackwardWriter* dest, size_t length) {
  RIEGELI_ASSERT_GT(length, UnsignedMin(available(), kMaxBytesToCopy()))
      << "Failed precondition of Reader::CopyToSlow(BackwardWriter*): "
         "length too small, use CopyTo(BackwardWriter*) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Reader* const src = src_reader();
  SyncBuffer(src);
  RIEGELI_ASSERT_LE(pos(), size_limit_)
      << "Failed invariant of LimitingReaderBase: position exceeds size limit";
  if (ABSL_PREDICT_FALSE(length > size_limit_ - pos())) {
    src->Seek(size_limit_);
    MakeBuffer(src);
    return false;
  }
  const bool ok = src->CopyTo(dest, length);
  MakeBuffer(src);
  return ok;
}

bool LimitingReaderBase::SupportsRandomAccess() const {
  const Reader* const src = src_reader();
  return src != nullptr && src->SupportsRandomAccess();
}

bool LimitingReaderBase::SeekSlow(Position new_pos) {
  RIEGELI_ASSERT(new_pos < start_pos() || new_pos > limit_pos_)
      << "Failed precondition of Reader::SeekSlow(): "
         "position in the buffer, use Seek() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Reader* const src = src_reader();
  SyncBuffer(src);
  const Position pos_to_seek = UnsignedMin(new_pos, size_limit_);
  const bool ok = src->Seek(pos_to_seek);
  MakeBuffer(src);
  return ok && pos_to_seek == new_pos;
}

bool LimitingReaderBase::Size(Position* size) {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Reader* const src = src_reader();
  SyncBuffer(src);
  const bool ok = src->Size(size);
  MakeBuffer(src);
  if (ABSL_PREDICT_FALSE(!ok)) return false;
  *size = UnsignedMin(*size, size_limit_);
  return true;
}

template class LimitingReader<Reader*>;
template class LimitingReader<std::unique_ptr<Reader>>;

}  // namespace riegeli
