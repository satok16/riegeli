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

#include "riegeli/bytes/chain_backward_writer.h"

#include <stddef.h>
#include <limits>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/bytes/backward_writer.h"

namespace riegeli {

void ChainBackwardWriterBase::Done() {
  if (ABSL_PREDICT_TRUE(healthy())) {
    Chain* const dest = dest_chain();
    RIEGELI_ASSERT_EQ(limit_pos(), dest->size())
        << "ChainBackwardWriter destination changed unexpectedly";
    SyncBuffer(dest);
  }
  BackwardWriter::Done();
}

bool ChainBackwardWriterBase::PushSlow() {
  RIEGELI_ASSERT_EQ(available(), 0u)
      << "Failed precondition of BackwardWriter::PushSlow(): "
         "space available, use Push() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Chain* const dest = dest_chain();
  RIEGELI_ASSERT_EQ(limit_pos(), dest->size())
      << "ChainBackwardWriter destination changed unexpectedly";
  if (ABSL_PREDICT_FALSE(dest->size() == std::numeric_limits<size_t>::max())) {
    return FailOverflow();
  }
  start_pos_ = pos();
  MakeBuffer(dest, 1);
  return true;
}

bool ChainBackwardWriterBase::WriteSlow(absl::string_view src) {
  RIEGELI_ASSERT_GT(src.size(), available())
      << "Failed precondition of BackwardWriter::WriteSlow(string_view): "
         "length too small, use Write(string_view) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Chain* const dest = dest_chain();
  RIEGELI_ASSERT_EQ(limit_pos(), dest->size())
      << "ChainBackwardWriter destination changed unexpectedly";
  if (ABSL_PREDICT_FALSE(src.size() > std::numeric_limits<size_t>::max() -
                                          IntCast<size_t>(pos()))) {
    return FailOverflow();
  }
  SyncBuffer(dest);
  start_pos_ += src.size();
  dest->Prepend(src, size_hint_);
  MakeBuffer(dest);
  return true;
}

bool ChainBackwardWriterBase::WriteSlow(std::string&& src) {
  RIEGELI_ASSERT_GT(src.size(), UnsignedMin(available(), kMaxBytesToCopy))
      << "Failed precondition of BackwardWriter::WriteSlow(string&&): "
         "length too small, use Write(string&&) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Chain* const dest = dest_chain();
  RIEGELI_ASSERT_EQ(limit_pos(), dest->size())
      << "ChainBackwardWriter destination changed unexpectedly";
  if (ABSL_PREDICT_FALSE(src.size() > std::numeric_limits<size_t>::max() -
                                          IntCast<size_t>(pos()))) {
    return FailOverflow();
  }
  SyncBuffer(dest);
  start_pos_ += src.size();
  dest->Prepend(std::move(src), size_hint_);
  MakeBuffer(dest);
  return true;
}

bool ChainBackwardWriterBase::WriteSlow(const Chain& src) {
  RIEGELI_ASSERT_GT(src.size(), UnsignedMin(available(), kMaxBytesToCopy))
      << "Failed precondition of BackwardWriter::WriteSlow(Chain): "
         "length too small, use Write(Chain) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Chain* const dest = dest_chain();
  RIEGELI_ASSERT_EQ(limit_pos(), dest->size())
      << "ChainBackwardWriter destination changed unexpectedly";
  if (ABSL_PREDICT_FALSE(src.size() > std::numeric_limits<size_t>::max() -
                                          IntCast<size_t>(pos()))) {
    return FailOverflow();
  }
  SyncBuffer(dest);
  start_pos_ += src.size();
  dest->Prepend(src, size_hint_);
  MakeBuffer(dest);
  return true;
}

bool ChainBackwardWriterBase::WriteSlow(Chain&& src) {
  RIEGELI_ASSERT_GT(src.size(), UnsignedMin(available(), kMaxBytesToCopy))
      << "Failed precondition of BackwardWriter::WriteSlow(Chain&&): "
         "length too small, use Write(Chain&&) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Chain* const dest = dest_chain();
  SyncBuffer(dest);
  start_pos_ += src.size();
  dest->Prepend(std::move(src), size_hint_);
  MakeBuffer(dest);
  return true;
}

bool ChainBackwardWriterBase::Truncate(Position new_size) {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  Chain* const dest = dest_chain();
  RIEGELI_ASSERT_EQ(limit_pos(), dest->size())
      << "ChainBackwardWriter destination changed unexpectedly";
  if (new_size >= start_pos_) {
    if (ABSL_PREDICT_FALSE(new_size > pos())) return false;
    cursor_ = start_ - (new_size - start_pos_);
    return true;
  }
  start_pos_ = new_size;
  dest->RemovePrefix(dest->size() - IntCast<size_t>(new_size));
  start_ = nullptr;
  cursor_ = nullptr;
  limit_ = nullptr;
  return true;
}

inline void ChainBackwardWriterBase::SyncBuffer(Chain* dest) {
  start_pos_ = pos();
  dest->RemovePrefix(available());
  start_ = nullptr;
  cursor_ = nullptr;
  limit_ = nullptr;
}

inline void ChainBackwardWriterBase::MakeBuffer(Chain* dest, size_t min_size) {
  const absl::Span<char> buffer = dest->PrependBuffer(min_size, 0, size_hint_);
  limit_ = buffer.data();
  start_ = limit_ + buffer.size();
  cursor_ = start_;
}

template class ChainBackwardWriter<Chain*>;
template class ChainBackwardWriter<Chain>;

}  // namespace riegeli
