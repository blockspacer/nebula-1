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

#pragma once

#include <stack>
#include <unordered_map>
#include <unordered_set>

#include "common/Memory.h"
#include "surface/DataSurface.h"
#include "type/Type.h"

// DEFINE_int32(SLICE_SIZE, 64 * 1024, "slice size in bytes");

/**
 * Build a flat buffer that can be used in hash table to build keys and update directly.
 * 
 * Every value has 1byte = HIGH4 (NULL) + LOW4 (TYPE).
 * Scalar types are embeded and others are referenced by offset and length in buffer.
 * So we have two memory chunks. 
 * 
 * In main memory chunk, it starts with # of bytes for nulls
 * bool/INT/float types: width bytes. 
 * string type: 8 bytes [4 bytes offset, 4 bytes length]
 * list type: 8 bytes [4 bytes of value N (number of items), 4 bytes offset in list buffer]
 * String data stored in data_
 * List items stores at list_
 * So In main_, we know exactly size of each type if it has non-null value
 * map type: not support for now 
 * struct type: not support for now
 */
namespace nebula {
namespace memory {
namespace keyed {

using nebula::surface::IndexType;

static constexpr int8_t HIGH6_1 = 1 << 6;
static constexpr auto SIZET_SIZE = sizeof(size_t);
static constexpr size_t MAGIC = 0x910928;

class RowAccessor;

struct Buffer {
  Buffer(size_t page) : offset{ 0 }, slice{ page } {}

  // initialize a buffer with given slice
  Buffer(size_t size, const NByte* buffer) : offset{ size }, slice{ buffer, size } {}
  // write offset and also current size
  size_t offset;
  nebula::common::PagedSlice slice;
};

struct ColumnProps {
  explicit ColumnProps(bool nv, uint32_t os) : isNull{ nv }, offset{ os } {}
  // whether it is a null value
  bool isNull;

  // its relative offset in current row in main buffer
  uint32_t offset;
};

// define column properties of given row: nullable, kind, column offset in main.
using FlatColumnProps = std::vector<ColumnProps>;

struct RowProps {
  explicit RowProps(size_t of, FlatColumnProps cp)
    : offset{ of }, colProps{ std::move(cp) } {}
  // row offset in buffer
  size_t offset;

  // column properties
  FlatColumnProps colProps;
};

// column parser to read data in from a row
using Parser = std::function<void(const nebula::surface::RowData&)>;

class FlatBuffer {
public:
  FlatBuffer(const nebula::type::Schema&);
  FlatBuffer(const nebula::type::Schema&, NByte*);

  virtual ~FlatBuffer() {
    if (chunk_) {
      nebula::common::Pool::getDefault().free(chunk_);
    }
  }

  // add a row into current batch
  size_t add(const nebula::surface::RowData&);

  // this method only rollback last added row and the only one row only.
  bool rollback();

  // instead of rollback, we continue the same row to fill data with remainings
  size_t resume(const nebula::surface::RowData&, const std::unordered_set<size_t>&, const size_t);

  // random access to a row - may require internal seek
  const nebula::surface::RowData& row(size_t);

  // const version without internal cache
  const std::unique_ptr<nebula::surface::RowData> crow(size_t) const;

  inline auto getRows() const {
    return rows_.size();
  }

  inline size_t binSize() const {
    return SIZET_SIZE +                                   // num rows
           rows_.size() * SIZET_SIZE +                    // rows offset
           SIZET_SIZE +                                   // main block size
           SIZET_SIZE +                                   // data block size
           SIZET_SIZE +                                   // list block size
           SIZET_SIZE +                                   // reserved
           main_->offset + data_->offset + list_->offset; // all binary size
  }

  size_t serialize(NByte*) const;

  const nebula::type::Schema& schema() const {
    return schema_;
  }

  inline void* chunk() const {
    return chunk_;
  }

private:
  bool appendNull(bool, nebula::type::Kind, Buffer&);

  template <typename T>
  size_t append(T, Buffer&);

  // goes to list_
  size_t appendList(nebula::type::Kind, std::unique_ptr<nebula::surface::ListData>);

  // get column properties of given row
  FlatColumnProps columnProps(size_t) const noexcept;

  static size_t widthInMain(nebula::type::Kind) noexcept;

  void initSchema() noexcept;

