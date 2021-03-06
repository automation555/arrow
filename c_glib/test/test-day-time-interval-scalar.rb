# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

class TestDayTimeIntervalScalar < Test::Unit::TestCase
  def setup
    @value = Arrow::DayMillisecond.new(3, 100)
    @scalar = Arrow::DayTimeIntervalScalar.new(@value)
  end

  def test_data_type
    assert_equal(Arrow::DayTimeIntervalDataType.new,
                 @scalar.data_type)
  end

  def test_valid?
    assert do
      @scalar.valid?
    end
  end

  def test_equal
    assert_equal(Arrow::DayTimeIntervalScalar.new(@value),
                 @scalar)
  end

  def test_to_s
    assert_equal("3d100ms", @scalar.to_s)
  end

  def test_value
    assert_equal(@value, @scalar.value)
  end
end
