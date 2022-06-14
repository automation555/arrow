// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "arrow/array.h"
#include "arrow/compute/api.h"
#include "arrow/compute/kernels/test_util.h"
#include "arrow/testing/builder.h"
#include "arrow/testing/gtest_util.h"
#include "arrow/testing/matchers.h"
#include "arrow/testing/random.h"
#include "arrow/type.h"
#include "arrow/type_traits.h"
#include "arrow/util/bitmap_reader.h"
#include "arrow/util/checked_cast.h"
#include "arrow/util/key_value_metadata.h"
#include "arrow/util/string_view.h"

namespace arrow {

using internal::BitmapReader;

namespace compute {

using util::string_view;

template <typename ArrowType>
static void ValidateCompare(CompareOptions options, const Datum& lhs, const Datum& rhs,
                            const Datum& expected) {
  ASSERT_OK_AND_ASSIGN(
      Datum result, CallFunction(CompareOperatorToFunctionName(options.op), {lhs, rhs}));
  if ((lhs.is_scalar()) && (rhs.is_scalar())) {
    AssertScalarsEqual(*expected.scalar(), *result.scalar(), /*verbose=*/true);
  } else {
    AssertArraysEqual(*expected.make_array(), *result.make_array(),
                      /*verbose=*/true);
  }
}

template <typename ArrowType>
static void ValidateCompare(CompareOptions options, const Datum& lhs, const Datum& rhs,
                            const char* expected_str) {
  if ((lhs.is_scalar()) && (rhs.is_scalar())) {
    auto expected =
        ScalarFromJSON(TypeTraits<BooleanType>::type_singleton(), expected_str);
    ValidateCompare<ArrowType>(options, lhs, rhs, expected);
  } else {
    auto expected =
        ArrayFromJSON(TypeTraits<BooleanType>::type_singleton(), expected_str);
    ValidateCompare<ArrowType>(options, lhs, rhs, expected);
  }
}

template <typename ArrowType>
static void ValidateCompare(CompareOptions options, const char* lhs_str, const Datum& rhs,
                            const char* expected_str) {
  auto lhs = ArrayFromJSON(TypeTraits<ArrowType>::type_singleton(), lhs_str);
  auto expected = ArrayFromJSON(TypeTraits<BooleanType>::type_singleton(), expected_str);
  ValidateCompare<ArrowType>(options, lhs, rhs, expected);
}

template <typename ArrowType>
static void ValidateCompare(CompareOptions options, const Datum& lhs, const char* rhs_str,
                            const char* expected_str) {
  auto rhs = ArrayFromJSON(TypeTraits<ArrowType>::type_singleton(), rhs_str);
  auto expected = ArrayFromJSON(TypeTraits<BooleanType>::type_singleton(), expected_str);
  ValidateCompare<ArrowType>(options, lhs, rhs, expected);
}

template <typename ArrowType>
static void ValidateCompare(CompareOptions options, const char* lhs_str,
                            const char* rhs_str, const char* expected_str) {
  auto lhs = ArrayFromJSON(TypeTraits<ArrowType>::type_singleton(), lhs_str);
  auto rhs = ArrayFromJSON(TypeTraits<ArrowType>::type_singleton(), rhs_str);
  auto expected = ArrayFromJSON(TypeTraits<BooleanType>::type_singleton(), expected_str);
  ValidateCompare<ArrowType>(options, lhs, rhs, expected);
}

template <typename T>
static inline bool SlowCompare(CompareOperator op, const T& lhs, const T& rhs) {
  switch (op) {
    case EQUAL:
      return lhs == rhs;
    case NOT_EQUAL:
      return lhs != rhs;
    case GREATER:
      return lhs > rhs;
    case GREATER_EQUAL:
      return lhs >= rhs;
    case LESS:
      return lhs < rhs;
    case LESS_EQUAL:
      return lhs <= rhs;
    default:
      return false;
  }
}

template <typename ArrowType>
Datum SimpleScalarArrayCompare(CompareOptions options, const Datum& lhs,
                               const Datum& rhs) {
  using ArrayType = typename TypeTraits<ArrowType>::ArrayType;
  using ScalarType = typename TypeTraits<ArrowType>::ScalarType;

  bool swap = lhs.is_array();
  auto array = std::static_pointer_cast<ArrayType>((swap ? lhs : rhs).make_array());
  auto value = std::static_pointer_cast<ScalarType>((swap ? rhs : lhs).scalar())->value;

  std::vector<bool> bitmap(array->length());
  for (int64_t i = 0; i < array->length(); i++) {
    bitmap[i] = swap ? SlowCompare(options.op, array->Value(i), value)
                     : SlowCompare(options.op, value, array->Value(i));
  }

  std::shared_ptr<Array> result;

  if (array->null_count() == 0) {
    ArrayFromVector<BooleanType>(bitmap, &result);
  } else {
    std::vector<bool> null_bitmap(array->length());
    auto reader =
        BitmapReader(array->null_bitmap_data(), array->offset(), array->length());
    for (int64_t i = 0; i < array->length(); i++, reader.Next()) {
      null_bitmap[i] = reader.IsSet();
    }
    ArrayFromVector<BooleanType>(null_bitmap, bitmap, &result);
  }

  return Datum(result);
}

template <>
Datum SimpleScalarArrayCompare<StringType>(CompareOptions options, const Datum& lhs,
                                           const Datum& rhs) {
  bool swap = lhs.is_array();
  auto array = std::static_pointer_cast<StringArray>((swap ? lhs : rhs).make_array());
  auto value = util::string_view(
      *std::static_pointer_cast<StringScalar>((swap ? rhs : lhs).scalar())->value);

  std::vector<bool> bitmap(array->length());
  for (int64_t i = 0; i < array->length(); i++) {
    bitmap[i] = swap ? SlowCompare(options.op, array->GetView(i), value)
                     : SlowCompare(options.op, value, array->GetView(i));
  }

  std::shared_ptr<Array> result;

  if (array->null_count() == 0) {
    ArrayFromVector<BooleanType>(bitmap, &result);
  } else {
    std::vector<bool> null_bitmap(array->length());
    auto reader =
        BitmapReader(array->null_bitmap_data(), array->offset(), array->length());
    for (int64_t i = 0; i < array->length(); i++, reader.Next()) {
      null_bitmap[i] = reader.IsSet();
    }
    ArrayFromVector<BooleanType>(null_bitmap, bitmap, &result);
  }

  return Datum(result);
}

template <typename ArrayType>
std::vector<bool> NullBitmapFromTwoArrays(const ArrayType& lhs, const ArrayType& rhs) {
  auto left_lambda = [&lhs](int64_t i) {
    return lhs.null_count() == 0 ? true : lhs.IsValid(i);
  };

  auto right_lambda = [&rhs](int64_t i) {
    return rhs.null_count() == 0 ? true : rhs.IsValid(i);
  };

  const int64_t length = lhs.length();
  std::vector<bool> null_bitmap(length);

  for (int64_t i = 0; i < length; i++) {
    null_bitmap[i] = left_lambda(i) && right_lambda(i);
  }

  return null_bitmap;
}

template <typename ArrowType>
Datum SimpleArrayArrayCompare(CompareOptions options, const Datum& lhs,
                              const Datum& rhs) {
  using ArrayType = typename TypeTraits<ArrowType>::ArrayType;

  auto l_array = std::static_pointer_cast<ArrayType>(lhs.make_array());
  auto r_array = std::static_pointer_cast<ArrayType>(rhs.make_array());
  const int64_t length = l_array->length();

  std::vector<bool> bitmap(length);
  for (int64_t i = 0; i < length; i++) {
    bitmap[i] = SlowCompare(options.op, l_array->Value(i), r_array->Value(i));
  }

  std::shared_ptr<Array> result;

  if (l_array->null_count() == 0 && r_array->null_count() == 0) {
    ArrayFromVector<BooleanType>(bitmap, &result);
  } else {
    std::vector<bool> null_bitmap = NullBitmapFromTwoArrays(*l_array, *r_array);
    ArrayFromVector<BooleanType>(null_bitmap, bitmap, &result);
  }

  return Datum(result);
}

template <>
Datum SimpleArrayArrayCompare<StringType>(CompareOptions options, const Datum& lhs,
                                          const Datum& rhs) {
  auto l_array = std::static_pointer_cast<StringArray>(lhs.make_array());
  auto r_array = std::static_pointer_cast<StringArray>(rhs.make_array());
  const int64_t length = l_array->length();

  std::vector<bool> bitmap(length);
  for (int64_t i = 0; i < length; i++) {
    bitmap[i] = SlowCompare(options.op, l_array->GetView(i), r_array->GetView(i));
  }

  std::shared_ptr<Array> result;

  if (l_array->null_count() == 0 && r_array->null_count() == 0) {
    ArrayFromVector<BooleanType>(bitmap, &result);
  } else {
    std::vector<bool> null_bitmap = NullBitmapFromTwoArrays(*l_array, *r_array);
    ArrayFromVector<BooleanType>(null_bitmap, bitmap, &result);
  }

  return Datum(result);
}

template <typename ArrowType>
void ValidateCompare(CompareOptions options, const Datum& lhs, const Datum& rhs) {
  bool has_scalar = lhs.is_scalar() || rhs.is_scalar();
  Datum expected = has_scalar ? SimpleScalarArrayCompare<ArrowType>(options, lhs, rhs)
                              : SimpleArrayArrayCompare<ArrowType>(options, lhs, rhs);

  ValidateCompare<ArrowType>(options, lhs, rhs, expected);
}

template <typename ArrowType>
class TestNumericCompareKernel : public ::testing::Test {};

TYPED_TEST_SUITE(TestNumericCompareKernel, NumericArrowTypes);
TYPED_TEST(TestNumericCompareKernel, SimpleCompareScalarScalar) {
  Datum one(ScalarFromJSON(TypeTraits<TypeParam>::type_singleton(), "1"));
  Datum two(ScalarFromJSON(TypeTraits<TypeParam>::type_singleton(), "2"));
  Datum null(ScalarFromJSON(TypeTraits<TypeParam>::type_singleton(), "null"));

  CompareOptions eq(CompareOperator::EQUAL);
  ValidateCompare<TypeParam>(eq, two, one, "0");
  ValidateCompare<TypeParam>(eq, one, one, "1");
  ValidateCompare<TypeParam>(eq, two, null, "null");
  ValidateCompare<TypeParam>(eq, null, one, "null");
  ValidateCompare<TypeParam>(eq, two, null, "null");
  ValidateCompare<TypeParam>(eq, null, one, "null");
  ValidateCompare<TypeParam>(eq, one, two, "0");

  CompareOptions neq(CompareOperator::NOT_EQUAL);
  ValidateCompare<TypeParam>(neq, two, one, "1");
  ValidateCompare<TypeParam>(neq, one, one, "0");
  ValidateCompare<TypeParam>(neq, two, null, "null");
  ValidateCompare<TypeParam>(neq, null, one, "null");
  ValidateCompare<TypeParam>(neq, one, two, "1");

  CompareOptions gt(CompareOperator::GREATER);
  ValidateCompare<TypeParam>(gt, two, one, "1");
  ValidateCompare<TypeParam>(gt, one, one, "0");
  ValidateCompare<TypeParam>(gt, two, null, "null");
  ValidateCompare<TypeParam>(gt, null, one, "null");
  ValidateCompare<TypeParam>(gt, one, two, "0");

  CompareOptions gte(CompareOperator::GREATER_EQUAL);
  ValidateCompare<TypeParam>(gte, two, one, "1");
  ValidateCompare<TypeParam>(gte, one, one, "1");
  ValidateCompare<TypeParam>(gte, two, null, "null");
  ValidateCompare<TypeParam>(gte, null, one, "null");
  ValidateCompare<TypeParam>(gte, one, two, "0");

  CompareOptions lt(CompareOperator::LESS);
  ValidateCompare<TypeParam>(lt, two, one, "0");
  ValidateCompare<TypeParam>(lt, one, one, "0");
  ValidateCompare<TypeParam>(lt, two, null, "null");
  ValidateCompare<TypeParam>(lt, null, one, "null");
  ValidateCompare<TypeParam>(lt, one, two, "1");

  CompareOptions lte(CompareOperator::LESS_EQUAL);
  ValidateCompare<TypeParam>(lte, two, one, "0");
  ValidateCompare<TypeParam>(lte, one, one, "1");
  ValidateCompare<TypeParam>(lte, two, null, "null");
  ValidateCompare<TypeParam>(lte, null, one, "null");
  ValidateCompare<TypeParam>(lte, one, two, "1");
}

TYPED_TEST(TestNumericCompareKernel, SimpleCompareArrayScalar) {
  Datum one(ScalarFromJSON(TypeTraits<TypeParam>::type_singleton(), "1"));

  CompareOptions eq(CompareOperator::EQUAL);
  ValidateCompare<TypeParam>(eq, "[]", one, "[]");
  ValidateCompare<TypeParam>(eq, "[null]", one, "[null]");
  ValidateCompare<TypeParam>(eq, "[0,0,1,1,2,2]", one, "[0,0,1,1,0,0]");
  ValidateCompare<TypeParam>(eq, "[0,1,2,3,4,5]", one, "[0,1,0,0,0,0]");
  ValidateCompare<TypeParam>(eq, "[5,4,3,2,1,0]", one, "[0,0,0,0,1,0]");
  ValidateCompare<TypeParam>(eq, "[null,0,1,1]", one, "[null,0,1,1]");

  CompareOptions neq(CompareOperator::NOT_EQUAL);
  ValidateCompare<TypeParam>(neq, "[]", one, "[]");
  ValidateCompare<TypeParam>(neq, "[null]", one, "[null]");
  ValidateCompare<TypeParam>(neq, "[0,0,1,1,2,2]", one, "[1,1,0,0,1,1]");
  ValidateCompare<TypeParam>(neq, "[0,1,2,3,4,5]", one, "[1,0,1,1,1,1]");
  ValidateCompare<TypeParam>(neq, "[5,4,3,2,1,0]", one, "[1,1,1,1,0,1]");
  ValidateCompare<TypeParam>(neq, "[null,0,1,1]", one, "[null,1,0,0]");

  CompareOptions gt(CompareOperator::GREATER);
  ValidateCompare<TypeParam>(gt, "[]", one, "[]");
  ValidateCompare<TypeParam>(gt, "[null]", one, "[null]");
  ValidateCompare<TypeParam>(gt, "[0,0,1,1,2,2]", one, "[0,0,0,0,1,1]");
  ValidateCompare<TypeParam>(gt, "[0,1,2,3,4,5]", one, "[0,0,1,1,1,1]");
  ValidateCompare<TypeParam>(gt, "[4,5,6,7,8,9]", one, "[1,1,1,1,1,1]");
  ValidateCompare<TypeParam>(gt, "[null,0,1,1]", one, "[null,0,0,0]");

  CompareOptions gte(CompareOperator::GREATER_EQUAL);
  ValidateCompare<TypeParam>(gte, "[]", one, "[]");
  ValidateCompare<TypeParam>(gte, "[null]", one, "[null]");
  ValidateCompare<TypeParam>(gte, "[0,0,1,1,2,2]", one, "[0,0,1,1,1,1]");
  ValidateCompare<TypeParam>(gte, "[0,1,2,3,4,5]", one, "[0,1,1,1,1,1]");
  ValidateCompare<TypeParam>(gte, "[4,5,6,7,8,9]", one, "[1,1,1,1,1,1]");
  ValidateCompare<TypeParam>(gte, "[null,0,1,1]", one, "[null,0,1,1]");

  CompareOptions lt(CompareOperator::LESS);
  ValidateCompare<TypeParam>(lt, "[]", one, "[]");
  ValidateCompare<TypeParam>(lt, "[null]", one, "[null]");
  ValidateCompare<TypeParam>(lt, "[0,0,1,1,2,2]", one, "[1,1,0,0,0,0]");
  ValidateCompare<TypeParam>(lt, "[0,1,2,3,4,5]", one, "[1,0,0,0,0,0]");
  ValidateCompare<TypeParam>(lt, "[4,5,6,7,8,9]", one, "[0,0,0,0,0,0]");
  ValidateCompare<TypeParam>(lt, "[null,0,1,1]", one, "[null,1,0,0]");

  CompareOptions lte(CompareOperator::LESS_EQUAL);
  ValidateCompare<TypeParam>(lte, "[]", one, "[]");
  ValidateCompare<TypeParam>(lte, "[null]", one, "[null]");
  ValidateCompare<TypeParam>(lte, "[0,0,1,1,2,2]", one, "[1,1,1,1,0,0]");
  ValidateCompare<TypeParam>(lte, "[0,1,2,3,4,5]", one, "[1,1,0,0,0,0]");
  ValidateCompare<TypeParam>(lte, "[4,5,6,7,8,9]", one, "[0,0,0,0,0,0]");
  ValidateCompare<TypeParam>(lte, "[null,0,1,1]", one, "[null,1,1,1]");
}

TYPED_TEST(TestNumericCompareKernel, SimpleCompareScalarArray) {
  Datum one(ScalarFromJSON(TypeTraits<TypeParam>::type_singleton(), "1"));

  CompareOptions eq(CompareOperator::EQUAL);
  ValidateCompare<TypeParam>(eq, one, "[]", "[]");
  ValidateCompare<TypeParam>(eq, one, "[null]", "[null]");
  ValidateCompare<TypeParam>(eq, one, "[0,0,1,1,2,2]", "[0,0,1,1,0,0]");
  ValidateCompare<TypeParam>(eq, one, "[0,1,2,3,4,5]", "[0,1,0,0,0,0]");
  ValidateCompare<TypeParam>(eq, one, "[5,4,3,2,1,0]", "[0,0,0,0,1,0]");
  ValidateCompare<TypeParam>(eq, one, "[null,0,1,1]", "[null,0,1,1]");

  CompareOptions neq(CompareOperator::NOT_EQUAL);
  ValidateCompare<TypeParam>(neq, one, "[]", "[]");
  ValidateCompare<TypeParam>(neq, one, "[null]", "[null]");
  ValidateCompare<TypeParam>(neq, one, "[0,0,1,1,2,2]", "[1,1,0,0,1,1]");
  ValidateCompare<TypeParam>(neq, one, "[0,1,2,3,4,5]", "[1,0,1,1,1,1]");
  ValidateCompare<TypeParam>(neq, one, "[5,4,3,2,1,0]", "[1,1,1,1,0,1]");
  ValidateCompare<TypeParam>(neq, one, "[null,0,1,1]", "[null,1,0,0]");

  CompareOptions gt(CompareOperator::GREATER);
  ValidateCompare<TypeParam>(gt, one, "[]", "[]");
  ValidateCompare<TypeParam>(gt, one, "[null]", "[null]");
  ValidateCompare<TypeParam>(gt, one, "[0,0,1,1,2,2]", "[1,1,0,0,0,0]");
  ValidateCompare<TypeParam>(gt, one, "[0,1,2,3,4,5]", "[1,0,0,0,0,0]");
  ValidateCompare<TypeParam>(gt, one, "[4,5,6,7,8,9]", "[0,0,0,0,0,0]");
  ValidateCompare<TypeParam>(gt, one, "[null,0,1,1]", "[null,1,0,0]");

  CompareOptions gte(CompareOperator::GREATER_EQUAL);
  ValidateCompare<TypeParam>(gte, one, "[]", "[]");
  ValidateCompare<TypeParam>(gte, one, "[null]", "[null]");
  ValidateCompare<TypeParam>(gte, one, "[0,0,1,1,2,2]", "[1,1,1,1,0,0]");
  ValidateCompare<TypeParam>(gte, one, "[0,1,2,3,4,5]", "[1,1,0,0,0,0]");
  ValidateCompare<TypeParam>(gte, one, "[4,5,6,7,8,9]", "[0,0,0,0,0,0]");
  ValidateCompare<TypeParam>(gte, one, "[null,0,1,1]", "[null,1,1,1]");

  CompareOptions lt(CompareOperator::LESS);
  ValidateCompare<TypeParam>(lt, one, "[]", "[]");
  ValidateCompare<TypeParam>(lt, one, "[null]", "[null]");
  ValidateCompare<TypeParam>(lt, one, "[0,0,1,1,2,2]", "[0,0,0,0,1,1]");
  ValidateCompare<TypeParam>(lt, one, "[0,1,2,3,4,5]", "[0,0,1,1,1,1]");
  ValidateCompare<TypeParam>(lt, one, "[4,5,6,7,8,9]", "[1,1,1,1,1,1]");
  ValidateCompare<TypeParam>(lt, one, "[null,0,1,1]", "[null,0,0,0]");

  CompareOptions lte(CompareOperator::LESS_EQUAL);
  ValidateCompare<TypeParam>(lte, one, "[]", "[]");
  ValidateCompare<TypeParam>(lte, one, "[null]", "[null]");
  ValidateCompare<TypeParam>(lte, one, "[0,0,1,1,2,2]", "[0,0,1,1,1,1]");
  ValidateCompare<TypeParam>(lte, one, "[0,1,2,3,4,5]", "[0,1,1,1,1,1]");
  ValidateCompare<TypeParam>(lte, one, "[4,5,6,7,8,9]", "[1,1,1,1,1,1]");
  ValidateCompare<TypeParam>(lte, one, "[null,0,1,1]", "[null,0,1,1]");
}

TYPED_TEST(TestNumericCompareKernel, TestNullScalar) {
  /* Ensure that null scalar broadcast to all null results. */
  Datum null(ScalarFromJSON(TypeTraits<TypeParam>::type_singleton(), "null"));
  EXPECT_FALSE(null.scalar()->is_valid);

  CompareOptions eq(CompareOperator::EQUAL);
  ValidateCompare<TypeParam>(eq, "[]", null, "[]");
  ValidateCompare<TypeParam>(eq, null, "[]", "[]");
  ValidateCompare<TypeParam>(eq, "[null]", null, "[null]");
  ValidateCompare<TypeParam>(eq, null, "[null]", "[null]");
  ValidateCompare<TypeParam>(eq, null, "[1,2,3]", "[null, null, null]");
}

TYPED_TEST_SUITE(TestNumericCompareKernel, NumericArrowTypes);

template <typename Type>
struct CompareRandomNumeric {
  static void Test(const std::shared_ptr<DataType>& type) {
    using ScalarType = typename TypeTraits<Type>::ScalarType;
    using CType = typename TypeTraits<Type>::CType;
    auto rand = random::RandomArrayGenerator(0x5416447);
    const int64_t length = 100;
    for (auto null_probability : {0.0, 0.01, 0.1, 0.25, 0.5, 1.0}) {
      for (auto op : {EQUAL, NOT_EQUAL, GREATER, LESS_EQUAL}) {
        auto data1 =
            rand.Numeric<typename Type::PhysicalType>(length, 0, 100, null_probability);
        auto data2 =
            rand.Numeric<typename Type::PhysicalType>(length, 0, 100, null_probability);

        // Create view of data as the type (e.g. timestamp)
        auto array1 = Datum(*data1->View(type));
        auto array2 = Datum(*data2->View(type));
        auto fifty = Datum(std::make_shared<ScalarType>(CType(50), type));
        auto options = CompareOptions(op);

        ValidateCompare<Type>(options, array1, fifty);
        ValidateCompare<Type>(options, fifty, array1);
        ValidateCompare<Type>(options, array1, array2);
      }
    }
  }
};

TEST(TestNumericCompareKernel, ComparePrimitiveRandomTests) {
  TestRandomPrimitiveCTypes<CompareRandomNumeric>();
}

TYPED_TEST(TestNumericCompareKernel, SimpleCompareArrayArray) {
  /* Ensure that null scalar broadcast to all null results. */
  CompareOptions eq(CompareOperator::EQUAL);
  ValidateCompare<TypeParam>(eq, "[]", "[]", "[]");
  ValidateCompare<TypeParam>(eq, "[null]", "[null]", "[null]");
  ValidateCompare<TypeParam>(eq, "[1]", "[1]", "[1]");
  ValidateCompare<TypeParam>(eq, "[1]", "[2]", "[0]");
  ValidateCompare<TypeParam>(eq, "[null]", "[1]", "[null]");
  ValidateCompare<TypeParam>(eq, "[1]", "[null]", "[null]");

  CompareOptions lte(CompareOperator::LESS_EQUAL);
  ValidateCompare<TypeParam>(lte, "[1,2,3,4,5]", "[2,3,4,5,6]", "[1,1,1,1,1]");
}

TEST(TestCompareTimestamps, Basics) {
  const char* example1_json = R"(["1970-01-01","2000-02-29","1900-02-28"])";
  const char* example2_json = R"(["1970-01-02","2000-02-01","1900-02-28"])";

  auto CheckArrayCase = [&](std::shared_ptr<DataType> type, CompareOperator op,
                            const char* expected_json) {
    auto lhs = ArrayFromJSON(type, example1_json);
    auto rhs = ArrayFromJSON(type, example2_json);
    auto expected = ArrayFromJSON(boolean(), expected_json);
    ASSERT_OK_AND_ASSIGN(Datum result,
                         CallFunction(CompareOperatorToFunctionName(op), {lhs, rhs}));
    AssertArraysEqual(*expected, *result.make_array(), /*verbose=*/true);
  };

  auto seconds = timestamp(TimeUnit::SECOND);
  auto millis = timestamp(TimeUnit::MILLI);
  auto micros = timestamp(TimeUnit::MICRO);
  auto nanos = timestamp(TimeUnit::NANO);

  CheckArrayCase(seconds, CompareOperator::EQUAL, "[false, false, true]");
  CheckArrayCase(seconds, CompareOperator::NOT_EQUAL, "[true, true, false]");
  CheckArrayCase(seconds, CompareOperator::LESS, "[true, false, false]");
  CheckArrayCase(seconds, CompareOperator::LESS_EQUAL, "[true, false, true]");
  CheckArrayCase(seconds, CompareOperator::GREATER, "[false, true, false]");
  CheckArrayCase(seconds, CompareOperator::GREATER_EQUAL, "[false, true, true]");

  // Check that comparisons with tz-aware timestamps work fine
  auto seconds_utc = timestamp(TimeUnit::SECOND, "utc");
  CheckArrayCase(seconds_utc, CompareOperator::EQUAL, "[false, false, true]");
}

TEST(TestCompareTimestamps, DifferentParameters) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"equal", "[0, 0, 1]"},   {"not_equal", "[1, 1, 0]"},
      {"less", "[1, 0, 0]"},    {"less_equal", "[1, 0, 1]"},
      {"greater", "[0, 1, 0]"}, {"greater_equal", "[0, 1, 1]"},
  };
  const std::string lhs_json = R"(["1970-01-01","2000-02-29","1900-02-28"])";
  const std::string rhs_json = R"(["1970-01-02","2000-02-01","1900-02-28"])";

  for (const auto& op : cases) {
    const auto& function = op.first;
    const auto& expected = op.second;

    SCOPED_TRACE(function);
    {
      // Different units should be fine
      auto lhs = ArrayFromJSON(timestamp(TimeUnit::SECOND), lhs_json);
      auto rhs = ArrayFromJSON(timestamp(TimeUnit::MILLI), rhs_json);
      CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
    }
    {
      // Different units should be fine
      auto lhs = ArrayFromJSON(timestamp(TimeUnit::SECOND), lhs_json);
      auto rhs = ArrayFromJSON(timestamp(TimeUnit::NANO), rhs_json);
      CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
    }
    {
      // So are different time zones
      auto lhs = ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/New_York"), lhs_json);
      auto rhs = ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/Phoenix"), rhs_json);
      CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
    }
    {
      // But comparing naive to zoned is not OK
      auto lhs = ArrayFromJSON(timestamp(TimeUnit::SECOND), lhs_json);
      auto rhs = ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/Phoenix"), rhs_json);
      EXPECT_RAISES_WITH_MESSAGE_THAT(
          TypeError,
          ::testing::HasSubstr(
              "Cannot compare timestamp with timezone to timestamp without timezone"),
          CallFunction(function, {lhs, rhs}));
    }
    {
      auto lhs = ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/New_York"), lhs_json);
      auto rhs = ArrayFromJSON(timestamp(TimeUnit::SECOND), rhs_json);
      EXPECT_RAISES_WITH_MESSAGE_THAT(
          TypeError,
          ::testing::HasSubstr(
              "Cannot compare timestamp with timezone to timestamp without timezone"),
          CallFunction(function, {lhs, rhs}));
    }
  }
}

