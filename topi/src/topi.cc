/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
* \brief Registration of TVM operators and schedules
* \file topi.cc
*/
#define TOPI_REDUCE_ATLEAST1D 0

#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/module.h>
#include <tvm/runtime/registry.h>
#include <tvm/packed_func_ext.h>
#include <tvm/build_module.h>

#include <topi/broadcast.h>
#include <topi/elemwise.h>
#include <topi/nn.h>
#include <topi/reduction.h>
#include <topi/transform.h>

#include <topi/nn/bias_add.h>
#include <topi/nn/bnn.h>
#include <topi/nn/dense.h>
#include <topi/nn/dilate.h>
#include <topi/nn/flatten.h>
#include <topi/nn/mapping.h>
#include <topi/nn/pooling.h>
#include <topi/nn/softmax.h>
#include <topi/nn/upsampling.h>
#include <topi/nn/l2_normalize.h>
#include <topi/nn/local_response_norm.h>
#include <topi/nn/batch_matmul.h>

#include <topi/vision/reorg.h>
#include <topi/image/resize.h>
#include <topi/generic/default.h>
#include <topi/generic/extern.h>
#include <topi/generic/injective.h>

#include <topi/cuda/dense.h>
#include <topi/cuda/injective.h>
#include <topi/cuda/pooling.h>
#include <topi/cuda/reduction.h>
#include <topi/cuda/softmax.h>
#include <topi/cuda/normalization.h>

#include <topi/x86/bnn.h>
#include <topi/x86/default.h>
#include <topi/x86/injective.h>

#include <topi/rocm/dense.h>
#include <topi/rocm/injective.h>
#include <topi/rocm/pooling.h>
#include <topi/rocm/reduction.h>
#include <topi/rocm/softmax.h>
#include <topi/rocm/normalization.h>

namespace topi {

using namespace tvm;
using namespace tvm::runtime;

/*! \brief Canonicalize an argument that may be Array<Expr> or int to Array<Expr> */
Array<Integer> ArrayOrInt(TVMArgValue arg) {
  if (arg.type_code() == kDLInt || arg.type_code() == kDLUInt) {
    Array<Integer> result;
    result.push_back(arg.operator int());
    return result;
  } else {
    return arg;
  }
}

inline bool IsTensorType(TVMArgValue arg) {
  return (arg.type_code() == kObjectHandle &&
          static_cast<Object*>(
              arg.value().v_handle)->IsInstance<tvm::TensorNode>());
}


TVM_REGISTER_GLOBAL("topi.TEST_create_target")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = tvm::Target::Create(args[0]);
  });

/* Ops from broadcast.h */
#define TOPI_REGISTER_BCAST_OP(OpName, Op)                              \
  TVM_REGISTER_GLOBAL(OpName)                                           \
  .set_body([](TVMArgs args, TVMRetValue *rv) {                         \
      bool lhs_is_tensor = IsTensorType(args[0]);                       \
      bool rhs_is_tensor = IsTensorType(args[1]);                       \
      if (lhs_is_tensor && rhs_is_tensor) {                             \
        *rv = Op(args[0].operator tvm::Tensor(),                        \
                 args[1].operator tvm::Tensor());                       \
      } else if (!lhs_is_tensor && rhs_is_tensor) {                     \
        *rv = Op(args[0].operator tvm::Expr(),                          \
                 args[1].operator tvm::Tensor());                       \
      } else if (lhs_is_tensor && !rhs_is_tensor) {                     \
        *rv = Op(args[0].operator tvm::Tensor(),                        \
                 args[1].operator tvm::Expr());                         \
      } else if (!lhs_is_tensor && !rhs_is_tensor) {                    \
        *rv = Op(args[0].operator tvm::Expr(),                          \
                 args[1].operator tvm::Expr());                         \
      }                                                                 \
    });                                                                 \

