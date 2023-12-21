// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Program steps for lazily initialized aliases (e.g. cel.bind).
//
// When used, any reference to variable should be replaced with a conditional
// step that either runs the initialization routine or pushes the already
// initialized variable to the stack.
//
// All references to the variable should be replaced with:
//
// +-----------------+-------------------+--------------------+
// |    stack        |       pc          |    step            |
// +-----------------+-------------------+--------------------+
// |    {}           |       0           | check init slot(i) |
// +-----------------+-------------------+--------------------+
// |    {value}      |       1           | assign slot(i)     |
// +-----------------+-------------------+--------------------+
// |    {value}      |       2           | <expr using value> |
// +-----------------+-------------------+--------------------+
// |                  ....                                    |
// +-----------------+-------------------+--------------------+
// |    {...}        | n (end of scope)  | clear slot(i)      |
// +-----------------+-------------------+--------------------+

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_LAZY_INIT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_LAZY_INIT_STEP_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "eval/eval/evaluator_core.h"

namespace google::api::expr::runtime {

// Creates a guard step that checks that an alias is initialized.
// If it is, push to stack and jump to the step that depends on the value.
// Otherwise, run the initialization routine (which pushes the value to top of
// stack) and set the corresponding slot.
//
// stack_delta should be the worst case stack requirement initializing (calling
// the subexpression).
std::unique_ptr<ExpressionStep> CreateCheckLazyInitStep(
    size_t slot_index, size_t subexpression_index, int stack_delta,
    int64_t expr_id);

// Helper step to assign a slot value from the top of stack on initialization.
//
// stack_delta is used along with the corresponding CheckLazyInitStep to offset
// the worst case stack growth if the subexpression is initialized at that
// point.
std::unique_ptr<ExpressionStep> CreateAssignSlotStep(size_t slot_index,
                                                     int stack_delta);
std::unique_ptr<ExpressionStep> CreateAssignSlotAndPopStep(size_t slot_index);

// Helper step to clear a slot.
// Slots may be reused in different contexts so need to be cleared after a
// context is done.
std::unique_ptr<ExpressionStep> CreateClearSlotStep(size_t slot_index,
                                                    int64_t expr_id);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_LAZY_INIT_STEP_H_
