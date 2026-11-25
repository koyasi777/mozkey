// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dictionary/dictionary_impl.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "base/container/tuple.h"
#include "base/util.h"
#include "config/config_handler.h"
#include "data_manager/testing/mock_data_manager.h"
#include "dictionary/dictionary_interface.h"
#include "dictionary/dictionary_token.h"
#include "dictionary/pos_matcher.h"
#include "dictionary/system/system_dictionary.h"
#include "dictionary/system/value_dictionary.h"
#include "dictionary/user_dictionary.h"
#include "dictionary/user_pos.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "request/options.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace dictionary {
namespace {

using UserEntry = user_dictionary::UserDictionary::Entry;

// A deterministic dictionary for testing exact raw-token provenance, without
// depending on the contents of the shipped system dictionary.
class SingleTokenDictionary final : public DictionaryInterface {
 public:
  explicit SingleTokenDictionary(Token token) : token_(std::move(token)) {}

  void LookupExact(absl::string_view reading,
                   Callback* callback) const override {
    if (reading != token_.key ||
        callback->OnKey(reading) != Callback::TRAVERSE_CONTINUE ||
        callback->OnActualKey(reading, reading, 0) !=
            Callback::TRAVERSE_CONTINUE) {
      return;
    }
    callback->OnToken(reading, reading, token_);
  }

 private:
  Token token_;
};

struct DictionaryData {
  std::unique_ptr<UserDictionaryInterface> user_dictionary;
  std::unique_ptr<PosMatcher> pos_matcher;
  std::unique_ptr<DictionaryInterface> dictionary;
};

std::unique_ptr<DictionaryData> CreateDictionaryData() {
  auto ret = std::make_unique<DictionaryData>();
  testing::MockDataManager data_manager;
  ret->pos_matcher =
      std::make_unique<PosMatcher>(data_manager.GetPosMatcherData());
  absl::string_view dictionary_data = data_manager.GetSystemDictionaryData();
  std::unique_ptr<SystemDictionary> sys_dict =
      SystemDictionary::Builder(dictionary_data.data(), dictionary_data.size())
          .Build()
          .value();
  auto val_dict = std::make_unique<ValueDictionary>(*ret->pos_matcher,
                                                    sys_dict->value_trie());

  auto user_pos =
      make_unique_from_tuples<UserPos>(data_manager.GetUserPosData());

  ret->user_dictionary = std::make_unique<dictionary::UserDictionary>(
      std::move(user_pos), *ret->pos_matcher);
  // Complete the asynchronous reload of the isolated test profile before
  // loading synthetic entries; a late reload must not replace test fixtures.
  ret->user_dictionary->WaitForReloader();

  ret->dictionary = std::make_unique<DictionaryImpl>(
      std::move(sys_dict), std::move(val_dict), *ret->user_dictionary,
      *ret->pos_matcher);
  return ret;
}

}  // namespace

class DictionaryImplTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override { config::ConfigHandler::GetDefaultConfig(&config_); }

  class CheckKeyValueExistenceCallback : public DictionaryInterface::Callback {
   public:
    CheckKeyValueExistenceCallback(absl::string_view key,
                                   absl::string_view value)
        : key_(key), value_(value), found_(false) {}

    ResultType OnToken(absl::string_view /* key */,
                       absl::string_view /* actual_key */,
                       Token token) override {
      // Read-only inspection; std::move is not used because token is only
      // checked and not stored.
      if (token.key == key_ && token.value == value_) {
        found_ = true;
        return TRAVERSE_DONE;
      }
      return TRAVERSE_CONTINUE;
    }

    bool found() const { return found_; }

   private:
    const absl::string_view key_, value_;
    bool found_;
  };

  class CheckSpellingExistenceCallback : public DictionaryInterface::Callback {
   public:
    CheckSpellingExistenceCallback(absl::string_view key,
                                   absl::string_view value)
        : key_(key), value_(value), found_(false) {}

    ResultType OnToken(absl::string_view /* key */,
                       absl::string_view /* actual_key */,
                       Token token) override {
      // Read-only inspection; std::move is not used because token is only
      // checked and not stored.
      if (token.key == key_ && token.value == value_ &&
          (token.attributes & Token::SPELLING_CORRECTION)) {
        found_ = true;
        return TRAVERSE_DONE;
      }
      return TRAVERSE_CONTINUE;
    }

    bool found() const { return found_; }

   private:
    const absl::string_view key_, value_;
    bool found_;
  };

  class CheckZipCodeExistenceCallback : public DictionaryInterface::Callback {
   public:
    explicit CheckZipCodeExistenceCallback(absl::string_view key,
                                           absl::string_view value,
                                           const PosMatcher& pos_matcher)
        : key_(key), value_(value), pos_matcher_(pos_matcher), found_(false) {}

    ResultType OnToken(absl::string_view /* key */,
                       absl::string_view /* actual_key */,
                       Token token) override {
      // Read-only inspection; std::move is not used because token is only
      // checked and not stored.
      if (token.key == key_ && token.value == value_ &&
          pos_matcher_.IsZipcode(token.lid)) {
        found_ = true;
        return TRAVERSE_DONE;
      }
      return TRAVERSE_CONTINUE;
    }

    bool found() const { return found_; }

   private:
    const absl::string_view key_, value_;
    const PosMatcher& pos_matcher_;
    bool found_;
  };

  class CheckEnglishT13nCallback : public DictionaryInterface::Callback {
   public:
    CheckEnglishT13nCallback(absl::string_view key, absl::string_view value)
        : key_(key), value_(value), found_(false) {}

    ResultType OnToken(absl::string_view /* key */,
                       absl::string_view /* actual_key */,
                       Token token) override {
      // Read-only inspection; std::move is not used because token is only
      // checked and not stored.
      if (token.key == key_ && token.value == value_ &&
          Util::IsEnglishTransliteration(token.value)) {
        found_ = true;
        return TRAVERSE_DONE;
      }
      return TRAVERSE_CONTINUE;
    }

    bool found() const { return found_; }

   private:
    const absl::string_view key_, value_;
    bool found_;
  };

  // Pair of DictionaryInterface's lookup method and query text.
  struct LookupMethodAndQuery {
    void (DictionaryInterface::*lookup_method)(
        absl::string_view, const ConversionOptions&,
        DictionaryInterface::Callback*) const;
    const char* query;
  };

  static ConversionRequest ConvReq(const config::Config& config) {
    return ConversionRequestBuilder().SetConfig(config).Build();
  }

  config::Config config_;
};

TEST_F(DictionaryImplTest, WordSuppressionTest) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* d = data->dictionary.get();

  constexpr absl::string_view kKey = "ぐーぐる";
  constexpr absl::string_view kValue = "グーグル";

  const LookupMethodAndQuery kTestPair[] = {
      {&DictionaryInterface::LookupPrefix, "ぐーぐるは"},
      {&DictionaryInterface::LookupPredictive, "ぐーぐ"},
  };

  // First add (kKey, kValue) to the suppression dictionary; thus it should not
  // be looked up.

  {
    user_dictionary::UserDictionaryStorage storage;
    UserEntry* entry = storage.add_dictionaries()->add_entries();
    entry->set_key(kKey);
    entry->set_value(kValue);
    entry->set_pos(user_dictionary::UserDictionary::SUPPRESSION_WORD);
    data->user_dictionary->Load(storage);
    EXPECT_TRUE(data->user_dictionary->HasSuppressedEntries());
  }

  const ConversionRequest convreq1 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckKeyValueExistenceCallback callback(kKey, kValue);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq1.options(),
                                     &callback);
    EXPECT_FALSE(callback.found());
  }

  {
    user_dictionary::UserDictionaryStorage storage;
    data->user_dictionary->Load(storage);
    EXPECT_FALSE(data->user_dictionary->HasSuppressedEntries());
  }

  const ConversionRequest convreq2 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckKeyValueExistenceCallback callback(kKey, kValue);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq2.options(),
                                     &callback);
    EXPECT_TRUE(callback.found());
  }
}

TEST_F(DictionaryImplTest, DisableSpellingCorrectionTest) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* d = data->dictionary.get();

  // "あぼがど" -> "アボカド", which is in the test dictionary.
  constexpr char kKey[] = "あぼがど";
  constexpr char kValue[] = "アボカド";

  const LookupMethodAndQuery kTestPair[] = {
      {&DictionaryInterface::LookupPrefix, kKey},
      {&DictionaryInterface::LookupPredictive, "あぼ"},
  };

  // The spelling correction entry (kKey, kValue) should be found if spelling
  // correction flag is set in the config.
  config_.set_use_spelling_correction(true);
  const ConversionRequest convreq1 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckSpellingExistenceCallback callback(kKey, kValue);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq1.options(),
                                     &callback);
    EXPECT_TRUE(callback.found());
  }

  // Without the flag, it should be suppressed.
  config_.set_use_spelling_correction(false);
  const ConversionRequest convreq2 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckSpellingExistenceCallback callback(kKey, kValue);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq2.options(),
                                     &callback);
    EXPECT_FALSE(callback.found());
  }
}

