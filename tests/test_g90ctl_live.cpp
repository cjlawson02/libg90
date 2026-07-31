#include "g90ctl_live.hpp"

#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

TEST(G90ctlLive, UnknownFlagRejected) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{"--confirm-bench", "--freqq", "14225000"};
  const auto err = g90ctl::parse_live_args(args, live);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("unknown"), std::string::npos);
}

TEST(G90ctlLive, FlagMissingValueRejected) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{"--confirm-bench", "--port"};
  const auto err = g90ctl::parse_live_args(args, live);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("missing value"), std::string::npos);
}

TEST(G90ctlLive, PortAndAutoPortExclusive) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{"--confirm-bench", "--port", "/dev/null", "--auto-port"};
  const auto err = g90ctl::parse_live_args(args, live);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("either --port"), std::string::npos);
}

TEST(G90ctlLive, ConfirmRequired) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{"--port", "/dev/null"};
  ASSERT_FALSE(g90ctl::parse_live_args(args, live).has_value());
  g90::HeadFrame frame{};
  const auto err = g90ctl::live_prepare_frame(live, frame);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("bench confirm"), std::string::npos);
}

TEST(G90ctlLive, ArmPttRefusedWithoutAllowTx) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{"--confirm-bench", "--port", "/dev/null", "--arm-ptt"};
  ASSERT_FALSE(g90ctl::parse_live_args(args, live).has_value());
  ASSERT_TRUE(live.req.lab.has_value());
  EXPECT_TRUE(live.req.lab->arm_ptt);
  EXPECT_TRUE(live.req.lab->allow_tx);

  g90::HeadFrame frame{};
  const auto err = g90ctl::live_prepare_frame(live, frame);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("refusing TX"), std::string::npos);

  const auto encoded = g90::encode_head(live.req);
  ASSERT_TRUE(encoded.ok());
  ASSERT_TRUE(g90::head_frame_requests_tx(*encoded.value).ok());
  EXPECT_TRUE(*g90::head_frame_requests_tx(*encoded.value).value);
}

TEST(G90ctlLive, ArmPttAllowedWithAllowTx) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{
      "--confirm-bench", "--port", "/dev/null", "--arm-ptt", "--allow-tx"};
  ASSERT_FALSE(g90ctl::parse_live_args(args, live).has_value());
  EXPECT_TRUE(live.allow_tx);

  g90::HeadFrame frame{};
  const auto err = g90ctl::live_prepare_frame(live, frame);
  ASSERT_FALSE(err.has_value()) << (err ? *err : "");
  ASSERT_TRUE(g90::head_frame_requests_tx(frame).ok());
  EXPECT_TRUE(*g90::head_frame_requests_tx(frame).value);
}

TEST(G90ctlLive, StockFrameNotBlocked) {
  g90ctl::LiveArgs live;
  const std::vector<std::string_view> args{
      "--i-know-what-im-doing", "--port", "/dev/null", "--mode", "USB", "--freq", "14225000"};
  ASSERT_FALSE(g90ctl::parse_live_args(args, live).has_value());
  g90::HeadFrame frame{};
  ASSERT_FALSE(g90ctl::live_prepare_frame(live, frame).has_value());
  ASSERT_TRUE(g90::head_frame_requests_tx(frame).ok());
  EXPECT_FALSE(*g90::head_frame_requests_tx(frame).value);
}
