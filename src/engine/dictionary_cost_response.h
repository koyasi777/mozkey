// Copyright 2026, Mozkey contributors.
// Read-only dictionary cost response assembly. No conversion behavior changes.
#ifndef MOZC_ENGINE_DICTIONARY_COST_RESPONSE_H_
#define MOZC_ENGINE_DICTIONARY_COST_RESPONSE_H_

#include "dictionary/dictionary_interface.h"
#include "protocol/commands.pb.h"

namespace mozc::engine::internal {

// Packs raw dictionary tokens into a bounded protobuf response. Both sources
// have protected capacity; unused capacity can be reclaimed by the other.
// Entries that do not fit are skipped, and their source's truncation flag is
// set. Input order among the entries actually returned is preserved.
void BuildBoundedDictionaryCostResponse(
    const dictionary::DictionaryCostLookupResult& raw,
    commands::DictionaryCostLookupResult* output);

}  // namespace mozc::engine::internal

#endif  // MOZC_ENGINE_DICTIONARY_COST_RESPONSE_H_
