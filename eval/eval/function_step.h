#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_

#include <stdint.h>

#include <memory>

#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "absl/status/statusor.h"
#include "eval/eval/evaluator_core.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_function_provider.h"
#include "eval/public/cel_value.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

// Factory method for Call-based execution step where the function will be
// resolved at runtime (lazily) from an input Activation.
absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateFunctionStep(
    const google::api::expr::v1alpha1::Expr::Call* call, int64_t expr_id,
    std::vector<const CelFunctionProvider*>& lazy_overloads);

// Factory method for Call-based execution step where the function has been
// statically resolved from a set of eagerly functions configured in the
// CelFunctionRegistry.
absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateFunctionStep(
    const google::api::expr::v1alpha1::Expr::Call* call, int64_t expr_id,
    std::vector<const CelFunction*>& overloads);

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_
