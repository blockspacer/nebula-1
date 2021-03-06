/*
 * Copyright 2017-present Shawn Cao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "TypeData.h"

#include <folly/Conv.h>
#include <gflags/gflags.h>

// initialize data page for each type with heuristic assumptions.
// this can be optimized in the future when the system can learn different category's data statistics.
// it can be allocated by external metadata hints.
DEFINE_int32(META_PAGE_SIZE, 4 * 1024, "page size for meta data");
DEFINE_int32(BOOL_PAGE_SIZE, 1024, "BOOL data page size");
DEFINE_int32(BYTE_PAGE_SIZE, 2 * 1024, "BYTE data page size");
DEFINE_int32(SHORT_PAGE_SIZE, 2 * 1024, "SHORT data page size");
DEFINE_int32(INT_PAGE_SIZE, 4 * 1024, "int data page size");
DEFINE_int32(LONG_PAGE_SIZE, 8 * 1024, "long data page size");
DEFINE_int32(REAL_PAGE_SIZE, 8 * 1024, "real data page size");
DEFINE_int32(BINARY_PAGE_SIZE, 32 * 1024, "string or bytes data page size");
DEFINE_int32(EMPTY_PAGE_SIZE, 8, "for compability only - compound types do not have data");

namespace nebula {
namespace memory {
namespace serde {

using nebula::meta::Column;

// convert string to void* with nullptr
void* void_any(const std::string&) {
  return nullptr;
}

#define TYPE_DATA_CONSTR(TYPE, SLICE_PAGE, CONV)                             \
  template <>                                                                \
  TYPE::TypeDataImpl(const Column& column, size_t batchSize)                 \
    : slice_{ (size_t)SLICE_PAGE }, bf_{ nullptr } {                         \
    if (column.withBloomFilter && Scalar) {                                  \
      bf_ = std::make_unique<nebula::common::BloomFilter<NType>>(batchSize); \
    }                                                                        \
                                                                             \
    if (column.defaultValue.size() > 0) {                                    \
      default_ = CONV(column.defaultValue);                                  \
    }                                                                        \
  }

TYPE_DATA_CONSTR(BoolData, FLAGS_BOOL_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(ByteData, FLAGS_BYTE_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(ShortData, FLAGS_SHORT_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(IntData, FLAGS_INT_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(LongData, FLAGS_LONG_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(FloatData, FLAGS_REAL_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(DoubleData, FLAGS_REAL_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(Int128Data, FLAGS_LONG_PAGE_SIZE, folly::to<NType>)
TYPE_DATA_CONSTR(StringData, FLAGS_BINARY_PAGE_SIZE, )
TYPE_DATA_CONSTR(EmptyData, FLAGS_EMPTY_PAGE_SIZE, void_any)

#undef TYPE_DATA_CONSTR

// string void data
template <>
void StringData::addVoid(IndexType) {
  size_ += slice_.write(size_, "", 0);
}

template <>
void StringData::add(IndexType, std::string_view value) {
  size_ += slice_.write(size_, value.data(), value.size());
  // TODO(cao) - disable bloom filter string type for now
  // Due to hash function missing for string_view type
  // if (UNLIKELY(bf_ != nullptr)) {
  //   bf_->add(value.);
  // }
}

#define TYPE_PROBABLY(DT, VT, BE)    \
  template <>                        \
  bool DT::probably(VT item) const { \
    if (BE(bf_ != nullptr)) {        \
      return bf_->probably(item);    \
    }                                \
    return true;                     \
  }

TYPE_PROBABLY(BoolData, bool, UNLIKELY)
TYPE_PROBABLY(ByteData, int8_t, UNLIKELY)
TYPE_PROBABLY(ShortData, int16_t, LIKELY)
TYPE_PROBABLY(IntData, int32_t, LIKELY)
TYPE_PROBABLY(LongData, int64_t, LIKELY)
TYPE_PROBABLY(FloatData, float, UNLIKELY)
TYPE_PROBABLY(DoubleData, double, UNLIKELY)
TYPE_PROBABLY(Int128Data, int128_t, UNLIKELY)

#undef TYPE_PROBABLY

///////////////////////////////////////////////////////////////////////////////////////////////////
#define HOOK_CONCRETE(TYPE, obj)          \
  if ((obj = dynamic_cast<TYPE*>(raw))) { \
    hasBf_ = obj->hasBloomFilter();       \
    void_ = [this](IndexType index) {     \
      obj->addVoid(index);                \
    };                                    \
  }

// TODO(cao) - revisit: seems proxy is unncessary if every type can be templated
TypeDataProxy::TypeDataProxy(PTypeData typeData)
  : data_{ std::move(typeData) }, void_{} {
  // perf reason: pay the cost of virtual function once
  // the pointer can be any type -
  // it would be great if base class can infer sub-class's template type??
  auto raw = data_.get();
  HOOK_CONCRETE(BoolData, bd_)
  HOOK_CONCRETE(ByteData, btd_)
  HOOK_CONCRETE(ShortData, sd_)
  HOOK_CONCRETE(IntData, id_)
  HOOK_CONCRETE(LongData, ld_)
  HOOK_CONCRETE(FloatData, fd_)
  HOOK_CONCRETE(DoubleData, dd_)
  HOOK_CONCRETE(Int128Data, i128d_)
  HOOK_CONCRETE(StringData, std_)

  // void_ could be null if current data proxy is created for compound
  // N_ENSURE(void_ != nullptr, "type data is not registered.");
}
#undef HOOK_CONCRETE

#define TYPE_ADD_PROXY(TYPE, OBJ)                        \
  template <>                                            \
  void TypeDataProxy::add(IndexType index, TYPE value) { \
    OBJ->add(index, value);                              \
  }

TYPE_ADD_PROXY(bool, bd_)
TYPE_ADD_PROXY(int8_t, btd_)
TYPE_ADD_PROXY(int16_t, sd_)
TYPE_ADD_PROXY(int32_t, id_)
TYPE_ADD_PROXY(int64_t, ld_)
TYPE_ADD_PROXY(float, fd_)
TYPE_ADD_PROXY(double, dd_)
TYPE_ADD_PROXY(int128_t, i128d_)
TYPE_ADD_PROXY(std::string_view, std_)
#undef TYPE_ADD_PROXY

#define TYPE_PROBABLY_PROXY(TYPE, OBJ)             \
  template <>                                      \
  bool TypeDataProxy::probably(TYPE value) const { \
    return OBJ->probably(value);                   \
  }

TYPE_PROBABLY_PROXY(bool, bd_)
TYPE_PROBABLY_PROXY(int8_t, btd_)
TYPE_PROBABLY_PROXY(int16_t, sd_)
TYPE_PROBABLY_PROXY(int32_t, id_)
TYPE_PROBABLY_PROXY(int64_t, ld_)
TYPE_PROBABLY_PROXY(float, fd_)
TYPE_PROBABLY_PROXY(double, dd_)
TYPE_PROBABLY_PROXY(int128_t, i128d_)

#undef TYPE_PROBABLY_PROXY

#define TYPE_DEFAULT_PROXY(TYPE, OBJ)        \
  template <>                                \
  TYPE TypeDataProxy::defaultValue() const { \
    return OBJ->defaultValue();              \
  }

TYPE_DEFAULT_PROXY(bool, bd_)
TYPE_DEFAULT_PROXY(int8_t, btd_)
TYPE_DEFAULT_PROXY(int16_t, sd_)
TYPE_DEFAULT_PROXY(int32_t, id_)
TYPE_DEFAULT_PROXY(int64_t, ld_)
TYPE_DEFAULT_PROXY(float, fd_)
TYPE_DEFAULT_PROXY(double, dd_)
TYPE_DEFAULT_PROXY(int128_t, i128d_)
TYPE_DEFAULT_PROXY(std::string_view, std_)

#undef TYPE_DEFAULT_PROXY

#define TYPE_READ_PROXY(TYPE, OBJ)                  \
  template <>                                       \
  TYPE TypeDataProxy::read(IndexType index) const { \
    return OBJ->read(index);                        \
  }

TYPE_READ_PROXY(bool, bd_)
TYPE_READ_PROXY(int8_t, btd_)
TYPE_READ_PROXY(int16_t, sd_)
TYPE_READ_PROXY(int32_t, id_)
TYPE_READ_PROXY(int64_t, ld_)
TYPE_READ_PROXY(float, fd_)
TYPE_READ_PROXY(double, dd_)
TYPE_READ_PROXY(int128_t, i128d_)

#undef TYPE_READ_PROXY

} // namespace serde
} // namespace memory
} // namespace nebula