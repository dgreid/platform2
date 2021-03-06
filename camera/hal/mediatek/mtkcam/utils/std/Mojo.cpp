/*
 * Copyright (C) 2020 MediaTek Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/******************************************************************************
 *
 *****************************************************************************/
#include <cros-camera/camera_mojo_channel_manager_token.h>
#include <mtkcam/utils/std/Mojo.h>
/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace Utils {

static cros::CameraMojoChannelManagerToken* gMojoManagerToken = nullptr;

/******************************************************************************
 *
 ******************************************************************************/
cros::CameraMojoChannelManagerToken* getMojoManagerToken() {
  return gMojoManagerToken;
}

/******************************************************************************
 *
 ******************************************************************************/
void setMojoManagerToken(cros::CameraMojoChannelManagerToken* token) {
  gMojoManagerToken = token;
}

};  // namespace Utils
};  // namespace NSCam
