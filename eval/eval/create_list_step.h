#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "base/ast_internal/expr.h"
#include "eval/eval/evaluator_core.h"

namespace google::api::expr::runtime {

// Factory method for CreateList which constructs an immutable list.
absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateCreateListStep(
    const cel::ast_internal::CreateList& create_list_expr, int64_t expr_id);

// Factory method for CreateList which constructs a mutable list as the list
// construction step is generated by a macro AST rewrite rather than by a user
// entered expression.
absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateCreateMutableListStep(
    const cel::ast_internal::CreateList& create_list_expr, int64_t expr_id);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_