template <typename ArrowType>
class TestCompareDecimal : public ::testing::Test {};
TYPED_TEST_SUITE(TestCompareDecimal, DecimalArrowTypes);

TYPED_TEST(TestCompareDecimal, ArrayScalar) {
  auto ty = std::make_shared<TypeParam>(3, 2);

  std::vector<std::pair<std::string, std::string>> cases = {
      {"equal", "[1, 0, 0, null]"},   {"not_equal", "[0, 1, 1, null]"},
      {"less", "[0, 0, 1, null]"},    {"less_equal", "[1, 0, 1, null]"},
      {"greater", "[0, 1, 0, null]"}, {"greater_equal", "[1, 1, 0, null]"},
  };

  auto lhs = ArrayFromJSON(ty, R"(["1.23", "2.34", "-1.23", null])");
  auto lhs_float = ArrayFromJSON(float64(), "[1.23, 2.34, -1.23, null]");
  auto lhs_intlike = ArrayFromJSON(ty, R"(["1.00", "2.00", "-1.00", null])");
  auto rhs = ScalarFromJSON(ty, R"("1.23")");
  auto rhs_float = ScalarFromJSON(float64(), "1.23");
  auto rhs_int = ScalarFromJSON(int64(), "1");
  for (const auto& op : cases) {
    const auto& function = op.first;
    const auto& expected = op.second;

    SCOPED_TRACE(function);
    CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs_float, rhs, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs, rhs_float, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs_intlike, rhs_int, ArrayFromJSON(boolean(), expected));
  }
}

TYPED_TEST(TestCompareDecimal, ScalarArray) {
  auto ty = std::make_shared<TypeParam>(3, 2);

  std::vector<std::pair<std::string, std::string>> cases = {
      {"equal", "[1, 0, 0, null]"},   {"not_equal", "[0, 1, 1, null]"},
      {"less", "[0, 1, 0, null]"},    {"less_equal", "[1, 1, 0, null]"},
      {"greater", "[0, 0, 1, null]"}, {"greater_equal", "[1, 0, 1, null]"},
  };

  auto lhs = ScalarFromJSON(ty, R"("1.23")");
  auto lhs_float = ScalarFromJSON(float64(), "1.23");
  auto lhs_int = ScalarFromJSON(int64(), "1");
  auto rhs = ArrayFromJSON(ty, R"(["1.23", "2.34", "-1.23", null])");
  auto rhs_float = ArrayFromJSON(float64(), "[1.23, 2.34, -1.23, null]");
  auto rhs_intlike = ArrayFromJSON(ty, R"(["1.00", "2.00", "-1.00", null])");
  for (const auto& op : cases) {
    const auto& function = op.first;
    const auto& expected = op.second;

    SCOPED_TRACE(function);
    CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs_float, rhs, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs, rhs_float, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs_int, rhs_intlike, ArrayFromJSON(boolean(), expected));
  }
}

TYPED_TEST(TestCompareDecimal, ArrayArray) {
  auto ty = std::make_shared<TypeParam>(3, 2);

  std::vector<std::pair<std::string, std::string>> cases = {
      {"equal", "[1, 0, 0, 1, 0, 0, null, null]"},
      {"not_equal", "[0, 1, 1, 0, 1, 1, null, null]"},
      {"less", "[0, 1, 0, 0, 1, 0, null, null]"},
      {"less_equal", "[1, 1, 0, 1, 1, 0, null, null]"},
      {"greater", "[0, 0, 1, 0, 0, 1, null, null]"},
      {"greater_equal", "[1, 0, 1, 1, 0, 1, null, null]"},
  };

  auto lhs = ArrayFromJSON(
      ty, R"(["1.23", "1.23", "2.34", "-1.23", "-1.23", "1.23", "1.23", null])");
  auto lhs_float =
      ArrayFromJSON(float64(), "[1.23, 1.23, 2.34, -1.23, -1.23, 1.23, 1.23, null]");
  auto lhs_intlike = ArrayFromJSON(
      ty, R"(["1.00", "1.00", "2.00", "-1.00", "-1.00", "1.00", "1.00", null])");
  auto rhs = ArrayFromJSON(
      ty, R"(["1.23", "2.34", "1.23", "-1.23", "1.23", "-1.23", null, "1.23"])");
  auto rhs_float =
      ArrayFromJSON(float64(), "[1.23, 2.34, 1.23, -1.23, 1.23, -1.23, null, 1.23]");
  auto rhs_int = ArrayFromJSON(int64(), "[1, 2, 1, -1, 1, -1, null, 1]");
  for (const auto& op : cases) {
    const auto& function = op.first;
    const auto& expected = op.second;

    SCOPED_TRACE(function);
    CheckScalarBinary(function, ArrayFromJSON(ty, R"([])"), ArrayFromJSON(ty, R"([])"),
                      ArrayFromJSON(boolean(), "[]"));
    CheckScalarBinary(function, ArrayFromJSON(ty, R"([null])"),
                      ArrayFromJSON(ty, R"([null])"), ArrayFromJSON(boolean(), "[null]"));
    CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs_float, rhs, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs, rhs_float, ArrayFromJSON(boolean(), expected));
    CheckScalarBinary(function, lhs_intlike, rhs_int, ArrayFromJSON(boolean(), expected));
  }
}

TYPED_TEST(TestCompareDecimal, DifferentParameters) {
  auto ty1 = std::make_shared<TypeParam>(3, 2);
  auto ty2 = std::make_shared<TypeParam>(4, 3);

  std::vector<std::pair<std::string, std::string>> cases = {
      {"equal", "[1, 0, 0, 1, 0, 0]"},   {"not_equal", "[0, 1, 1, 0, 1, 1]"},
      {"less", "[0, 1, 0, 0, 1, 0]"},    {"less_equal", "[1, 1, 0, 1, 1, 0]"},
      {"greater", "[0, 0, 1, 0, 0, 1]"}, {"greater_equal", "[1, 0, 1, 1, 0, 1]"},
  };

  auto lhs = ArrayFromJSON(ty1, R"(["1.23", "1.23", "2.34", "-1.23", "-1.23", "1.23"])");
  auto rhs =
      ArrayFromJSON(ty2, R"(["1.230", "2.340", "1.230", "-1.230", "1.230", "-1.230"])");
  for (const auto& op : cases) {
    const auto& function = op.first;
    const auto& expected = op.second;

    SCOPED_TRACE(function);
    CheckScalarBinary(function, lhs, rhs, ArrayFromJSON(boolean(), expected));
  }
}

// Helper to organize tests for fixed size binary comparisons
struct CompareCase {
  std::shared_ptr<DataType> lhs_type;
  std::shared_ptr<DataType> rhs_type;
  std::string lhs;
  std::string rhs;
  // An index into cases[...].second
  int result_index;
};

TEST(TestCompareFixedSizeBinary, ArrayScalar) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
      std::make_pair("equal",
                     std::vector<std::string>{
                         "[0, 1, 0, null]",
                         "[0, 0, 0, null]",
                         "[0, 0, 0, null]",
                     }),
      std::make_pair("not_equal",
                     std::vector<std::string>{
                         "[1, 0, 1, null]",
                         "[1, 1, 1, null]",
                         "[1, 1, 1, null]",
                     }),
      std::make_pair("less",
                     std::vector<std::string>{
                         "[1, 0, 0, null]",
                         "[1, 1, 1, null]",
                         "[1, 0, 0, null]",
                     }),
      std::make_pair("less_equal",
                     std::vector<std::string>{
                         "[1, 1, 0, null]",
                         "[1, 1, 1, null]",
                         "[1, 0, 0, null]",
                     }),
      std::make_pair("greater",
                     std::vector<std::string>{
                         "[0, 0, 1, null]",
                         "[0, 0, 0, null]",
                         "[0, 1, 1, null]",
                     }),
      std::make_pair("greater_equal",
                     std::vector<std::string>{
                         "[0, 1, 1, null]",
                         "[0, 0, 0, null]",
                         "[0, 1, 1, null]",
                     }),
  };

  const std::string lhs1 = R"(["aba", "abc", "abd", null])";
  const std::string rhs1 = R"("abc")";
  const std::string lhs2 = R"(["a", "b", "c", null])";
  const std::string rhs2 = R"("b")";

  std::vector<CompareCase> types = {
      {ty1, ty1, lhs1, rhs1, 0},
      {ty2, ty2, lhs2, rhs2, 0},
      {ty1, ty2, lhs1, rhs2, 1},
      {ty2, ty1, lhs2, rhs1, 2},
      {ty1, binary(), lhs1, rhs1, 0},
      {binary(), ty1, lhs1, rhs1, 0},
      {ty1, large_binary(), lhs1, rhs1, 0},
      {large_binary(), ty1, lhs1, rhs1, 0},
      {ty1, utf8(), lhs1, rhs1, 0},
      {utf8(), ty1, lhs1, rhs1, 0},
      {ty1, large_utf8(), lhs1, rhs1, 0},
      {large_utf8(), ty1, lhs1, rhs1, 0},
  };

  for (const auto& op : cases) {
    const auto& function = op.first;

    SCOPED_TRACE(function);
    for (const auto& test_case : types) {
      const auto& lhs_type = test_case.lhs_type;
      const auto& rhs_type = test_case.rhs_type;
      auto lhs = ArrayFromJSON(lhs_type, test_case.lhs);
      auto rhs = ScalarFromJSON(rhs_type, test_case.rhs);
      auto expected = ArrayFromJSON(boolean(), op.second[test_case.result_index]);

      CheckScalarBinary(function, ArrayFromJSON(lhs_type, R"([null])"),
                        ScalarFromJSON(rhs_type, "null"),
                        ArrayFromJSON(boolean(), "[null]"));
      CheckScalarBinary(function, lhs, rhs, expected);
    }
  }
}

