#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <g90/body.hpp>
#include <g90/de9.hpp>
#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef LIBG90_FIXTURES_DIR
#error "LIBG90_FIXTURES_DIR must be defined"
#endif

namespace {

[[nodiscard]] std::vector<uint8_t> read_hex_file(const std::string& path) {
  std::ifstream in(path);
  EXPECT_TRUE(in) << "missing " << path;
  std::stringstream ss;
  ss << in.rdbuf();
  std::string hex = ss.str();
  std::string compact;
  for (char c : hex) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      compact.push_back(c);
    }
  }
  EXPECT_EQ(compact.size() % 2, 0u) << path;
  std::vector<uint8_t> out;
  out.reserve(compact.size() / 2);
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  for (std::size_t i = 0; i + 1 < compact.size(); i += 2) {
    const int hi = nibble(compact[i]);
    const int lo = nibble(compact[i + 1]);
    EXPECT_GE(hi, 0);
    EXPECT_GE(lo, 0);
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

[[nodiscard]] std::vector<uint8_t> live_body_fixture() {
  return read_hex_file(std::string(LIBG90_FIXTURES_DIR) + "/body_181_live.hex");
}

/// Delays the first N bytes of a read to push past a short deadline after fill.
class SlowFirstReadStream final : public g90::de9::ByteStream {
public:
  explicit SlowFirstReadStream(std::vector<uint8_t> payload, std::chrono::milliseconds delay)
      : payload_(std::move(payload)), delay_(delay) {}

  g90::Result<void> write_exact(std::span<const uint8_t> data,
                                std::chrono::milliseconds timeout) override {
    (void)timeout;
    tx_.insert(tx_.end(), data.begin(), data.end());
    return g90::Result<void>::Ok();
  }

  g90::Result<void> read_exact(std::span<uint8_t> buf, std::chrono::milliseconds timeout) override {
    (void)timeout;
    if (off_ + buf.size() > payload_.size()) {
      return g90::Result<void>::Err(g90::Error::Timeout);
    }
    if (!delayed_) {
      std::this_thread::sleep_for(delay_);
      delayed_ = true;
    }
    std::copy_n(payload_.begin() + static_cast<std::ptrdiff_t>(off_), buf.size(), buf.begin());
    off_ += buf.size();
    return g90::Result<void>::Ok();
  }

  [[nodiscard]] const std::vector<uint8_t>& tx_bytes() const noexcept { return tx_; }

private:
  std::vector<uint8_t> payload_;
  std::vector<uint8_t> tx_;
  std::size_t off_ = 0;
  std::chrono::milliseconds delay_;
  bool delayed_ = false;
};

class TimeoutWriteStream final : public g90::de9::ByteStream {
public:
  g90::Result<void> write_exact(std::span<const uint8_t> data,
                                std::chrono::milliseconds timeout) override {
    (void)data;
    if (timeout.count() <= 0) {
      return g90::Result<void>::Err(g90::Error::Timeout);
    }
    return g90::Result<void>::Err(g90::Error::Timeout);
  }

  g90::Result<void> read_exact(std::span<uint8_t> buf, std::chrono::milliseconds timeout) override {
    (void)buf;
    (void)timeout;
    return g90::Result<void>::Err(g90::Error::Timeout);
  }
};

} // namespace

TEST(De9Fake, InjectReadExactRoundTrip) {
  g90::de9::FakeStream fake;
  const std::array<uint8_t, 4> payload{0xde, 0xad, 0xbe, 0xef};
  fake.inject_rx(payload);

  std::array<uint8_t, 4> out{};
  const auto rr = fake.read_exact(out, std::chrono::milliseconds{10});
  ASSERT_TRUE(rr.ok());
  EXPECT_EQ(out, payload);

  const std::array<uint8_t, 2> tx{0x01, 0x02};
  ASSERT_TRUE(fake.write_exact(tx, std::chrono::milliseconds{10}).ok());
  ASSERT_EQ(fake.tx_bytes().size(), 2u);
  EXPECT_EQ(fake.tx_bytes()[0], 0x01);
  EXPECT_EQ(fake.tx_bytes()[1], 0x02);
}

