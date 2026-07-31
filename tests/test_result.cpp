#include <g90/error.hpp>
#include <g90/types.hpp>
#include <gtest/gtest.h>

TEST(Result, OkLeavesErrorEmpty) {
  auto r = g90::Result<int>::Ok(42);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(*r.value, 42);
  EXPECT_FALSE(r.error.has_value()) << "Ok must not leave a spurious Error code";
}

TEST(Result, ErrPreservesError) {
  auto r = g90::Result<int>::Err(g90::Error::BadCrc);
  ASSERT_FALSE(r.ok());
  ASSERT_TRUE(r.error.has_value());
  EXPECT_EQ(*r.error, g90::Error::BadCrc);
  EXPECT_EQ(r.err(), g90::Error::BadCrc);
  EXPECT_EQ(g90::to_string(r.err()), "BadCrc");
  EXPECT_NE(g90::to_string(r.err()), "InvalidLength");
}

TEST(Result, VoidOkAndErr) {
  auto ok = g90::Result<void>::Ok();
  ASSERT_TRUE(ok.ok());
  EXPECT_FALSE(ok.error.has_value());

  auto err = g90::Result<void>::Err(g90::Error::Timeout);
  ASSERT_FALSE(err.ok());
  ASSERT_TRUE(err.error.has_value());
  EXPECT_EQ(err.err(), g90::Error::Timeout);
  EXPECT_EQ(g90::to_string(err.err()), "Timeout");
}