TEST(TestCompareFixedSizeBinary, ScalarArray) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
      std::make_pair("equal",
                     std::vector<std::string>{
                         "[0, 1, 0, null]",
                         "[0, 0, 0, null]",
                         "[0, 0, 0, null]",
                     }),
      std::make_pair("not_equal",
                     std::vector<std::string>{
                         "[1, 0, 1, null]",
                         "[1, 1, 1, null]",
                         "[1, 1, 1, null]",
                     }),
      std::make_pair("less",
                     std::vector<std::string>{
                         "[0, 0, 1, null]",
                         "[0, 1, 1, null]",
                         "[0, 0, 0, null]",
                     }),
      std::make_pair("less_equal",
                     std::vector<std::string>{
                         "[0, 1, 1, null]",
                         "[0, 1, 1, null]",
                         "[0, 0, 0, null]",
                     }),
      std::make_pair("greater",
                     std::vector<std::string>{
                         "[1, 0, 0, null]",
                         "[1, 0, 0, null]",
                         "[1, 1, 1, null]",
                     }),
      std::make_pair("greater_equal",
                     std::vector<std::string>{
                         "[1, 1, 0, null]",
                         "[1, 0, 0, null]",
                         "[1, 1, 1, null]",
                     }),
  };

  const std::string lhs1 = R"("abc")";
  const std::string rhs1 = R"(["aba", "abc", "abd", null])";
  const std::string lhs2 = R"("b")";
  const std::string rhs2 = R"(["a", "b", "c", null])";

  std::vector<CompareCase> types = {
      {ty1, ty1, lhs1, rhs1, 0},
      {ty2, ty2, lhs2, rhs2, 0},
      {ty1, ty2, lhs1, rhs2, 1},
      {ty2, ty1, lhs2, rhs1, 2},
      {ty1, binary(), lhs1, rhs1, 0},
      {binary(), ty1, lhs1, rhs1, 0},
      {ty1, large_binary(), lhs1, rhs1, 0},
      {large_binary(), ty1, lhs1, rhs1, 0},
      {ty1, utf8(), lhs1, rhs1, 0},
      {utf8(), ty1, lhs1, rhs1, 0},
      {ty1, large_utf8(), lhs1, rhs1, 0},
      {large_utf8(), ty1, lhs1, rhs1, 0},
  };

  for (const auto& op : cases) {
    const auto& function = op.first;

    SCOPED_TRACE(function);
    for (const auto& test_case : types) {
      const auto& lhs_type = test_case.lhs_type;
      const auto& rhs_type = test_case.rhs_type;
      auto lhs = ScalarFromJSON(lhs_type, test_case.lhs);
      auto rhs = ArrayFromJSON(rhs_type, test_case.rhs);
      auto expected = ArrayFromJSON(boolean(), op.second[test_case.result_index]);

      CheckScalarBinary(function, ScalarFromJSON(rhs_type, "null"),
                        ArrayFromJSON(lhs_type, R"([null])"),
                        ArrayFromJSON(boolean(), "[null]"));
      CheckScalarBinary(function, lhs, rhs, expected);
    }
  }
}

TEST(TestCompareFixedSizeBinary, ArrayArray) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
      std::make_pair("equal",
                     std::vector<std::string>{
                         "[1, 0, 0, null, null]",
                         "[1, 0, 0, null, null]",
                         "[1, 0, 0, null, null]",
                         "[1, 0, 0, null, null]",
                         "[0, 0, 0, null, null]",
                         "[0, 0, 0, null, null]",
                     }),
      std::make_pair("not_equal",
                     std::vector<std::string>{
                         "[0, 1, 1, null, null]",
                         "[0, 1, 1, null, null]",
                         "[0, 1, 1, null, null]",
                         "[0, 1, 1, null, null]",
                         "[1, 1, 1, null, null]",
                         "[1, 1, 1, null, null]",
                     }),
      std::make_pair("less",
                     std::vector<std::string>{
                         "[0, 1, 0, null, null]",
                         "[0, 0, 1, null, null]",
                         "[0, 1, 0, null, null]",
                         "[0, 0, 1, null, null]",
                         "[0, 1, 1, null, null]",
                         "[1, 1, 0, null, null]",
                     }),
      std::make_pair("less_equal",
                     std::vector<std::string>{
                         "[1, 1, 0, null, null]",
                         "[1, 0, 1, null, null]",
                         "[1, 1, 0, null, null]",
                         "[1, 0, 1, null, null]",
                         "[0, 1, 1, null, null]",
                         "[1, 1, 0, null, null]",
                     }),
      std::make_pair("greater",
                     std::vector<std::string>{
                         "[0, 0, 1, null, null]",
                         "[0, 1, 0, null, null]",
                         "[0, 0, 1, null, null]",
                         "[0, 1, 0, null, null]",
                         "[1, 0, 0, null, null]",
                         "[0, 0, 1, null, null]",
                     }),
      std::make_pair("greater_equal",
                     std::vector<std::string>{
                         "[1, 0, 1, null, null]",
                         "[1, 1, 0, null, null]",
                         "[1, 0, 1, null, null]",
                         "[1, 1, 0, null, null]",
                         "[1, 0, 0, null, null]",
                         "[0, 0, 1, null, null]",
                     }),
  };

  const std::string lhs1 = R"(["abc", "abc", "abd", null, "abc"])";
  const std::string rhs1 = R"(["abc", "abd", "abc", "abc", null])";
  const std::string lhs2 = R"(["a", "a", "d", null, "a"])";
  const std::string rhs2 = R"(["a", "d", "c", "a", null])";

  std::vector<CompareCase> types = {
      {ty1, ty1, lhs1, rhs1, 0},
      {ty1, ty1, rhs1, lhs1, 1},
      {ty2, ty2, lhs2, rhs2, 2},
      {ty2, ty2, rhs2, lhs2, 3},
      {ty1, ty2, lhs1, rhs2, 4},
      {ty2, ty1, lhs2, rhs1, 5},
      {ty1, binary(), lhs1, rhs1, 0},
      {binary(), ty1, lhs1, rhs1, 0},
      {ty1, large_binary(), lhs1, rhs1, 0},
      {large_binary(), ty1, lhs1, rhs1, 0},
      {ty1, utf8(), lhs1, rhs1, 0},
      {utf8(), ty1, lhs1, rhs1, 0},
      {ty1, large_utf8(), lhs1, rhs1, 0},
      {large_utf8(), ty1, lhs1, rhs1, 0},
  };

  for (const auto& op : cases) {
    const auto& function = op.first;

    SCOPED_TRACE(function);
    for (const auto& test_case : types) {
      const auto& lhs_type = test_case.lhs_type;
      const auto& rhs_type = test_case.rhs_type;
      auto lhs = ArrayFromJSON(lhs_type, test_case.lhs);
      auto rhs = ArrayFromJSON(rhs_type, test_case.rhs);
      auto expected = ArrayFromJSON(boolean(), op.second[test_case.result_index]);

      CheckScalarBinary(function, ArrayFromJSON(lhs_type, R"([])"),
                        ArrayFromJSON(rhs_type, R"([])"), ArrayFromJSON(boolean(), "[]"));
      CheckScalarBinary(function, ArrayFromJSON(lhs_type, R"([null])"),
                        ArrayFromJSON(rhs_type, R"([null])"),
                        ArrayFromJSON(boolean(), "[null]"));
      CheckScalarBinary(function, lhs, rhs, expected);
    }
  }
}

TEST(TestCompareKernel, DispatchBest) {
  for (std::string name :
       {"equal", "not_equal", "less", "less_equal", "greater", "greater_equal"}) {
    CheckDispatchBest(name, {int32(), int32()}, {int32(), int32()});
    CheckDispatchBest(name, {int32(), null()}, {int32(), int32()});
    CheckDispatchBest(name, {null(), int32()}, {int32(), int32()});

    CheckDispatchBest(name, {int32(), int8()}, {int32(), int32()});
    CheckDispatchBest(name, {int32(), int16()}, {int32(), int32()});
    CheckDispatchBest(name, {int32(), int32()}, {int32(), int32()});
    CheckDispatchBest(name, {int32(), int64()}, {int64(), int64()});

    CheckDispatchBest(name, {int32(), uint8()}, {int32(), int32()});
    CheckDispatchBest(name, {int32(), uint16()}, {int32(), int32()});
    CheckDispatchBest(name, {int32(), uint32()}, {int64(), int64()});
    CheckDispatchBest(name, {int32(), uint64()}, {int64(), int64()});

    CheckDispatchBest(name, {uint8(), uint8()}, {uint8(), uint8()});
    CheckDispatchBest(name, {uint8(), uint16()}, {uint16(), uint16()});

    CheckDispatchBest(name, {int32(), float32()}, {float32(), float32()});
    CheckDispatchBest(name, {float32(), int64()}, {float32(), float32()});
    CheckDispatchBest(name, {float64(), int32()}, {float64(), float64()});

    CheckDispatchBest(name, {dictionary(int8(), float64()), float64()},
                      {float64(), float64()});
    CheckDispatchBest(name, {dictionary(int8(), float64()), int16()},
                      {float64(), float64()});

    CheckDispatchBest(name, {timestamp(TimeUnit::MICRO), date64()},
                      {timestamp(TimeUnit::MICRO), timestamp(TimeUnit::MICRO)});

    CheckDispatchBest(name, {timestamp(TimeUnit::MILLI), timestamp(TimeUnit::MICRO)},
                      {timestamp(TimeUnit::MICRO), timestamp(TimeUnit::MICRO)});

    CheckDispatchBest(name, {utf8(), binary()}, {binary(), binary()});
    CheckDispatchBest(name, {large_utf8(), binary()}, {large_binary(), large_binary()});
    CheckDispatchBest(name, {large_utf8(), fixed_size_binary(2)},
                      {large_binary(), large_binary()});
    CheckDispatchBest(name, {binary(), fixed_size_binary(2)}, {binary(), binary()});
    CheckDispatchBest(name, {fixed_size_binary(4), fixed_size_binary(2)},
                      {fixed_size_binary(4), fixed_size_binary(2)});

    CheckDispatchBest(name, {decimal128(3, 2), decimal128(6, 3)},
                      {decimal128(4, 3), decimal128(6, 3)});
    CheckDispatchBest(name, {decimal128(3, 2), decimal256(3, 2)},
                      {decimal256(3, 2), decimal256(3, 2)});
    CheckDispatchBest(name, {decimal128(3, 2), float64()}, {float64(), float64()});
    CheckDispatchBest(name, {float64(), decimal128(3, 2)}, {float64(), float64()});
    CheckDispatchBest(name, {decimal128(3, 2), int64()},
                      {decimal128(3, 2), decimal128(21, 2)});
    CheckDispatchBest(name, {int64(), decimal128(3, 2)},
                      {decimal128(21, 2), decimal128(3, 2)});
  }
}

TEST(TestCompareKernel, GreaterWithImplicitCasts) {
  CheckScalarBinary("greater", ArrayFromJSON(int32(), "[0, 1, 2, null]"),
                    ArrayFromJSON(float64(), "[0.5, 1.0, 1.5, 2.0]"),
                    ArrayFromJSON(boolean(), "[false, false, true, null]"));

  CheckScalarBinary("greater", ArrayFromJSON(int8(), "[-16, 0, 16, null]"),
                    ArrayFromJSON(uint32(), "[3, 4, 5, 7]"),
                    ArrayFromJSON(boolean(), "[false, false, true, null]"));

  CheckScalarBinary("greater", ArrayFromJSON(int8(), "[-16, 0, 16, null]"),
                    ArrayFromJSON(uint8(), "[255, 254, 1, 0]"),
                    ArrayFromJSON(boolean(), "[false, false, true, null]"));

  CheckScalarBinary("greater",
                    ArrayFromJSON(dictionary(int32(), int32()), "[0, 1, 2, null]"),
                    ArrayFromJSON(uint32(), "[3, 4, 5, 7]"),
                    ArrayFromJSON(boolean(), "[false, false, false, null]"));

  CheckScalarBinary("greater", ArrayFromJSON(int32(), "[0, 1, 2, null]"),
                    std::make_shared<NullArray>(4),
                    ArrayFromJSON(boolean(), "[null, null, null, null]"));

  CheckScalarBinary("greater",
                    ArrayFromJSON(timestamp(TimeUnit::SECOND),
                                  R"(["1970-01-01","2000-02-29","1900-02-28"])"),
                    ArrayFromJSON(date64(), "[86400000, 0, 86400000]"),
                    ArrayFromJSON(boolean(), "[false, true, false]"));

  CheckScalarBinary("greater",
                    ArrayFromJSON(dictionary(int32(), int8()), "[3, -3, -28, null]"),
                    ArrayFromJSON(uint32(), "[3, 4, 5, 7]"),
                    ArrayFromJSON(boolean(), "[false, false, false, null]"));
}

TEST(TestCompareKernel, GreaterWithImplicitCastsUint64EdgeCase) {
  // int64 is as wide as we can promote
  CheckDispatchBest("greater", {int8(), uint64()}, {int64(), int64()});

  // this works sometimes
  CheckScalarBinary("greater", ArrayFromJSON(int8(), "[-1]"),
                    ArrayFromJSON(uint64(), "[0]"), ArrayFromJSON(boolean(), "[false]"));

  // ... but it can result in impossible implicit casts in  the presence of uint64, since
  // some uint64 values cannot be cast to int64:
  ASSERT_RAISES(
      Invalid,
      CallFunction("greater", {ArrayFromJSON(int64(), "[-1]"),
                               ArrayFromJSON(uint64(), "[18446744073709551615]")}));
}

class TestStringCompareKernel : public ::testing::Test {};

TEST(TestStringCompareKernel, SimpleCompareArrayScalar) {
  Datum one(std::make_shared<StringScalar>("one"));

  CompareOptions eq(CompareOperator::EQUAL);
  ValidateCompare<StringType>(eq, "[]", one, "[]");
  ValidateCompare<StringType>(eq, "[null]", one, "[null]");
  ValidateCompare<StringType>(eq, R"(["zero","zero","one","one","two","two"])", one,
                              "[0,0,1,1,0,0]");
  ValidateCompare<StringType>(eq, R"(["zero","one","two","three","four","five"])", one,
                              "[0,1,0,0,0,0]");
  ValidateCompare<StringType>(eq, R"(["five","four","three","two","one","zero"])", one,
                              "[0,0,0,0,1,0]");
  ValidateCompare<StringType>(eq, R"([null,"zero","one","one"])", one, "[null,0,1,1]");

  Datum na(std::make_shared<StringScalar>());
  ValidateCompare<StringType>(eq, R"([null,"zero","one","one"])", na,
                              "[null,null,null,null]");
  ValidateCompare<StringType>(eq, na, R"([null,"zero","one","one"])",
                              "[null,null,null,null]");

  CompareOptions neq(CompareOperator::NOT_EQUAL);
  ValidateCompare<StringType>(neq, "[]", one, "[]");
  ValidateCompare<StringType>(neq, "[null]", one, "[null]");
  ValidateCompare<StringType>(neq, R"(["zero","zero","one","one","two","two"])", one,
                              "[1,1,0,0,1,1]");
  ValidateCompare<StringType>(neq, R"(["zero","one","two","three","four","five"])", one,
                              "[1,0,1,1,1,1]");
  ValidateCompare<StringType>(neq, R"(["five","four","three","two","one","zero"])", one,
                              "[1,1,1,1,0,1]");
  ValidateCompare<StringType>(neq, R"([null,"zero","one","one"])", one, "[null,1,0,0]");

  CompareOptions gt(CompareOperator::GREATER);
  ValidateCompare<StringType>(gt, "[]", one, "[]");
  ValidateCompare<StringType>(gt, "[null]", one, "[null]");
  ValidateCompare<StringType>(gt, R"(["zero","zero","one","one","two","two"])", one,
                              "[1,1,0,0,1,1]");
  ValidateCompare<StringType>(gt, R"(["zero","one","two","three","four","five"])", one,
                              "[1,0,1,1,0,0]");
  ValidateCompare<StringType>(gt, R"(["four","five","six","seven","eight","nine"])", one,
                              "[0,0,1,1,0,0]");
  ValidateCompare<StringType>(gt, R"([null,"zero","one","one"])", one, "[null,1,0,0]");

  CompareOptions gte(CompareOperator::GREATER_EQUAL);
  ValidateCompare<StringType>(gte, "[]", one, "[]");
  ValidateCompare<StringType>(gte, "[null]", one, "[null]");
  ValidateCompare<StringType>(gte, R"(["zero","zero","one","one","two","two"])", one,
                              "[1,1,1,1,1,1]");
  ValidateCompare<StringType>(gte, R"(["zero","one","two","three","four","five"])", one,
                              "[1,1,1,1,0,0]");
  ValidateCompare<StringType>(gte, R"(["four","five","six","seven","eight","nine"])", one,
                              "[0,0,1,1,0,0]");
  ValidateCompare<StringType>(gte, R"([null,"zero","one","one"])", one, "[null,1,1,1]");

  CompareOptions lt(CompareOperator::LESS);
  ValidateCompare<StringType>(lt, "[]", one, "[]");
  ValidateCompare<StringType>(lt, "[null]", one, "[null]");
  ValidateCompare<StringType>(lt, R"(["zero","zero","one","one","two","two"])", one,
                              "[0,0,0,0,0,0]");
  ValidateCompare<StringType>(lt, R"(["zero","one","two","three","four","five"])", one,
                              "[0,0,0,0,1,1]");
  ValidateCompare<StringType>(lt, R"(["four","five","six","seven","eight","nine"])", one,
                              "[1,1,0,0,1,1]");
  ValidateCompare<StringType>(lt, R"([null,"zero","one","one"])", one, "[null,0,0,0]");

  CompareOptions lte(CompareOperator::LESS_EQUAL);
  ValidateCompare<StringType>(lte, "[]", one, "[]");
  ValidateCompare<StringType>(lte, "[null]", one, "[null]");
  ValidateCompare<StringType>(lte, R"(["zero","zero","one","one","two","two"])", one,
                              "[0,0,1,1,0,0]");
  ValidateCompare<StringType>(lte, R"(["zero","one","two","three","four","five"])", one,
                              "[0,1,0,0,1,1]");
  ValidateCompare<StringType>(lte, R"(["four","five","six","seven","eight","nine"])", one,
                              "[1,1,0,0,1,1]");
  ValidateCompare<StringType>(lte, R"([null,"zero","one","one"])", one, "[null,0,1,1]");
}