TEST(De9Fake, ReadExactTimeoutWhenShort) {
  g90::de9::FakeStream fake;
  fake.inject_rx(std::array<uint8_t, 1>{0xaa});
  std::array<uint8_t, 2> out{};
  const auto rr = fake.read_exact(out, std::chrono::milliseconds{1});
  ASSERT_FALSE(rr.ok());
  EXPECT_EQ(rr.err(), g90::Error::Timeout);
}

TEST(De9Fake, NullPortRejected) {
  EXPECT_THROW(g90::De9Link(nullptr, g90::Profile::V179), std::invalid_argument);
}

TEST(De9Fake, SerialPortClosedFailsIo) {
  g90::de9::SerialPort port;
  EXPECT_FALSE(port.is_open());
  std::array<uint8_t, 1> buf{0};
  EXPECT_FALSE(port.write_exact(buf, std::chrono::milliseconds{1}).ok());
  EXPECT_EQ(port.write_exact(buf, std::chrono::milliseconds{1}).err(), g90::Error::IoError);
  EXPECT_EQ(port.read_exact(buf, std::chrono::milliseconds{1}).err(), g90::Error::IoError);
}

TEST(De9Fake, SerialPortOpenFailsOnBadPath) {
  g90::de9::SerialPort port;
  EXPECT_FALSE(port.open("/nonexistent/libg90-no-such-serial-port").ok());
  EXPECT_EQ(port.open("/nonexistent/libg90-no-such-serial-port").err(), g90::Error::IoError);
  EXPECT_FALSE(port.is_open());
}

TEST(De9Fake, HuntBodyRecoversFromGarbage) {
  const auto body = live_body_fixture();
  ASSERT_EQ(body.size(), g90::kBodyFrameLen);

  g90::de9::FakeStream fake;
  const std::array<uint8_t, 5> garbage{0x00, 0xff, 0xaa, 0x55, 0x01};
  fake.inject_rx(garbage);
  fake.inject_rx(body);

  g90::de9::Session session(fake);
  const auto frame = session.hunt_body(std::chrono::milliseconds{1000});
  ASSERT_TRUE(frame.ok()) << g90::to_string(frame.err());
  const auto dec = g90::decode_body(*frame.value);
  ASSERT_TRUE(dec.ok());
  EXPECT_TRUE(dec.value->crc_ok);
  ASSERT_NE(session.last_body(), nullptr);
  EXPECT_EQ(*session.last_body(), *frame.value);
}

TEST(De9Fake, HuntBodyAcceptsFrameFilledAtDeadline) {
  const auto body = live_body_fixture();
  ASSERT_EQ(body.size(), g90::kBodyFrameLen);

  // Delay the initial 372 B fill past a 5 ms budget; frame must still be validated.
  SlowFirstReadStream slow(body, std::chrono::milliseconds{20});
  g90::de9::Session session(slow);
  const auto frame = session.hunt_body(std::chrono::milliseconds{5});
  ASSERT_TRUE(frame.ok()) << g90::to_string(frame.err());
  EXPECT_TRUE(g90::decode_body(*frame.value).value->crc_ok);
}

TEST(De9Fake, SessionExchangeRoundTrip) {
  const auto body = live_body_fixture();
  ASSERT_EQ(body.size(), g90::kBodyFrameLen);

  g90::de9::FakeStream fake;
  fake.set_reply_after_write(body);

  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.mode = g90::Mode::Usb;
  req.vfo_a.freq_hz = 14'225'000;
  const auto head = g90::encode_head(req);
  ASSERT_TRUE(head.ok());

  g90::de9::Session session(fake);
  const auto decoded = session.exchange(*head.value, std::chrono::milliseconds{1000});
  ASSERT_TRUE(decoded.ok()) << g90::to_string(decoded.err());
  EXPECT_TRUE(decoded.value->crc_ok);
  EXPECT_EQ(fake.tx_bytes().size(), g90::kHeadFrameLen);
  for (std::size_t i = 0; i < g90::kHeadFrameLen; ++i) {
    EXPECT_EQ(fake.tx_bytes()[i], (*head.value)[i]);
  }
  EXPECT_GE(decoded.value->status.voltage_raw, 0);
}

