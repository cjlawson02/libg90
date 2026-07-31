#include <array>
#include <g90/body.hpp>
#include <gtest/gtest.h>

TEST(BodyHelpers, HeaderNzCountsNonZero) {
  g90::BodyStatus st;
  EXPECT_EQ(st.header_nz_13_47(), 0u);
  st.header_tail[0] = 1;
  st.header_tail[10] = 2;
  st.header_tail[34] = 3;
  EXPECT_EQ(st.header_nz_13_47(), 3u);
}

TEST(BodyHelpers, FftFingerprintSumMaxArgmax) {
  std::array<uint16_t, 160> fft{};
  fft[7] = 100;
  fft[42] = 250;
  fft[99] = 200;
  const auto fp = g90::compute_fft_fingerprint(fft);
  EXPECT_EQ(fp.sum, 550u);
  EXPECT_EQ(fp.max, 250);
  EXPECT_EQ(fp.argmax, 42u);
}