TEST(TestStringCompareKernel, RandomCompareArrayScalar) {
  using ScalarType = typename TypeTraits<StringType>::ScalarType;

  auto rand = random::RandomArrayGenerator(0x5416447);
  for (size_t i = 3; i < 10; i++) {
    for (auto null_probability : {0.0, 0.01, 0.1, 0.25, 0.5, 1.0}) {
      for (auto op : {EQUAL, NOT_EQUAL, GREATER, LESS_EQUAL}) {
        const int64_t length = static_cast<int64_t>(1ULL << i);
        auto array = Datum(rand.String(length, 0, 16, null_probability));
        auto hello = Datum(std::make_shared<ScalarType>("hello"));
        auto options = CompareOptions(op);
        ValidateCompare<StringType>(options, array, hello);
        ValidateCompare<StringType>(options, hello, array);
      }
    }
  }
}

TEST(TestStringCompareKernel, RandomCompareArrayArray) {
  auto rand = random::RandomArrayGenerator(0x5416447);
  for (size_t i = 3; i < 5; i++) {
    for (auto null_probability : {0.0, 0.01, 0.1, 0.25, 0.5, 1.0}) {
      for (auto op : {EQUAL, NOT_EQUAL, GREATER, LESS_EQUAL}) {
        auto length = static_cast<int64_t>(1ULL << i);
        auto lhs = Datum(rand.String(length << i, 0, 16, null_probability));
        auto rhs = Datum(rand.String(length << i, 0, 16, null_probability));
        auto options = CompareOptions(op);
        ValidateCompare<StringType>(options, lhs, rhs);
      }
    }
  }
}

template <typename T>
class TestVarArgsCompare : public ::testing::Test {
 protected:
  static std::shared_ptr<DataType> type_singleton() {
    return TypeTraits<T>::type_singleton();
  }

  using VarArgsFunction = std::function<Result<Datum>(
      const std::vector<Datum>&, ElementWiseAggregateOptions, ExecContext*)>;

  Datum scalar(const std::string& value) {
    return ScalarFromJSON(type_singleton(), value);
  }

  Datum array(const std::string& value) { return ArrayFromJSON(type_singleton(), value); }

  Datum Eval(VarArgsFunction func, const std::vector<Datum>& args) {
    EXPECT_OK_AND_ASSIGN(auto actual,
                         func(args, element_wise_aggregate_options_, nullptr));
    ValidateOutput(actual);
    return actual;
  }

  void AssertNullScalar(VarArgsFunction func, const std::vector<Datum>& args) {
    auto datum = this->Eval(func, args);
    ASSERT_TRUE(datum.is_scalar());
    ASSERT_FALSE(datum.scalar()->is_valid);
  }

  void Assert(VarArgsFunction func, Datum expected, const std::vector<Datum>& args) {
    auto actual = Eval(func, args);
    AssertDatumsApproxEqual(expected, actual, /*verbose=*/true, equal_options_);
  }

  void SetSignedZerosEqual(bool v) {
    equal_options_ = equal_options_.signed_zeros_equal(v);
  }

  ElementWiseAggregateOptions element_wise_aggregate_options_;
  EqualOptions equal_options_ = EqualOptions::Defaults().nans_equal(true);
};

template <typename T>
class TestVarArgsCompareNumeric : public TestVarArgsCompare<T> {};

template <typename T>
class TestVarArgsCompareDecimal : public TestVarArgsCompare<T> {
 protected:
  Datum scalar(const std::string& value, int32_t precision = 38, int32_t scale = 2) {
    return ScalarFromJSON(std::make_shared<T>(/*precision=*/precision, /*scale=*/scale),
                          value);
  }

  Datum array(const std::string& value) {
    return ArrayFromJSON(std::make_shared<T>(/*precision=*/38, /*scale=*/2), value);
  }
};

template <typename T>
class TestVarArgsCompareFloating : public TestVarArgsCompare<T> {};

template <typename T>
class TestVarArgsCompareBinary : public TestVarArgsCompare<T> {};

template <typename T>
class TestVarArgsCompareFixedSizeBinary : public TestVarArgsCompare<T> {
 protected:
  Datum scalar(const std::string& value, int32_t byte_width = 3) {
    return ScalarFromJSON(fixed_size_binary(byte_width), value);
  }

  Datum array(const std::string& value) {
    return ArrayFromJSON(fixed_size_binary(3), value);
  }
};

template <typename T>
class TestVarArgsCompareParametricTemporal : public TestVarArgsCompare<T> {
 protected:
  static std::shared_ptr<DataType> type_singleton() {
    // Time32 requires second/milli, Time64 requires nano/micro
    if (TypeTraits<T>::bytes_required(1) == 4) {
      return std::make_shared<T>(TimeUnit::type::SECOND);
    } else {
      return std::make_shared<T>(TimeUnit::type::NANO);
    }
  }

  Datum scalar(const std::string& value) {
    return ScalarFromJSON(type_singleton(), value);
  }

  Datum array(const std::string& value) { return ArrayFromJSON(type_singleton(), value); }
};

using CompareNumericBasedTypes =
    ::testing::Types<UInt8Type, UInt16Type, UInt32Type, UInt64Type, Int8Type, Int16Type,
                     Int32Type, Int64Type, FloatType, DoubleType, Date32Type, Date64Type>;
using CompareParametricTemporalTypes =
    ::testing::Types<TimestampType, Time32Type, Time64Type>;
using CompareFixedSizeBinaryTypes = ::testing::Types<FixedSizeBinaryType>;

TYPED_TEST_SUITE(TestVarArgsCompareNumeric, CompareNumericBasedTypes);
TYPED_TEST_SUITE(TestVarArgsCompareDecimal, DecimalArrowTypes);
TYPED_TEST_SUITE(TestVarArgsCompareFloating, RealArrowTypes);
TYPED_TEST_SUITE(TestVarArgsCompareParametricTemporal, CompareParametricTemporalTypes);
TYPED_TEST_SUITE(TestVarArgsCompareBinary, BaseBinaryArrowTypes);
TYPED_TEST_SUITE(TestVarArgsCompareFixedSizeBinary, CompareFixedSizeBinaryTypes);

TYPED_TEST(TestVarArgsCompareNumeric, MinElementWise) {
  this->AssertNullScalar(MinElementWise, {});
  this->AssertNullScalar(MinElementWise, {this->scalar("null"), this->scalar("null")});

  if (std::is_same<TypeParam, Date64Type>::value) {
    this->Assert(MinElementWise, this->scalar("0"), {this->scalar("0")});
    this->Assert(
        MinElementWise, this->scalar("0"),
        {this->scalar("172800000"), this->scalar("0"), this->scalar("86400000")});
    this->Assert(MinElementWise, this->scalar("0"),
                 {this->scalar("172800000"), this->scalar("0"), this->scalar("86400000"),
                  this->scalar("null")});
    this->Assert(MinElementWise, this->scalar("86400000"),
                 {this->scalar("null"), this->scalar("null"), this->scalar("86400000"),
                  this->scalar("null")});

    this->Assert(MinElementWise, (this->array("[]")), {this->array("[]")});
    this->Assert(MinElementWise, this->array("[86400000, 172800000, 259200000, null]"),
                 {this->array("[86400000, 172800000, 259200000, null]")});

    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->scalar("172800000")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("172800000")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("172800000"), this->scalar("345600000")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("null"), this->scalar("172800000")});

    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[172800000, 172800000, 172800000, 172800000]")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[172800000, null, 172800000, 172800000]")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 172800000, 172800000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->array("[172800000, 172800000, 172800000, 172800000]")});

    this->Assert(MinElementWise, this->array("[86400000, 172800000, null, 518400000]"),
                 {this->array("[86400000, 172800000, null, null]"),
                  this->array("[345600000, null, null, 518400000]")});
    this->Assert(MinElementWise, this->array("[86400000, 172800000, null, 518400000]"),
                 {this->array("[345600000, null, null, 518400000]"),
                  this->array("[86400000, 172800000, null, null]")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[null, null, null, null]")});
    this->Assert(MinElementWise,
                 this->array("[86400000, 172800000, 259200000, 345600000]"),
                 {this->array("[null, null, null, null]"),
                  this->array("[86400000, 172800000, 259200000, 345600000]")});

    this->Assert(MinElementWise, this->array("[86400000, 86400000, 86400000, 86400000]"),
                 {this->scalar("86400000"),
                  this->array("[86400000, 172800000, 259200000, 345600000]")});
    this->Assert(MinElementWise, this->array("[86400000, 86400000, 86400000, 86400000]"),
                 {this->scalar("86400000"), this->array("[null, null, null, null]")});
    this->Assert(
        MinElementWise, this->array("[86400000, 86400000, 86400000, 86400000]"),
        {this->scalar("null"), this->array("[86400000, 86400000, 86400000, 86400000]")});
    this->Assert(MinElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("null"), this->array("[null, null, null, null]")});

    // Test null handling
    this->element_wise_aggregate_options_.skip_nulls = false;
    this->AssertNullScalar(MinElementWise, {this->scalar("null"), this->scalar("null")});
    this->AssertNullScalar(MinElementWise, {this->scalar("0"), this->scalar("null")});

    this->Assert(MinElementWise, this->array("[86400000, null, 172800000, 172800000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("172800000"), this->scalar("345600000")});
    this->Assert(MinElementWise, this->array("[null, null, null, null]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("null"), this->scalar("172800000")});
    this->Assert(MinElementWise, this->array("[86400000, null, 172800000, 172800000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[172800000, null, 172800000, 172800000]")});

    this->Assert(MinElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("86400000"), this->array("[null, null, null, null]")});
    this->Assert(
        MinElementWise, this->array("[null, null, null, null]"),
        {this->scalar("null"), this->array("[86400000, 86400000, 86400000, 86400000]")});
  } else {
    this->Assert(MinElementWise, this->scalar("0"), {this->scalar("0")});
    this->Assert(MinElementWise, this->scalar("0"),
                 {this->scalar("2"), this->scalar("0"), this->scalar("1")});
    this->Assert(
        MinElementWise, this->scalar("0"),
        {this->scalar("2"), this->scalar("0"), this->scalar("1"), this->scalar("null")});
    this->Assert(MinElementWise, this->scalar("1"),
                 {this->scalar("null"), this->scalar("null"), this->scalar("1"),
                  this->scalar("null")});

    this->Assert(MinElementWise, (this->array("[]")), {this->array("[]")});
    this->Assert(MinElementWise, this->array("[1, 2, 3, null]"),
                 {this->array("[1, 2, 3, null]")});

    this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
                 {this->array("[1, 2, 3, 4]"), this->scalar("2")});
    this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
                 {this->array("[1, null, 3, 4]"), this->scalar("2")});
    this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
                 {this->array("[1, null, 3, 4]"), this->scalar("2"), this->scalar("4")});
    this->Assert(
        MinElementWise, this->array("[1, 2, 2, 2]"),
        {this->array("[1, null, 3, 4]"), this->scalar("null"), this->scalar("2")});

    this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[2, 2, 2, 2]")});
    this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[2, null, 2, 2]")});
    this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
                 {this->array("[1, null, 3, 4]"), this->array("[2, 2, 2, 2]")});

    this->Assert(MinElementWise, this->array("[1, 2, null, 6]"),
                 {this->array("[1, 2, null, null]"), this->array("[4, null, null, 6]")});
    this->Assert(MinElementWise, this->array("[1, 2, null, 6]"),
                 {this->array("[4, null, null, 6]"), this->array("[1, 2, null, null]")});
    this->Assert(MinElementWise, this->array("[1, 2, 3, 4]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[null, null, null, null]")});
    this->Assert(MinElementWise, this->array("[1, 2, 3, 4]"),
                 {this->array("[null, null, null, null]"), this->array("[1, 2, 3, 4]")});

    this->Assert(MinElementWise, this->array("[1, 1, 1, 1]"),
                 {this->scalar("1"), this->array("[1, 2, 3, 4]")});
    this->Assert(MinElementWise, this->array("[1, 1, 1, 1]"),
                 {this->scalar("1"), this->array("[null, null, null, null]")});
    this->Assert(MinElementWise, this->array("[1, 1, 1, 1]"),
                 {this->scalar("null"), this->array("[1, 1, 1, 1]")});
    this->Assert(MinElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("null"), this->array("[null, null, null, null]")});

    // Test null handling
    this->element_wise_aggregate_options_.skip_nulls = false;
    this->AssertNullScalar(MinElementWise, {this->scalar("null"), this->scalar("null")});
    this->AssertNullScalar(MinElementWise, {this->scalar("0"), this->scalar("null")});

    this->Assert(MinElementWise, this->array("[1, null, 2, 2]"),
                 {this->array("[1, null, 3, 4]"), this->scalar("2"), this->scalar("4")});
    this->Assert(
        MinElementWise, this->array("[null, null, null, null]"),
        {this->array("[1, null, 3, 4]"), this->scalar("null"), this->scalar("2")});
    this->Assert(MinElementWise, this->array("[1, null, 2, 2]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[2, null, 2, 2]")});

    this->Assert(MinElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("1"), this->array("[null, null, null, null]")});
    this->Assert(MinElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("null"), this->array("[1, 1, 1, 1]")});
  }
}

TYPED_TEST(TestVarArgsCompareDecimal, MinElementWise) {
  this->Assert(MinElementWise, this->scalar(R"("2.14")"),
               {this->scalar(R"("3.14")"), this->scalar(R"("2.14")")});

  this->Assert(MinElementWise, this->scalar(R"("2.14")"),
               {this->scalar("null"), this->scalar(R"("2.14")")});
  this->Assert(MinElementWise, this->scalar(R"("3.14")"),
               {this->scalar(R"("3.14")"), this->scalar("null")});
  this->Assert(MinElementWise, this->scalar("null"),
               {this->scalar("null"), this->scalar("null")});

  this->Assert(MinElementWise, this->array(R"(["1.00", "2.00", "2.00", "2.00"])"),
               {this->array(R"(["1.00", "12.01", "3.00", "4.00"])"),
                this->array(R"(["2.00", "2.00", "2.00", "2.00"])")});
  this->Assert(MinElementWise, this->array(R"(["1.00", "12.01", "2.00", "2.00"])"),
               {this->array(R"(["1.00", "12.01", "3.00", "4.00"])"),
                this->array(R"(["2.00", null, "2.00", "2.00"])")});
  this->Assert(MinElementWise, this->array(R"(["1.00", "2.00", "2.00", "2.00"])"),
               {this->array(R"(["1.00", null, "3.00", "4.00"])"),
                this->array(R"(["2.00", "2.00", "2.00", "2.00"])")});
  this->Assert(MinElementWise, this->array(R"([null, null, null, null])"),
               {this->array(R"([null, null, null, null])"),
                this->array(R"([null, null, null, null])")});

  this->Assert(
      MinElementWise, this->array(R"(["1.00", "2.00", "2.00", "2.00"])"),
      {this->array(R"(["1.00", null, "3.00", "4.00"])"), this->scalar(R"("2.00")")});
  this->Assert(MinElementWise, this->array(R"([null, "2.00", "3.00", "4.00"])"),
               {this->array(R"([null, "2.00", "3.00", "4.00"])"), this->scalar("null")});

  // Test null handling
  this->element_wise_aggregate_options_.skip_nulls = false;

  this->Assert(MinElementWise, this->scalar("null"),
               {this->scalar("null"), this->scalar(R"("2.14")")});
  this->Assert(MinElementWise, this->scalar("null"),
               {this->scalar(R"("3.14")"), this->scalar("null")});

  this->Assert(MinElementWise, this->array(R"(["1.00", null, "2.00", "2.00"])"),
               {this->array(R"(["1.00", "12.01", "3.00", "4.00"])"),
                this->array(R"(["2.00", null, "2.00", "2.00"])")});
  this->Assert(MinElementWise, this->array(R"(["1.00", null, "2.00", "2.00"])"),
               {this->array(R"(["1.00", null, "3.00", "4.00"])"),
                this->array(R"(["2.00", "2.00", "2.00", "2.00"])")});

  this->Assert(
      MinElementWise, this->array(R"(["1.00", null, "2.00", "2.00"])"),
      {this->array(R"(["1.00", null, "3.00", "4.00"])"), this->scalar(R"("2.00")")});
  this->Assert(
      MinElementWise, this->array(R"([null, null, null, null])"),
      {this->array(R"(["1.00", "2.00", "3.00", "4.00"])"), this->scalar("null")});

  // Test error handling
  auto result =
      MinElementWise({this->scalar(R"("3.1415")", /*precision=*/38, /*scale=*/4),
                      this->scalar(R"("2.14")", /*precision=*/38, /*scale=*/2)},
                     this->element_wise_aggregate_options_, nullptr);
  ASSERT_TRUE(result.status().IsNotImplemented());
}