TOPI_REGISTER_BCAST_OP("topi.add", topi::add);
TOPI_REGISTER_BCAST_OP("topi.subtract", topi::subtract);
TOPI_REGISTER_BCAST_OP("topi.multiply", topi::multiply);
TOPI_REGISTER_BCAST_OP("topi.divide", topi::divide);
TOPI_REGISTER_BCAST_OP("topi.floor_divide", topi::floor_divide);
TOPI_REGISTER_BCAST_OP("topi.mod", topi::mod);
TOPI_REGISTER_BCAST_OP("topi.floor_mod", topi::floor_mod);
TOPI_REGISTER_BCAST_OP("topi.maximum", topi::maximum);
TOPI_REGISTER_BCAST_OP("topi.minimum", topi::minimum);
TOPI_REGISTER_BCAST_OP("topi.power", topi::power);
TOPI_REGISTER_BCAST_OP("topi.left_shift", topi::left_shift);
TOPI_REGISTER_BCAST_OP("topi.logical_and", topi::logical_and);
TOPI_REGISTER_BCAST_OP("topi.logical_or", topi::logical_or);
TOPI_REGISTER_BCAST_OP("topi.right_shift", topi::right_shift);
TOPI_REGISTER_BCAST_OP("topi.greater", topi::greater);
TOPI_REGISTER_BCAST_OP("topi.less", topi::less);
TOPI_REGISTER_BCAST_OP("topi.equal", topi::equal);
TOPI_REGISTER_BCAST_OP("topi.not_equal", topi::not_equal);
TOPI_REGISTER_BCAST_OP("topi.greater_equal", topi::greater_equal);
TOPI_REGISTER_BCAST_OP("topi.less_equal", topi::less_equal);

TVM_REGISTER_GLOBAL("topi.broadcast_to")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = broadcast_to(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.logical_not")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = logical_not(args[0]);
  });

/* Ops from elemwise.h */
TVM_REGISTER_GLOBAL("topi.exp")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = exp(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.erf")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = erf(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.cos")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = cos(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.sin")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = sin(args[0]);
  });
TVM_REGISTER_GLOBAL("topi.tanh")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = tanh(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.atan")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = atan(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.sigmoid")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = sigmoid(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.sqrt")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = sqrt(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.rsqrt")
.set_body([](TVMArgs args, TVMRetValue *rv) {
*rv = rsqrt(args[0]);
});

TVM_REGISTER_GLOBAL("topi.log")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = log(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.identity")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = identity(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.negative")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = negative(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.clip")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = clip(args[0], args[1], args[2]);
  });

TVM_REGISTER_GLOBAL("topi.cast")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = cast(args[0], args[1]);
  });


TVM_REGISTER_GLOBAL("topi.reinterpret")
.set_body([](TVMArgs args, TVMRetValue* rv) {
  *rv = reinterpret(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.elemwise_sum")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = elemwise_sum(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.sign")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = sign(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.full")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = full(args[0], args[1], args[2]);
  });

TVM_REGISTER_GLOBAL("topi.full_like")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = full_like(args[0], args[1]);
  });

/* Ops from nn.h */
TVM_REGISTER_GLOBAL("topi.nn.relu")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = relu<float>(args[0]);
  });

TVM_REGISTER_GLOBAL("topi.nn.leaky_relu")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = leaky_relu(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.nn.prelu")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = prelu(args[0], args[1], args[2]);
  });

TVM_REGISTER_GLOBAL("topi.nn.pad")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = pad(args[0], args[1], args[2], args[3]);
  });

/* Ops from reduction.h */
TVM_REGISTER_GLOBAL("topi.sum")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::sum(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.min")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::min(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.max")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::max(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.argmin")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::argmin(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.argmax")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::argmax(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.prod")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::prod(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.all")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::all(args[0], ArrayOrInt(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.any")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::any(args[0], ArrayOrInt(args[1]), args[2]);
  });

/* Ops from transform.h */
TVM_REGISTER_GLOBAL("topi.expand_dims")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = expand_dims(args[0], args[1], args[2]);
  });

