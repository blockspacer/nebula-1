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

#include <string>
#include "Table.h"
#include "type/Serde.h"

/**
 * Nebula test table used for integration test.
 * Used to turn on/off test hooks.
 */
namespace nebula {
namespace meta {

class TestTable : public Table {
  static constexpr auto NAME = "nebula.test";

public:
  TestTable() : Table(NAME) {
    // TODO(cao) - let's make date as a number
    schema_ = nebula::type::TypeSerializer::from(
      "ROW<_time_: bigint, id:int, event:string, items:list<string>, flag:bool, value:tinyint, weight:double, stamp:int128>");
  }

  virtual const Column& column(const std::string& col) const noexcept override {
    if (col == "id") {
      // enable bloom filter on id column
      static const Column COL_ID{ true, false, "", {} };
      return COL_ID;
    }

    if (col == "event") {
      // place an access rule on event column requiring user to be in nebula-users to read
      static const Column COL_EVENT{
        false,
        true,
        "",
        { AccessRule{ AccessType::READ, { "nebula-users" }, ActionType::MASK } }
      };
      return COL_EVENT;
    }

    if (col == "value") {
      static const Column COL_VALUE{ false, false, "23", {} };
      return COL_VALUE;
    }

    if (col == "stamp") {
      static const Column COL_STAMP{ false, false, "128", {} };
      return COL_STAMP;
    }

    return Table::column(col);
  }
};

} // namespace meta
} // namespace nebula