TYPED_TEST(TestVarArgsCompareFloating, MinElementWise) {
  auto Check = [this](const std::string& expected,
                      const std::vector<std::string>& inputs) {
    std::vector<Datum> args;
    for (const auto& input : inputs) {
      args.emplace_back(this->scalar(input));
    }
    this->Assert(MinElementWise, this->scalar(expected), args);

    args.clear();
    for (const auto& input : inputs) {
      args.emplace_back(this->array("[" + input + "]"));
    }
    this->Assert(MinElementWise, this->array("[" + expected + "]"), args);
  };
  Check("0.0", {"0.0", "0.0"});
  Check("-0.0", {"-0.0", "-0.0"});
  // XXX implementation detail: as signed zeros are equal, we're allowed
  // to return either value if both are present.
  this->SetSignedZerosEqual(true);
  Check("0.0", {"-0.0", "0.0"});
  Check("0.0", {"0.0", "-0.0"});
  Check("0.0", {"1.0", "-0.0", "0.0"});
  Check("-1.0", {"-1.0", "-0.0"});
  Check("0", {"0", "NaN"});
  Check("0", {"NaN", "0"});
  Check("Inf", {"Inf", "NaN"});
  Check("Inf", {"NaN", "Inf"});
  Check("-Inf", {"-Inf", "NaN"});
  Check("-Inf", {"NaN", "-Inf"});
  Check("NaN", {"NaN", "null"});
  Check("0", {"0", "Inf"});
  Check("-Inf", {"0", "-Inf"});
}

TYPED_TEST(TestVarArgsCompareParametricTemporal, MinElementWise) {
  // Temporal kernel is implemented with numeric kernel underneath
  this->AssertNullScalar(MinElementWise, {});
  this->AssertNullScalar(MinElementWise, {this->scalar("null"), this->scalar("null")});

  this->Assert(MinElementWise, this->scalar("0"), {this->scalar("0")});
  this->Assert(MinElementWise, this->scalar("0"), {this->scalar("2"), this->scalar("0")});
  this->Assert(MinElementWise, this->scalar("0"),
               {this->scalar("0"), this->scalar("null")});

  this->Assert(MinElementWise, (this->array("[]")), {this->array("[]")});
  this->Assert(MinElementWise, this->array("[1, 2, 3, null]"),
               {this->array("[1, 2, 3, null]")});

  this->Assert(MinElementWise, this->array("[1, 2, 2, 2]"),
               {this->array("[1, null, 3, 4]"), this->scalar("null"), this->scalar("2")});

  this->Assert(MinElementWise, this->array("[1, 2, 3, 2]"),
               {this->array("[1, null, 3, 4]"), this->array("[2, 2, null, 2]")});
}

TYPED_TEST(TestVarArgsCompareBinary, MinElementWise) {
  this->AssertNullScalar(MinElementWise, {});
  this->AssertNullScalar(MinElementWise, {this->scalar("null"), this->scalar("null")});

  this->Assert(MinElementWise, this->scalar(R"("")"),
               {this->scalar(R"("")"), this->scalar(R"("")")});
  this->Assert(MinElementWise, this->scalar(R"("")"),
               {this->scalar(R"("")"), this->scalar("null")});
  this->Assert(MinElementWise, this->scalar(R"("")"),
               {this->scalar(R"("a")"), this->scalar(R"("")")});
  this->Assert(MinElementWise, this->scalar(R"("")"),
               {this->scalar(R"("")"), this->scalar(R"("a")")});
  this->Assert(MinElementWise, (this->array("[]")), {this->array("[]")});

  this->Assert(MinElementWise, this->scalar(R"("ab")"), {this->scalar(R"("ab")")});
  this->Assert(
      MinElementWise, this->scalar(R"("aaa")"),
      {this->scalar(R"("bb")"), this->scalar(R"("aaa")"), this->scalar(R"("c")")});
  this->Assert(MinElementWise, this->scalar(R"("aaa")"),
               {this->scalar(R"("bb")"), this->scalar(R"("aaa")"), this->scalar(R"("c")"),
                this->scalar("null")});
  this->Assert(MinElementWise, this->scalar(R"("aa")"),
               {this->scalar("null"), this->scalar("null"), this->scalar(R"("aa")"),
                this->scalar("null")});

  this->Assert(MinElementWise, this->array(R"(["aaa", "b", "cc", null])"),
               {this->array(R"(["aaa", "b", "cc", null])")});
  this->Assert(MinElementWise, this->array(R"(["aaa", "bb", "bb", "bb"])"),
               {this->array(R"(["aaa", "bb", "cc", "dddd"])"), this->scalar(R"("bb")")});
  this->Assert(MinElementWise, this->array(R"(["aaa", "bb", "bb", "bb"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")")});
  this->Assert(MinElementWise, this->array(R"(["aaa", "bb", "bb", "bb"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")"),
                this->scalar(R"("dddd")")});
  this->Assert(MinElementWise, this->array(R"(["aaa", "bb", "bb", "bb"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar("null"),
                this->scalar(R"("bb")")});

  this->Assert(MinElementWise, this->array(R"(["foo", "a", "bb", "bb"])"),
               {this->array(R"([null, "a", "bb", "cccc"])"),
                this->array(R"(["gg", null, "h", "iii"])"),
                this->array(R"(["foo", "bar", null, "bb"])")});

  // Test null handling
  this->element_wise_aggregate_options_.skip_nulls = false;
  this->Assert(MinElementWise, this->scalar("null"),
               {this->scalar(R"("bb")"), this->scalar(R"("aaa")"), this->scalar(R"("c")"),
                this->scalar("null")});
  this->Assert(MinElementWise, this->scalar("null"),
               {this->scalar("null"), this->scalar("null"), this->scalar(R"("aa")"),
                this->scalar("null")});

  this->Assert(MinElementWise, this->array(R"(["aaa", null, "bb", "bb"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")")});
  this->Assert(MinElementWise, this->array(R"(["aaa", null, "bb", "bb"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")"),
                this->scalar(R"("dddd")")});
  this->Assert(MinElementWise, this->array(R"([null, null, null, null])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar("null"),
                this->scalar(R"("bb")")});

  this->Assert(MinElementWise, this->array(R"([null, null, null, "bb"])"),
               {this->array(R"([null, "a", "bb", "cccc"])"),
                this->array(R"(["gg", null, "h", "iii"])"),
                this->array(R"(["foo", "bar", null, "bb"])")});
}

TYPED_TEST(TestVarArgsCompareFixedSizeBinary, MinElementWise) {
  this->AssertNullScalar(MinElementWise, {});
  this->AssertNullScalar(MinElementWise, {this->scalar("null"), this->scalar("null")});

  this->Assert(MinElementWise, this->scalar(R"("aaa")"), {this->scalar(R"("aaa")")});
  this->Assert(
      MinElementWise, this->scalar(R"("aaa")"),
      {this->scalar(R"("ccc")"), this->scalar(R"("aaa")"), this->scalar(R"("bbb")")});
  this->Assert(MinElementWise, this->scalar(R"("aaa")"),
               {this->scalar(R"("ccc")"), this->scalar(R"("aaa")"),
                this->scalar(R"("bbb")"), this->scalar("null")});
  this->Assert(MinElementWise, (this->array("[]")), {this->array("[]")});

  this->Assert(MinElementWise, this->array(R"(["abc", "abc", "abc", "abc", "abc"])"),
               {this->array(R"(["abc", "abc", "abd", null, "abc"])"),
                this->array(R"(["abc", "abd", "abc", "abc", null])")});
  this->Assert(
      MinElementWise, this->scalar(R"("abc")"),
      {this->scalar(R"("abe")"), this->scalar(R"("abc")"), this->scalar(R"("abd")")});

  this->Assert(
      MinElementWise, this->array(R"(["abc", "abc", "abc", "abc", "abc"])"),
      {this->array(R"(["abc", "abc", "abd", null, "abc"])"), this->scalar(R"("abc")")});
  this->Assert(
      MinElementWise, this->array(R"(["abc", "abc", "abc", "abc", "abc"])"),
      {this->array(R"(["abc", null, "abd", null, "abc"])"), this->scalar(R"("abc")")});
  this->Assert(MinElementWise, this->array(R"(["abc", "abc", "abc", "abc", "abc"])"),
               {this->array(R"(["abc", null, "abd", null, "abc"])"),
                this->scalar(R"("abc")"), this->scalar(R"("abd")")});
  this->Assert(MinElementWise, this->array(R"(["abc", "abc", "abc", "abc", "abc"])"),
               {this->array(R"(["abc", null, "abd", null, "abc"])"), this->scalar("null"),
                this->scalar(R"("abc")")});

  this->Assert(MinElementWise, this->scalar(R"("")", /*byte_width=*/0),
               {this->scalar(R"("")", /*byte_width=*/0)});
  this->Assert(MinElementWise, this->scalar("null", /*byte_width=*/0),
               {this->scalar("null", /*byte_width=*/0)});
  this->Assert(
      MinElementWise, this->scalar(R"("")", /*byte_width=*/0),
      {this->scalar("null", /*byte_width=*/0), this->scalar(R"("")", /*byte_width=*/0)});

  // Test null handling
  this->element_wise_aggregate_options_.skip_nulls = false;
  this->Assert(MinElementWise, this->array(R"(["abc", "abc", "abc", null, null])"),
               {this->array(R"(["abc", "abc", "abd", null, "abc"])"),
                this->array(R"(["abc", "abd", "abc", "abc", null])")});

  this->Assert(
      MinElementWise, this->scalar("null", /*byte_width=*/0),
      {this->scalar("null", /*byte_width=*/0), this->scalar(R"("")", /*byte_width=*/0)});

  // Test error handling
  auto result = MinElementWise({this->scalar(R"("abc")", /*byte_width=*/3),
                                this->scalar(R"("abcd")", /*byte_width=*/4)},
                               this->element_wise_aggregate_options_, nullptr);
  ASSERT_TRUE(result.status().IsNotImplemented());
}

TYPED_TEST(TestVarArgsCompareNumeric, MaxElementWise) {
  this->AssertNullScalar(MaxElementWise, {});
  this->AssertNullScalar(MaxElementWise, {this->scalar("null"), this->scalar("null")});
  this->Assert(MaxElementWise, this->scalar("0"), {this->scalar("0")});

  if (std::is_same<TypeParam, Date64Type>::value) {
    this->Assert(
        MaxElementWise, this->scalar("172800000"),
        {this->scalar("172800000"), this->scalar("0"), this->scalar("86400000")});
    this->Assert(MaxElementWise, this->scalar("172800000"),
                 {this->scalar("172800000"), this->scalar("0"), this->scalar("86400000"),
                  this->scalar("null")});
    this->Assert(MaxElementWise, this->scalar("86400000"),
                 {this->scalar("null"), this->scalar("null"), this->scalar("86400000"),
                  this->scalar("null")});

    this->Assert(MaxElementWise, (this->array("[]")), {this->array("[]")});
    this->Assert(MaxElementWise, this->array("[86400000, 172800000, 259200000, null]"),
                 {this->array("[86400000, 172800000, 259200000, null]")});

    this->Assert(MaxElementWise,
                 this->array("[172800000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->scalar("172800000")});
    this->Assert(MaxElementWise,
                 this->array("[172800000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("172800000")});
    this->Assert(MaxElementWise,
                 this->array("[345600000, 345600000, 345600000, 345600000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("172800000"), this->scalar("345600000")});
    this->Assert(MaxElementWise,
                 this->array("[172800000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("null"), this->scalar("172800000")});

    this->Assert(MaxElementWise,
                 this->array("[172800000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[172800000, 172800000, 172800000, 172800000]")});
    this->Assert(MaxElementWise,
                 this->array("[172800000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[172800000, null, 172800000, 172800000]")});
    this->Assert(MaxElementWise,
                 this->array("[172800000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->array("[172800000, 172800000, 172800000, 172800000]")});

    this->Assert(MaxElementWise, this->array("[345600000, 172800000, null, 518400000]"),
                 {this->array("[86400000, 172800000, null, null]"),
                  this->array("[345600000, null, null, 518400000]")});
    this->Assert(MaxElementWise, this->array("[345600000, 172800000, null, 518400000]"),
                 {this->array("[345600000, null, null, 518400000]"),
                  this->array("[86400000, 172800000, null, null]")});
    this->Assert(MaxElementWise,
                 this->array("[86400000, 172800000, 259200000, 345600000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[null, null, null, null]")});
    this->Assert(MaxElementWise,
                 this->array("[86400000, 172800000, 259200000, 345600000]"),
                 {this->array("[null, null, null, null]"),
                  this->array("[86400000, 172800000, 259200000, 345600000]")});

    this->Assert(MaxElementWise,
                 this->array("[86400000, 172800000, 259200000, 345600000]"),
                 {this->scalar("86400000"),
                  this->array("[86400000, 172800000, 259200000, 345600000]")});
    this->Assert(MaxElementWise, this->array("[86400000, 86400000, 86400000, 86400000]"),
                 {this->scalar("86400000"), this->array("[null, null, null, null]")});
    this->Assert(
        MaxElementWise, this->array("[86400000, 86400000, 86400000, 86400000]"),
        {this->scalar("null"), this->array("[86400000, 86400000, 86400000, 86400000]")});
    this->Assert(MaxElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("null"), this->array("[null, null, null, null]")});

    // Test null handling
    this->element_wise_aggregate_options_.skip_nulls = false;
    this->AssertNullScalar(MaxElementWise, {this->scalar("null"), this->scalar("null")});
    this->AssertNullScalar(MaxElementWise, {this->scalar("0"), this->scalar("null")});

    this->Assert(MaxElementWise, this->array("[345600000, null, 345600000, 345600000]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("172800000"), this->scalar("345600000")});
    this->Assert(MaxElementWise, this->array("[null, null, null, null]"),
                 {this->array("[86400000, null, 259200000, 345600000]"),
                  this->scalar("null"), this->scalar("172800000")});
    this->Assert(MaxElementWise, this->array("[172800000, null, 259200000, 345600000]"),
                 {this->array("[86400000, 172800000, 259200000, 345600000]"),
                  this->array("[172800000, null, 172800000, 172800000]")});

    this->Assert(MaxElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("86400000"), this->array("[null, null, null, null]")});
    this->Assert(
        MaxElementWise, this->array("[null, null, null, null]"),
        {this->scalar("null"), this->array("[86400000, 86400000, 86400000, 86400000]")});
  } else {
    this->Assert(MaxElementWise, this->scalar("2"),
                 {this->scalar("2"), this->scalar("0"), this->scalar("1")});
    this->Assert(
        MaxElementWise, this->scalar("2"),
        {this->scalar("2"), this->scalar("0"), this->scalar("1"), this->scalar("null")});
    this->Assert(MaxElementWise, this->scalar("1"),
                 {this->scalar("null"), this->scalar("null"), this->scalar("1"),
                  this->scalar("null")});

    this->Assert(MaxElementWise, (this->array("[]")), {this->array("[]")});
    this->Assert(MaxElementWise, this->array("[1, 2, 3, null]"),
                 {this->array("[1, 2, 3, null]")});

    this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
                 {this->array("[1, 2, 3, 4]"), this->scalar("2")});
    this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
                 {this->array("[1, null, 3, 4]"), this->scalar("2")});
    this->Assert(MaxElementWise, this->array("[4, 4, 4, 4]"),
                 {this->array("[1, null, 3, 4]"), this->scalar("2"), this->scalar("4")});
    this->Assert(
        MaxElementWise, this->array("[2, 2, 3, 4]"),
        {this->array("[1, null, 3, 4]"), this->scalar("null"), this->scalar("2")});

    this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[2, 2, 2, 2]")});
    this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[2, null, 2, 2]")});
    this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
                 {this->array("[1, null, 3, 4]"), this->array("[2, 2, 2, 2]")});

    this->Assert(MaxElementWise, this->array("[4, 2, null, 6]"),
                 {this->array("[1, 2, null, null]"), this->array("[4, null, null, 6]")});
    this->Assert(MaxElementWise, this->array("[4, 2, null, 6]"),
                 {this->array("[4, null, null, 6]"), this->array("[1, 2, null, null]")});
    this->Assert(MaxElementWise, this->array("[1, 2, 3, 4]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[null, null, null, null]")});
    this->Assert(MaxElementWise, this->array("[1, 2, 3, 4]"),
                 {this->array("[null, null, null, null]"), this->array("[1, 2, 3, 4]")});

    this->Assert(MaxElementWise, this->array("[1, 2, 3, 4]"),
                 {this->scalar("1"), this->array("[1, 2, 3, 4]")});
    this->Assert(MaxElementWise, this->array("[1, 1, 1, 1]"),
                 {this->scalar("1"), this->array("[null, null, null, null]")});
    this->Assert(MaxElementWise, this->array("[1, 1, 1, 1]"),
                 {this->scalar("null"), this->array("[1, 1, 1, 1]")});
    this->Assert(MaxElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("null"), this->array("[null, null, null, null]")});

    // Test null handling
    this->element_wise_aggregate_options_.skip_nulls = false;
    this->AssertNullScalar(MaxElementWise, {this->scalar("null"), this->scalar("null")});
    this->AssertNullScalar(MaxElementWise, {this->scalar("0"), this->scalar("null")});

    this->Assert(MaxElementWise, this->array("[4, null, 4, 4]"),
                 {this->array("[1, null, 3, 4]"), this->scalar("2"), this->scalar("4")});
    this->Assert(
        MaxElementWise, this->array("[null, null, null, null]"),
        {this->array("[1, null, 3, 4]"), this->scalar("null"), this->scalar("2")});
    this->Assert(MaxElementWise, this->array("[2, null, 3, 4]"),
                 {this->array("[1, 2, 3, 4]"), this->array("[2, null, 2, 2]")});

    this->Assert(MaxElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("1"), this->array("[null, null, null, null]")});
    this->Assert(MaxElementWise, this->array("[null, null, null, null]"),
                 {this->scalar("null"), this->array("[1, 1, 1, 1]")});
  }
}

TYPED_TEST(TestVarArgsCompareDecimal, MaxElementWise) {
  this->Assert(MaxElementWise, this->scalar(R"("3.14")"),
               {this->scalar(R"("3.14")"), this->scalar(R"("2.14")")});

  this->Assert(MaxElementWise, this->scalar(R"("2.14")"),
               {this->scalar("null"), this->scalar(R"("2.14")")});
  this->Assert(MaxElementWise, this->scalar(R"("3.14")"),
               {this->scalar(R"("3.14")"), this->scalar("null")});
  this->Assert(MaxElementWise, this->scalar("null"),
               {this->scalar("null"), this->scalar("null")});

  this->Assert(MaxElementWise, this->array(R"(["2.00", "12.01", "3.00", "4.00"])"),
               {this->array(R"(["1.00", "12.01", "3.00", "4.00"])"),
                this->array(R"(["2.00", "2.00", "2.00", "2.00"])")});
  this->Assert(MaxElementWise, this->array(R"(["2.00", "12.01", "3.00", "4.00"])"),
               {this->array(R"(["1.00", "12.01", "3.00", "4.00"])"),
                this->array(R"(["2.00", null, "2.00", "2.00"])")});
  this->Assert(MaxElementWise, this->array(R"(["2.00", "2.00", "3.00", "4.00"])"),
               {this->array(R"(["1.00", null, "3.00", "4.00"])"),
                this->array(R"(["2.00", "2.00", "2.00", "2.00"])")});
  this->Assert(MaxElementWise, this->array(R"([null, null, null, null])"),
               {this->array(R"([null, null, null, null])"),
                this->array(R"([null, null, null, null])")});

  this->Assert(
      MaxElementWise, this->array(R"(["2.00", "2.00", "3.00", "4.00"])"),
      {this->array(R"(["1.00", null, "3.00", "4.00"])"), this->scalar(R"("2.00")")});
  this->Assert(MaxElementWise, this->array(R"([null, "2.00", "3.00", "4.00"])"),
               {this->array(R"([null, "2.00", "3.00", "4.00"])"), this->scalar("null")});

  // Test null handling
  this->element_wise_aggregate_options_.skip_nulls = false;

  this->Assert(MaxElementWise, this->scalar("null"),
               {this->scalar("null"), this->scalar(R"("2.14")")});
  this->Assert(MaxElementWise, this->scalar("null"),
               {this->scalar(R"("3.14")"), this->scalar("null")});

  this->Assert(MaxElementWise, this->array(R"(["2.00", null, "3.00", "4.00"])"),
               {this->array(R"(["1.00", "12.01", "3.00", "4.00"])"),
                this->array(R"(["2.00", null, "2.00", "2.00"])")});
  this->Assert(MaxElementWise, this->array(R"(["2.00", null, "3.00", "4.00"])"),
               {this->array(R"(["1.00", null, "3.00", "4.00"])"),
                this->array(R"(["2.00", "2.00", "2.00", "2.00"])")});

  this->Assert(
      MaxElementWise, this->array(R"(["2.00", null, "3.00", "4.00"])"),
      {this->array(R"(["1.00", null, "3.00", "4.00"])"), this->scalar(R"("2.00")")});
  this->Assert(
      MaxElementWise, this->array(R"([null, null, null, null])"),
      {this->array(R"(["1.00", "2.00", "3.00", "4.00"])"), this->scalar("null")});

  // Test error handling
  auto result =
      MaxElementWise({this->scalar(R"("3.1415")", /*precision=*/38, /*scale=*/4),
                      this->scalar(R"("2.14")", /*precision=*/38, /*scale=*/2)},
                     this->element_wise_aggregate_options_, nullptr);
  ASSERT_TRUE(result.status().IsNotImplemented());
}

TYPED_TEST(TestVarArgsCompareFloating, MaxElementWise) {
  auto Check = [this](const std::string& expected,
                      const std::vector<std::string>& inputs) {
    std::vector<Datum> args;
    for (const auto& input : inputs) {
      args.emplace_back(this->scalar(input));
    }
    this->Assert(MaxElementWise, this->scalar(expected), args);

    args.clear();
    for (const auto& input : inputs) {
      args.emplace_back(this->array("[" + input + "]"));
    }
    this->Assert(MaxElementWise, this->array("[" + expected + "]"), args);
  };
  Check("0.0", {"0.0", "0.0"});
  Check("-0.0", {"-0.0", "-0.0"});
  // XXX implementation detail: as signed zeros are equal, we're allowed
  // to return either value if both are present.
  this->SetSignedZerosEqual(true);
  Check("0.0", {"-0.0", "0.0"});
  Check("0.0", {"0.0", "-0.0"});
  Check("0.0", {"-1.0", "-0.0", "0.0"});
  Check("1.0", {"1.0", "-0.0"});
  Check("0", {"0", "NaN"});
  Check("0", {"NaN", "0"});
  Check("Inf", {"Inf", "NaN"});
  Check("Inf", {"NaN", "Inf"});
  Check("-Inf", {"-Inf", "NaN"});
  Check("-Inf", {"NaN", "-Inf"});
  Check("NaN", {"NaN", "null"});
  Check("Inf", {"0", "Inf"});
  Check("0", {"0", "-Inf"});
}

TYPED_TEST(TestVarArgsCompareParametricTemporal, MaxElementWise) {
  // Temporal kernel is implemented with numeric kernel underneath
  this->AssertNullScalar(MaxElementWise, {});
  this->AssertNullScalar(MaxElementWise, {this->scalar("null"), this->scalar("null")});

  this->Assert(MaxElementWise, this->scalar("0"), {this->scalar("0")});
  this->Assert(MaxElementWise, this->scalar("2"), {this->scalar("2"), this->scalar("0")});
  this->Assert(MaxElementWise, this->scalar("0"),
               {this->scalar("0"), this->scalar("null")});

  this->Assert(MaxElementWise, (this->array("[]")), {this->array("[]")});
  this->Assert(MaxElementWise, this->array("[1, 2, 3, null]"),
               {this->array("[1, 2, 3, null]")});

  this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
               {this->array("[1, null, 3, 4]"), this->scalar("null"), this->scalar("2")});

  this->Assert(MaxElementWise, this->array("[2, 2, 3, 4]"),
               {this->array("[1, null, 3, 4]"), this->array("[2, 2, null, 2]")});
}

