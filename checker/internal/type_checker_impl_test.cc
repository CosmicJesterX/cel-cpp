// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "checker/internal/type_checker_impl.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "base/ast_internal/ast_impl.h"
#include "base/ast_internal/expr.h"
#include "checker/internal/test_ast_helpers.h"
#include "checker/internal/type_check_env.h"
#include "checker/type_check_issue.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/type.h"
#include "internal/status_macros.h"
#include "internal/testing.h"

namespace cel {
namespace checker_internal {

namespace {

using ::absl_testing::IsOk;
using ::cel::ast_internal::AstImpl;
using ::cel::ast_internal::Reference;
using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;

using Severity = TypeCheckIssue::Severity;

std::string SevString(Severity severity) {
  switch (severity) {
    case Severity::kDeprecated:
      return "Deprecated";
    case Severity::kError:
      return "Error";
    case Severity::kWarning:
      return "Warning";
    case Severity::kInformation:
      return "Information";
  }
}

}  // namespace
}  // namespace checker_internal

template <typename Sink>
void AbslStringify(Sink& sink, const TypeCheckIssue& issue) {
  absl::Format(&sink, "TypeCheckIssue(%s): %s",
               checker_internal::SevString(issue.severity()), issue.message());
}

namespace checker_internal {
namespace {

MATCHER_P2(IsIssueWithSubstring, severity, substring, "") {
  const TypeCheckIssue& issue = arg;
  if (issue.severity() == severity &&
      absl::StrContains(issue.message(), substring)) {
    return true;
  }

  *result_listener << "expected: " << SevString(severity) << " " << substring
                   << "\nactual: " << SevString(issue.severity()) << " "
                   << issue.message();

  return false;
}

MATCHER_P(IsVariableReference, var_name, "") {
  const Reference& reference = arg;
  if (reference.name() == var_name) {
    return true;
  }
  *result_listener << "expected: " << var_name
                   << "\nactual: " << reference.name();

  return false;
}

class TypeCheckerImplTest : public ::testing::Test {
 public:
  TypeCheckerImplTest() = default;

