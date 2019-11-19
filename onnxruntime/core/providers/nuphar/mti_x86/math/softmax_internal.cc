// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/nuphar/mti_x86/math/softmax_internal.h"

#include "core/codegen/mti/math/binary_ops.h"
#include "core/codegen/mti/math/reduce_ops.h"
#include "core/codegen/mti/math/unary_ops.h"

#include "core/providers/nuphar/compiler/x86/x86_target_info.h"
#include "core/providers/nuphar/mti_x86/math/unary_ops.h"
#include "core/providers/nuphar/mti_x86/math/reduce_ops.h"
#include "gsl/gsl"

namespace onnxruntime {
namespace nuphar {
namespace internal {

static std::vector<int64_t> ReduceAxes(int64_t axis, std::size_t size) {
  std::vector<int64_t> reduce_axis;
  for (int64_t i = axis; i < gsl::narrow_cast<int64_t>(size); ++i)
    reduce_axis.push_back(i);
  return reduce_axis;
}

static int32_t FuseDim(int64_t axis) {
  if (axis == 0)
    return 0;  // fuse all
  else {
    return gsl::narrow_cast<int32_t>(axis) + 1;  // fuse all after axis
  }
}

tvm::Tensor SoftmaxInternal(const tvm::Tensor& input,
                            int64_t axis,
                            tvm_codegen::CodeGenContext& ctx_codegen,
                            const std::string& name,
                            bool logarithmic) {
  std ::vector<int64_t> reduce_axis = ReduceAxes(axis, input->shape.size());
  // TODO use natural vector size check by type later
  CodeGenTargetX86* target = dynamic_cast<CodeGenTargetX86*>(ctx_codegen.GetCodeGenHandle()->codegen_target);
  ORT_ENFORCE(target != nullptr, "CodeGen target unknown: not AVX/AVX2/AVX512 !");
#if 0
  int64_t vectorization_width = target->NaturalVectorWidth(input->dtype.bits());
#else
  int32_t vectorization_width = 16;
#endif
  int32_t fuse_dim = FuseDim(axis);
  auto max_element = ReduceMax(input, reduce_axis, true, vectorization_width, false, fuse_dim, name + "_reduce_max");
  auto x_shift = tvm_codegen::Sub(input, max_element, name + "_sub");
  auto exp_x = nuphar::Exp(x_shift, name + "_exp");
  auto exp_x_sum = ReduceSum(exp_x, reduce_axis, true, vectorization_width, false, fuse_dim, name + "_reduce_sum");

  if (logarithmic) {
    auto log_exp_x_sum = nuphar::Log(exp_x_sum, name + "_log");
    return tvm_codegen::Sub(x_shift, log_exp_x_sum, name + "_sub_log");
  } else {
    return tvm_codegen::Div(exp_x, exp_x_sum, name + "_div");
  }
}

}  // namespace internal
}  // namespace nuphar
}  // namespace onnxruntime