TVM_REGISTER_GLOBAL("topi.transpose")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = transpose(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.flip")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = flip(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.reshape")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = reshape(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.squeeze")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = squeeze(args[0], ArrayOrInt(args[1]));
  });

TVM_REGISTER_GLOBAL("topi.concatenate")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = concatenate(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.stack")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = stack(args[0], args[1]);
});

TVM_REGISTER_GLOBAL("topi.shape")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = shape(args[0], args[1]);
});

TVM_REGISTER_GLOBAL("topi.ndarray_size")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = ndarray_size(args[0], args[1]);
});

TVM_REGISTER_GLOBAL("topi.split")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  if (args[1].type_code() == kDLInt || args[1].type_code() == kDLUInt) {
    *rv = split_sections(args[0], args[1], args[2]);
  } else {
    *rv = split(args[0], args[1], args[2]);
  }
});

TVM_REGISTER_GLOBAL("topi.layout_transform")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = layout_transform(args[0], args[1], args[2]);
});

TVM_REGISTER_GLOBAL("topi.take")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  if (args.size() == 3) {
    std::string mode = args[2];
    *rv = take(args[0], args[1], mode);
  } else {
    int axis = args[2];
    std::string mode = args[3];
    *rv = take(args[0], args[1], axis, mode);
  }
  });

TVM_REGISTER_GLOBAL("topi.sequence_mask")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  double pad_val = args[2];
  int axis = args[3];
  *rv = sequence_mask(args[0], args[1], pad_val, axis);
});


TVM_REGISTER_GLOBAL("topi.where")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = where(args[0], args[1], args[2]);
});

TVM_REGISTER_GLOBAL("topi.arange")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = arange(args[0], args[1], args[2], args[3]);
});

TVM_REGISTER_GLOBAL("topi.repeat")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = repeat(args[0], args[1], args[2]);
});

TVM_REGISTER_GLOBAL("topi.tile")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = tile(args[0], args[1]);
});

TVM_REGISTER_GLOBAL("topi.gather_nd")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = gather_nd(args[0], args[1]);
});

TVM_REGISTER_GLOBAL("topi.matmul")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  switch ( args.size() ) {
    case 2: *rv = matmul(args[0], args[1]); break;
    case 3: *rv = matmul(args[0], args[1], args[2]); break;
    case 4: *rv = matmul(args[0], args[1], args[2], args[3]); break;
    default: CHECK(0) << "topi.matmul expects 2, 3 or 4 arguments";
  }});

TVM_REGISTER_GLOBAL("topi.tensordot")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  if (args.size() == 2) {
    *rv = tensordot(args[0], args[1]);
  } else if (args.size() == 3) {
    *rv = tensordot(args[0], args[1], args[2]);
  } else {
    Array<Expr> axes = args[3];
    *rv = tensordot(args[0], args[1], args[2], axes);
  }
  });

TVM_REGISTER_GLOBAL("topi.strided_slice")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = strided_slice(args[0], args[1], args[2], args[3]);
  });

TVM_REGISTER_GLOBAL("topi.one_hot")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  int depth = args[3];
  int axis = args[4];
  DataType dtype = args[5];
  *rv = one_hot(args[0], args[1], args[2], depth, axis, dtype);
  });

/* Ops from nn/upsampling.h */
TVM_REGISTER_GLOBAL("topi.nn.upsampling")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::upsampling(args[0], args[1], args[2], args[3]);
  });

/* Ops from nn/bnn.h */
TVM_REGISTER_GLOBAL("topi.nn.binarize_pack")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::binarize_pack(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.nn.binary_dense")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::binary_dense(args[0], args[1]);
  });

/* Ops from nn/dense.h */
TVM_REGISTER_GLOBAL("topi.nn.dense")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::dense(args[0], args[1], args[2], args[3]);
  });

/* Ops from nn/bias_add.h */
TVM_REGISTER_GLOBAL("topi.nn.bias_add")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::bias_add(args[0], args[1], args[2]);
  });

