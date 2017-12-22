/* Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "hwang/util/cuda.h"

namespace hwang {

#ifdef HAVE_CUDA

#include <cuda_runtime.h>

cudaError_t convertNV12toRGBA(
    const uint8_t *in,
    size_t inPitch, uint8_t *out,
    size_t outPitch,
    int width, int height,
    cudaStream_t stream);

#endif

}
