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

#include "gtest/gtest.h"
#include "Errors.h"
#include "Tree.h"
#include "fmt/format.h"
#include "glog/logging.h"

namespace nebula {
namespace type {
namespace test {
using namespace nebula::common;

TEST(TypeTest, Dummy) {
  EXPECT_EQ(4, 2 + 2);
  EXPECT_EQ(fmt::format("a{}", 1), "a1");
  N_ENSURE_EQ(4, 2 + 2);

  LOG(INFO) << fmt::format("The date is {}", 9);

  for (auto i = 0; i < 10; ++i) {
    LOG(INFO) << "COUNTING: " << i;
  }
}

TEST(TreeTest, Basic) {
  Tree<int> tree(0);
  EXPECT_EQ(tree.size(), 0);
}

} // namespace test
} // namespace type
} // namespace nebula