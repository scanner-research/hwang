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

#include "hwang/video_index.h"
#include "hwang/descriptors.pb.h"

#include <string>
#include <vector>

namespace hwang {

VideoIndex VideoIndex::deserialize(const std::vector<uint8_t> &data) {
  proto::VideoIndex desc;
  desc.ParseFromArray(data.data(), data.size());
  return VideoIndex();
}

std::vector<uint8_t> VideoIndex::serialize() {
  proto::VideoIndex desc;
  std::vector<uint8_t> data(desc.ByteSizeLong());
  desc.SerializeToArray(data.data(), data.size());
  return data;
}

}