  absl::Status RegisterMinimalBuiltins(TypeCheckEnv& env) {
    FunctionDecl add_op;
    add_op.set_name("_+_");
    CEL_RETURN_IF_ERROR(add_op.AddOverload(
        MakeOverloadDecl("add_int_int", IntType(), IntType(), IntType())));

    FunctionDecl not_op;
    not_op.set_name("!_");
    CEL_RETURN_IF_ERROR(not_op.AddOverload(
        MakeOverloadDecl("logical_not",
                         /*return_type=*/BoolType{}, BoolType{})));
    FunctionDecl not_strictly_false;
    not_strictly_false.set_name("@not_strictly_false");
    CEL_RETURN_IF_ERROR(not_strictly_false.AddOverload(
        MakeOverloadDecl("not_strictly_false",
                         /*return_type=*/BoolType{}, DynType{})));
    FunctionDecl mult_op;
    mult_op.set_name("_*_");
    CEL_RETURN_IF_ERROR(mult_op.AddOverload(
        MakeOverloadDecl("mult_int_int",
                         /*return_type=*/IntType(), IntType(), IntType())));
    FunctionDecl or_op;
    or_op.set_name("_||_");
    CEL_RETURN_IF_ERROR(or_op.AddOverload(
        MakeOverloadDecl("logical_or",
                         /*return_type=*/BoolType{}, BoolType{}, BoolType{})));

    FunctionDecl and_op;
    and_op.set_name("_&&_");
    CEL_RETURN_IF_ERROR(and_op.AddOverload(
        MakeOverloadDecl("logical_and",
                         /*return_type=*/BoolType{}, BoolType{}, BoolType{})));

    FunctionDecl lt_op;
    lt_op.set_name("_<_");
    CEL_RETURN_IF_ERROR(lt_op.AddOverload(
        MakeOverloadDecl("lt_int_int",
                         /*return_type=*/BoolType{}, IntType(), IntType())));

    FunctionDecl gt_op;
    gt_op.set_name("_>_");
    CEL_RETURN_IF_ERROR(gt_op.AddOverload(
        MakeOverloadDecl("gt_int_int",
                         /*return_type=*/BoolType{}, IntType(), IntType())));

    FunctionDecl eq_op;
    eq_op.set_name("_==_");
    CEL_RETURN_IF_ERROR(eq_op.AddOverload(
        MakeOverloadDecl("eq_int_int",
                         /*return_type=*/BoolType{}, IntType(), IntType())));

    FunctionDecl to_int;
    to_int.set_name("int");
    CEL_RETURN_IF_ERROR(to_int.AddOverload(
        MakeOverloadDecl("to_int",
                         /*return_type=*/IntType(), DynType{})));

    env.InsertFunctionIfAbsent(std::move(not_op));
    env.InsertFunctionIfAbsent(std::move(not_strictly_false));
    env.InsertFunctionIfAbsent(std::move(add_op));
    env.InsertFunctionIfAbsent(std::move(mult_op));
    env.InsertFunctionIfAbsent(std::move(or_op));
    env.InsertFunctionIfAbsent(std::move(and_op));
    env.InsertFunctionIfAbsent(std::move(lt_op));
    env.InsertFunctionIfAbsent(std::move(gt_op));
    env.InsertFunctionIfAbsent(std::move(to_int));
    env.InsertFunctionIfAbsent(std::move(eq_op));

    return absl::OkStatus();
  }
};

TEST_F(TypeCheckerImplTest, SmokeTest) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("1 + 2"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
}

TEST_F(TypeCheckerImplTest, SimpleIdentsResolved) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));
  env.InsertVariableIfAbsent(MakeVariableDecl("y", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x + y"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
}

TEST_F(TypeCheckerImplTest, ReportMissingIdentDecl) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x + y"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_FALSE(result.IsValid());

  EXPECT_THAT(result.GetIssues(),
              ElementsAre(IsIssueWithSubstring(Severity::kError,
                                               "undeclared reference to 'y'")));
}

TEST_F(TypeCheckerImplTest, QualifiedIdentsResolved) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  env.InsertVariableIfAbsent(MakeVariableDecl("x.y", IntType()));
  env.InsertVariableIfAbsent(MakeVariableDecl("x.z", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x.y + x.z"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
}

TEST_F(TypeCheckerImplTest, ReportMissingQualifiedIdentDecl) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("y.x"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_FALSE(result.IsValid());

  EXPECT_THAT(result.GetIssues(),
              ElementsAre(IsIssueWithSubstring(
                  Severity::kError, "undeclared reference to 'y.x'")));
}

TEST_F(TypeCheckerImplTest, ResolveMostQualfiedIdent) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));
  env.InsertVariableIfAbsent(MakeVariableDecl("x.y", MapType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x.y.z"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  ASSERT_OK_AND_ASSIGN(auto checked_ast, result.ReleaseAst());
  auto& ast_impl = AstImpl::CastFromPublicAst(*checked_ast);
  EXPECT_THAT(ast_impl.reference_map(),
              Contains(Pair(_, IsVariableReference("x.y"))));
}

TEST_F(TypeCheckerImplTest, MemberFunctionCallResolved) {
  TypeCheckEnv env;

  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));

  env.InsertVariableIfAbsent(MakeVariableDecl("y", IntType()));
  FunctionDecl foo;
  foo.set_name("foo");
  ASSERT_THAT(foo.AddOverload(MakeMemberOverloadDecl("int_foo_int",
                                                     /*return_type=*/IntType(),
                                                     IntType(), IntType())),
              IsOk());
  env.InsertFunctionIfAbsent(std::move(foo));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x.foo(y)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
}

