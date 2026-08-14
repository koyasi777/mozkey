#ifndef MOZC_ZENZ_SCORER_LLAMA_SERVER_PROCESS_H_
#define MOZC_ZENZ_SCORER_LLAMA_SERVER_PROCESS_H_

#include <sys/types.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mozc {
namespace zenz_scorer {

struct LlamaServerProcessOptions {
  std::string executable_path;
  std::string model_path;
  int context_size = 256;
  int threads = 4;

  // Cold start includes model loading and the first authenticated completion.
  // Keep each completion probe long enough for slower supported hosts to
  // finish useful work instead of repeatedly cancelling in-flight inference.
  int readiness_timeout_msec = 180000;
  int readiness_probe_interval_msec = 250;
  int readiness_probe_timeout_msec = 10000;

  // Appended after the normal llama-server arguments.  Production callers
  // should normally leave this empty.  It exists so process behavior can be
  // exercised by a controlled test helper without changing the production
  // command line.
  std::vector<std::string> additional_args;
};

// Owns one localhost llama-server child process.  Start() allocates an
// OS-assigned loopback port, creates a per-launch API key, spawns the child in
// its own process group, and waits until an authenticated /completion request
// succeeds.  Child output is drained continuously into a bounded in-memory
// tail so startup failures remain diagnosable without allowing unbounded logs.
// Stop() terminates and reaps the whole child process group.
class LlamaServerProcess final {
 public:
  explicit LlamaServerProcess(LlamaServerProcessOptions options);
  ~LlamaServerProcess();

  LlamaServerProcess(const LlamaServerProcess&) = delete;
  LlamaServerProcess& operator=(const LlamaServerProcess&) = delete;

  bool Start(std::string* error);
  void Stop();

  bool running();
  int port() const { return port_; }
  const std::string& api_key() const { return api_key_; }
  pid_t pid() const { return pid_; }

 private:
  bool Spawn(std::string* error);
  bool WaitUntilReady(std::string* error);
  bool ReapIfExited(int* status);
  void CaptureOutput(int fd);
  void JoinOutputCapture();
  void ClearCapturedOutput();
  void AppendCapturedOutput(std::string_view api_key, std::string* error);
  void ResetState();

  LlamaServerProcessOptions options_;
  pid_t pid_ = -1;
  int port_ = 0;
  std::string api_key_;
  std::thread output_thread_;
  std::mutex output_mutex_;
  std::string output_tail_;
};

}  // namespace zenz_scorer
}  // namespace mozc

#endif  // MOZC_ZENZ_SCORER_LLAMA_SERVER_PROCESS_H_