  Parser genParser(const nebula::type::TypeNode&, size_t) noexcept;

protected:
  // offset of last row used for supporting roll back
  std::tuple<size_t, size_t, size_t> last_;

  // record each row's offset and length
  std::vector<RowProps> rows_;
  nebula::type::Schema schema_;
  std::unique_ptr<Buffer> main_;
  std::unique_ptr<Buffer> data_;
  std::unique_ptr<Buffer> list_;

  // an owned data buffer passed in - need to free it in destructor
  void* chunk_;

  // A row accessor cursor to read data of given row
  friend class RowAccessor;
  std::unique_ptr<RowAccessor> current_;

  // fields_ is name->index mapping
  std::unordered_map<std::string, size_t> fields_;

  // kw_ indicats the column's kind and width in main_ buffer if not null.
  std::vector<std::pair<nebula::type::Kind, size_t>> kw_;

  // parsers are function pointers to parse row data of each column
  std::vector<Parser> parsers_;

  // number of columns - reduce func calls even they are inlined
  size_t numColumns_;
};

class RowAccessor : public nebula::surface::RowData {
public:
  RowAccessor(const FlatBuffer&, const RowProps&);
  virtual ~RowAccessor() = default;

public:
  bool isNull(const std::string& field) const override;
  bool readBool(const std::string& field) const override;
  int8_t readByte(const std::string& field) const override;
  int16_t readShort(const std::string& field) const override;
  int32_t readInt(const std::string& field) const override;
  int64_t readLong(const std::string& field) const override;
  float readFloat(const std::string& field) const override;
  double readDouble(const std::string& field) const override;
  int128_t readInt128(const std::string& field) const override;
  std::string_view readString(const std::string& field) const override;

  // compound types
  std::unique_ptr<nebula::surface::ListData> readList(const std::string& field) const override;
  std::unique_ptr<nebula::surface::MapData> readMap(const std::string& field) const override;

  bool isNull(IndexType) const override;
  bool readBool(IndexType) const override;
  int8_t readByte(IndexType) const override;
  int16_t readShort(IndexType) const override;
  int32_t readInt(IndexType) const override;
  int64_t readLong(IndexType) const override;
  float readFloat(IndexType) const override;
  double readDouble(IndexType) const override;
  int128_t readInt128(IndexType) const override;
  std::string_view readString(IndexType) const override;

  // compound types
  std::unique_ptr<nebula::surface::ListData> readList(IndexType) const override;
  std::unique_ptr<nebula::surface::MapData> readMap(IndexType) const override;

private:
  const FlatBuffer& fb_;

  // current row offset and all column properties
  const RowProps& rowProps_;
};

using nebula::surface::IndexType;
class ListAccessor : public nebula::surface::ListData {
public:
  ListAccessor(IndexType items, IndexType offset, const Buffer& buffer, const Buffer& strings, size_t itemWidth)
    : ListData(items),
      offset_{ offset },
      buffer_{ buffer },
      strings_{ strings },
      itemWidth_{ itemWidth } {

    // move current index to index and adjust current offset
    itemOffsets_.reserve(items);
    size_t itemOffset = 0;
    for (IndexType i = 0; i < items; ++i) {
      itemOffsets_.push_back(itemOffset);
      // check if its null
      if (!isOffsetNull(itemOffset)) {
        itemOffset += itemWidth_;
      }

      // the always null byte
      itemOffset += 1;
    }
  }

  bool isNull(IndexType index) const override;
  bool readBool(IndexType index) const override;
  std::int8_t readByte(IndexType index) const override;
  int16_t readShort(IndexType index) const override;
  int32_t readInt(IndexType index) const override;
  int64_t readLong(IndexType index) const override;
  float readFloat(IndexType index) const override;
  double readDouble(IndexType index) const override;
  int128_t readInt128(IndexType index) const override;
  std::string_view readString(IndexType index) const override;

private:
  inline bool isOffsetNull(size_t itemOffset) const {
    return (HIGH6_1 & (buffer_.slice.read<int8_t>(offset_ + itemOffset))) != 0;
  }

private:
  IndexType offset_;
  const Buffer& buffer_;
  const Buffer& strings_;
  size_t itemWidth_;

  std::vector<size_t> itemOffsets_;
};

} // namespace keyed
} // namespace memory
} // namespace nebula