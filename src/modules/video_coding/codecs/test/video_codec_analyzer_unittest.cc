/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/test/video_codec_analyzer.h"

#include "absl/types/optional.h"
#include "api/video/i420_buffer.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"

namespace webrtc {
namespace test {

namespace {
using ::testing::Return;
using ::testing::Values;

const size_t kTimestamp = 3000;
const size_t kSpatialIdx = 2;

class MockReferenceVideoSource
    : public VideoCodecAnalyzer::ReferenceVideoSource {
 public:
  MOCK_METHOD(VideoFrame, GetFrame, (uint32_t, Resolution), (override));
};

VideoFrame CreateVideoFrame(uint32_t timestamp_rtp,
                            uint8_t y = 0,
                            uint8_t u = 0,
                            uint8_t v = 0) {
  rtc::scoped_refptr<I420Buffer> buffer(I420Buffer::Create(2, 2));

  libyuv::I420Rect(buffer->MutableDataY(), buffer->StrideY(),
                   buffer->MutableDataU(), buffer->StrideU(),
                   buffer->MutableDataV(), buffer->StrideV(), 0, 0,
                   buffer->width(), buffer->height(), y, u, v);

  return VideoFrame::Builder()
      .set_video_frame_buffer(buffer)
      .set_timestamp_rtp(timestamp_rtp)
      .build();
}

EncodedImage CreateEncodedImage(uint32_t timestamp_rtp, int spatial_idx = 0) {
  EncodedImage encoded_image;
  encoded_image.SetTimestamp(timestamp_rtp);
  encoded_image.SetSpatialIndex(spatial_idx);
  return encoded_image;
}
}  // namespace

TEST(VideoCodecAnalyzerTest, EncodeStartedCreatesFrameStats) {
  TaskQueueForTest task_queue;
  VideoCodecAnalyzer analyzer(task_queue);
  analyzer.StartEncode(CreateVideoFrame(kTimestamp));

  auto fs = analyzer.GetStats()->GetFrameStatistics();
  EXPECT_EQ(1u, fs.size());
  EXPECT_EQ(fs[0].rtp_timestamp, kTimestamp);
}

TEST(VideoCodecAnalyzerTest, EncodeFinishedUpdatesFrameStats) {
  TaskQueueForTest task_queue;
  VideoCodecAnalyzer analyzer(task_queue);
  analyzer.StartEncode(CreateVideoFrame(kTimestamp));

  EncodedImage encoded_frame = CreateEncodedImage(kTimestamp, kSpatialIdx);
  analyzer.FinishEncode(encoded_frame);

  auto fs = analyzer.GetStats()->GetFrameStatistics();
  EXPECT_EQ(2u, fs.size());
  EXPECT_TRUE(fs[1].encoding_successful);
}

TEST(VideoCodecAnalyzerTest, DecodeStartedNoFrameStatsCreatesFrameStats) {
  TaskQueueForTest task_queue;
  VideoCodecAnalyzer analyzer(task_queue);
  analyzer.StartDecode(CreateEncodedImage(kTimestamp, kSpatialIdx));

  auto fs = analyzer.GetStats()->GetFrameStatistics();
  EXPECT_EQ(1u, fs.size());
  EXPECT_EQ(fs[0].rtp_timestamp, kTimestamp);
}

TEST(VideoCodecAnalyzerTest, DecodeStartedFrameStatsExistsReusesFrameStats) {
  TaskQueueForTest task_queue;
  VideoCodecAnalyzer analyzer(task_queue);
  analyzer.StartEncode(CreateVideoFrame(kTimestamp));
  analyzer.StartDecode(CreateEncodedImage(kTimestamp, /*spatial_idx=*/0));

  auto fs = analyzer.GetStats()->GetFrameStatistics();
  EXPECT_EQ(1u, fs.size());
}

TEST(VideoCodecAnalyzerTest, DecodeFinishedUpdatesFrameStats) {
  TaskQueueForTest task_queue;
  VideoCodecAnalyzer analyzer(task_queue);
  analyzer.StartDecode(CreateEncodedImage(kTimestamp, kSpatialIdx));
  VideoFrame decoded_frame = CreateVideoFrame(kTimestamp);
  analyzer.FinishDecode(decoded_frame, kSpatialIdx);

  auto fs = analyzer.GetStats()->GetFrameStatistics();
  EXPECT_EQ(1u, fs.size());

  EXPECT_TRUE(fs[0].decoding_successful);
  EXPECT_EQ(static_cast<int>(fs[0].decoded_width), decoded_frame.width());
  EXPECT_EQ(static_cast<int>(fs[0].decoded_height), decoded_frame.height());
}

TEST(VideoCodecAnalyzerTest, DecodeFinishedComputesPsnr) {
  TaskQueueForTest task_queue;
  MockReferenceVideoSource reference_video_source;
  VideoCodecAnalyzer analyzer(task_queue, &reference_video_source);
  analyzer.StartDecode(CreateEncodedImage(kTimestamp, kSpatialIdx));

  EXPECT_CALL(reference_video_source, GetFrame)
      .WillOnce(Return(CreateVideoFrame(kTimestamp, /*y=*/0,
                                        /*u=*/0, /*v=*/0)));

  analyzer.FinishDecode(
      CreateVideoFrame(kTimestamp, /*value_y=*/1, /*value_u=*/2, /*value_v=*/3),
      kSpatialIdx);

  auto fs = analyzer.GetStats()->GetFrameStatistics();
  EXPECT_EQ(1u, fs.size());

  EXPECT_NEAR(fs[0].psnr_y, 48, 1);
  EXPECT_NEAR(fs[0].psnr_u, 42, 1);
  EXPECT_NEAR(fs[0].psnr_v, 38, 1);
}

}  // namespace test
}  // namespace webrtc