TYPED_TEST(TestVarArgsCompareBinary, MaxElementWise) {
  this->AssertNullScalar(MaxElementWise, {});
  this->AssertNullScalar(MaxElementWise, {this->scalar("null"), this->scalar("null")});

  this->Assert(MaxElementWise, this->scalar(R"("")"),
               {this->scalar(R"("")"), this->scalar(R"("")")});
  this->Assert(MaxElementWise, this->scalar(R"("")"),
               {this->scalar(R"("")"), this->scalar("null")});
  this->Assert(MaxElementWise, this->scalar(R"("a")"),
               {this->scalar(R"("a")"), this->scalar(R"("")")});
  this->Assert(MaxElementWise, this->scalar(R"("a")"),
               {this->scalar(R"("")"), this->scalar(R"("a")")});
  this->Assert(MaxElementWise, (this->array("[]")), {this->array("[]")});

  this->Assert(MaxElementWise, this->scalar(R"("ab")"), {this->scalar(R"("ab")")});
  this->Assert(
      MaxElementWise, this->scalar(R"("c")"),
      {this->scalar(R"("bb")"), this->scalar(R"("aaa")"), this->scalar(R"("c")")});
  this->Assert(MaxElementWise, this->scalar(R"("c")"),
               {this->scalar(R"("bb")"), this->scalar(R"("aaa")"), this->scalar(R"("c")"),
                this->scalar("null")});
  this->Assert(MaxElementWise, this->scalar(R"("aa")"),
               {this->scalar("null"), this->scalar("null"), this->scalar(R"("aa")"),
                this->scalar("null")});

  this->Assert(MaxElementWise, this->array(R"(["aaa", "b", "cc", null])"),
               {this->array(R"(["aaa", "b", "cc", null])")});
  this->Assert(MaxElementWise, this->array(R"(["bb", "bb", "cc", "dddd"])"),
               {this->array(R"(["aaa", "bb", "cc", "dddd"])"), this->scalar(R"("bb")")});
  this->Assert(MaxElementWise, this->array(R"(["bb", "bb", "cc", "dddd"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")")});
  this->Assert(MaxElementWise, this->array(R"(["dddd", "dddd", "dddd", "dddd"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")"),
                this->scalar(R"("dddd")")});
  this->Assert(MaxElementWise, this->array(R"(["bb", "bb", "cc", "dddd"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar("null"),
                this->scalar(R"("bb")")});

  this->Assert(MaxElementWise, this->array(R"(["gg", "bar", "h", "iii"])"),
               {this->array(R"([null, "a", "bb", "cccc"])"),
                this->array(R"(["gg", null, "h", "iii"])"),
                this->array(R"(["foo", "bar", null, "bb"])")});

  // Test null handling
  this->element_wise_aggregate_options_.skip_nulls = false;
  this->Assert(MaxElementWise, this->scalar("null"),
               {this->scalar(R"("bb")"), this->scalar(R"("aaa")"), this->scalar(R"("c")"),
                this->scalar("null")});
  this->Assert(MaxElementWise, this->scalar("null"),
               {this->scalar("null"), this->scalar("null"), this->scalar(R"("aa")"),
                this->scalar("null")});

  this->Assert(MaxElementWise, this->array(R"(["bb", null, "cc", "dddd"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")")});
  this->Assert(MaxElementWise, this->array(R"(["dddd", null, "dddd", "dddd"])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar(R"("bb")"),
                this->scalar(R"("dddd")")});
  this->Assert(MaxElementWise, this->array(R"([null, null, null, null])"),
               {this->array(R"(["aaa", null, "cc", "dddd"])"), this->scalar("null"),
                this->scalar(R"("bb")")});

  this->Assert(MaxElementWise, this->array(R"([null, null, null, "iii"])"),
               {this->array(R"([null, "a", "bb", "cccc"])"),
                this->array(R"(["gg", null, "h", "iii"])"),
                this->array(R"(["foo", "bar", null, "bb"])")});
}

TYPED_TEST(TestVarArgsCompareFixedSizeBinary, MaxElementWise) {
  this->AssertNullScalar(MaxElementWise, {});
  this->AssertNullScalar(MaxElementWise, {this->scalar("null"), this->scalar("null")});

  this->Assert(MaxElementWise, this->scalar(R"("aaa")"), {this->scalar(R"("aaa")")});
  this->Assert(
      MaxElementWise, this->scalar(R"("ccc")"),
      {this->scalar(R"("ccc")"), this->scalar(R"("aaa")"), this->scalar(R"("bbb")")});
  this->Assert(MaxElementWise, this->scalar(R"("ccc")"),
               {this->scalar(R"("ccc")"), this->scalar(R"("aaa")"),
                this->scalar(R"("bbb")"), this->scalar("null")});
  this->Assert(MaxElementWise, (this->array("[]")), {this->array("[]")});

  this->Assert(MaxElementWise, this->array(R"(["abc", "abd", "abd", "abc", "abc"])"),
               {this->array(R"(["abc", "abc", "abd", null, "abc"])"),
                this->array(R"(["abc", "abd", "abc", "abc", null])")});
  this->Assert(
      MaxElementWise, this->scalar(R"("abe")"),
      {this->scalar(R"("abe")"), this->scalar(R"("abc")"), this->scalar(R"("abd")")});

  this->Assert(
      MaxElementWise, this->array(R"(["abc", "abc", "abd", "abc", "abc"])"),
      {this->array(R"(["abc", "abc", "abd", null, "abc"])"), this->scalar(R"("abc")")});
  this->Assert(
      MaxElementWise, this->array(R"(["abc", "abc", "abd", "abc", "abc"])"),
      {this->array(R"(["abc", null, "abd", null, "abc"])"), this->scalar(R"("abc")")});
  this->Assert(MaxElementWise, this->array(R"(["abd", "abd", "abd", "abd", "abd"])"),
               {this->array(R"(["abc", null, "abd", null, "abc"])"),
                this->scalar(R"("abc")"), this->scalar(R"("abd")")});
  this->Assert(MaxElementWise, this->array(R"(["abc", "abc", "abd", "abc", "abc"])"),
               {this->array(R"(["abc", null, "abd", null, "abc"])"), this->scalar("null"),
                this->scalar(R"("abc")")});

  this->Assert(MaxElementWise, this->scalar(R"("")", /*byte_width=*/0),
               {this->scalar(R"("")", /*byte_width=*/0)});
  this->Assert(MaxElementWise, this->scalar("null", /*byte_width=*/0),
               {this->scalar("null", /*byte_width=*/0)});
  this->Assert(
      MaxElementWise, this->scalar(R"("")", /*byte_width=*/0),
      {this->scalar("null", /*byte_width=*/0), this->scalar(R"("")", /*byte_width=*/0)});

  // Test null handling
  this->element_wise_aggregate_options_.skip_nulls = false;
  this->Assert(MaxElementWise, this->array(R"(["abc", "abd", "abd", null, null])"),
               {this->array(R"(["abc", "abc", "abd", null, "abc"])"),
                this->array(R"(["abc", "abd", "abc", "abc", null])")});

  this->Assert(
      MaxElementWise, this->scalar("null", /*byte_width=*/0),
      {this->scalar("null", /*byte_width=*/0), this->scalar(R"("")", /*byte_width=*/0)});

  // Test error handling
  auto result = MaxElementWise({this->scalar(R"("abc")", /*byte_width=*/3),
                                this->scalar(R"("abcd")", /*byte_width=*/4)},
                               this->element_wise_aggregate_options_, nullptr);
  ASSERT_TRUE(result.status().IsNotImplemented());
}

TEST(TestMaxElementWiseMinElementWise, CommonTemporal) {
  EXPECT_THAT(MinElementWise({
                  ScalarFromJSON(timestamp(TimeUnit::SECOND), "1"),
                  ScalarFromJSON(timestamp(TimeUnit::MILLI), "12000"),
              }),
              ResultWith(ScalarFromJSON(timestamp(TimeUnit::MILLI), "1000")));
  EXPECT_THAT(MaxElementWise({
                  ScalarFromJSON(date32(), "1"),
                  ScalarFromJSON(timestamp(TimeUnit::SECOND), "86401"),
              }),
              ResultWith(ScalarFromJSON(timestamp(TimeUnit::SECOND), "86401")));
  EXPECT_THAT(MinElementWise({
                  ScalarFromJSON(date32(), "1"),
                  ScalarFromJSON(date64(), "172800000"),
              }),
              ResultWith(ScalarFromJSON(date64(), "86400000")));
}

static void ValidateBetween(BetweenOptions options, const Datum& val, const Datum& lhs,
                            const Datum& rhs, const Datum& expected) {
  ASSERT_OK_AND_ASSIGN(Datum result, Between(val, lhs, rhs, options, nullptr));
  if ((val.is_scalar()) && (lhs.is_scalar()) && (rhs.is_scalar())) {
    AssertScalarsEqual(*expected.scalar(), *result.scalar(), /*verbose=*/true);
  } else {
    AssertArraysEqual(*expected.make_array(), *result.make_array(),
                      /*verbose=*/true);
  }
}

void ValidateBetween(const Datum& val, const Datum& lhs, const Datum& rhs) {
  CompareOperator lhs_val;
  CompareOperator val_rhs;
  for (auto inclusive :
       {BetweenOptions::Inclusive::BOTH, BetweenOptions::Inclusive::LEFT,
        BetweenOptions::Inclusive::RIGHT, BetweenOptions::Inclusive::NEITHER}) {
    auto options = BetweenOptions(inclusive);
    if (inclusive == BetweenOptions::Inclusive::NEITHER) {
      lhs_val = LESS;
      val_rhs = LESS;
    } else if (inclusive == BetweenOptions::Inclusive::LEFT) {
      lhs_val = LESS_EQUAL;
      val_rhs = LESS;
    } else if (inclusive == BetweenOptions::Inclusive::RIGHT) {
      lhs_val = LESS;
      val_rhs = LESS_EQUAL;
    } else {
      lhs_val = LESS_EQUAL;
      val_rhs = LESS_EQUAL;
    }

    ASSERT_OK_AND_ASSIGN(
        Datum resultl, CallFunction(CompareOperatorToFunctionName(lhs_val), {lhs, val}));
    ASSERT_OK_AND_ASSIGN(
        Datum resultr, CallFunction(CompareOperatorToFunctionName(val_rhs), {val, rhs}));
    ASSERT_OK_AND_ASSIGN(Datum expected, CallFunction("and", {resultl, resultr}));

    ValidateBetween(options, val, lhs, rhs, expected);
  }
}

class TestNumericBetweenKernel : public ::testing::Test {};

std::shared_ptr<DataType> GetType(std::shared_ptr<DataType> type) {
  auto type_string = type->ToString();
  if (type_string == "duration[s]") {
    return int64();
  } else if (type_string == "duration[us]") {
    return int64();
  } else if (type_string == "duration[ms]") {
    return int64();
  } else if (type_string == "duration[ns]") {
    return int64();
  } else if (type_string == "uint8") {
    return uint8();
  } else if (type_string == "uint16") {
    return uint16();
  } else if (type_string == "uint32") {
    return uint32();
  } else if (type_string == "uint64") {
    return uint64();
  } else if (type_string == "int8") {
    return int8();
  } else if (type_string == "int16") {
    return int16();
  } else if (type_string == "int32") {
    return int32();
  } else if (type_string == "int64") {
    return int64();
  } else if (type_string == "float") {
    return float32();
  } else if (type_string == "double") {
    return float64();
  } else if (type_string == "large_binary") {
    return large_binary();
  } else if (type_string == "binary") {
    return binary();
  } else if (type_string == "string") {
    return utf8();
  } else if (type_string == "large_string") {
    return large_utf8();
  } else {
    throw std::invalid_argument("Invalid type specified: " + std::string(type_string));
  }
}

TEST(TestNumericBetweenKernel, 3Scalars) {
  for (const auto& types : {DurationTypes(), NumericTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto tt = GetType(ty);
      auto zero = Datum(ScalarFromJSON(tt, "0"));
      auto two = Datum(ScalarFromJSON(tt, "2"));
      auto four = Datum(ScalarFromJSON(tt, "4"));
      auto null = Datum(ScalarFromJSON(tt, "null"));
      ValidateBetween(zero, two, four);
      ValidateBetween(two, zero, four);
      ValidateBetween(two, two, four);
      ValidateBetween(four, two, four);
      ValidateBetween(null, two, four);
      ValidateBetween(two, null, four);
      ValidateBetween(two, zero, null);
    }
  }
}

TEST(TestNumericBetweenKernel, 1Array2Scalars) {
  for (const auto& types : {DurationTypes(), NumericTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto tt = GetType(ty);
      auto zero = Datum(ScalarFromJSON(tt, "0"));
      auto four = Datum(ScalarFromJSON(tt, "4"));
      auto null = Datum(ScalarFromJSON(tt, "null"));
      ValidateBetween(Datum(ArrayFromJSON(tt, "[]")), zero, four);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[null]")), zero, four);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[0,1,2,3,4,5]")), zero, four);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[null,0,1,1]")), zero, four);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[5,4,3,2,1,0]")), null, four);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[5,4,3,2,1,0]")), zero, null);
      ValidateBetween(zero, Datum(ArrayFromJSON(tt, "[]")), four);
      ValidateBetween(zero, Datum(ArrayFromJSON(tt, "[null]")), four);
      ValidateBetween(zero, Datum(ArrayFromJSON(tt, "[0,1,2,3,4,5]")), four);
      ValidateBetween(zero, Datum(ArrayFromJSON(tt, "[null,0,1,1]")), four);
      ValidateBetween(null, Datum(ArrayFromJSON(tt, "[5,4,3,2,1,0]")), four);
      ValidateBetween(zero, Datum(ArrayFromJSON(tt, "[5,4,3,2,1,0]")), null);
      ValidateBetween(zero, four, Datum(ArrayFromJSON(tt, "[]")));
      ValidateBetween(zero, four, Datum(ArrayFromJSON(tt, "[null]")));
      ValidateBetween(zero, four, Datum(ArrayFromJSON(tt, "[0,1,2,3,4,5]")));
      ValidateBetween(zero, four, Datum(ArrayFromJSON(tt, "[null,0,1,1]")));
      ValidateBetween(null, four, Datum(ArrayFromJSON(tt, "[5,4,3,2,1,0]")));
      ValidateBetween(zero, null, Datum(ArrayFromJSON(tt, "[5,4,3,2,1,0]")));
    }
  }
}

