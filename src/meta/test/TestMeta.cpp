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

#include <fmt/format.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "meta/ClusterInfo.h"
#include "meta/NBlock.h"
#include "meta/Table.h"
#include "meta/TestTable.h"

namespace nebula {
namespace meta {
namespace test {

TEST(MetaTest, TestTestTable) {
  nebula::meta::TestTable test;
  LOG(INFO) << "Table provides table logic data and physical data";
  LOG(INFO) << "Test table name: " << test.name();
  LOG(INFO) << "Test table schema: " << test.schema();
}

TEST(MetaTest, TestNBlock) {
  BlockState state;
  NBlock<int> b1(BlockSignature{ "mock", 0, 5, 10 }, NNode::inproc(), state);

  // check in range
  ASSERT_TRUE(b1.inRange(5));
  ASSERT_TRUE(b1.inRange(10));
  ASSERT_TRUE(b1.inRange(7));
  ASSERT_FALSE(b1.inRange(11));
  ASSERT_FALSE(b1.inRange(1));

  // check overlap
  ASSERT_TRUE(b1.overlap({ 10, 11 }));
  ASSERT_TRUE(b1.overlap({ 5, 11 }));
  ASSERT_TRUE(b1.overlap({ 2, 11 }));
  ASSERT_TRUE(b1.overlap({ 2, 5 }));
  ASSERT_FALSE(b1.overlap({ 2, 4 }));
  ASSERT_FALSE(b1.overlap({ 12, 18 }));
}

TEST(MetaTest, TestNNode) {
  NNode n1{ NRole::NODE, "1.0.0.1", 90 };
  NNode n2{ n1 };
  ASSERT_TRUE(n1.equals(n2));
  LOG(INFO) << "N2=" << n2.toString();
}

TEST(MetaTest, TestClusterConfigLoad) {
  auto yamlFile = "configs/test.yml";
  auto& clusterInfo = nebula::meta::ClusterInfo::singleton();
  clusterInfo.load(yamlFile);

  // verify data against config file
  const auto& nodes = clusterInfo.nodes();
  EXPECT_EQ(nodes.size(), 1);
  for (auto itr = nodes.cbegin(); itr != nodes.cend(); ++itr) {
    LOG(INFO) << "NODE: " << itr->toString();
  }

  const auto& tables = clusterInfo.tables();
  EXPECT_EQ(tables.size(), 1);
  for (auto itr = tables.cbegin(); itr != tables.cend(); ++itr) {
    LOG(INFO) << "TABLE: " << (*itr)->toString();
  }

  // test the table level settings can be read as expected
  auto test = (*tables.cbegin());
  EXPECT_EQ(test->settings.size(), 2);
  EXPECT_EQ(test->settings.at("key1"), "value1");
  EXPECT_EQ(test->settings.at("key2"), "value2");
  LOG(INFO) << "key1=" << test->settings.at("key1");
}

TEST(MetaTest, TestAccessRules) {
  auto schema = nebula::type::TypeSerializer::from(
    "ROW<_time_: bigint, id:int, event:string, email:string, fund:bigint>");

  // column level access control - mask email when user is not in the group of pii_sg
  ColumnProps columnProps;
  columnProps["email"] = Column{
    false,
    false,
    "*@*",
    { AccessRule{ AccessType::READ, { "pii_sg" }, ActionType::MASK } }
  };
  columnProps["fund"] = Column{
    false,
    false,
    "0",
    { AccessRule{ AccessType::READ, { "pii_sg" }, ActionType::DENY } }
  };

  // no table level access control, pass empty {} access rules
  nebula::meta::Table test{
    "pii_table",
    schema,
    columnProps,
    {}
  };

  // assume user has two normal groups
  std::unordered_set<std::string> groups;
  groups.emplace("eng", "ads");

  // run check access function to ensure it works as expected
#define TEST_COL_READ_ACCESS(COL, EXPECT) \
  EXPECT_EQ(test.checkAccess(AccessType::READ, groups, COL), ActionType::EXPECT);

  // 1. table access - pass
  EXPECT_EQ(test.checkAccess(AccessType::READ, groups), ActionType::PASS);

  // 2. column id access - pass
  TEST_COL_READ_ACCESS("id", PASS)

  // 3. column event access - pass
  TEST_COL_READ_ACCESS("event", PASS)

  // 4. column email access - mask
  TEST_COL_READ_ACCESS("email", MASK)

  // 5. column fund access - deny
  TEST_COL_READ_ACCESS("fund", DENY)

  // now, let's put this user into pii_sg group and run the test again
  groups.emplace("pii_sg");
  TEST_COL_READ_ACCESS("id", PASS)
  TEST_COL_READ_ACCESS("event", PASS)
  TEST_COL_READ_ACCESS("email", PASS)
  TEST_COL_READ_ACCESS("fund", PASS)

#undef TEST_COL_READ_ACCESS
}

} // namespace test
} // namespace meta
} // namespace nebula