TEST_F(TypeCheckerImplTest, MemberFunctionCallNotDeclared) {
  TypeCheckEnv env;

  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));
  env.InsertVariableIfAbsent(MakeVariableDecl("y", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x.foo(y)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_FALSE(result.IsValid());

  EXPECT_THAT(result.GetIssues(),
              ElementsAre(IsIssueWithSubstring(
                  Severity::kError, "undeclared reference to 'foo'")));
}

TEST_F(TypeCheckerImplTest, FunctionShapeMismatch) {
  TypeCheckEnv env;
  // foo(int, int) -> int
  ASSERT_OK_AND_ASSIGN(
      auto foo,
      MakeFunctionDecl("foo", MakeOverloadDecl("foo_int_int", IntType(),
                                               IntType(), IntType())));
  env.InsertFunctionIfAbsent(foo);
  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("foo(1, 2, 3)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_FALSE(result.IsValid());

  EXPECT_THAT(result.GetIssues(),
              ElementsAre(IsIssueWithSubstring(
                  Severity::kError, "undeclared reference to 'foo'")));
}

TEST_F(TypeCheckerImplTest, NamespaceFunctionCallResolved) {
  TypeCheckEnv env;
  // Variables
  env.InsertVariableIfAbsent(MakeVariableDecl("x", IntType()));
  env.InsertVariableIfAbsent(MakeVariableDecl("y", IntType()));

  // add x.foo as a namespaced function.
  FunctionDecl foo;
  foo.set_name("x.foo");
  ASSERT_THAT(
      foo.AddOverload(MakeOverloadDecl("x_foo_int",
                                       /*return_type=*/IntType(), IntType())),
      IsOk());
  env.InsertFunctionIfAbsent(std::move(foo));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("x.foo(y)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());
  EXPECT_THAT(result.GetIssues(), IsEmpty());

  ASSERT_OK_AND_ASSIGN(auto checked_ast, result.ReleaseAst());
  auto& ast_impl = AstImpl::CastFromPublicAst(*checked_ast);
  EXPECT_TRUE(ast_impl.root_expr().has_call_expr())
      << absl::StrCat("kind: ", ast_impl.root_expr().kind().index());
  EXPECT_EQ(ast_impl.root_expr().call_expr().function(), "x.foo");
  EXPECT_FALSE(ast_impl.root_expr().call_expr().has_target());
}

TEST_F(TypeCheckerImplTest, ComprehensionVariablesResolved) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast,
                       MakeTestParsedAst("[1, 2, 3].exists(x, x * x > 10)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
}

TEST_F(TypeCheckerImplTest, NestedComprehensions) {
  TypeCheckEnv env;

  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(
      auto ast,
      MakeTestParsedAst("[1, 2].all(x, ['1', '2'].exists(y, int(y) == x))"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
}

TEST_F(TypeCheckerImplTest, ComprehensionVarsFollowNamespacePriorityRules) {
  TypeCheckEnv env;
  env.set_container("com");
  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  // Namespace resolution still applies, compre var doesn't shadow com.x
  env.InsertVariableIfAbsent(MakeVariableDecl("com.x", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast,
                       MakeTestParsedAst("['1', '2'].all(x, x == 2)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
  ASSERT_OK_AND_ASSIGN(auto checked_ast, result.ReleaseAst());
  auto& ast_impl = AstImpl::CastFromPublicAst(*checked_ast);
  EXPECT_THAT(ast_impl.reference_map(),
              Contains(Pair(_, IsVariableReference("com.x"))));
}

TEST_F(TypeCheckerImplTest, ComprehensionVarsFollowQualifiedIdentPriority) {
  TypeCheckEnv env;
  ASSERT_THAT(RegisterMinimalBuiltins(env), IsOk());

  // Namespace resolution still applies, compre var doesn't shadow x.y
  env.InsertVariableIfAbsent(MakeVariableDecl("x.y", IntType()));

  TypeCheckerImpl impl(std::move(env));
  ASSERT_OK_AND_ASSIGN(auto ast,
                       MakeTestParsedAst("[{'y': '2'}].all(x, x.y == 2)"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, impl.Check(std::move(ast)));

  EXPECT_TRUE(result.IsValid());

  EXPECT_THAT(result.GetIssues(), IsEmpty());
  ASSERT_OK_AND_ASSIGN(auto checked_ast, result.ReleaseAst());
  auto& ast_impl = AstImpl::CastFromPublicAst(*checked_ast);
  EXPECT_THAT(ast_impl.reference_map(),
              Contains(Pair(_, IsVariableReference("x.y"))));
}

}  // namespace
}  // namespace checker_internal
}  // namespace cel