TEST_F(DictionaryImplTest, DisableZipCodeConversionTest) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* d = data->dictionary.get();

  // "100-0000" -> "東京都千代田区", which is in the test dictionary.
  constexpr char kKey[] = "100-0000";
  constexpr char kValue[] = "東京都千代田区";

  const LookupMethodAndQuery kTestPair[] = {
      {&DictionaryInterface::LookupPrefix, kKey},
      {&DictionaryInterface::LookupPredictive, "100"},
  };

  // The zip code entry (kKey, kValue) should be found if the flag is set in the
  // config.
  config_.set_use_zip_code_conversion(true);
  const ConversionRequest convreq1 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckZipCodeExistenceCallback callback(kKey, kValue, *data->pos_matcher);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq1.options(),
                                     &callback);
    EXPECT_TRUE(callback.found());
  }

  // Without the flag, it should be suppressed.
  config_.set_use_zip_code_conversion(false);
  const ConversionRequest convreq2 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckZipCodeExistenceCallback callback(kKey, kValue, *data->pos_matcher);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq2.options(),
                                     &callback);
    EXPECT_FALSE(callback.found());
  }
}

TEST_F(DictionaryImplTest, HyphenlessZipcodeLookup) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* dictionary = data->dictionary.get();

  // The mock dictionary only contains "100-0000", not "1000000".
  constexpr char kOriginalKey[] = "1000000";
  constexpr char kAddress[] = "東京都千代田区";
  const LookupMethodAndQuery kPositiveCases[] = {
      {&DictionaryInterface::LookupPrefix, kOriginalKey},
      {&DictionaryInterface::LookupPrefix, "1000000です"},
      {&DictionaryInterface::LookupPredictive, kOriginalKey},
      {&DictionaryInterface::LookupExact, kOriginalKey},
  };

  config_.set_use_zip_code_conversion(true);
  const ConversionRequest enabled = ConversionRequestBuilder()
                                        .SetConfig(config_)
                                        .SetKey(kOriginalKey)
                                        .Build();
  ASSERT_TRUE(enabled.options().enable_hyphenless_zipcode_alias);
  for (const auto& test_case : kPositiveCases) {
    CheckZipCodeExistenceCallback callback(kOriginalKey, kAddress,
                                           *data->pos_matcher);
    (dictionary->*test_case.lookup_method)(test_case.query, enabled.options(),
                                           &callback);
    EXPECT_TRUE(callback.found()) << test_case.query;
  }

  // Do not match partial or overlong numeric keys, even in prefix lookup.
  constexpr absl::string_view kNonZipcodeKeys[] = {
      "100000", "10000000", "10000000です", "x1000000", "100000x",
      "1000000A", "1000000ABC", "1000000x", "1000000aです"};
  for (const absl::string_view query : kNonZipcodeKeys) {
    CheckZipCodeExistenceCallback callback(kOriginalKey, kAddress,
                                           *data->pos_matcher);
    dictionary->LookupPrefix(query, enabled.options(), &callback);
    EXPECT_FALSE(callback.found()) << query;
  }

  // Sentence conversion now permits the alias. The caller that knows the
  // lattice offset, not DictionaryImpl, checks the preceding digit boundary.
  const ConversionRequest embedded = ConversionRequestBuilder()
                                         .SetConfig(config_)
                                         .SetKey("文中1000000")
                                         .Build();
  EXPECT_TRUE(embedded.options().enable_hyphenless_zipcode_alias);
  {
    CheckZipCodeExistenceCallback callback(kOriginalKey, kAddress,
                                           *data->pos_matcher);
    dictionary->LookupPrefix(kOriginalKey, embedded.options(), &callback);
    EXPECT_TRUE(callback.found());
  }

  config_.set_use_zip_code_conversion(false);
  const ConversionRequest disabled = ConversionRequestBuilder()
                                         .SetConfig(config_)
                                         .SetKey(kOriginalKey)
                                         .Build();
  for (const auto& test_case : kPositiveCases) {
    CheckZipCodeExistenceCallback callback(kOriginalKey, kAddress,
                                           *data->pos_matcher);
    (dictionary->*test_case.lookup_method)(test_case.query, disabled.options(),
                                           &callback);
    EXPECT_FALSE(callback.found()) << test_case.query;
  }
}

