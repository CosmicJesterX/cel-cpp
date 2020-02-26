#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_BUILD_WARNING_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_BUILD_WARNING_H_

#include <vector>

#include "absl/status/status.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

// Container for recording warnings.
class BuilderWarnings {
 public:
  explicit BuilderWarnings(bool fail_immediately = false)
      : fail_immediately_(fail_immediately) {}

  // Add a warning. Returns the util:Status immediately if fail on warning is
  // set.
  absl::Status AddWarning(const absl::Status& warning);

  // Return the list of recorded warnings.
  const std::vector<absl::Status>& warnings() const { return warnings_; }

 private:
  std::vector<absl::Status> warnings_;
  bool fail_immediately_;
};

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_BUILD_WARNING_H_
