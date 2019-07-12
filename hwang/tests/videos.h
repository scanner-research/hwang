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

#include "hwang/util/fs.h"

#include <thread>

namespace hwang {

struct TestVideoInfo {
  TestVideoInfo(const std::string &url) : data_url(url) {}

  std::string data_url;
};

const TestVideoInfo
    test_video_fragmented("https://storage.googleapis.com/scanner-data/"
                          "tutorial_assets/star_wars_heros.mp4");

const TestVideoInfo
    test_video_unfragmented("https://storage.googleapis.com/scanner-data/"
                            "tutorial_assets/star_wars_heros_faces.mp4");

const TestVideoInfo
    test_video_hevc("https://test-videos.co.uk/vids/bigbuckbunny/mp4/h265/1080/"
                    "Big_Buck_Bunny_1080_10s_1MB.mp4");

inline std::string download_video(const TestVideoInfo& info) {
  std::string local_video_path;
  temp_file(local_video_path);
  download(info.data_url, local_video_path);
  return local_video_path;
}

}
