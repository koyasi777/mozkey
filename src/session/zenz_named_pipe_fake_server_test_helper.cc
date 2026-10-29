// Separate-process fake server for the Windows Zenz Named Pipe test.
// Returns zero only if it observed no request bytes. No input content is logged.
#if !defined(_WIN32)
#error "zenz_named_pipe_fake_server_test_helper is Windows only"
#endif

#include <windows.h>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2 || argv[1] == nullptr) {
    return 20;
  }
  HANDLE pipe = ::CreateNamedPipeW(
      argv[1], PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      1, 4096, 4096, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return 21;
  }

  bool connected = ::ConnectNamedPipe(pipe, nullptr) != FALSE;
  const DWORD connect_error = connected ? ERROR_SUCCESS : ::GetLastError();
  if (!connected && connect_error == ERROR_PIPE_CONNECTED) {
    connected = true;
  }

  int result = 0;
  if (connected) {
    // A rejected client will close the pipe immediately. Reading one byte
    // verifies that even the protocol header was not disclosed to this
    // different-process impostor. Do not print the received byte.
    char byte = 0;
    DWORD received = 0;
    if (::ReadFile(pipe, &byte, 1, &received, nullptr) && received > 0) {
      result = 22;
    }
  } else if (connect_error != ERROR_NO_DATA) {
    // ERROR_NO_DATA is expected if the rejected client closes before
    // ConnectNamedPipe observes the connection.
    result = 23;
  }

  ::DisconnectNamedPipe(pipe);
  ::CloseHandle(pipe);
  return result;
}
