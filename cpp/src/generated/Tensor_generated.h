// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_TENSOR_ORG_APACHE_ARROW_FLATBUF_H_
#define FLATBUFFERS_GENERATED_TENSOR_ORG_APACHE_ARROW_FLATBUF_H_

#include "flatbuffers/flatbuffers.h"

#include "Schema_generated.h"

namespace org {
namespace apache {
namespace arrow {
namespace flatbuf {

struct TensorDim;
struct TensorDimBuilder;

struct Tensor;
struct TensorBuilder;

/// ----------------------------------------------------------------------
/// Data structures for dense tensors
/// Shape data for a single axis in a tensor
struct TensorDim FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef TensorDimBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_SIZE = 4,
    VT_NAME = 6
  };
  /// Length of dimension
  int64_t size() const {
    return GetField<int64_t>(VT_SIZE, 0);
  }
  /// Name of the dimension, optional
  const flatbuffers::String *name() const {
    return GetPointer<const flatbuffers::String *>(VT_NAME);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<int64_t>(verifier, VT_SIZE) &&
           VerifyOffset(verifier, VT_NAME) &&
           verifier.VerifyString(name()) &&
           verifier.EndTable();
  }
};

struct TensorDimBuilder {
  typedef TensorDim Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_size(int64_t size) {
    fbb_.AddElement<int64_t>(TensorDim::VT_SIZE, size, 0);
  }
  void add_name(flatbuffers::Offset<flatbuffers::String> name) {
    fbb_.AddOffset(TensorDim::VT_NAME, name);
  }
  explicit TensorDimBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  flatbuffers::Offset<TensorDim> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<TensorDim>(end);
    return o;
  }
};

inline flatbuffers::Offset<TensorDim> CreateTensorDim(
    flatbuffers::FlatBufferBuilder &_fbb,
    int64_t size = 0,
    flatbuffers::Offset<flatbuffers::String> name = 0) {
  TensorDimBuilder builder_(_fbb);
  builder_.add_size(size);
  builder_.add_name(name);
  return builder_.Finish();
}

inline flatbuffers::Offset<TensorDim> CreateTensorDimDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    int64_t size = 0,
    const char *name = nullptr) {
  auto name__ = name ? _fbb.CreateString(name) : 0;
  return org::apache::arrow::flatbuf::CreateTensorDim(
      _fbb,
      size,
      name__);
}