/* Ops from nn/batch_matmul.h */
TVM_REGISTER_GLOBAL("topi.nn.batch_matmul")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::batch_matmul(args[0], args[1]);
  });

/* Ops from nn/dilate.h */
TVM_REGISTER_GLOBAL("topi.nn.dilate")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::dilate(args[0], args[1]);
  });

/* Ops from nn/flatten.h */
TVM_REGISTER_GLOBAL("topi.nn.flatten")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::flatten(args[0]);
  });

/* Ops from nn/mapping.h */
TVM_REGISTER_GLOBAL("topi.nn.scale_shift_nchw")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::scale_shift_nchw(args[0], args[1], args[2]);
  });

TVM_REGISTER_GLOBAL("topi.nn.scale_shift_nhwc")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::scale_shift_nhwc(args[0], args[1], args[2]);
  });

/* Ops from nn/pooling.h */
TVM_REGISTER_GLOBAL("topi.nn.pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::pool(args[0], args[1], args[2], args[3],
                 static_cast<nn::PoolType>(static_cast<int>(args[4])),
                 args[5], args[6], args[7]);
  });

TVM_REGISTER_GLOBAL("topi.nn.pool_grad")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::pool_grad(args[0], args[1], args[2], args[3], args[4],
                 static_cast<nn::PoolType>(static_cast<int>(args[5])),
                 args[6], args[7], args[8]);
  });

TVM_REGISTER_GLOBAL("topi.nn.global_pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::global_pool(args[0],
                        static_cast<nn::PoolType>(static_cast<int>(args[1])));
  });

TVM_REGISTER_GLOBAL("topi.nn.adaptive_pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::adaptive_pool(args[0], args[1],
                          static_cast<nn::PoolType>(static_cast<int>(args[2])),
                          args[3]);
});

/* Ops from nn/softmax.h */
TVM_REGISTER_GLOBAL("topi.nn.softmax")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::softmax(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.nn.log_softmax")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::log_softmax(args[0]);
  });

/* Ops from nn/l2_normalize.h */
TVM_REGISTER_GLOBAL("topi.nn.l2_normalize")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::l2_normalize(args[0], static_cast<double>(args[1]), args[2]);
  });

TVM_REGISTER_GLOBAL("topi.nn.lrn")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = nn::lrn(args[0], args[1], args[2],
                static_cast<double>(args[3]),
                static_cast<double>(args[4]),
                static_cast<double>(args[5]));
  });

TVM_REGISTER_GLOBAL("topi.vision.reorg")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = vision::reorg(args[0], args[1]);
  });

/* Ops from image/resize.h */
TVM_REGISTER_GLOBAL("topi.image.bilinear_sample_nchw")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = image::bilinear_sample_nchw(args[0], args[1], args[2], args[3]);
  });

TVM_REGISTER_GLOBAL("topi.image.resize")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = image::resize(args[0], args[1], args[2], args[3], args[4]);
  });

/* Generic schedules */
TVM_REGISTER_GLOBAL("topi.generic.default_schedule")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  if (args[2]) {
    *rv = topi::generic::default_schedule_auto_inline(args[0], args[1]);
  } else {
    *rv = topi::generic::default_schedule(args[0], args[1]);
  }
  });

TVM_REGISTER_GLOBAL("topi.generic.schedule_extern")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::generic::schedule_extern(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.generic.schedule_injective")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::generic::schedule_injective(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.generic.schedule_injective_from_existing")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::generic::schedule_injective_from_existing(args[0], args[1]);
 });

/* x86 schedules */
TVM_REGISTER_GLOBAL("topi.x86.schedule_binarize_pack")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::x86::schedule_binarize_pack(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.x86.schedule_binary_dense")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::x86::schedule_binary_dense(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.x86.default_schedule")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  if (args[2]) {
    *rv = topi::x86::default_schedule_auto_inline(args[0], args[1]);
  } else {
    *rv = topi::x86::default_schedule(args[0], args[1]);
  }
  });

