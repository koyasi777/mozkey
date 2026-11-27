

// Mozkey benchmark: fixed desktop Roman input, without dictionary conversion.

#include <memory>
#include <string>
#include <string_view>

#include "composer/composer.h"
#include "composer/table.h"
#include "config/character_form_manager.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "testing/benchmark.h"

namespace mozc::composer {
namespace {

constexpr std::string_view kInput = "nihongo";
constexpr std::string_view kReading = "にほんご";

void MeasureComposer(benchmark::State& state, bool query_each_key) {
  commands::Request request;
  config::Config config;
  config.set_preedit_method(config::Config::ROMAN);
  auto table = std::make_shared<Table>();
  if (!table->InitializeWithRequestAndConfig(request, config)) {
    state.SkipWithError("Roman table initialization failed");
    return;
  }
  config::CharacterFormManager::GetCharacterFormManager()->SetDefaultRule();
  Composer composer(table, request, config);
  // Verify the workload before measuring it.
  for (char key : kInput) {
    composer.InsertCharacter(std::string(1, key));
  }
  if (composer.GetQueryForConversion() != kReading) {
    state.SkipWithError("Unexpected Roman input reading");
    return;
  }
  composer.Reset();
  for (auto _ : state) {
    state.PauseTiming();
    composer.Reset();
    state.ResumeTiming();
    for (char key : kInput) {
      composer.InsertCharacter(std::string(1, key));
      if (query_each_key) {
        const std::string preedit = composer.GetStringForPreedit();
        const std::string query = composer.GetQueryForConversion();
        benchmark::DoNotOptimize(preedit);
        benchmark::DoNotOptimize(query);
      }
    }
    state.PauseTiming();
    const bool valid = composer.GetQueryForConversion() == kReading;
    state.ResumeTiming();
    if (!valid) {
      state.SkipWithError("Unexpected reading after measured input");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations() * kInput.size());
}

void BM_ComposerRomanInput(benchmark::State& state) {
  MeasureComposer(state, false);
}
BENCHMARK(BM_ComposerRomanInput);

void BM_ComposerRomanInputWithQueries(benchmark::State& state) {
  MeasureComposer(state, true);
}
BENCHMARK(BM_ComposerRomanInputWithQueries);

}  // namespace
}  // namespace mozc::composer
