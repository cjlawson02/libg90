#include <g90/version.hpp>
#include <gtest/gtest.h>

TEST(Smoke, VersionStringNonEmpty) {
  EXPECT_FALSE(g90::version_string().empty());
  EXPECT_EQ(g90::version_string(), "0.1.0");
}
