#include <g90/head.hpp>
#include <gtest/gtest.h>

TEST(HeadTxGate, SafeStockFrameDoesNotRequestTx) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.mode = g90::Mode::Usb;
  const auto frame = g90::encode_head(req);
  ASSERT_TRUE(frame.ok());
  const auto r = g90::head_frame_requests_tx(*frame.value);
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(*r.value);
}

TEST(HeadTxGate, DetectsArmedPtt) {
  g90::LabExtras lab;
  lab.allow_tx = true;
  lab.arm_ptt = true;
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.mode = g90::Mode::Usb;
  req.lab = lab;
  const auto frame = g90::encode_head(req);
  ASSERT_TRUE(frame.ok());
  const auto r = g90::head_frame_requests_tx(*frame.value);
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(*r.value);
  EXPECT_TRUE((*frame.value)[g90::kHeadPttByte] & g90::kHeadPttBit);
}

TEST(HeadTxGate, DetectsAtuTuneAndVox) {
  {
    g90::LabExtras lab;
    lab.allow_tx = true;
    lab.arm_atu_tune = true;
    g90::HeadEncodeRequest req;
    req.lab = lab;
    const auto frame = g90::encode_head(req);
    ASSERT_TRUE(frame.ok());
    EXPECT_TRUE(*g90::head_frame_requests_tx(*frame.value).value);
  }
  {
    g90::LabExtras lab;
    lab.allow_tx = true;
    lab.arm_vox = true;
    g90::HeadEncodeRequest req;
    req.lab = lab;
    const auto frame = g90::encode_head(req);
    ASSERT_TRUE(frame.ok());
    EXPECT_TRUE(*g90::head_frame_requests_tx(*frame.value).value);
  }
}

TEST(HeadTxGate, BadSyncIsErr) {
  g90::HeadFrame frame{};
  const auto r = g90::head_frame_requests_tx(frame);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.err(), g90::Error::BadSync);
}

TEST(HeadTxGate, BadCrcIsErr) {
  g90::HeadEncodeRequest req;
  const auto good = g90::encode_head(req);
  ASSERT_TRUE(good.ok());
  auto bad = *good.value;
  bad.back() ^= 0xFF;
  const auto r = g90::head_frame_requests_tx(bad);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.err(), g90::Error::BadCrc);
}
