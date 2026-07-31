#include <cstdint>
#include <g90/crc.hpp>
#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <utility>

TEST(Head, Modes178) {
  EXPECT_EQ(*g90::mode_to_raw(g90::Profile::V178, g90::Mode::Usb).value, 1);
  EXPECT_EQ(*g90::mode_to_raw(g90::Profile::V178, g90::Mode::Nfm).value, 5);
  EXPECT_EQ(*g90::mode_from_raw(g90::Profile::V178, 2).value, g90::Mode::Cw);
  EXPECT_EQ(g90::mode_to_string(g90::Mode::Cw), "CW");
  EXPECT_FALSE(g90::mode_to_raw(g90::Profile::V178, g90::Mode::Ld).ok());
}

TEST(Head, Modes179Insert) {
  EXPECT_EQ(*g90::mode_to_raw(g90::Profile::V179, g90::Mode::Usb).value, 2);
  EXPECT_EQ(*g90::mode_to_raw(g90::Profile::V179, g90::Mode::Ld).value, 1);
  EXPECT_EQ(*g90::mode_to_raw(g90::Profile::V179, g90::Mode::Ud).value, 3);
  EXPECT_EQ(*g90::mode_to_raw(g90::Profile::V179, g90::Mode::Nfm).value, 7);
  EXPECT_EQ(*g90::mode_from_raw(g90::Profile::V179, 5).value, g90::Mode::Cwr);
  EXPECT_EQ(g90::mode_to_string(g90::Mode::Cwr), "CWR");
}

TEST(Head, CrossProfileMishap) {
  const auto gsoc_usb = g90::mode_to_raw(g90::Profile::V178, g90::Mode::Usb);
  ASSERT_TRUE(gsoc_usb.ok());
  EXPECT_EQ(*gsoc_usb.value, 1);
  const auto landed = g90::mode_from_raw(g90::Profile::V179, *gsoc_usb.value);
  ASSERT_TRUE(landed.ok());
  EXPECT_EQ(*landed.value, g90::Mode::Ld);
  EXPECT_EQ(g90::mode_to_string(*landed.value), "L-D");
}

TEST(Head, Filter178) {
  const auto raw = g90::encode_filter(g90::Profile::V178, 2625, 200);
  EXPECT_EQ(raw.high, 100);
  EXPECT_EQ(raw.low, 4);
  const auto hz = g90::decode_filter(g90::Profile::V178, 100, 4);
  EXPECT_EQ(hz.high, 2625);
  EXPECT_EQ(hz.low, 200);
}

TEST(Head, Filter179) {
  const auto raw = g90::encode_filter(g90::Profile::V179, 2700, 200);
  EXPECT_EQ(raw.high, 53);
  EXPECT_EQ(raw.low, 3);
  const auto hz = g90::decode_filter(g90::Profile::V179, 53, 3);
  EXPECT_EQ(hz.high, 2700);
  EXPECT_EQ(hz.low, 200);
}

TEST(Head, Filter179FloorClamp) {
  const auto floor = g90::encode_filter(g90::Profile::V179, 49, 0);
  EXPECT_EQ(floor.high, 0);
  EXPECT_EQ(floor.low, 0);
  const auto clamp = g90::encode_filter(g90::Profile::V179, 50 + 255 * 50, 50);
  EXPECT_EQ(clamp.high, 255);
  EXPECT_EQ(clamp.low, 0);
}

TEST(Head, FftScale178) {
  EXPECT_EQ(*g90::validate_fft_scale(g90::Profile::V178, 10).value, 10);
  EXPECT_FALSE(g90::validate_fft_scale(g90::Profile::V178, 11).ok());
}

TEST(Head, FftScale179Auto) {
  EXPECT_EQ(*g90::validate_fft_scale(g90::Profile::V179, 1).value, 1);
  EXPECT_FALSE(g90::validate_fft_scale(g90::Profile::V179, 2).ok());
}

