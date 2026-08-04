#ifndef MOZC_ZENZ_SCORER_POSIX_SERVER_H_
#define MOZC_ZENZ_SCORER_POSIX_SERVER_H_

#include <cstdint>
#include <functional>
#include <string>

namespace mozc {
namespace zenz_scorer {

struct PosixZenzRequest {
  uint32_t generation = 0;
  uint32_t timeout_msec = 0;
  uint32_t max_output_chars = 0;
  std::string prompt;
};

struct PosixZenzResponse {
  uint32_t status = 1;
  uint32_t latency_msec = 0;
  std::string value;
  std::string debug;
};

using PosixZenzBackend =
    std::function<PosixZenzResponse(const PosixZenzRequest&)>;

class PosixZenzScorerServer final {
 public:
  PosixZenzScorerServer(std::string socket_path, PosixZenzBackend backend);
  ~PosixZenzScorerServer();

  PosixZenzScorerServer(const PosixZenzScorerServer&) = delete;
  PosixZenzScorerServer& operator=(const PosixZenzScorerServer&) = delete;

  bool Start(std::string* error);

  // Waits for at most |timeout_msec| and serves one client when available.
  // A timeout is a successful no-op.  False indicates a fatal listener error.
  bool ServeOne(int timeout_msec, std::string* error);

 private:
  bool HandleClient(int client_fd, std::string* error);
  void Reset();

  std::string socket_path_;
  std::string directory_path_;
  std::string lock_path_;
  PosixZenzBackend backend_;
  int lock_fd_ = -1;
  int listen_fd_ = -1;
  bool owns_socket_path_ = false;
};

}  // namespace zenz_scorer
}  // namespace mozc

#endif  // MOZC_ZENZ_SCORER_POSIX_SERVER_H_
