// Copyright 2026, Mozc contributors.
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

#include <objbase.h>
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "base/win32/scoped_com.h"
#include "win32/base/tsf_profile.h"

namespace {

#ifdef GOOGLE_JAPANESE_INPUT_BUILD
constexpr wchar_t kForwarder[] = L"GoogleIMEJaTIP64X.dll";
constexpr wchar_t kX64Implementation[] = L"GoogleIMEJaTIP64.dll";
constexpr wchar_t kArm64Implementation[] = L"GoogleIMEJaTIP64Arm.dll";
#else   // GOOGLE_JAPANESE_INPUT_BUILD
constexpr wchar_t kForwarder[] = L"mozc_tip64x.dll";
constexpr wchar_t kX64Implementation[] = L"mozc_tip64.dll";
constexpr wchar_t kArm64Implementation[] = L"mozc_tip64arm.dll";
#endif  // GOOGLE_JAPANESE_INPUT_BUILD

#if defined(_M_ARM64)
constexpr wchar_t kProcessArchitecture[] = L"ARM64";
constexpr const wchar_t *kExpectedImplementation = kArm64Implementation;
constexpr const wchar_t *kForbiddenImplementation = kX64Implementation;
#elif defined(_M_X64)
constexpr wchar_t kProcessArchitecture[] = L"x64";
constexpr const wchar_t *kExpectedImplementation = kX64Implementation;
constexpr const wchar_t *kForbiddenImplementation = kArm64Implementation;
#else
#error This smoke helper supports only ARM64 and x64.
#endif

void PrintModule(const wchar_t *label, const wchar_t *name, HMODULE module) {
  if (module == nullptr) {
    std::wprintf(L"%ls=%ls:NOT_LOADED\n", label, name);
    return;
  }

  wchar_t path[32768] = {};
  const DWORD length = ::GetModuleFileNameW(module, path, _countof(path));
  if (length == 0 || length >= _countof(path)) {
    std::wprintf(L"%ls=%ls:LOADED:path_unavailable:error=%lu\n", label, name,
                 ::GetLastError());
    return;
  }

  std::wprintf(L"%ls=%ls:LOADED:path=%ls\n", label, name, path);
}

}  // namespace

int main() {
  std::wprintf(L"process_architecture=%ls\n", kProcessArchitecture);

  mozc::ScopedCOMInitializer com_initializer;
  const HRESULT init_result = com_initializer.error_code();
  std::wprintf(L"CoInitialize=0x%08lX\n",
               static_cast<unsigned long>(init_result));
  if (FAILED(init_result)) {
    return 10;
  }

  IUnknown *object = nullptr;
  const HRESULT activation_result = ::CoCreateInstance(
      mozc::win32::TsfProfile::GetTextServiceGuid(), nullptr,
      CLSCTX_INPROC_SERVER, IID_IUnknown,
      reinterpret_cast<void **>(&object));

  std::wprintf(L"CoCreateInstance=0x%08lX\n",
               static_cast<unsigned long>(activation_result));
  if (FAILED(activation_result) || object == nullptr) {
    if (object != nullptr) {
      object->Release();
    }
    return 20;
  }

  HMODULE expected = ::GetModuleHandleW(kExpectedImplementation);
  HMODULE forbidden = ::GetModuleHandleW(kForbiddenImplementation);
  HMODULE forwarder = ::GetModuleHandleW(kForwarder);

  PrintModule(L"expected_implementation", kExpectedImplementation, expected);
  PrintModule(L"forbidden_implementation", kForbiddenImplementation, forbidden);
  PrintModule(L"forwarder_informational", kForwarder, forwarder);

  int result = 0;
  if (expected == nullptr) {
    std::wprintf(L"gate_expected_implementation=FAIL\n");
    result = 30;
  } else {
    std::wprintf(L"gate_expected_implementation=PASS\n");
  }

  if (forbidden != nullptr) {
    std::wprintf(L"gate_forbidden_implementation=FAIL\n");
    result = 40;
  } else {
    std::wprintf(L"gate_forbidden_implementation=PASS\n");
  }

  object->Release();

  if (result == 0) {
    std::wprintf(L"TIP_CLIENT_ARCHITECTURE_SMOKE=PASS\n");
  }
  return result;
}