struct Tensor FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef TensorBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_TYPE_TYPE = 4,
    VT_TYPE = 6,
    VT_SHAPE = 8,
    VT_STRIDES = 10,
    VT_DATA = 12
  };
  org::apache::arrow::flatbuf::Type type_type() const {
    return static_cast<org::apache::arrow::flatbuf::Type>(GetField<uint8_t>(VT_TYPE_TYPE, 0));
  }
  /// The type of data contained in a value cell. Currently only fixed-width
  /// value types are supported, no strings or nested types
  const void *type() const {
    return GetPointer<const void *>(VT_TYPE);
  }
  template<typename T> const T *type_as() const;
  const org::apache::arrow::flatbuf::Null *type_as_Null() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Null ? static_cast<const org::apache::arrow::flatbuf::Null *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Int *type_as_Int() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Int ? static_cast<const org::apache::arrow::flatbuf::Int *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::FloatingPoint *type_as_FloatingPoint() const {
    return type_type() == org::apache::arrow::flatbuf::Type::FloatingPoint ? static_cast<const org::apache::arrow::flatbuf::FloatingPoint *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Binary *type_as_Binary() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Binary ? static_cast<const org::apache::arrow::flatbuf::Binary *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Utf8 *type_as_Utf8() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Utf8 ? static_cast<const org::apache::arrow::flatbuf::Utf8 *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Bool *type_as_Bool() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Bool ? static_cast<const org::apache::arrow::flatbuf::Bool *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Decimal *type_as_Decimal() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Decimal ? static_cast<const org::apache::arrow::flatbuf::Decimal *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Date *type_as_Date() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Date ? static_cast<const org::apache::arrow::flatbuf::Date *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Time *type_as_Time() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Time ? static_cast<const org::apache::arrow::flatbuf::Time *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Timestamp *type_as_Timestamp() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Timestamp ? static_cast<const org::apache::arrow::flatbuf::Timestamp *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Interval *type_as_Interval() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Interval ? static_cast<const org::apache::arrow::flatbuf::Interval *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::List *type_as_List() const {
    return type_type() == org::apache::arrow::flatbuf::Type::List ? static_cast<const org::apache::arrow::flatbuf::List *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Struct_ *type_as_Struct_() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Struct_ ? static_cast<const org::apache::arrow::flatbuf::Struct_ *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Union *type_as_Union() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Union ? static_cast<const org::apache::arrow::flatbuf::Union *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::FixedSizeBinary *type_as_FixedSizeBinary() const {
    return type_type() == org::apache::arrow::flatbuf::Type::FixedSizeBinary ? static_cast<const org::apache::arrow::flatbuf::FixedSizeBinary *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::FixedSizeList *type_as_FixedSizeList() const {
    return type_type() == org::apache::arrow::flatbuf::Type::FixedSizeList ? static_cast<const org::apache::arrow::flatbuf::FixedSizeList *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Map *type_as_Map() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Map ? static_cast<const org::apache::arrow::flatbuf::Map *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Duration *type_as_Duration() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Duration ? static_cast<const org::apache::arrow::flatbuf::Duration *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::LargeBinary *type_as_LargeBinary() const {
    return type_type() == org::apache::arrow::flatbuf::Type::LargeBinary ? static_cast<const org::apache::arrow::flatbuf::LargeBinary *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::LargeUtf8 *type_as_LargeUtf8() const {
    return type_type() == org::apache::arrow::flatbuf::Type::LargeUtf8 ? static_cast<const org::apache::arrow::flatbuf::LargeUtf8 *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::LargeList *type_as_LargeList() const {
    return type_type() == org::apache::arrow::flatbuf::Type::LargeList ? static_cast<const org::apache::arrow::flatbuf::LargeList *>(type()) : nullptr;
  }
  const org::apache::arrow::flatbuf::Complex *type_as_Complex() const {
    return type_type() == org::apache::arrow::flatbuf::Type::Complex ? static_cast<const org::apache::arrow::flatbuf::Complex *>(type()) : nullptr;
  }
  /// The dimensions of the tensor, optionally named
  const flatbuffers::Vector<flatbuffers::Offset<org::apache::arrow::flatbuf::TensorDim>> *shape() const {
    return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<org::apache::arrow::flatbuf::TensorDim>> *>(VT_SHAPE);
  }
  /// Non-negative byte offsets to advance one value cell along each dimension
  /// If omitted, default to row-major order (C-like).
  const flatbuffers::Vector<int64_t> *strides() const {
    return GetPointer<const flatbuffers::Vector<int64_t> *>(VT_STRIDES);
  }
  /// The location and size of the tensor's data
  const org::apache::arrow::flatbuf::Buffer *data() const {
    return GetStruct<const org::apache::arrow::flatbuf::Buffer *>(VT_DATA);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint8_t>(verifier, VT_TYPE_TYPE) &&
           VerifyOffsetRequired(verifier, VT_TYPE) &&
           VerifyType(verifier, type(), type_type()) &&
           VerifyOffsetRequired(verifier, VT_SHAPE) &&
           verifier.VerifyVector(shape()) &&
           verifier.VerifyVectorOfTables(shape()) &&
           VerifyOffset(verifier, VT_STRIDES) &&
           verifier.VerifyVector(strides()) &&
           VerifyFieldRequired<org::apache::arrow::flatbuf::Buffer>(verifier, VT_DATA) &&
           verifier.EndTable();
  }
};

template<> inline const org::apache::arrow::flatbuf::Null *Tensor::type_as<org::apache::arrow::flatbuf::Null>() const {
  return type_as_Null();
}

template<> inline const org::apache::arrow::flatbuf::Int *Tensor::type_as<org::apache::arrow::flatbuf::Int>() const {
  return type_as_Int();
}

template<> inline const org::apache::arrow::flatbuf::FloatingPoint *Tensor::type_as<org::apache::arrow::flatbuf::FloatingPoint>() const {
  return type_as_FloatingPoint();
}

template<> inline const org::apache::arrow::flatbuf::Binary *Tensor::type_as<org::apache::arrow::flatbuf::Binary>() const {
  return type_as_Binary();
}

template<> inline const org::apache::arrow::flatbuf::Utf8 *Tensor::type_as<org::apache::arrow::flatbuf::Utf8>() const {
  return type_as_Utf8();
}

template<> inline const org::apache::arrow::flatbuf::Bool *Tensor::type_as<org::apache::arrow::flatbuf::Bool>() const {
  return type_as_Bool();
}

template<> inline const org::apache::arrow::flatbuf::Decimal *Tensor::type_as<org::apache::arrow::flatbuf::Decimal>() const {
  return type_as_Decimal();
}

template<> inline const org::apache::arrow::flatbuf::Date *Tensor::type_as<org::apache::arrow::flatbuf::Date>() const {
  return type_as_Date();
}

template<> inline const org::apache::arrow::flatbuf::Time *Tensor::type_as<org::apache::arrow::flatbuf::Time>() const {
  return type_as_Time();
}

template<> inline const org::apache::arrow::flatbuf::Timestamp *Tensor::type_as<org::apache::arrow::flatbuf::Timestamp>() const {
  return type_as_Timestamp();
}

template<> inline const org::apache::arrow::flatbuf::Interval *Tensor::type_as<org::apache::arrow::flatbuf::Interval>() const {
  return type_as_Interval();
}

template<> inline const org::apache::arrow::flatbuf::List *Tensor::type_as<org::apache::arrow::flatbuf::List>() const {
  return type_as_List();
}

template<> inline const org::apache::arrow::flatbuf::Struct_ *Tensor::type_as<org::apache::arrow::flatbuf::Struct_>() const {
  return type_as_Struct_();
}

template<> inline const org::apache::arrow::flatbuf::Union *Tensor::type_as<org::apache::arrow::flatbuf::Union>() const {
  return type_as_Union();
}

template<> inline const org::apache::arrow::flatbuf::FixedSizeBinary *Tensor::type_as<org::apache::arrow::flatbuf::FixedSizeBinary>() const {
  return type_as_FixedSizeBinary();
}

template<> inline const org::apache::arrow::flatbuf::FixedSizeList *Tensor::type_as<org::apache::arrow::flatbuf::FixedSizeList>() const {
  return type_as_FixedSizeList();
}

template<> inline const org::apache::arrow::flatbuf::Map *Tensor::type_as<org::apache::arrow::flatbuf::Map>() const {
  return type_as_Map();
}

template<> inline const org::apache::arrow::flatbuf::Duration *Tensor::type_as<org::apache::arrow::flatbuf::Duration>() const {
  return type_as_Duration();
}

template<> inline const org::apache::arrow::flatbuf::LargeBinary *Tensor::type_as<org::apache::arrow::flatbuf::LargeBinary>() const {
  return type_as_LargeBinary();
}

template<> inline const org::apache::arrow::flatbuf::LargeUtf8 *Tensor::type_as<org::apache::arrow::flatbuf::LargeUtf8>() const {
  return type_as_LargeUtf8();
}

template<> inline const org::apache::arrow::flatbuf::LargeList *Tensor::type_as<org::apache::arrow::flatbuf::LargeList>() const {
  return type_as_LargeList();
}

template<> inline const org::apache::arrow::flatbuf::Complex *Tensor::type_as<org::apache::arrow::flatbuf::Complex>() const {
  return type_as_Complex();
}

struct TensorBuilder {
  typedef Tensor Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_type_type(org::apache::arrow::flatbuf::Type type_type) {
    fbb_.AddElement<uint8_t>(Tensor::VT_TYPE_TYPE, static_cast<uint8_t>(type_type), 0);
  }
  void add_type(flatbuffers::Offset<void> type) {
    fbb_.AddOffset(Tensor::VT_TYPE, type);
  }
  void add_shape(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<org::apache::arrow::flatbuf::TensorDim>>> shape) {
    fbb_.AddOffset(Tensor::VT_SHAPE, shape);
  }
  void add_strides(flatbuffers::Offset<flatbuffers::Vector<int64_t>> strides) {
    fbb_.AddOffset(Tensor::VT_STRIDES, strides);
  }
  void add_data(const org::apache::arrow::flatbuf::Buffer *data) {
    fbb_.AddStruct(Tensor::VT_DATA, data);
  }
  explicit TensorBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  flatbuffers::Offset<Tensor> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<Tensor>(end);
    fbb_.Required(o, Tensor::VT_TYPE);
    fbb_.Required(o, Tensor::VT_SHAPE);
    fbb_.Required(o, Tensor::VT_DATA);
    return o;
  }
};

inline flatbuffers::Offset<Tensor> CreateTensor(
    flatbuffers::FlatBufferBuilder &_fbb,
    org::apache::arrow::flatbuf::Type type_type = org::apache::arrow::flatbuf::Type::NONE,
    flatbuffers::Offset<void> type = 0,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<org::apache::arrow::flatbuf::TensorDim>>> shape = 0,
    flatbuffers::Offset<flatbuffers::Vector<int64_t>> strides = 0,
    const org::apache::arrow::flatbuf::Buffer *data = 0) {
  TensorBuilder builder_(_fbb);
  builder_.add_data(data);
  builder_.add_strides(strides);
  builder_.add_shape(shape);
  builder_.add_type(type);
  builder_.add_type_type(type_type);
  return builder_.Finish();
}

inline flatbuffers::Offset<Tensor> CreateTensorDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    org::apache::arrow::flatbuf::Type type_type = org::apache::arrow::flatbuf::Type::NONE,
    flatbuffers::Offset<void> type = 0,
    const std::vector<flatbuffers::Offset<org::apache::arrow::flatbuf::TensorDim>> *shape = nullptr,
    const std::vector<int64_t> *strides = nullptr,
    const org::apache::arrow::flatbuf::Buffer *data = 0) {
  auto shape__ = shape ? _fbb.CreateVector<flatbuffers::Offset<org::apache::arrow::flatbuf::TensorDim>>(*shape) : 0;
  auto strides__ = strides ? _fbb.CreateVector<int64_t>(*strides) : 0;
  return org::apache::arrow::flatbuf::CreateTensor(
      _fbb,
      type_type,
      type,
      shape__,
      strides__,
      data);
}

inline const org::apache::arrow::flatbuf::Tensor *GetTensor(const void *buf) {
  return flatbuffers::GetRoot<org::apache::arrow::flatbuf::Tensor>(buf);
}

inline const org::apache::arrow::flatbuf::Tensor *GetSizePrefixedTensor(const void *buf) {
  return flatbuffers::GetSizePrefixedRoot<org::apache::arrow::flatbuf::Tensor>(buf);
}

inline bool VerifyTensorBuffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<org::apache::arrow::flatbuf::Tensor>(nullptr);
}

inline bool VerifySizePrefixedTensorBuffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<org::apache::arrow::flatbuf::Tensor>(nullptr);
}

inline void FinishTensorBuffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<org::apache::arrow::flatbuf::Tensor> root) {
  fbb.Finish(root);
}

inline void FinishSizePrefixedTensorBuffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<org::apache::arrow::flatbuf::Tensor> root) {
  fbb.FinishSizePrefixed(root);
}

}  // namespace flatbuf
}  // namespace arrow
}  // namespace apache
}  // namespace org

#endif  // FLATBUFFERS_GENERATED_TENSOR_ORG_APACHE_ARROW_FLATBUF_H_
