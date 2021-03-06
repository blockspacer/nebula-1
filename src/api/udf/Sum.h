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

#include "surface/eval/UDF.h"

/**
 * Define expressions used in the nebula DSL.
 */
namespace nebula {
namespace api {
namespace udf {

// UDAF - sum
template <nebula::type::Kind IK,
          typename Traits = nebula::surface::eval::UdfTraits<nebula::surface::eval::UDFType::SUM, IK>,
          typename BaseType = nebula::surface::eval::UDAF<Traits::Type, Traits::Store, IK>>
class Sum : public BaseType {
public:
  using InputType = typename BaseType::InputType;
  using StoreType = typename BaseType::StoreType;
  using NativeType = typename BaseType::NativeType;

  Sum(const std::string& name, std::unique_ptr<nebula::surface::eval::ValueEval> expr)
    : BaseType(name,
               std::move(expr),

               // stack method
               {},

               // merge method
               [](StoreType ov, StoreType nv) {
                 return ov + nv;
               }) {}
  virtual ~Sum() = default;
};

template <>
Sum<nebula::type::Kind::INVALID>::Sum(
  const std::string&, std::unique_ptr<nebula::surface::eval::ValueEval>);

template <>
Sum<nebula::type::Kind::BOOLEAN>::Sum(
  const std::string&, std::unique_ptr<nebula::surface::eval::ValueEval>);

template <>
Sum<nebula::type::Kind::VARCHAR>::Sum(
  const std::string&, std::unique_ptr<nebula::surface::eval::ValueEval>);

template <>
Sum<nebula::type::Kind::INT128>::Sum(
  const std::string& name, std::unique_ptr<nebula::surface::eval::ValueEval> expr);

} // namespace udf
} // namespace api
} // namespace nebula