TVM_REGISTER_GLOBAL("topi.x86.schedule_injective")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::x86::schedule_injective(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.x86.schedule_injective_from_existing")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::x86::schedule_injective_from_existing(args[0], args[1]);
 });

/* ROCm schedules */
TVM_REGISTER_GLOBAL("topi.rocm.dense_cuda")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = rocm::dense_rocm(args[0], args[1], args[2], args[3], args[4]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_dense")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_dense(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_injective")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_injective(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_injective_from_existing")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_injective_from_existing(args[0], args[1]);
 });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_pool(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_global_pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_global_pool(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_reduce")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_reduce(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_softmax")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_softmax(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_lrn")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_lrn(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.rocm.schedule_l2_normalize")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::rocm::schedule_l2_normalize(args[0], args[1]);
  });

/* CUDA schedules */
TVM_REGISTER_GLOBAL("topi.cuda.dense_cuda")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = cuda::dense_cuda(args[0], args[1], args[2], args[3], args[4]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_dense")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_dense(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_injective")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_injective(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_injective_from_existing")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_injective_from_existing(args[0], args[1]);
 });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_pool(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_global_pool")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_global_pool(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_reduce")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_reduce(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_softmax")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_softmax(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_lrn")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_lrn(args[0], args[1]);
  });

TVM_REGISTER_GLOBAL("topi.cuda.schedule_l2_normalize")
.set_body([](TVMArgs args, TVMRetValue *rv) {
  *rv = topi::cuda::schedule_l2_normalize(args[0], args[1]);
  });

/*! \brief Builder function for instantiating schedules. */
using FTVMScheduleBuilder = std::function<
  tvm::Schedule(const tvm::Target& target, const tvm::Array<tvm::Tensor>& outs)>;

/*!
 * \brief Helper function for registering generic functions matching the
 * FTVMScheduleBuilder signature. The schedule builder function is wrapped
 * with a PackedFunc suitable for passing to a tvm::GenericFunc.
 *
 * \param builder The schedule builder to wrap.
 *
 * \return The wrapped schedule builder
 */
inline PackedFunc WrapSchedule(FTVMScheduleBuilder builder) {
  return PackedFunc([builder](TVMArgs args, TVMRetValue* ret) {
    auto target = Target::Current(false);
    Array<Tensor> outs;
    NodeRef argNodeRef = args[0];
    if (argNodeRef->type_index() == outs->type_index()) {
      outs = args[0];
    } else {
      outs = Array<Tensor> { args[0] };
    }

    *ret = builder(target, outs);
  });
}

TVM_REGISTER_GENERIC_FUNC(schedule_injective)
.set_default(WrapSchedule(topi::generic::schedule_injective))
.register_func({ "cpu" }, WrapSchedule(topi::x86::schedule_injective))
.register_func({ "cuda", "gpu" }, WrapSchedule(topi::cuda::schedule_injective));

TVM_REGISTER_GENERIC_FUNC(schedule_softmax)
.set_default(WrapSchedule(topi::generic::default_schedule))
.register_func({ "cpu" }, WrapSchedule(topi::x86::default_schedule))
.register_func({ "cuda", "gpu" }, WrapSchedule(topi::cuda::schedule_softmax));

TVM_REGISTER_GENERIC_FUNC(schedule_dense)
.set_default(WrapSchedule(topi::generic::default_schedule))
.register_func({ "cuda", "gpu" }, WrapSchedule(topi::cuda::schedule_dense))
.register_func({ "rocm" }, WrapSchedule(topi::rocm::schedule_dense));

TVM_REGISTER_GENERIC_FUNC(schedule_batch_matmul)
.set_default(WrapSchedule(topi::generic::default_schedule));

TVM_REGISTER_GENERIC_FUNC(schedule_pool)
.set_default(WrapSchedule(topi::generic::default_schedule))
.register_func({ "cpu" }, WrapSchedule(topi::x86::default_schedule))
.register_func({ "cuda", "gpu" }, WrapSchedule(topi::cuda::schedule_pool));