TEST(Head, Roundtrip178) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V178;
  req.volume = 3;
  req.vfo_a.freq_hz = 14'225'000;
  req.vfo_a.mode = g90::Mode::Cw;
  req.vfo_a.filter_high_hz = 2725;
  req.vfo_a.filter_low_hz = 200;
  req.vfo_a.fft_scale = 5;

  const auto raw = g90::encode_head(req);
  ASSERT_TRUE(raw.ok());
  const auto dec = g90::decode_head(g90::Profile::V178, *raw.value);
  ASSERT_TRUE(dec.ok());
  EXPECT_TRUE(dec.value->crc_ok);
  EXPECT_EQ(dec.value->mode_a, g90::Mode::Cw);
  EXPECT_EQ(dec.value->filter_high_hz, 2725);
  EXPECT_EQ(dec.value->filter_low_hz, 200);
  EXPECT_EQ(dec.value->fft_scale, 5);
  EXPECT_EQ(dec.value->volume, 3);

  g90::HeadEncodeRequest req2;
  req2.profile = g90::Profile::V178;
  req2.volume = dec.value->volume;
  req2.vfo_a.freq_hz = dec.value->freq_a_hz;
  req2.vfo_a.mode = dec.value->mode_a;
  req2.vfo_a.filter_high_raw = dec.value->filter_high_raw;
  req2.vfo_a.filter_low_raw = dec.value->filter_low_raw;
  req2.vfo_a.fft_scale = dec.value->fft_scale;
  req2.vfo_b.freq_hz = dec.value->freq_b_hz;
  req2.vfo_b.mode = dec.value->mode_b;
  req2.vfo_b.filter_high_raw = dec.value->filter_high_raw_b;
  req2.vfo_b.filter_low_raw = dec.value->filter_low_raw_b;
  req2.vfo_b.fft_scale = dec.value->fft_scale_b;

  const auto raw2 = g90::encode_head(req2);
  ASSERT_TRUE(raw2.ok());
  EXPECT_EQ(*raw.value, *raw2.value);
}

TEST(Head, Roundtrip179) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.volume = 8;
  req.vfo_a.freq_hz = 14'074'000;
  req.vfo_a.mode = g90::Mode::Ud;
  req.vfo_a.filter_high_hz = 2700;
  req.vfo_a.filter_low_hz = 200;
  req.vfo_a.fft_scale = 1;

  const auto raw = g90::encode_head(req);
  ASSERT_TRUE(raw.ok());
  const auto dec = g90::decode_head(g90::Profile::V179, *raw.value);
  ASSERT_TRUE(dec.ok());
  EXPECT_TRUE(dec.value->crc_ok);
  EXPECT_EQ(dec.value->mode_a, g90::Mode::Ud);
  EXPECT_EQ(g90::mode_to_string(dec.value->mode_a), "U-D");
  EXPECT_EQ(dec.value->filter_high_hz, 2700);
  EXPECT_EQ(dec.value->filter_low_hz, 200);
  EXPECT_EQ(dec.value->fft_scale, 1);

  g90::HeadEncodeRequest req2;
  req2.profile = g90::Profile::V179;
  req2.volume = dec.value->volume;
  req2.vfo_a.freq_hz = dec.value->freq_a_hz;
  req2.vfo_a.mode = dec.value->mode_a;
  req2.vfo_a.filter_high_raw = dec.value->filter_high_raw;
  req2.vfo_a.filter_low_raw = dec.value->filter_low_raw;
  req2.vfo_a.fft_scale = dec.value->fft_scale;
  req2.vfo_b.freq_hz = dec.value->freq_b_hz;
  req2.vfo_b.mode = dec.value->mode_b;
  req2.vfo_b.filter_high_raw = dec.value->filter_high_raw_b;
  req2.vfo_b.filter_low_raw = dec.value->filter_low_raw_b;
  req2.vfo_b.fft_scale = dec.value->fft_scale_b;
  const auto raw2 = g90::encode_head(req2);
  ASSERT_TRUE(raw2.ok());
  EXPECT_EQ(*raw.value, *raw2.value);
}

TEST(Head, RejectsManualFft179) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.mode = g90::Mode::Usb;
  req.vfo_a.fft_scale = 5;
  const auto raw = g90::encode_head(req);
  ASSERT_FALSE(raw.ok());
  EXPECT_EQ(raw.err(), g90::Error::InvalidFftScale);
}

TEST(Head, VfoBExplicitFftScaleOneNotInherited) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V178;
  req.vfo_a.mode = g90::Mode::Cw;
  req.vfo_a.fft_scale = 5;
  req.vfo_b.fft_scale = 1; // explicit — must not inherit 5
  const auto raw = g90::encode_head(req);
  ASSERT_TRUE(raw.ok());
  const auto dec = g90::decode_head(g90::Profile::V178, *raw.value);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value->fft_scale, 5);
  EXPECT_EQ(dec.value->fft_scale_b, 1);
}