TEST(De9Fake, ExchangeFlushesStaleRx) {
  const auto body = live_body_fixture();
  ASSERT_EQ(body.size(), g90::kBodyFrameLen);

  g90::de9::FakeStream fake;
  fake.inject_rx(body); // stale — must be flushed before TX
  // No reply_after_write → hunt should time out after flush.

  g90::HeadEncodeRequest req;
  const auto head = g90::encode_head(req);
  ASSERT_TRUE(head.ok());

  g90::de9::Session session(fake);
  const auto decoded = session.exchange(*head.value, std::chrono::milliseconds{20});
  ASSERT_FALSE(decoded.ok());
  EXPECT_EQ(decoded.err(), g90::Error::Timeout);
  EXPECT_EQ(fake.tx_bytes().size(), g90::kHeadFrameLen);
}

TEST(De9Fake, ResetBuffersClearsRxOnly) {
  g90::de9::FakeStream fake;
  ASSERT_TRUE(fake.write_exact(std::array<uint8_t, 2>{1, 2}, std::chrono::milliseconds{1}).ok());
  fake.inject_rx(std::array<uint8_t, 1>{0xaa});
  fake.reset_buffers();
  EXPECT_EQ(fake.tx_bytes().size(), 2u);
  std::array<uint8_t, 1> out{};
  EXPECT_EQ(fake.read_exact(out, std::chrono::milliseconds{1}).err(), g90::Error::Timeout);
}

TEST(De9Fake, De9LinkExchangeRoundTrip) {
  const auto body = live_body_fixture();
  ASSERT_EQ(body.size(), g90::kBodyFrameLen);

  auto fake = std::make_unique<g90::de9::FakeStream>();
  fake->set_reply_after_write(body);
  auto* fake_ptr = fake.get();

  g90::De9Link link(std::move(fake), g90::Profile::V179);
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V178; // overridden by link profile
  req.vfo_a.mode = g90::Mode::Usb;
  req.vfo_a.freq_hz = 14'225'000;

  const auto decoded = link.exchange(req, std::chrono::milliseconds{1000});
  ASSERT_TRUE(decoded.ok()) << g90::to_string(decoded.err());
  EXPECT_TRUE(decoded.value->crc_ok);
  EXPECT_EQ(fake_ptr->tx_bytes().size(), g90::kHeadFrameLen);
  ASSERT_NE(link.last_tx(), nullptr);
  ASSERT_NE(link.last_rx(), nullptr);
  EXPECT_EQ(link.last_rx()->size(), g90::kBodyFrameLen);
}

TEST(De9Fake, LastBodyClearedOnFailedHunt) {
  const auto body = live_body_fixture();
  g90::de9::FakeStream fake;
  fake.inject_rx(body);
  g90::de9::Session session(fake);
  ASSERT_TRUE(session.hunt_body(std::chrono::milliseconds{100}).ok());
  ASSERT_NE(session.last_body(), nullptr);

  std::vector<uint8_t> garbage(g90::kBodyFrameLen, 0x5a);
  fake.inject_rx(garbage);
  const auto failed = session.hunt_body(std::chrono::milliseconds{20});
  ASSERT_FALSE(failed.ok());
  EXPECT_EQ(session.last_body(), nullptr);
}

TEST(De9Fake, HuntBodyTimeoutWhenNoValidFrame) {
  g90::de9::FakeStream fake;
  std::vector<uint8_t> garbage(g90::kBodyFrameLen, 0x5a);
  fake.inject_rx(garbage);

  g90::de9::Session session(fake);
  const auto frame = session.hunt_body(std::chrono::milliseconds{50});
  ASSERT_FALSE(frame.ok());
  EXPECT_EQ(frame.err(), g90::Error::Timeout);
}

TEST(De9Fake, WriteExactTimeoutPropagates) {
  TimeoutWriteStream slow;
  g90::de9::Session session(slow);
  g90::HeadFrame head{};
  head[0] = 0x55;
  head[1] = 0xaa;
  const auto r = session.exchange(head, std::chrono::milliseconds{0});
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.err(), g90::Error::Timeout);
}