TEST(TestNumericBetweenKernel, 2Arrays1Scalar) {
  for (const auto& types : {DurationTypes(), NumericTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto tt = GetType(ty);
      auto one = Datum(ScalarFromJSON(tt, "1"));
      ValidateBetween(one, Datum(ArrayFromJSON(tt, "[]")),
                      Datum(ArrayFromJSON(tt, "[]")));
      ValidateBetween(one, Datum(ArrayFromJSON(tt, "[null]")),
                      Datum(ArrayFromJSON(tt, "[null]")));
      ValidateBetween(one, Datum(ArrayFromJSON(tt, "[0,0,1,3,3]")),
                      Datum(ArrayFromJSON(tt, "[10,10,2,5,5]")));
      ValidateBetween(one, Datum(ArrayFromJSON(tt, "[0,0,1,null,3,3]")),
                      Datum(ArrayFromJSON(tt, "[0,10,2,2,5,5]")));
      ValidateBetween(Datum(ArrayFromJSON(tt, "[]")), one,
                      Datum(ArrayFromJSON(tt, "[]")));
      ValidateBetween(Datum(ArrayFromJSON(tt, "[null]")), one,
                      Datum(ArrayFromJSON(tt, "[null]")));
      ValidateBetween(Datum(ArrayFromJSON(tt, "[0,0,1,3,3]")), one,
                      Datum(ArrayFromJSON(tt, "[10,10,2,5,5]")));
      ValidateBetween(Datum(ArrayFromJSON(tt, "[0,0,1,null,3,3]")), one,
                      Datum(ArrayFromJSON(tt, "[0,10,2,2,5,5]")));
      ValidateBetween(Datum(ArrayFromJSON(tt, "[]")), Datum(ArrayFromJSON(tt, "[]")),
                      one);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[null]")),
                      Datum(ArrayFromJSON(tt, "[null]")), one);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[0,0,1,3,3]")),
                      Datum(ArrayFromJSON(tt, "[10,10,2,5,5]")), one);
      ValidateBetween(Datum(ArrayFromJSON(tt, "[0,0,1,null,3,3]")),
                      Datum(ArrayFromJSON(tt, "[0,10,2,2,5,5]")), one);
    }
  }
}

TEST(TestNumericBetweenKernel, 3Arrays) {
  for (const auto& types : {DurationTypes(), NumericTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      ValidateBetween(Datum(ArrayFromJSON(ty, "[]")), Datum(ArrayFromJSON(ty, "[]")),
                      Datum(ArrayFromJSON(ty, "[]")));
      ValidateBetween(Datum(ArrayFromJSON(ty, "[null]")),
                      Datum(ArrayFromJSON(ty, "[null]")),
                      Datum(ArrayFromJSON(ty, "[null]")));
      ValidateBetween(Datum(ArrayFromJSON(ty, "[1,1,2,2,2]")),
                      Datum(ArrayFromJSON(ty, "[0,0,1,3,3]")),
                      Datum(ArrayFromJSON(ty, "[10,10,2,5,5]")));
      ValidateBetween(Datum(ArrayFromJSON(ty, "[0,1,2,2,2,2]")),
                      Datum(ArrayFromJSON(ty, "[0,0,1,null,3,3]")),
                      Datum(ArrayFromJSON(ty, "[0,10,2,2,5,5]")));
    }
  }
}

TEST(TestNumericBetweenKernel, Random) {
  for (const auto& types : {DurationTypes(), NumericTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      auto rand = random::RandomArrayGenerator(0x5416447);
      const int64_t length = 100;
      for (auto null_probability : {0.0, 0.01, 0.1, 0.25, 0.5, 1.0}) {
        ARROW_SCOPED_TRACE("type = ", ty->ToString());
        auto tt = GetType(ty);
        auto metadata =
            key_value_metadata({"null_probability"}, {std::to_string(null_probability)});
        auto field = ::arrow::field("[0,100]", std::move(ty), std::move(metadata));
        auto data1 = rand.ArrayOf(*field, length);
        auto data2 = rand.ArrayOf(*field, length);
        auto data3 = rand.ArrayOf(*field, length);

        // Create view of data as the type (e.g. float64)
        auto array1 = Datum(*data1->View(tt));
        auto array2 = Datum(*data2->View(tt));
        auto array3 = Datum(*data3->View(tt));
        auto scalar1 = Datum(ScalarFromJSON(tt, "10"));
        auto scalar2 = Datum(ScalarFromJSON(tt, "30"));
        auto scalar3 = Datum(ScalarFromJSON(tt, "50"));
        ValidateBetween(scalar1, scalar2, scalar3);
        ValidateBetween(array1, scalar2, scalar3);
        ValidateBetween(array1, array2, scalar3);
        ValidateBetween(array1, array2, array3);
        ValidateBetween(array1, scalar2, scalar3);
        ValidateBetween(scalar1, array2, array3);
        ValidateBetween(scalar1, array2, scalar3);
        ValidateBetween(scalar1, scalar2, array3);
        ValidateBetween(array1, scalar2, array3);
      }
    }
  }
}

class TestStringAndBinaryBetweenKernel : public ::testing::Test {};

TEST(TestStringAndBinaryBetweenKernel, Random) {
  for (const auto& types : {BaseBinaryTypes(), StringTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto rand = random::RandomArrayGenerator(0x5416447);
      for (size_t i = 3; i < 10; i++) {
        for (auto null_probability : {0.0, 0.01, 0.1, 0.25, 0.5, 1.0}) {
          const int64_t length = static_cast<int64_t>(1ULL << i);
          auto tt = GetType(ty);
          auto metadata = key_value_metadata({"null_probability"},
                                             {std::to_string(null_probability)});
          auto field = ::arrow::field("[0,16]", std::move(ty), std::move(metadata));
          auto data1 = rand.ArrayOf(*field, length);
          auto data2 = rand.ArrayOf(*field, length);
          auto data3 = rand.ArrayOf(*field, length);

          auto array1 = Datum(*data1->View(tt));
          auto array2 = Datum(*data2->View(tt));
          auto array3 = Datum(*data3->View(tt));
          auto scalar1 = Datum(ScalarFromJSON(tt, R"("fupi")"));
          auto scalar2 = Datum(ScalarFromJSON(tt, R"("tupu")"));
          auto scalar3 = Datum(ScalarFromJSON(tt, R"("zito")"));
          ValidateBetween(scalar1, scalar2, scalar3);
          ValidateBetween(array1, scalar2, scalar3);
          ValidateBetween(scalar1, array2, scalar3);
          ValidateBetween(scalar1, scalar2, array3);
          ValidateBetween(scalar1, array2, array3);
          ValidateBetween(array1, scalar2, array3);
          ValidateBetween(array1, array2, scalar3);
          ValidateBetween(array1, array2, array3);
        }
      }
    }
  }
}

TEST(TestStringAndBinaryBetweenKernel, 3Scalars) {
  for (const auto& types : {BaseBinaryTypes(), StringTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto tt = GetType(ty);
      auto a = Datum(ScalarFromJSON(tt, R"("a")"));
      auto b = Datum(ScalarFromJSON(tt, R"("b")"));
      auto c = Datum(ScalarFromJSON(tt, R"("c")"));
      auto null = Datum(ScalarFromJSON(tt, R"("null")"));
      auto empty = Datum(ScalarFromJSON(tt, R"("")"));

      ValidateBetween(a, b, c);
      ValidateBetween(b, a, c);
      ValidateBetween(a, a, a);
      ValidateBetween(a, a, b);
      ValidateBetween(null, a, b);
      ValidateBetween(empty, a, b);
    }
  }
}

TEST(TestStringAndBinaryBetweenKernel, 1Array2Scalars) {
  for (const auto& types : {BaseBinaryTypes(), StringTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto tt = GetType(ty);
      auto a = Datum(ScalarFromJSON(tt, R"("abc")"));
      auto z = Datum(ScalarFromJSON(tt, R"("zzz")"));
      ValidateBetween(Datum(ArrayFromJSON(ty, "[]")), a, z);
      ValidateBetween(Datum(ArrayFromJSON(ty, "[null]")), a, z);
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["aaa", "aaaa", "ccc", "z"])")), a, z);
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")), a, z);
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["abd", null, null, "zzx"])")), a, z);
      ValidateBetween(a, Datum(ArrayFromJSON(ty, "[]")), z);
      ValidateBetween(a, Datum(ArrayFromJSON(ty, "[null]")), z);
      ValidateBetween(a, Datum(ArrayFromJSON(ty, R"(["aaa", "aaaa", "ccc", "z"])")), z);
      ValidateBetween(a, Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")), z);
      ValidateBetween(a, Datum(ArrayFromJSON(ty, R"(["abd", null, null, "zzx"])")), z);
      ValidateBetween(a, z, Datum(ArrayFromJSON(ty, "[]")));
      ValidateBetween(a, z, Datum(ArrayFromJSON(ty, "[null]")));
      ValidateBetween(a, z, Datum(ArrayFromJSON(ty, R"(["aaa", "aaaa", "ccc", "z"])")));
      ValidateBetween(a, z, Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")));
      ValidateBetween(a, z, Datum(ArrayFromJSON(ty, R"(["abd", null, null, "zzx"])")));
    }
  }
}

TEST(TestStringAndBinaryBetweenKernel, 2Arrays1Scalar) {
  for (const auto& types : {BaseBinaryTypes(), StringTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      auto tt = GetType(ty);
      auto s = Datum(ScalarFromJSON(tt, R"("zzz")"));
      ValidateBetween(s, Datum(ArrayFromJSON(ty, "[]")), Datum(ArrayFromJSON(ty, "[]")));
      ValidateBetween(s, Datum(ArrayFromJSON(ty, R"(["aaa", "aaaa", "ccc", "z"])")),
                      Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")));
      ValidateBetween(s, Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")),
                      Datum(ArrayFromJSON(ty, R"(["abd", null, null, "zzx"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, "[]")), s, Datum(ArrayFromJSON(ty, "[]")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["aaa", "aaaa", "ccc", "z"])")), s,
                      Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")), s,
                      Datum(ArrayFromJSON(ty, R"(["abd", null, null, "zzx"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, "[]")), Datum(ArrayFromJSON(ty, "[]")), s);
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["aaa", "aaaa", "ccc", "z"])")),
                      Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")), s);
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["abc", "baa", "fff", "zzz"])")),
                      Datum(ArrayFromJSON(ty, R"(["abd", null, null, "zzx"])")), s);
    }
  }
}

TEST(TestStringAndBinaryBetweenKernel, 3Arrays) {
  for (const auto& types : {BaseBinaryTypes(), StringTypes()}) {
    for (const std::shared_ptr<DataType>& ty : types) {
      ARROW_SCOPED_TRACE("type = ", ty->ToString());
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["david","hello","world"])")),
                      Datum(ArrayFromJSON(ty, R"(["adam","hi","whirl"])")),
                      Datum(ArrayFromJSON(ty, R"(["robert","goeiemoren","whirlwind"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["x","a","f"])")),
                      Datum(ArrayFromJSON(ty, R"(["w","a","e"])")),
                      Datum(ArrayFromJSON(ty, R"(["z","a","g"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["block","bit","binary"])")),
                      Datum(ArrayFromJSON(ty, R"(["bit","nibble","ternary"])")),
                      Datum(ArrayFromJSON(ty, R"(["word","d","xyz"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["よしもと","の","ち"])")),
                      Datum(ArrayFromJSON(ty, R"(["は","へ","あ"])")),
                      Datum(ArrayFromJSON(ty, R"(["な","を","ち"])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["A","ア","王"])")),
                      Datum(ArrayFromJSON(ty, R"(["た","あ","歩"])")),
                      Datum(ArrayFromJSON(ty, R"(["李","田",null])")));
      ValidateBetween(Datum(ArrayFromJSON(ty, R"(["Б",null,"Я"])")),
                      Datum(ArrayFromJSON(ty, R"(["А","Ж","Щ"])")),
                      Datum(ArrayFromJSON(ty, R"(["Д","Л","Ф"])")));
    }
  }
}

class TestTimestampsBetweenKernel : public ::testing::Test {};

TEST(TestTimestampsBetweenKernel, 3Scalars) {
  const std::string scalar1_json = R"("1980-02-02")";
  const std::string scalar2_json = R"("1970-01-01")";
  const std::string scalar3_json = R"("1970-01-02")";
  // Same units should be fine
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json)));
  // Different timezones should be fine
  ValidateBetween(
      Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND, "Africa/Cairo"), scalar3_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND, "America/Chicago"), scalar1_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND, "Asia/Beijing"), scalar2_json)));
  ValidateBetween(
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), scalar1_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "America/Phoenix"), scalar3_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), scalar2_json)));
  ValidateBetween(
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), scalar1_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Asia/Tokyo"), scalar2_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), scalar3_json)));
  // Different units should be fine
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::MILLI), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::NANO), scalar3_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::NANO), scalar2_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json)));
  // But comparing naive to zoned is not OK
  for (auto inclusive :
       {BetweenOptions::Inclusive::BOTH, BetweenOptions::Inclusive::LEFT,
        BetweenOptions::Inclusive::RIGHT, BetweenOptions::Inclusive::NEITHER}) {
    auto options = BetweenOptions(inclusive);
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json),
                ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json),
                ScalarFromJSON(timestamp(TimeUnit::SECOND, "Asia/Tokyo"), scalar2_json),
                options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(
            ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND, "America/New_York"), scalar3_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND, "Europe/Berlin"), scalar2_json),
            options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(
            ScalarFromJSON(timestamp(TimeUnit::SECOND, "Africa/Nairobi"), scalar1_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar3_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json), options, nullptr));
  }
}

TEST(TestTimestampsBetweenKernel, 1Array2Scalars) {
  const std::string scalar1_json = R"("1980-02-02")";
  const std::string scalar2_json = R"("1970-01-01")";
  const std::string array_json = R"(["1970-01-02","1980-02-02","1970-02-28"])";
  // Same units should be fine
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json)));
  // Different timezones should be fine
  ValidateBetween(
      Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND, "Africa/Cairo"), array_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND, "America/Chicago"), scalar1_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND, "Asia/Beijing"), scalar2_json)));
  ValidateBetween(
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), scalar1_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "America/Phoenix"), array_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), scalar2_json)));
  ValidateBetween(
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), scalar1_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Asia/Tokyo"), scalar2_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), array_json)));
  // Different units should be fine
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::MILLI), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::NANO), array_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json)));
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::NANO), scalar2_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json)));
  // But comparing naive to zoned is not OK
  for (auto inclusive :
       {BetweenOptions::Inclusive::BOTH, BetweenOptions::Inclusive::LEFT,
        BetweenOptions::Inclusive::RIGHT, BetweenOptions::Inclusive::NEITHER}) {
    auto options = BetweenOptions(inclusive);
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json),
                ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json),
                ScalarFromJSON(timestamp(TimeUnit::SECOND, "Asia/Tokyo"), scalar2_json),
                options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(
            ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar1_json),
            ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/New_York"), array_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND, "Europe/Berlin"), scalar2_json),
            options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(
            ScalarFromJSON(timestamp(TimeUnit::SECOND, "Africa/Nairobi"), scalar1_json),
            ArrayFromJSON(timestamp(TimeUnit::SECOND), array_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar2_json), options, nullptr));
  }
}

TEST(TestTimestampsBetweenKernel, 2Arrays1Scalar) {
  const std::string scalar_json = R"("1980-02-02")";
  const std::string array1_json = R"(["1970-01-01","1980-02-01","1970-02-28"])";
  const std::string array2_json = R"(["1970-01-02","1980-02-02","1970-02-28"])";
  // Same units should be fine
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)));
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)));
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json)));
  // Different timezones should be fine
  ValidateBetween(
      Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND, "Africa/Cairo"), scalar_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/Chicago"), array1_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND, "Asia/Beijing"), array2_json)));
  ValidateBetween(
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), array1_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "America/Phoenix"), scalar_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), array2_json)));
  ValidateBetween(
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), array1_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Asia/Tokyo"), array2_json)),
      Datum(ScalarFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), scalar_json)));
  // Different units should be fine
  ValidateBetween(Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::MILLI), array1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)));
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::NANO), scalar_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)));
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::NANO), array2_json)),
                  Datum(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json)));
  // But comparing naive to zoned is not OK
  for (auto inclusive :
       {BetweenOptions::Inclusive::BOTH, BetweenOptions::Inclusive::LEFT,
        BetweenOptions::Inclusive::RIGHT, BetweenOptions::Inclusive::NEITHER}) {
    auto options = BetweenOptions(inclusive);
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND, "Asia/Tokyo"), array2_json),
                options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(
            ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json),
            ScalarFromJSON(timestamp(TimeUnit::SECOND, "America/New_York"), scalar_json),
            ArrayFromJSON(timestamp(TimeUnit::SECOND, "Europe/Berlin"), array2_json),
            options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(ArrayFromJSON(timestamp(TimeUnit::SECOND, "Africa/Nairobi"), array1_json),
                ScalarFromJSON(timestamp(TimeUnit::SECOND), scalar_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json), options,
                nullptr));
  }
}

