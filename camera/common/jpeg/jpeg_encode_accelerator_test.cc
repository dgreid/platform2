/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/memory/shared_memory.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/threading/thread.h>
#include <brillo/message_loops/base_message_loop.h>
#include <gtest/gtest.h>
#include <libyuv.h>

#include "cros-camera/exif_utils.h"
#include "cros-camera/future.h"
#include "cros-camera/jpeg_compressor.h"
#include "cros-camera/jpeg_encode_accelerator.h"

namespace cros {

namespace tests {
// Environment to create test data for all test cases.
class JpegEncodeTestEnvironment;
JpegEncodeTestEnvironment* g_env;

namespace {

const size_t kInitializeRetryLimit = 5;
const unsigned int kInitRetrySleepIntervalUs = 1000000;

// Download test image URI.
const char* kDownloadTestImageURI1 =
    "https://storage.googleapis.com/chromiumos-test-assets-public/jpeg_test/"
    "bali_640x360_P420.yuv";
const char* kDownloadTestImageURI2 =
    "https://storage.googleapis.com/chromiumos-test-assets-public/jpeg_test/"
    "lake_4096x3072.yuv";

// Default test image file.
const char kDefaultJpegFilename1[] = "bali_640x360_P420.yuv:640x360";
const char kDefaultJpegFilename2[] = "lake_4096x3072.yuv:4096x3072";
// Threshold for mean absolute difference of hardware and software encode.
// Absolute difference is to calculate the difference between each pixel in two
// images. This is used for measuring of the similarity of two images.
const double kMeanDiffThreshold = 7.0;
const int kJpegDefaultQuality = 90;
}  // namespace

struct Frame {
  // The input content of the test YUV file.
  // It will be loaded after calling LoadFrame().
  std::string data_str;
  int width;
  int height;
  base::FilePath yuv_file;

  // Mapped memory of input file.
  std::unique_ptr<base::SharedMemory> in_shm;
  // Mapped memory of output buffer from hardware encoder.
  std::unique_ptr<base::SharedMemory> hw_out_shm;
  // Mapped memory of output buffer from software encoder.
  std::unique_ptr<base::SharedMemory> sw_out_shm;

  // Actual data size in |hw_out_shm|.
  uint32_t hw_out_size;
  // Actual data size in |sw_out_shm|.
  uint32_t sw_out_size;
};

class JpegEncodeAcceleratorTest : public ::testing::Test {
 public:
  JpegEncodeAcceleratorTest() {}

  ~JpegEncodeAcceleratorTest() {}

  void SetUp();
  void TearDown() {}

  bool StartJea();

  void ParseInputFileString(const char* yuv_filename,
                            int* width,
                            int* height,
                            base::FilePath* yuv_file);
  void LoadFrame(const char* yuv_filename, Frame* frame);
  void PrepareMemory(Frame* frame);
  bool GetSoftwareEncodeResult(Frame* frame);
  bool CompareHwAndSwResults(Frame* frame);
  double GetMeanAbsoluteDifference(uint8_t* hw_yuv_result,
                                   uint8_t* sw_yuv_result,
                                   size_t yuv_size);
  void EncodeTest(Frame* frame);
  void EncodeSyncCallback(base::Callback<void(int)> callback,
                          int32_t buffer_id,
                          int error);

 protected:
  std::unique_ptr<JpegEncodeAccelerator> jpeg_encoder_;

  Frame jpeg_frame1_;
  Frame jpeg_frame2_;

 private:
  DISALLOW_COPY_AND_ASSIGN(JpegEncodeAcceleratorTest);
};

class JpegEncodeTestEnvironment : public ::testing::Environment {
 public:
  JpegEncodeTestEnvironment(const char* yuv_filename1,
                            const char* yuv_filename2,
                            bool save_to_file) {
    yuv_filename1_ = yuv_filename1 ? yuv_filename1 : kDefaultJpegFilename1;
    yuv_filename2_ = yuv_filename2 ? yuv_filename2 : kDefaultJpegFilename2;
    save_to_file_ = save_to_file;
    mojo_manager_ = CameraMojoChannelManager::CreateInstance();
  }

