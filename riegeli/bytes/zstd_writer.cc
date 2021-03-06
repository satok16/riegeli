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

// Make ZSTD_WINDOWLOG_MIN, ZSTD_WINDOWLOG_MAX, ZSTD_getParams(), and
// ZSTD_initCStream_advanced() available.
#define ZSTD_STATIC_LINKING_ONLY

#include "riegeli/bytes/zstd_writer.h"

#include <stddef.h>
#include <limits>

#include "absl/base/optimization.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "riegeli/base/base.h"
#include "riegeli/bytes/buffered_writer.h"
#include "riegeli/bytes/writer.h"
#include "zstd.h"

namespace riegeli {

// Before C++17 if a constexpr static data member is ODR-used, its definition at
// namespace scope is required. Since C++17 these definitions are deprecated:
// http://en.cppreference.com/w/cpp/language/static
#if __cplusplus < 201703
constexpr int ZstdWriterBase::Options::kMinCompressionLevel;
constexpr int ZstdWriterBase::Options::kMaxCompressionLevel;
constexpr int ZstdWriterBase::Options::kDefaultCompressionLevel;
constexpr int ZstdWriterBase::Options::kMinWindowLog;
constexpr int ZstdWriterBase::Options::kMaxWindowLog;
constexpr int ZstdWriterBase::Options::kDefaultWindowLog;
#endif

void ZstdWriterBase::Done() {
  if (ABSL_PREDICT_TRUE(PushInternal())) {
    Writer* const dest = dest_writer();
    RIEGELI_ASSERT_EQ(written_to_buffer(), 0u)
        << "BufferedWriter::PushInternal() did not empty the buffer";
    FlushInternal(ZSTD_endStream, "ZSTD_endStream()", dest);
  }
  BufferedWriter::Done();
}

inline bool ZstdWriterBase::EnsureCStreamCreated() {
  if (ABSL_PREDICT_FALSE(compressor_ == nullptr)) {
    compressor_.reset(ZSTD_createCStream());
    if (ABSL_PREDICT_FALSE(compressor_ == nullptr)) {
      return Fail("ZSTD_createCStream() failed");
    }
    return InitializeCStream();
  }
  return true;
}

bool ZstdWriterBase::InitializeCStream() {
  ZSTD_parameters params = ZSTD_getParams(
      compression_level_, IntCast<unsigned long long>(size_hint_), 0);
  if (window_log_ >= 0) {
    params.cParams.windowLog = IntCast<unsigned>(window_log_);
  }
  const size_t result = ZSTD_initCStream_advanced(
      compressor_.get(), nullptr, 0, params, ZSTD_CONTENTSIZE_UNKNOWN);
  if (ABSL_PREDICT_FALSE(ZSTD_isError(result))) {
    return Fail(absl::StrCat("ZSTD_initCStream_advanced() failed: ",
                             ZSTD_getErrorName(result)));
  }
  return true;
}

bool ZstdWriterBase::WriteInternal(absl::string_view src) {
  RIEGELI_ASSERT(!src.empty())
      << "Failed precondition of BufferedWriter::WriteInternal(): "
         "nothing to write";
  RIEGELI_ASSERT(healthy())
      << "Failed precondition of BufferedWriter::WriteInternal(): "
      << message();
  RIEGELI_ASSERT_EQ(written_to_buffer(), 0u)
      << "Failed precondition of BufferedWriter::WriteInternal(): "
         "buffer not empty";
  Writer* const dest = dest_writer();
  if (ABSL_PREDICT_FALSE(src.size() >
                         std::numeric_limits<Position>::max() - limit_pos())) {
    return FailOverflow();
  }
  if (ABSL_PREDICT_FALSE(!EnsureCStreamCreated())) return false;
  ZSTD_inBuffer input = {src.data(), src.size(), 0};
  for (;;) {
    ZSTD_outBuffer output = {dest->cursor(), dest->available(), 0};
    const size_t result =
        ZSTD_compressStream(compressor_.get(), &output, &input);
    dest->set_cursor(static_cast<char*>(output.dst) + output.pos);
    if (ABSL_PREDICT_FALSE(ZSTD_isError(result))) {
      return Fail(absl::StrCat("ZSTD_compressStream() failed: ",
                               ZSTD_getErrorName(result)));
    }
    if (output.pos < output.size) {
      RIEGELI_ASSERT_EQ(input.pos, input.size)
          << "ZSTD_compressStream() returned but there are still input data "
             "and output space";
      start_pos_ += input.pos;
      return true;
    }
    if (ABSL_PREDICT_FALSE(!dest->Push())) return Fail(*dest);
  }
}

bool ZstdWriterBase::Flush(FlushType flush_type) {
  if (ABSL_PREDICT_FALSE(!PushInternal())) return false;
  Writer* const dest = dest_writer();
  RIEGELI_ASSERT_EQ(written_to_buffer(), 0u)
      << "BufferedWriter::PushInternal() did not empty the buffer";
  if (ABSL_PREDICT_FALSE(
          !FlushInternal(ZSTD_flushStream, "ZSTD_flushStream()", dest))) {
    return false;
  }
  if (ABSL_PREDICT_FALSE(!dest->Flush(flush_type))) return Fail(*dest);
  return true;
}

template <typename Function>
bool ZstdWriterBase::FlushInternal(Function function,
                                   absl::string_view function_name,
                                   Writer* dest) {
  RIEGELI_ASSERT(healthy())
      << "Failed precondition of ZstdWriterBase::FlushInternal(): "
      << message();
  RIEGELI_ASSERT_EQ(written_to_buffer(), 0u)
      << "Failed precondition of ZstdWriterBase::FlushInternal(): "
         "buffer not empty";
  if (ABSL_PREDICT_FALSE(!EnsureCStreamCreated())) return false;
  for (;;) {
    ZSTD_outBuffer output = {dest->cursor(), dest->available(), 0};
    const size_t result = function(compressor_.get(), &output);
    dest->set_cursor(static_cast<char*>(output.dst) + output.pos);
    if (result == 0) return true;
    if (ABSL_PREDICT_FALSE(ZSTD_isError(result))) {
      return Fail(
          absl::StrCat(function_name, " failed: ", ZSTD_getErrorName(result)));
    }
    RIEGELI_ASSERT_EQ(output.pos, output.size)
        << function_name << " returned but there is still output space";
    if (ABSL_PREDICT_FALSE(!dest->Push())) return Fail(*dest);
  }
}

template class ZstdWriter<Writer*>;
template class ZstdWriter<std::unique_ptr<Writer>>;

}  // namespace riegeli