TEST(Head, V178Intentional2700HzPreserved) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V178;
  req.vfo_a.mode = g90::Mode::Usb;
  req.vfo_a.filter_high_hz = 2700; // intentional, not stock 2725
  req.vfo_a.filter_low_hz = 200;
  const auto raw = g90::encode_head(req);
  ASSERT_TRUE(raw.ok());
  const auto dec = g90::decode_head(g90::Profile::V178, *raw.value);
  ASSERT_TRUE(dec.ok());
  EXPECT_EQ(dec.value->filter_high_hz, 2700);
  // Stock (unset) still uses 2725:
  g90::HeadEncodeRequest stock;
  stock.profile = g90::Profile::V178;
  stock.vfo_a.mode = g90::Mode::Usb;
  const auto stock_raw = g90::encode_head(stock);
  ASSERT_TRUE(stock_raw.ok());
  EXPECT_EQ(g90::decode_head(g90::Profile::V178, *stock_raw.value).value->filter_high_hz, 2725);
}

TEST(Head, PartialFilterRawRejected) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.filter_high_raw = 53; // missing low
  const auto raw = g90::encode_head(req);
  ASSERT_FALSE(raw.ok());
  EXPECT_EQ(raw.err(), g90::Error::InvalidFilter);
}

TEST(Head, ControlsPackAndOverlay) {
  EXPECT_EQ(g90::pack_af_gain_beep(15, true), (std::pair<uint8_t, uint8_t>{0x78, 0x08}));
  for (int hz : {-100, 0, 100, 500, -500}) {
    const auto [lo, hi] = g90::pack_rclk_hz(hz);
    EXPECT_EQ(g90::unpack_rclk_hz(lo, hi), hz);
    const auto [rlo, rhi] = g90::pack_rit_hz(hz);
    EXPECT_EQ(g90::unpack_rit_hz(rlo, rhi), hz);
  }

  g90::HeadControls ctl;
  ctl.nb_en = true;
  ctl.nb_level = 7;
  ctl.nb_width = 2;
  ctl.split_en = true;
  ctl.vfo_b_en = true;
  ctl.headphones = true;
  ctl.audio_in_line_en = true;
  ctl.cw_disp_en = true;
  ctl.sql_level = 5;
  ctl.rclk_hz = 100;
  ctl.rit_hz = -50;
  ctl.af_gain = 31;
  ctl.beep_en = false;
  ctl.cw_wpm = 25;
  ctl.cw_qsk = false;
  ctl.mic_gain = 15;

  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.mode = g90::Mode::Usb;
  req.controls = ctl;

  const auto raw = g90::encode_head(req);
  ASSERT_TRUE(raw.ok());
  const auto& f = *raw.value;
  ASSERT_TRUE(g90::head_frame_requests_tx(f).ok());
  EXPECT_FALSE(*g90::head_frame_requests_tx(f).value);
  EXPECT_TRUE(f[38] & g90::kHeadCtrl3Operate);
  EXPECT_TRUE(f[36] & g90::kHeadCtrl1NbEn);
  EXPECT_TRUE(f[36] & g90::kHeadCtrl1Split);
  EXPECT_TRUE(f[36] & g90::kHeadCtrl1Headphones);
  EXPECT_TRUE(f[38] & g90::kHeadCtrl3AudioLine);
  EXPECT_TRUE(f[38] & g90::kHeadCtrl3CwDisp);
  EXPECT_FALSE(f[38] & 0x01);
  EXPECT_TRUE(f[54] & g90::kHeadCtrl6VfoB);
  EXPECT_EQ(f[45], 5);
  EXPECT_EQ(f[46], 0x72);
  EXPECT_EQ(f[49], 15);
  EXPECT_EQ(f[60], 25);
  const auto [rclk_lo, rclk_hi] = g90::pack_rclk_hz(100);
  EXPECT_EQ((f[38] >> 5) & 7, rclk_lo);
  EXPECT_EQ(f[39], rclk_hi);
  const auto [rit_lo, rit_hi] = g90::pack_rit_hz(-50);
  EXPECT_EQ(f[84], rit_lo);
  EXPECT_EQ(f[85] & 3, rit_hi);
  const auto af = g90::pack_af_gain_beep(31, false);
  EXPECT_EQ(f[42], af.first);
  EXPECT_EQ(f[43], af.second);

  g90::HeadControls shut_ctl;
  shut_ctl.shutdown_req = true;
  shut_ctl.panel_lock = true;
  g90::HeadEncodeRequest shut_req;
  shut_req.profile = g90::Profile::V179;
  shut_req.vfo_a.mode = g90::Mode::Usb;
  shut_req.controls = shut_ctl;
  const auto shut = g90::encode_head(shut_req);
  ASSERT_TRUE(shut.ok());
  EXPECT_FALSE(*g90::head_frame_requests_tx(*shut.value).value);
  EXPECT_EQ((*shut.value)[37] & 0x80, 0x80);
  EXPECT_EQ((*shut.value)[36] & 0x01, 0x01);

  g90::HeadControls tune_ctl;
  tune_ctl.tuner_en = true;
  g90::LabExtras tune_lab;
  tune_lab.allow_tx = true;
  tune_lab.arm_atu_tune = true;
  EXPECT_TRUE(g90::lab_arms_tx(tune_lab));
  g90::HeadEncodeRequest tune_req;
  tune_req.profile = g90::Profile::V179;
  tune_req.vfo_a.mode = g90::Mode::Usb;
  tune_req.controls = tune_ctl;
  tune_req.lab = tune_lab;
  const auto tune = g90::encode_head(tune_req);
  ASSERT_TRUE(tune.ok());
  EXPECT_TRUE(*g90::head_frame_requests_tx(*tune.value).value);
  EXPECT_EQ((*tune.value)[37] & 0x20, 0x20);
  EXPECT_EQ((*tune.value)[36] & 0x80, 0);
  EXPECT_EQ((*tune.value)[36] & 0x20, 0x20);
  EXPECT_TRUE((*tune.value)[38] & g90::kHeadCtrl3Operate);

  // Arms without allow_tx must not key TX.
  g90::LabExtras ignored;
  ignored.arm_atu_tune = true;
  EXPECT_TRUE(g90::lab_arms_tx(ignored));
  g90::HeadEncodeRequest ignored_req;
  ignored_req.lab = ignored;
  const auto ignored_frame = g90::encode_head(ignored_req);
  ASSERT_TRUE(ignored_frame.ok());
  EXPECT_FALSE(*g90::head_frame_requests_tx(*ignored_frame.value).value);

  EXPECT_EQ(g90::pack_ctrl7(5, true, 2), 0x56);
  EXPECT_EQ(g90::pack_ctrl8(1, 9, 8), (std::pair<uint8_t, uint8_t>{0x01, 0x98}));

  g90::HeadControls rest_ctl;
  rest_ctl.mem_en = true;
  rest_ctl.mem_ch = 12;
  rest_ctl.mic_compression = true;
  rest_ctl.cw_ratio_raw = 5;
  rest_ctl.cw_mode_b = true;
  rest_ctl.cw_mlr = 2;
  rest_ctl.swr_threshold_raw = 4;
  rest_ctl.aux_out_vol = 12;
  rest_ctl.aux_in_vol = 3;
  rest_ctl.band_stack_full = true;
  g90::HeadEncodeRequest rest_req;
  rest_req.profile = g90::Profile::V179;
  rest_req.vfo_a.mode = g90::Mode::Cw;
  rest_req.controls = rest_ctl;
  const auto rest = g90::encode_head(rest_req);
  ASSERT_TRUE(rest.ok());
  EXPECT_FALSE(*g90::head_frame_requests_tx(*rest.value).value);
  EXPECT_EQ((*rest.value)[36] & 0x48, 0x48);
  EXPECT_EQ((*rest.value)[53], 12);
  EXPECT_EQ((*rest.value)[61], 0x56);
  EXPECT_EQ((*rest.value)[82], 0x04);
  EXPECT_EQ((*rest.value)[83], 0xC3);
  EXPECT_TRUE((*rest.value)[43] & 0x04);

  g90::LabExtras vox_lab;
  vox_lab.allow_tx = true;
  vox_lab.arm_vox = true;
  g90::HeadEncodeRequest vox_req;
  vox_req.profile = g90::Profile::V179;
  vox_req.lab = vox_lab;
  const auto vox = g90::encode_head(vox_req);
  ASSERT_TRUE(vox.ok());
  EXPECT_TRUE(*g90::head_frame_requests_tx(*vox.value).value);
  EXPECT_EQ((*vox.value)[38] & 0x08, 0x08);
}

TEST(Head, ProfileAlias181) {
  const auto p = g90::parse_profile("1.81");
  ASSERT_TRUE(p.ok());
  EXPECT_EQ(*p.value, g90::Profile::V179);
}

TEST(Head, Encode181Identical179) {
  auto make = [](g90::Profile profile) {
    g90::HeadEncodeRequest req;
    req.profile = profile;
    req.vfo_a.mode = g90::Mode::Cw;
    req.vfo_a.freq_hz = 7'030'000;
    req.vfo_a.filter_high_hz = 500;
    req.vfo_a.filter_low_hz = 200;
    req.vfo_a.fft_scale = 1;
    return g90::encode_head(req);
  };
  const auto a = make(g90::Profile::V179);
  const auto b = make(*g90::parse_profile("1.81").value);
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(b.ok());
  EXPECT_EQ(*a.value, *b.value);
}
