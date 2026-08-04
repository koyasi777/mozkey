#ifndef MOZC_ZENZ_SCORER_LLAMA_BACKEND_H_
#define MOZC_ZENZ_SCORER_LLAMA_BACKEND_H_

#include <string>

#include "zenz_scorer/posix_server.h"

namespace mozc {
namespace zenz_scorer {

struct LlamaBackendOptions {
  int port = 0;
  std::string api_key;
  int n_predict = 64;
};

// Creates the POSIX scorer backend that forwards one ZNZ1 request to the
// non-streaming /completion endpoint of a localhost llama-server.
PosixZenzBackend CreateLlamaHttpBackend(LlamaBackendOptions options);

}  // namespace zenz_scorer
}  // namespace mozc

#endif  // MOZC_ZENZ_SCORER_LLAMA_BACKEND_H_