  const char* yuv_filename1_;
  const char* yuv_filename2_;
  bool save_to_file_;
  std::unique_ptr<CameraMojoChannelManager> mojo_manager_;
};

void JpegEncodeAcceleratorTest::SetUp() {
  jpeg_encoder_ =
      JpegEncodeAccelerator::CreateInstance(g_env->mojo_manager_.get());
}

bool JpegEncodeAcceleratorTest::StartJea() {
  size_t retry_count = 0;
  while (retry_count < kInitializeRetryLimit) {
    if (jpeg_encoder_->Start()) {
      return true;
    }
    usleep(kInitRetrySleepIntervalUs);
    retry_count++;
  }
  return false;
}

void JpegEncodeAcceleratorTest::ParseInputFileString(const char* yuv_filename,
                                                     int* width,
                                                     int* height,
                                                     base::FilePath* yuv_file) {
  std::vector<std::string> filename_and_size =
      base::SplitString(yuv_filename, std::string(1, ':'),
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(2, filename_and_size.size());
  std::string filename(filename_and_size[0]);

  std::vector<std::string> image_resolution =
      base::SplitString(filename_and_size[1], std::string(1, 'x'),
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(2u, image_resolution.size());
  ASSERT_TRUE(base::StringToInt(image_resolution[0], width));
  ASSERT_TRUE(base::StringToInt(image_resolution[1], height));

  *yuv_file = base::FilePath(filename);
}

void JpegEncodeAcceleratorTest::LoadFrame(const char* yuv_filename,
                                          Frame* frame) {
  ParseInputFileString(yuv_filename, &frame->width, &frame->height,
                       &frame->yuv_file);
  base::FilePath yuv_filepath = frame->yuv_file;
  if (!PathExists(yuv_filepath)) {
    LOG(ERROR) << "There is no test image file: " << yuv_filepath.value();
    LOG(ERROR) << "You may download one from " << kDownloadTestImageURI1;
    LOG(ERROR) << " Or from " << kDownloadTestImageURI2;
    return;
  }

  LOG(INFO) << "Read file:" << yuv_filepath.value();
  ASSERT_TRUE(base::ReadFileToString(yuv_filepath, &frame->data_str));

  VLOG(1) << "GetWidth() = " << frame->width
          << ",GetHeight() = " << frame->height;
}

void JpegEncodeAcceleratorTest::PrepareMemory(Frame* frame) {
  size_t input_size = frame->data_str.size();
  // Prepare enought size for encoded JPEG.
  size_t output_size = frame->width * frame->height * 3 / 2;
  if (!frame->in_shm.get() || input_size > frame->in_shm->mapped_size()) {
    frame->in_shm.reset(new base::SharedMemory);
    LOG_ASSERT(frame->in_shm->CreateAndMapAnonymous(input_size));
  }
  memcpy(frame->in_shm->memory(), frame->data_str.data(), input_size);

  if (!frame->hw_out_shm.get() ||
      output_size > frame->hw_out_shm->mapped_size()) {
    frame->hw_out_shm.reset(new base::SharedMemory);
    LOG_ASSERT(frame->hw_out_shm->CreateAndMapAnonymous(output_size));
  }
  memset(frame->hw_out_shm->memory(), 0, output_size);

  if (!frame->sw_out_shm.get() ||
      output_size > frame->sw_out_shm->mapped_size()) {
    frame->sw_out_shm.reset(new base::SharedMemory);
    LOG_ASSERT(frame->sw_out_shm->CreateAndMapAnonymous(output_size));
  }
  memset(frame->sw_out_shm->memory(), 0, output_size);
}

double JpegEncodeAcceleratorTest::GetMeanAbsoluteDifference(
    uint8_t* hw_yuv_result, uint8_t* sw_yuv_result, size_t yuv_size) {
  double total_difference = 0;
  for (size_t i = 0; i < yuv_size; i++)
    total_difference += std::abs(hw_yuv_result[i] - sw_yuv_result[i]);
  return total_difference / yuv_size;
}

bool JpegEncodeAcceleratorTest::GetSoftwareEncodeResult(Frame* frame) {
  std::unique_ptr<JpegCompressor> compressor(
      JpegCompressor::GetInstance(g_env->mojo_manager_.get()));
  if (!compressor->CompressImage(
          frame->in_shm->memory(), frame->width, frame->height,
          kJpegDefaultQuality, nullptr, 0, frame->sw_out_shm->mapped_size(),
          frame->sw_out_shm->memory(), &frame->sw_out_size,
          JpegCompressor::Mode::kSwOnly)) {
    LOG(ERROR) << "Software encode failed.";
    return false;
  }
  return true;
}

bool JpegEncodeAcceleratorTest::CompareHwAndSwResults(Frame* frame) {
  int width = frame->width;
  int height = frame->height;
  size_t yuv_size = width * height * 3 / 2;
  uint8_t* hw_yuv_result = new uint8_t[yuv_size];
  int y_stride = width;
  int u_stride = width / 2;
  int v_stride = u_stride;
  if (libyuv::ConvertToI420(
          static_cast<const uint8_t*>(frame->hw_out_shm->memory()),
          frame->hw_out_size, hw_yuv_result, y_stride,
          hw_yuv_result + y_stride * height, u_stride,
          hw_yuv_result + y_stride * height + u_stride * height / 2, v_stride,
          0, 0, width, height, width, height, libyuv::kRotate0,
          libyuv::FOURCC_MJPG)) {
    LOG(ERROR) << "Convert HW encoded result to YUV failed";
  }

  uint8_t* sw_yuv_result = new uint8_t[yuv_size];
  if (libyuv::ConvertToI420(
          static_cast<const uint8_t*>(frame->sw_out_shm->memory()),
          frame->sw_out_size, sw_yuv_result, y_stride,
          sw_yuv_result + y_stride * height, u_stride,
          sw_yuv_result + y_stride * height + u_stride * height / 2, v_stride,
          0, 0, width, height, width, height, libyuv::kRotate0,
          libyuv::FOURCC_MJPG)) {
    LOG(ERROR) << "Convert SW encoded result to YUV failed";
  }

  double difference =
      GetMeanAbsoluteDifference(hw_yuv_result, sw_yuv_result, yuv_size);
  delete[] hw_yuv_result;
  delete[] sw_yuv_result;

  if (difference > kMeanDiffThreshold) {
    LOG(ERROR) << "HW and SW encode results are not similar enough. diff = "
               << difference;
    return false;
  } else {
    return true;
  }
}

void JpegEncodeAcceleratorTest::EncodeTest(Frame* frame) {
  base::SharedMemoryHandle input_handle;
  base::SharedMemoryHandle output_handle;
  int input_fd, output_fd;
  int status;

  // Clear HW encode results.
  memset(frame->hw_out_shm->memory(), 0, frame->hw_out_shm->mapped_size());

  input_handle = frame->in_shm->handle();
  output_handle = frame->hw_out_shm->handle();
  input_fd = base::SharedMemory::GetFdFromSharedMemoryHandle(input_handle);
  output_fd = base::SharedMemory::GetFdFromSharedMemoryHandle(output_handle);
  VLOG(1) << "input fd " << input_fd << " output fd " << output_fd;

  ExifUtils utils;
  ASSERT_TRUE(utils.Initialize());
  ASSERT_TRUE(utils.SetImageWidth(frame->width));
  ASSERT_TRUE(utils.SetImageLength(frame->height));
  std::vector<uint8_t> thumbnail;
  thumbnail.resize(0);
  utils.GenerateApp1(thumbnail.data(), 0);

  // Pretend the shared memory as DMA buffer. Since we use mmap to get the user
  // space address, it won't cause any problems.
  status = jpeg_encoder_->EncodeSync(
      input_fd, nullptr, frame->in_shm->mapped_size(), frame->width,
      frame->height, utils.GetApp1Buffer(), utils.GetApp1Length(), output_fd,
      frame->hw_out_shm->mapped_size(), &frame->hw_out_size);
  EXPECT_EQ(status, JpegEncodeAccelerator::ENCODE_OK);
  if (status == static_cast<int>(JpegEncodeAccelerator::ENCODE_OK)) {
    if (g_env->save_to_file_) {
      base::FilePath encoded_file = frame->yuv_file.ReplaceExtension(".jpg");
      base::WriteFile(encoded_file,
                      static_cast<char*>(frame->hw_out_shm->memory()),
                      frame->hw_out_size);
    }

    EXPECT_EQ(true, GetSoftwareEncodeResult(frame));
    EXPECT_EQ(true, CompareHwAndSwResults(frame));
  }
}

TEST_F(JpegEncodeAcceleratorTest, InitTest) {
  ASSERT_EQ(StartJea(), true);
}

TEST_F(JpegEncodeAcceleratorTest, EncodeTest) {
  ASSERT_EQ(StartJea(), true);
  LoadFrame(g_env->yuv_filename1_, &jpeg_frame1_);
  PrepareMemory(&jpeg_frame1_);
  EncodeTest(&jpeg_frame1_);
}

TEST_F(JpegEncodeAcceleratorTest, EncodeTestFor2Resolutions) {
  ASSERT_EQ(StartJea(), true);
  LoadFrame(g_env->yuv_filename1_, &jpeg_frame1_);
  LoadFrame(g_env->yuv_filename2_, &jpeg_frame2_);
  PrepareMemory(&jpeg_frame1_);
  EncodeTest(&jpeg_frame1_);
  PrepareMemory(&jpeg_frame2_);
  EncodeTest(&jpeg_frame2_);
}

TEST_F(JpegEncodeAcceleratorTest, Encode60Images) {
  LoadFrame(g_env->yuv_filename1_, &jpeg_frame1_);
  PrepareMemory(&jpeg_frame1_);
  ASSERT_EQ(StartJea(), true);
  for (int i = 0; i < 60; i++) {
    EncodeTest(&jpeg_frame1_);
  }
}

TEST_F(JpegEncodeAcceleratorTest, Encode1000Images) {
  LoadFrame(g_env->yuv_filename1_, &jpeg_frame1_);
  PrepareMemory(&jpeg_frame1_);
  ASSERT_EQ(StartJea(), true);
  for (int i = 0; i < 1000; i++) {
    EncodeTest(&jpeg_frame1_);
  }
}

}  // namespace tests
}  // namespace cros

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  base::AtExitManager exit_manager;

