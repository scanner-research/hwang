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

#include "glog/logging.h"

namespace hwang {

enum class DeviceType {
  CPU = 0,
  GPU = 1,
};

struct DeviceHandle {
 public:
  bool operator==(const DeviceHandle& other) {
    return type == other.type && id == other.id;
  }

  bool operator!=(const DeviceHandle& other) { return !(*this == other); }

  bool operator<(const DeviceHandle& other) const {
    return type < other.type && id < other.id;
  }

  bool can_copy_to(const DeviceHandle& other) {
    return !(this->type == DeviceType::GPU && other.type == DeviceType::GPU &&
             this->id != other.id);
  }

  bool is_same_address_space(const DeviceHandle& other) {
    return this->type == other.type &&
           ((this->type == DeviceType::CPU) ||
            (this->type == DeviceType::GPU && this->id == other.id));
  }

  DeviceType type;
  int32_t id;
};

static const DeviceHandle CPU_DEVICE = {DeviceType::CPU, 0};

}