TVM_REGISTER_GENERIC_FUNC(schedule_global_pool)
.set_default(WrapSchedule(topi::generic::default_schedule))
.register_func({ "cpu" }, WrapSchedule(topi::x86::default_schedule))
.register_func({ "cuda", "gpu" }, WrapSchedule(topi::cuda::schedule_global_pool));

TVM_REGISTER_GENERIC_FUNC(schedule_reduce)
.set_default(WrapSchedule(topi::generic::default_schedule_auto_inline))
.register_func({ "cpu" }, WrapSchedule(topi::x86::default_schedule_auto_inline))
.register_func({ "cuda", "gpu" }, WrapSchedule(topi::cuda::schedule_reduce));

TVM_REGISTER_GENERIC_FUNC(schedule_binarize_pack)
.set_default(WrapSchedule(topi::generic::default_schedule))
.register_func({ "cpu" }, WrapSchedule(topi::x86::schedule_binarize_pack));

TVM_REGISTER_GENERIC_FUNC(schedule_binary_dense)
.set_default(WrapSchedule(topi::generic::default_schedule))
.register_func({ "cpu" }, WrapSchedule(topi::x86::schedule_binary_dense));

/*! \brief Builder function for instantiating schedules from existing schedules. */
using FTVMScheduleFromExistingBuilder = std::function<
  tvm::Schedule(tvm::Schedule sch, const tvm::Tensor& out)>;

/*!
 * \brief Helper function for registering generic functions matching the
 * FTVMScheduleFromExistingBuilder signature. The schedule builder function is wrapped
 * with a PackedFunc suitable for passing to a tvm::GenericFunc.
 *
 * \param builder The schedule builder to wrap.
 *
 * \return The wrapped schedule builder
 */
inline PackedFunc WrapScheduleFromExisting(FTVMScheduleFromExistingBuilder builder) {
  return PackedFunc([builder](TVMArgs args, TVMRetValue* ret) {
    *ret = builder(args[0], args[1]);
  });
}

TVM_REGISTER_GENERIC_FUNC(schedule_injective_from_existing)
.set_default(WrapScheduleFromExisting(topi::generic::schedule_injective_from_existing))
.register_func({ "cpu" }, WrapScheduleFromExisting(topi::x86::schedule_injective_from_existing))
.register_func({ "cuda", "gpu" }, WrapScheduleFromExisting(topi::cuda::schedule_injective_from_existing));

/*! \brief Builder function for instantiating dense ops. */
using FTVMDenseOpBuilder = std::function<tvm::Tensor(const Target& target,
                                                     const tvm::Tensor& data,
                                                     const tvm::Tensor& weight,
                                                     const tvm::Tensor& bias,
                                                     const Type& out_dtype)>;

/*!
* \brief Helper function for registering dense ops matching the
* FTVMDenseOpBuilder signature. The op builder function is wrapped
* with a PackedFunc suitable for passing to a tvm::GenericFunc.
*
* \param builder The op builder to wrap.
*
* \return The wrapped op builder
*/
inline PackedFunc WrapDenseOp(FTVMDenseOpBuilder builder) {
  return PackedFunc([builder](TVMArgs args, TVMRetValue* ret) {
    auto target = Target::Current(false);
    Tensor data = args[0];
    Tensor weight = args[1];
    Tensor bias = args[2];
    Type out_dtype = args[3];

    *ret = builder(target, data, weight, bias, out_dtype);
  });
}

TVM_REGISTER_GENERIC_FUNC(dense)
.set_default(WrapDenseOp([](const Target& target,
                            const tvm::Tensor& data,
                            const tvm::Tensor& weight,
                            const tvm::Tensor& bias,
                            const Type& out_dtype) {
  return topi::nn::dense(data, weight, bias, out_dtype);
}))
.register_func({ "cuda", "gpu" }, WrapDenseOp(topi::cuda::dense_cuda))
.register_func({ "rocm" }, WrapDenseOp(topi::rocm::dense_rocm));

}  // namespace topi