TEST(TestTimestampsBetweenKernel, 3Arrays) {
  const std::string array1_json = R"(["1970-01-01","1980-02-02","1970-02-28"])";
  const std::string array2_json = R"(["1970-01-01","1980-02-01","1970-02-28"])";
  const std::string array3_json = R"(["1970-01-02","1980-02-02","1970-02-28"])";
  // Same units should be fine
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array3_json)));
  // Different timezones should be fine
  ValidateBetween(
      Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND, "Africa/Cairo"), array1_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/Chicago"), array3_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND, "Asia/Beijing"), array2_json)));
  ValidateBetween(
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Europe/Berlin"), array2_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "America/Phoenix"), array3_json)),
      Datum(ArrayFromJSON(timestamp(TimeUnit::NANO, "Africa/Nairobi"), array1_json)));
  // Different units should be fine
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::MILLI), array2_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array3_json)));
  ValidateBetween(Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::NANO), array3_json)),
                  Datum(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json)));
  // But comparing naive to zoned is not OK
  for (auto inclusive :
       {BetweenOptions::Inclusive::BOTH, BetweenOptions::Inclusive::LEFT,
        BetweenOptions::Inclusive::RIGHT, BetweenOptions::Inclusive::NEITHER}) {
    auto options = BetweenOptions(inclusive);
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND), array3_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND, "Asia/Tokyo"), array2_json),
                options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(
            ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json),
            ArrayFromJSON(timestamp(TimeUnit::SECOND, "America/New_York"), array3_json),
            ArrayFromJSON(timestamp(TimeUnit::SECOND, "Europe/Berlin"), array1_json),
            options, nullptr));
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        TypeError,
        ::testing::HasSubstr(
            "Cannot compare timestamp with timezone to timestamp without timezone"),
        Between(ArrayFromJSON(timestamp(TimeUnit::SECOND, "Africa/Nairobi"), array3_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND), array1_json),
                ArrayFromJSON(timestamp(TimeUnit::SECOND), array2_json), options,
                nullptr));
  }
}

class TestBetweenDecimal : public ::testing::Test {};

TEST(TestBetweenDecimal, 3Scalars) {
  for (auto decimal_factory : {decimal128, decimal256}) {
    auto ty = decimal_factory(3, 2);
    ARROW_SCOPED_TRACE("Type =", ty->ToString());
    auto scalar1 = ScalarFromJSON(ty, R"("1.23")");
    auto scalar2 = ScalarFromJSON(ty, R"("2.34")");
    auto scalar3 = ScalarFromJSON(ty, R"("1.23")");

    ValidateBetween(scalar1, scalar2, ScalarFromJSON(ty, R"(null)"));
    ValidateBetween(scalar1, ScalarFromJSON(ty, R"(null)"), scalar3);
    ValidateBetween(ScalarFromJSON(ty, R"(null)"), scalar2, scalar3);
    ValidateBetween(scalar1, scalar2, scalar3);
    ValidateBetween(scalar1, scalar3, scalar2);
    ValidateBetween(scalar2, scalar1, scalar3);
    ValidateBetween(scalar3, scalar2, scalar1);
  }
}

TEST(TestBetweenDecimal, 1Array2Scalars) {
  for (auto decimal_factory : {decimal128, decimal256}) {
    auto ty = decimal_factory(3, 2);
    ARROW_SCOPED_TRACE("Type =", ty->ToString());
    auto array = ArrayFromJSON(
        ty, R"(["1.23", "1.22", "2.35", "-1.23", "-2.24", "1.23", "1.24", null])");
    auto scalar1 = ScalarFromJSON(ty, R"("2.34")");
    auto scalar2 = ScalarFromJSON(ty, R"("1.23")");

    ValidateBetween(array, scalar1, ScalarFromJSON(ty, R"(null)"));
    ValidateBetween(array, ScalarFromJSON(ty, R"(null)"), scalar2);
    ValidateBetween(scalar1, ScalarFromJSON(ty, R"(null)"), array);
    ValidateBetween(scalar1, array, ScalarFromJSON(ty, R"(null)"));
    ValidateBetween(array, scalar1, scalar2);
    ValidateBetween(array, scalar2, scalar1);
    ValidateBetween(scalar1, array, scalar2);
    ValidateBetween(scalar2, scalar1, array);
  }
}

TEST(TestBetweenDecimal, 2Arrays1Scalar) {
  for (auto decimal_factory : {decimal128, decimal256}) {
    auto ty = decimal_factory(3, 2);
    ARROW_SCOPED_TRACE("Type =", ty->ToString());
    auto array1 = ArrayFromJSON(
        ty, R"(["1.23", "1.22", "2.35", "-1.23", "-2.24", "1.23", "1.24", null])");
    auto array2 = ArrayFromJSON(
        ty, R"(["1.23", "1.23", "2.34", "-1.23", "-1.23", "1.23", "1.23", null])");
    auto scalar = ScalarFromJSON(ty, R"("1.23")");

    ValidateBetween(array1, array2, ScalarFromJSON(ty, R"(null)"));
    ValidateBetween(array1, ScalarFromJSON(ty, R"(null)"), array2);
    ValidateBetween(ScalarFromJSON(ty, R"(null)"), array2, array1);
    ValidateBetween(array1, array2, scalar);
    ValidateBetween(array1, scalar, array2);
    ValidateBetween(array2, array1, scalar);
    ValidateBetween(scalar, array2, array1);
  }
}

TEST(TestBetweenDecimal, 3Arrays) {
  for (auto decimal_factory : {decimal128, decimal256}) {
    auto ty = decimal_factory(3, 2);

    auto array1 = ArrayFromJSON(
        ty, R"(["1.23", "1.22", "2.35", "-1.23", "-2.24", "1.23", "1.24", null])");
    auto array2 = ArrayFromJSON(
        ty, R"(["1.23", "1.23", "2.34", "-1.23", "-1.23", "1.23", "1.23", null])");
    auto array3 = ArrayFromJSON(
        ty, R"(["1.23", "2.34", "1.23", "-1.23", "1.23", "-1.23", null, "1.23"])");

    ValidateBetween(ArrayFromJSON(ty, R"([])"), ArrayFromJSON(ty, R"([])"),
                    ArrayFromJSON(ty, R"([])"));
    ValidateBetween(ArrayFromJSON(ty, R"([null])"), ArrayFromJSON(ty, R"([null])"),
                    ArrayFromJSON(ty, R"([null])"));
    ValidateBetween(array1, array2, array3);
    ValidateBetween(array2, array1, array3);
    ValidateBetween(array1, array3, array2);
    ValidateBetween(array3, array2, array1);
  }
}

TEST(TestBetweenDecimal, DifferentParameters) {
  for (auto decimal_factory : {decimal128, decimal256}) {
    auto ty1 = decimal_factory(3, 2);
    auto ty2 = decimal_factory(4, 3);
    auto ty3 = decimal_factory(2, 3);

    auto array1 =
        ArrayFromJSON(ty1, R"(["1.23", "1.23", "2.34", "-1.23", "-1.23", "1.23"])");
    auto array2 =
        ArrayFromJSON(ty2, R"(["1.230", "2.340", null, "-1.230", "1.230", "-1.230"])");
    auto array3 =
        ArrayFromJSON(ty3, R"(["1.231", null, "2.210", "-1.230", "1.231", "-1.230"])");

    ValidateBetween(Datum(array1), Datum(array2), Datum(array3));
    ValidateBetween(Datum(array1), Datum(array3), Datum(array2));
    ValidateBetween(Datum(array2), Datum(array1), Datum(array3));
  }
}

class TestFixedSizeBinaryBetweenKernel : public ::testing::Test {};

// Helper to organize tests for fixed size binary comparisons
struct BetweenCase {
  std::shared_ptr<DataType> val_type;
  std::shared_ptr<DataType> lhs_type;
  std::shared_ptr<DataType> rhs_type;
  std::string val;
  std::string lhs;
  std::string rhs;
};

TEST(TestFixedSizeBinaryBetweenKernel, 3Scalars) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  const std::string scalar31 = R"("abc")";
  const std::string scalar32 = R"("abc")";
  const std::string scalar33 = R"("abd")";
  const std::string scalar11 = R"("a")";
  const std::string scalar12 = R"("b")";
  const std::string scalar13 = R"("a")";

  std::vector<BetweenCase> types = {
      {ty1, ty1, ty1, scalar31, scalar32, scalar33},
      {ty1, ty1, ty1, scalar33, scalar31, scalar32},
      {ty1, ty1, ty1, scalar32, scalar31, scalar33},
      {ty2, ty2, ty2, scalar11, scalar12, scalar13},
      {ty2, ty2, ty2, scalar12, scalar11, scalar13},
      {ty2, ty2, ty2, scalar13, scalar12, scalar11},
      {ty1, binary(), ty1, scalar32, scalar33, scalar31},
      {binary(), ty1, binary(), scalar32, scalar33, scalar31},
      {ty1, large_binary(), ty1, scalar32, scalar33, scalar31},
      {large_binary(), ty1, large_binary(), scalar31, scalar32, scalar33},
      {ty1, utf8(), ty1, scalar31, scalar32, scalar33},
      {utf8(), ty1, utf8(), scalar31, scalar32, scalar33},
      {ty1, large_utf8(), ty1, scalar32, scalar33, scalar31},
      {large_utf8(), ty1, large_utf8(), scalar31, scalar32, scalar33},
  };

  for (const auto& test_case : types) {
    const auto& val_type = test_case.val_type;
    const auto& lhs_type = test_case.lhs_type;
    const auto& rhs_type = test_case.rhs_type;
    auto val = Datum(ScalarFromJSON(val_type, test_case.val));
    auto lhs = Datum(ScalarFromJSON(lhs_type, test_case.lhs));
    auto rhs = Datum(ScalarFromJSON(rhs_type, test_case.rhs));

    ValidateBetween(Datum(ScalarFromJSON(val_type, R"(null)")), lhs, rhs);
    ValidateBetween(val, Datum(ScalarFromJSON(lhs_type, R"(null)")), rhs);
    ValidateBetween(val, lhs, Datum(ScalarFromJSON(rhs_type, R"(null)")));
    ValidateBetween(val, lhs, rhs);
  }
}

TEST(TestFixedSizeBinaryBetweenKernel, 1Array2Scalars) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  const std::string array31 = R"(["abc", "abd", null, "abc", "acc"])";
  const std::string scalar32 = R"("abc")";
  const std::string scalar33 = R"("abd")";
  const std::string array11 = R"(["a", "b", null, "d", "a"])";
  const std::string scalar12 = R"("b")";
  const std::string scalar13 = R"("a")";

  std::vector<BetweenCase> types = {
      {ty1, ty1, ty1, array31, scalar32, scalar33},
      {ty1, ty1, ty1, array31, scalar33, scalar32},
      {ty2, ty2, ty2, array11, scalar12, scalar13},
      {ty2, ty2, ty2, array11, scalar13, scalar12},
      {ty1, binary(), ty1, array31, scalar32, scalar33},
      {binary(), ty1, binary(), array31, scalar33, scalar32},
      {ty1, large_binary(), ty1, array31, scalar32, scalar33},
      {large_binary(), ty1, large_binary(), array31, scalar32, scalar33},
      {ty1, utf8(), ty1, array31, scalar32, scalar33},
      {utf8(), ty1, utf8(), array31, scalar33, scalar32},
      {ty1, large_utf8(), ty1, array31, scalar32, scalar33},
      {large_utf8(), ty1, large_utf8(), array31, scalar32, scalar33},
  };

  for (const auto& test_case : types) {
    const auto& val_type = test_case.val_type;
    const auto& lhs_type = test_case.lhs_type;
    const auto& rhs_type = test_case.rhs_type;
    auto val = Datum(ArrayFromJSON(val_type, test_case.val));
    auto lhs = Datum(ScalarFromJSON(lhs_type, test_case.lhs));
    auto rhs = Datum(ScalarFromJSON(rhs_type, test_case.rhs));

    ValidateBetween(Datum(ArrayFromJSON(val_type, R"([null])")),
                    Datum(ScalarFromJSON(lhs_type, "null")),
                    Datum(ScalarFromJSON(rhs_type, "null")));
    ValidateBetween(Datum(ScalarFromJSON(val_type, "null")),
                    Datum(ArrayFromJSON(lhs_type, R"([null])")),
                    Datum(ScalarFromJSON(rhs_type, "null")));
    ValidateBetween(Datum(ScalarFromJSON(val_type, "null")),
                    Datum(ScalarFromJSON(lhs_type, "null")),
                    Datum(ArrayFromJSON(rhs_type, R"([null])")));
    ValidateBetween(val, lhs, rhs);
    ValidateBetween(lhs, val, rhs);
    ValidateBetween(rhs, lhs, val);
  }
}

TEST(TestFixedSizeBinaryBetweenKernel, 2Arrays1Scalar) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  const std::string array31 = R"(["abc", "abd", null, "abc", "acc"])";
  const std::string array32 = R"(["abc", "abc", "abd", null, "abc"])";
  const std::string scalar33 = R"("abd")";
  const std::string array11 = R"(["a", "b", null, "d", "a"])";
  const std::string array12 = R"(["a", "a", "d", null, "a"])";
  const std::string array13 = R"("a")";

  std::vector<BetweenCase> types = {
      {ty1, ty1, ty1, array31, array32, scalar33},
      {ty1, ty1, ty1, array32, array31, scalar33},
      {ty2, ty2, ty2, array11, array12, array13},
      {ty2, ty2, ty2, array12, array11, array13},
      {ty1, binary(), ty1, array32, array31, scalar33},
      {binary(), ty1, binary(), array31, array32, scalar33},
      {ty1, large_binary(), ty1, array32, array31, scalar33},
      {large_binary(), ty1, large_binary(), array31, array32, scalar33},
      {ty1, utf8(), ty1, array31, array32, scalar33},
      {utf8(), ty1, utf8(), array31, array32, scalar33},
      {ty1, large_utf8(), ty1, array32, array31, scalar33},
      {large_utf8(), ty1, large_utf8(), array31, array32, scalar33},
  };

  for (const auto& test_case : types) {
    const auto& val_type = test_case.val_type;
    const auto& lhs_type = test_case.lhs_type;
    const auto& rhs_type = test_case.rhs_type;
    auto val = ArrayFromJSON(val_type, test_case.val);
    auto lhs = ArrayFromJSON(lhs_type, test_case.lhs);
    auto rhs = ScalarFromJSON(rhs_type, test_case.rhs);

    ValidateBetween(ArrayFromJSON(val_type, R"([null])"),
                    ArrayFromJSON(lhs_type, R"([null])"),
                    ScalarFromJSON(rhs_type, "null"));
    ValidateBetween(ArrayFromJSON(val_type, R"([null])"),
                    ScalarFromJSON(lhs_type, "null"),
                    ArrayFromJSON(rhs_type, R"([null])"));
    ValidateBetween(ArrayFromJSON(val_type, R"([null])"),
                    ScalarFromJSON(lhs_type, "null"),
                    ArrayFromJSON(rhs_type, R"([null])"));
    ValidateBetween(val, lhs, rhs);
    ValidateBetween(val, rhs, lhs);
    ValidateBetween(rhs, lhs, val);
    ValidateBetween(rhs, val, lhs);
  }
}

TEST(TestFixedSizeBinaryBetweenKernel, 3Arrays) {
  auto ty1 = fixed_size_binary(3);
  auto ty2 = fixed_size_binary(1);

  const std::string array31 = R"(["abc", "abd", null, "abc", "acc"])";
  const std::string array32 = R"(["abc", "abc", "abd", null, "abc"])";
  const std::string array33 = R"(["abc", "abd", "abc", "abc", null])";
  const std::string array11 = R"(["a", "b", null, "d", "a"])";
  const std::string array12 = R"(["a", "a", "d", null, "a"])";
  const std::string array13 = R"(["a", "d", "c", "a", null])";

  std::vector<BetweenCase> types = {
      {ty1, ty1, ty1, array31, array32, array33},
      {ty1, ty1, ty1, array33, array31, array32},
      {ty1, ty1, ty1, array32, array31, array33},
      {ty2, ty2, ty2, array11, array12, array13},
      {ty2, ty2, ty2, array12, array11, array13},
      {ty2, ty2, ty2, array13, array12, array11},
      {ty1, ty2, ty2, array32, array12, array11},
      {ty2, ty1, ty1, array12, array33, array31},
      {ty2, ty1, ty1, array12, array31, array33},
      {ty1, binary(), ty1, array32, array33, array31},
      {binary(), ty1, binary(), array32, array33, array31},
      {ty1, large_binary(), ty1, array32, array33, array31},
      {large_binary(), ty1, large_binary(), array31, array32, array33},
      {ty1, utf8(), ty1, array31, array32, array33},
      {utf8(), ty1, utf8(), array31, array32, array33},
      {ty1, large_utf8(), ty1, array32, array33, array31},
      {large_utf8(), ty1, large_utf8(), array31, array32, array33},
  };

  for (const auto& test_case : types) {
    const auto& val_type = test_case.val_type;
    const auto& lhs_type = test_case.lhs_type;
    const auto& rhs_type = test_case.rhs_type;
    auto val = ArrayFromJSON(val_type, test_case.val);
    auto lhs = ArrayFromJSON(lhs_type, test_case.lhs);
    auto rhs = ArrayFromJSON(rhs_type, test_case.rhs);

    ValidateBetween(ArrayFromJSON(val_type, R"([])"), ArrayFromJSON(lhs_type, R"([])"),
                    ArrayFromJSON(rhs_type, R"([])"));
    ValidateBetween(ArrayFromJSON(val_type, R"([null])"),
                    ArrayFromJSON(lhs_type, R"([null])"),
                    ArrayFromJSON(rhs_type, R"([null])"));
    ValidateBetween(val, lhs, rhs);
  }
}

}  // namespace compute
}  // namespace arrow
