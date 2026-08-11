// Copyright 2026
// Licensed under the same terms as Mozc.

#include "win32/tip/tip_zenz_context_request.h"

#include <cstddef>

#include "gtest/gtest.h"
#include "protocol/commands.pb.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

TEST(TipZenzContextRequestStateTest, StartsEmptyAndTakeIsOneShot) {
  TipZenzContextRequestState state;

  const TipZenzContextRequest initial = state.Take();
  EXPECT_TRUE(initial.empty());

  commands::Output output;
  output.set_zenz_preceding_text_request_length(24);
  output.set_zenz_following_text_request_length(10);
  state.UpdateFromOutput(output);

  const TipZenzContextRequest first = state.Take();
  EXPECT_EQ(first.preceding_length, 24);
  EXPECT_EQ(first.following_length, 10);

  const TipZenzContextRequest second = state.Take();
  EXPECT_TRUE(second.empty());
}

TEST(TipZenzContextRequestStateTest, ResetPreventsStaleRequest) {
  TipZenzContextRequestState state;
  commands::Output output;
  output.set_zenz_preceding_text_request_length(24);
  state.UpdateFromOutput(output);

  state.Reset();

  const TipZenzContextRequest request = state.Take();
  EXPECT_TRUE(request.empty());
}

TEST(TipZenzContextRequestStateTest, MissingFieldsReplacePreviousRequest) {
  TipZenzContextRequestState state;
  commands::Output first_output;
  first_output.set_zenz_preceding_text_request_length(24);
  first_output.set_zenz_following_text_request_length(10);
  state.UpdateFromOutput(first_output);

  commands::Output empty_output;
  state.UpdateFromOutput(empty_output);

  const TipZenzContextRequest request = state.Take();
  EXPECT_TRUE(request.empty());
}

TEST(TipZenzContextRequestStateTest, DefensivelyClampsServerLengths) {
  TipZenzContextRequestState state;
  commands::Output output;
  output.set_zenz_preceding_text_request_length(4096);
  output.set_zenz_following_text_request_length(1024);
  state.UpdateFromOutput(output);

  const TipZenzContextRequest request = state.Take();
  EXPECT_EQ(request.preceding_length, 128);
  EXPECT_EQ(request.following_length, 128);
}

TEST(TipZenzContextRequestTest, NativeBudgetCoversUtf16SurrogatePairs) {
  TipZenzContextRequest request;
  request.preceding_length = 24;
  request.following_length = 10;
  EXPECT_EQ(GetZenzTsfNativeAcquisitionLength(request), 48);

  request.preceding_length = 0;
  request.following_length = 128;
  EXPECT_EQ(GetZenzTsfNativeAcquisitionLength(request), 256);

  request.preceding_length = 0;
  request.following_length = 0;
  EXPECT_EQ(GetZenzTsfNativeAcquisitionLength(request), 0);
}

TEST(TipZenzContextRequestTest, CountsUnicodeCharactersNotUtf8Bytes) {
  constexpr char kText[] =
      "\xF0\xA0\xAE\x9F\xE3\x82\x8B\xE7\x8C\xAB";
  EXPECT_TRUE(HasAtLeastZenzContextCharacters(kText, 3));
  EXPECT_FALSE(HasAtLeastZenzContextCharacters(kText, 4));
}

TEST(TipZenzContextRequestTest, TakesLeadingUnicodeCharacters) {
  constexpr char kText[] =
      "\xE7\x94\xB2\xF0\xA0\xAE\x9F\xE4\xB9\x99\xE4\xB8\x99";
  constexpr char kFirstTwo[] =
      "\xE7\x94\xB2\xF0\xA0\xAE\x9F";

  EXPECT_EQ(TakeLeadingZenzContextCharacters(kText, 2), kFirstTwo);
  EXPECT_EQ(TakeLeadingZenzContextCharacters(kText, 99), kText);
  EXPECT_EQ(TakeLeadingZenzContextCharacters(kText, 0), "");
}

TEST(TipZenzContextRequestTest, TakesTrailingUnicodeCharacters) {
  constexpr char kText[] =
      "\xE7\x94\xB2\xF0\xA0\xAE\x9F\xE4\xB9\x99\xE4\xB8\x99";
  constexpr char kLastTwo[] =
      "\xE4\xB9\x99\xE4\xB8\x99";
  constexpr char kLastThree[] =
      "\xF0\xA0\xAE\x9F\xE4\xB9\x99\xE4\xB8\x99";

  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 2), kLastTwo);
  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 3), kLastThree);
  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 99), kText);
  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 0), "");
}

}  // namespace

TEST(TipZenzContextRequestStateTest, TracksAuthoritativeOutputPresence) {
  TipZenzContextRequestState state;
  EXPECT_FALSE(state.has_result());

  commands::Output empty_output;
  state.UpdateFromOutput(empty_output);
  EXPECT_TRUE(state.has_result());

  const TipZenzContextRequest empty_request = state.Take();
  EXPECT_TRUE(empty_request.empty());
  EXPECT_FALSE(state.has_result());

  commands::Output request_output;
  request_output.set_zenz_preceding_text_request_length(24);
  request_output.set_zenz_following_text_request_length(10);
  state.UpdateFromOutput(request_output);
  EXPECT_TRUE(state.has_result());

  const TipZenzContextRequest request = state.Take();
  EXPECT_EQ(request.preceding_length, 24);
  EXPECT_EQ(request.following_length, 10);
  EXPECT_FALSE(state.has_result());

  state.UpdateFromOutput(request_output);
  EXPECT_TRUE(state.has_result());
  state.Reset();
  EXPECT_FALSE(state.has_result());
  EXPECT_TRUE(state.Take().empty());
}

TEST(TipZenzContextRequestTest, FallbackUsesCurrentCompositionState) {
  EXPECT_FALSE(ShouldRunZenzContextRequestFallback(
      /*has_test_key_result=*/true,
      /*has_generic_surrounding_text=*/true,
      /*in_composition=*/false));

  EXPECT_FALSE(ShouldRunZenzContextRequestFallback(
      /*has_test_key_result=*/false,
      /*has_generic_surrounding_text=*/false,
      /*in_composition=*/false));

  EXPECT_TRUE(ShouldRunZenzContextRequestFallback(
      /*has_test_key_result=*/false,
      /*has_generic_surrounding_text=*/true,
      /*in_composition=*/false));

  EXPECT_FALSE(ShouldRunZenzContextRequestFallback(
      /*has_test_key_result=*/false,
      /*has_generic_surrounding_text=*/true,
      /*in_composition=*/true));
}
}  // namespace tsf
}  // namespace win32
}  // namespace mozc