TEST_F(DictionaryImplTest, DisableT13nConversionTest) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* d = data->dictionary.get();

  constexpr char kKey[] = "ぐーぐる";
  constexpr char kValue[] = "Google";

  const LookupMethodAndQuery kTestPair[] = {
      {&DictionaryInterface::LookupPrefix, kKey},
      {&DictionaryInterface::LookupPredictive, "ぐー"},
  };

  // The T13N entry (kKey, kValue) should be found if the flag is set in the
  // config.
  config_.set_use_t13n_conversion(true);
  const ConversionRequest convreq1 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckEnglishT13nCallback callback(kKey, kValue);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq1.options(),
                                     &callback);
    EXPECT_TRUE(callback.found());
  }

  // Without the flag, it should be suppressed.
  config_.set_use_t13n_conversion(false);
  const ConversionRequest convreq2 = ConvReq(config_);
  for (size_t i = 0; i < std::size(kTestPair); ++i) {
    CheckEnglishT13nCallback callback(kKey, kValue);
    (d->*kTestPair[i].lookup_method)(kTestPair[i].query, convreq2.options(),
                                     &callback);
    EXPECT_FALSE(callback.found());
  }
}

TEST_F(DictionaryImplTest, LookupComment) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* d = data->dictionary.get();

  {
    user_dictionary::UserDictionaryStorage storage;
    UserEntry* entry = storage.add_dictionaries()->add_entries();
    entry->set_key("key");
    entry->set_value("comment");
    entry->set_comment("UserDictionaryStub");
    entry->set_pos(user_dictionary::UserDictionary::NOUN);
    data->user_dictionary->Load(storage);
  }

  std::string comment;
  const ConversionRequest convreq = ConvReq(config_);
  EXPECT_FALSE(d->LookupComment("key", "value", convreq.options(), &comment));
  EXPECT_TRUE(comment.empty());

  // If key or value is "comment", UserDictionaryStub returns
  // "UserDictionaryStub" as comment.
  EXPECT_TRUE(d->LookupComment("key", "comment", convreq.options(), &comment));
  EXPECT_EQ(comment, "UserDictionaryStub");
}

TEST_F(DictionaryImplTest, HasKeyValue) {
  std::unique_ptr<DictionaryData> data = CreateDictionaryData();
  DictionaryInterface* d = data->dictionary.get();
  EXPECT_TRUE(d->HasKey("きょうと"));
  EXPECT_TRUE(d->HasValue("京都"));

  EXPECT_FALSE(d->HasKey("__きょうと__"));
  EXPECT_FALSE(d->HasValue("__京都__"));
}

TEST_F(DictionaryImplTest, CostLookupSeparatesSourcesAndIgnoresValueDictionary) {
  auto data = CreateDictionaryData();
  user_dictionary::UserDictionaryStorage storage;
  UserEntry* user_entry = storage.add_dictionaries()->add_entries();
  user_entry->set_key("てすとよみ");
  user_entry->set_value("ユーザー語");
  user_entry->set_pos(user_dictionary::UserDictionary::NOUN);
  ASSERT_TRUE(data->user_dictionary->Load(storage));

  DictionaryImpl dictionary(
      std::make_unique<SingleTokenDictionary>(
          Token("てすとよみ", "システム語", 4800, 1, 2, 0)),
      std::make_unique<SingleTokenDictionary>(
          Token("てすとよみ", "値辞書の合成語", 100, 3, 4, 0)),
      *data->user_dictionary, *data->pos_matcher);

  const auto result = dictionary.LookupExactCostEntries("てすとよみ");
  ASSERT_EQ(result.entries.size(), 2);
  EXPECT_FALSE(result.system_truncated);
  EXPECT_FALSE(result.user_truncated);

  const auto& system_entry = result.entries[0];
  EXPECT_EQ(system_entry.source, DictionaryImpl::CostEntry::Source::kSystem);
  EXPECT_EQ(system_entry.token.key, "てすとよみ");
  EXPECT_EQ(system_entry.token.value, "システム語");
  EXPECT_EQ(system_entry.token.cost, 4800);
  EXPECT_EQ(system_entry.token.lid, 1);
  EXPECT_EQ(system_entry.token.rid, 2);

  const auto& actual_user_entry = result.entries[1];
  EXPECT_EQ(actual_user_entry.source,
            DictionaryImpl::CostEntry::Source::kUser);
  EXPECT_EQ(actual_user_entry.token.key, "てすとよみ");
  EXPECT_EQ(actual_user_entry.token.value, "ユーザー語");
  EXPECT_GT(actual_user_entry.token.cost, 0);
  EXPECT_TRUE(actual_user_entry.token.attributes & Token::USER_DICTIONARY);
}