  // Needed to enable VLOG through --vmodule.
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  LOG_ASSERT(logging::InitLogging(settings));

  const base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  DCHECK(cmd_line);

  const char* yuv_filename1 = nullptr;
  const char* yuv_filename2 = nullptr;
  bool save_to_file = false;
  base::CommandLine::SwitchMap switches = cmd_line->GetSwitches();
  for (base::CommandLine::SwitchMap::const_iterator it = switches.begin();
       it != switches.end(); ++it) {
    if (it->first == "yuv_filename1") {
      yuv_filename1 = it->second.c_str();
      continue;
    }
    if (it->first == "yuv_filename2") {
      yuv_filename2 = it->second.c_str();
      continue;
    }
    if (it->first == "save_to_file") {
      save_to_file = true;
      continue;
    }
    if (it->first == "v" || it->first == "vmodule")
      continue;
    if (it->first == "h" || it->first == "help")
      continue;
    LOG(ERROR) << "Unexpected switch: " << it->first << ":" << it->second;
    return -EINVAL;
  }

  brillo::BaseMessageLoop message_loop;
  message_loop.SetAsCurrent();

  cros::tests::g_env =
      reinterpret_cast<cros::tests::JpegEncodeTestEnvironment*>(
          testing::AddGlobalTestEnvironment(
              new cros::tests::JpegEncodeTestEnvironment(
                  yuv_filename1, yuv_filename2, save_to_file)));

  return RUN_ALL_TESTS();
}
