#ifndef MOZC_ZENZ_SCORER_POSIX_RUNTIME_H_
#define MOZC_ZENZ_SCORER_POSIX_RUNTIME_H_

#include <memory>
#include <string>

#include "zenz_scorer/llama_server_process.h"

namespace mozc {
namespace zenz_scorer {

class PosixZenzScorerServer;

struct PosixScorerRuntimeOptions {
  std::string socket_path;
  LlamaServerProcessOptions llama_server;
  int n_predict = 64;
};

// Owns the complete macOS scorer runtime: the private Unix-domain socket
// listener and one authenticated localhost llama-server child process.
class PosixScorerRuntime final {
 public:
  explicit PosixScorerRuntime(PosixScorerRuntimeOptions options);
  ~PosixScorerRuntime();

  PosixScorerRuntime(const PosixScorerRuntime&) = delete;
  PosixScorerRuntime& operator=(const PosixScorerRuntime&) = delete;

  bool Start(std::string* error);

  // Waits for at most |timeout_msec| and serves one ZNZ1 client.  A timeout is
  // a successful no-op.  False indicates a fatal listener or child-process
  // failure.
  bool ServeOne(int timeout_msec, std::string* error);

  void Stop();
  bool running();

 private:
  PosixScorerRuntimeOptions options_;
  LlamaServerProcess llama_process_;
  std::unique_ptr<PosixZenzScorerServer> server_;
};

}  // namespace zenz_scorer
}  // namespace mozc

#endif  // MOZC_ZENZ_SCORER_POSIX_RUNTIME_H_