TEST_F(DictionaryImplTest, CostLookupLimitsSourcesIndependently) {
  auto data = CreateDictionaryData();
  user_dictionary::UserDictionaryStorage storage;
  for (const char* value : {"ユーザー語一", "ユーザー語二"}) {
    UserEntry* entry = storage.add_dictionaries()->add_entries();
    entry->set_key("てすとよみ");
    entry->set_value(value);
    entry->set_pos(user_dictionary::UserDictionary::NOUN);
  }
  ASSERT_TRUE(data->user_dictionary->Load(storage));

  DictionaryImpl dictionary(
      std::make_unique<SingleTokenDictionary>(
          Token("てすとよみ", "システム語", 4800, 1, 2, 0)),
      std::make_unique<SingleTokenDictionary>(
          Token("てすとよみ", "値辞書の合成語", 100, 3, 4, 0)),
      *data->user_dictionary, *data->pos_matcher);

  const auto limited = dictionary.LookupExactCostEntries("てすとよみ", 1);
  ASSERT_EQ(limited.entries.size(), 2);
  EXPECT_EQ(limited.entries[0].source,
            DictionaryImpl::CostEntry::Source::kSystem);
  EXPECT_EQ(limited.entries[1].source,
            DictionaryImpl::CostEntry::Source::kUser);
  EXPECT_FALSE(limited.system_truncated);
  EXPECT_TRUE(limited.user_truncated);

  const auto all = dictionary.LookupExactCostEntries("てすとよみ", 2);
  EXPECT_EQ(all.entries.size(), 3);
  EXPECT_FALSE(all.system_truncated);
  EXPECT_FALSE(all.user_truncated);
  const auto none = dictionary.LookupExactCostEntries("てすとよみ", 0);
  EXPECT_TRUE(none.entries.empty());
  EXPECT_FALSE(none.system_truncated);
  EXPECT_FALSE(none.user_truncated);
}

TEST_F(DictionaryImplTest, CostLookupRequiresExactNonEmptyReading) {
  auto data = CreateDictionaryData();
  DictionaryImpl* dictionary =
      static_cast<DictionaryImpl*>(data->dictionary.get());
  EXPECT_TRUE(dictionary->LookupExactCostEntries("").entries.empty());
  EXPECT_TRUE(dictionary->LookupExactCostEntries("__きょうと__").entries.empty());
  const auto exact = dictionary->LookupExactCostEntries("きょうと");
  EXPECT_TRUE(std::any_of(
      exact.entries.begin(), exact.entries.end(),
      [](const DictionaryImpl::CostEntry& entry) {
        return entry.source == DictionaryImpl::CostEntry::Source::kSystem &&
               entry.token.key == "きょうと";
      }));
}

TEST_F(DictionaryImplTest, CostLookupDoesNotExposeSuggestionOnlyUserWords) {
  auto data = CreateDictionaryData();
  user_dictionary::UserDictionaryStorage storage;
  UserEntry* entry = storage.add_dictionaries()->add_entries();
  entry->set_key("てすとよみ");
  entry->set_value("サジェスト専用");
  entry->set_pos(user_dictionary::UserDictionary::SUGGESTION_ONLY);
  ASSERT_TRUE(data->user_dictionary->Load(storage));
  DictionaryImpl dictionary(
      std::make_unique<SingleTokenDictionary>(
          Token("べつのよみ", "システム語", 1000, 1, 2, 0)),
      std::make_unique<SingleTokenDictionary>(
          Token("べつのよみ", "値辞書の合成語", 100, 3, 4, 0)),
      *data->user_dictionary, *data->pos_matcher);
  EXPECT_TRUE(dictionary.LookupExactCostEntries("てすとよみ").entries.empty());
}

}  // namespace dictionary
}  // namespace mozc
