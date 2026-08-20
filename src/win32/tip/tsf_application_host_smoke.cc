// Copyright 2026, Mozc contributors.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//     * Neither the name of Google Inc. nor the names of its contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <windows.h>

#include <msctf.h>
#include <objbase.h>
#include <olectl.h>
#include <textstor.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <string>
#include <thread>

#include "base/win32/scoped_com.h"
#include "win32/base/tsf_profile.h"
#include "win32/tip/tip_status.h"

namespace {

constexpr TsViewCookie kViewCookie = 1;

class TextStore final : public ITextStoreACP,
                        public ITfContextOwnerCompositionSink {
 public:
  TextStore() = default;

  TextStore(const TextStore &) = delete;
  TextStore &operator=(const TextStore &) = delete;

  bool sink_advised() const { return sink_ != nullptr; }
  bool composition_started() const { return composition_started_; }
  bool composition_updated() const { return composition_updated_; }
  bool composition_ended() const { return composition_ended_; }
  const std::wstring &text() const { return text_; }

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void **object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;

    if (::IsEqualIID(riid, IID_IUnknown) ||
        ::IsEqualIID(riid, IID_ITextStoreACP)) {
      *object = static_cast<ITextStoreACP *>(this);
    } else if (::IsEqualIID(riid, IID_ITfContextOwnerCompositionSink)) {
      *object = static_cast<ITfContextOwnerCompositionSink *>(this);
    } else {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return ++ref_count_;
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG result = --ref_count_;
    if (result == 0) {
      delete this;
    }
    return result;
  }

  // ITextStoreACP
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown *unknown,
                          DWORD mask) override {
    if (!::IsEqualIID(riid, IID_ITextStoreACPSink) || unknown == nullptr) {
      return E_INVALIDARG;
    }

    ITextStoreACPSink *new_sink = nullptr;
    const HRESULT hr =
        unknown->QueryInterface(IID_PPV_ARGS(&new_sink));
    if (FAILED(hr) || new_sink == nullptr) {
      return E_NOINTERFACE;
    }

    if (sink_ != nullptr && sink_ != new_sink) {
      new_sink->Release();
      return CONNECT_E_ADVISELIMIT;
    }

    if (sink_ == nullptr) {
      sink_ = new_sink;
    } else {
      new_sink->Release();
    }
    sink_mask_ = mask;
    return S_OK;
  }

  STDMETHODIMP UnadviseSink(IUnknown *unknown) override {
    if (unknown == nullptr) {
      return E_INVALIDARG;
    }
    if (sink_ == nullptr) {
      return CONNECT_E_NOCONNECTION;
    }

    IUnknown *sink_identity = nullptr;
    IUnknown *unknown_identity = nullptr;
    HRESULT hr1 = sink_->QueryInterface(IID_PPV_ARGS(&sink_identity));
    HRESULT hr2 = unknown->QueryInterface(IID_PPV_ARGS(&unknown_identity));

    const bool same =
        SUCCEEDED(hr1) && SUCCEEDED(hr2) &&
        sink_identity == unknown_identity;

    if (sink_identity != nullptr) {
      sink_identity->Release();
    }
    if (unknown_identity != nullptr) {
      unknown_identity->Release();
    }

    if (!same) {
      return CONNECT_E_NOCONNECTION;
    }

    sink_->Release();
    sink_ = nullptr;
    sink_mask_ = 0;
    return S_OK;
  }

  STDMETHODIMP RequestLock(DWORD lock_flags,
                           HRESULT *session_result) override {
    if (session_result == nullptr) {
      return E_POINTER;
    }
    if (sink_ == nullptr) {
      return E_UNEXPECTED;
    }

    const DWORD requested =
        ((lock_flags & TS_LF_READWRITE) == TS_LF_READWRITE)
            ? TS_LF_READWRITE
            : TS_LF_READ;

    if (lock_type_ != 0) {
      if ((lock_flags & TS_LF_SYNC) != 0) {
        *session_result = TS_E_SYNCHRONOUS;
        return S_OK;
      }

      if (requested == TS_LF_READWRITE && lock_type_ != TS_LF_READWRITE) {
        pending_lock_ = TS_LF_READWRITE;
      } else if (pending_lock_ == 0) {
        pending_lock_ = requested;
      }
      *session_result = TS_S_ASYNC;
      return S_OK;
    }

    GrantLock(requested, session_result);

    if (pending_lock_ != 0) {
      const DWORD pending = pending_lock_;
      pending_lock_ = 0;
      HRESULT ignored = S_OK;
      GrantLock(pending, &ignored);
    }

    return S_OK;
  }

  STDMETHODIMP GetStatus(TS_STATUS *status) override {
    if (status == nullptr) {
      return E_POINTER;
    }
    status->dwDynamicFlags = 0;
    status->dwStaticFlags = TS_SS_NOHIDDENTEXT;
    return S_OK;
  }

  STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG cch,
                           LONG *result_start,
                           LONG *result_end) override {
    if (result_start == nullptr || result_end == nullptr) {
      return E_POINTER;
    }
    if (!ValidRange(start, end)) {
      return TS_E_INVALIDPOS;
    }
    *result_start = start;
    *result_end = start + static_cast<LONG>(cch);
    return S_OK;
  }

  STDMETHODIMP GetSelection(ULONG index, ULONG count,
                            TS_SELECTION_ACP *selection,
                            ULONG *fetched) override {
    if (!HasReadLock()) {
      return TS_E_NOLOCK;
    }
    if (selection == nullptr || fetched == nullptr || count == 0) {
      return E_INVALIDARG;
    }
    if (index != TS_DEFAULT_SELECTION && index != 0) {
      *fetched = 0;
      return S_OK;
    }

    selection[0].acpStart = selection_start_;
    selection[0].acpEnd = selection_end_;
    selection[0].style.ase = TS_AE_END;
    selection[0].style.fInterimChar = FALSE;
    *fetched = 1;
    return S_OK;
  }

  STDMETHODIMP SetSelection(ULONG count,
                            const TS_SELECTION_ACP *selection) override {
    if (!HasWriteLock()) {
      return TS_E_NOLOCK;
    }
    if (selection == nullptr || count != 1) {
      return E_INVALIDARG;
    }
    if (!ValidRange(selection[0].acpStart, selection[0].acpEnd)) {
      return TS_E_INVALIDPOS;
    }

    selection_start_ = selection[0].acpStart;
    selection_end_ = selection[0].acpEnd;
    return S_OK;
  }

  STDMETHODIMP GetText(LONG start, LONG end,
                       WCHAR *plain, ULONG plain_capacity,
                       ULONG *plain_count,
                       TS_RUNINFO *run_info, ULONG run_capacity,
                       ULONG *run_count, LONG *next) override {
    if (!HasReadLock()) {
      return TS_E_NOLOCK;
    }
    if (plain_count == nullptr || run_count == nullptr || next == nullptr) {
      return E_POINTER;
    }

    const LONG text_size = static_cast<LONG>(text_.size());
    if (end == -1) {
      end = text_size;
    }
    if (!ValidRange(start, end)) {
      return TS_E_INVALIDPOS;
    }

    const ULONG available = static_cast<ULONG>(end - start);
    ULONG copied = 0;
    if (plain != nullptr && plain_capacity > 0) {
      copied = std::min(available, plain_capacity);
      if (copied > 0) {
        std::wmemcpy(plain, text_.data() + start, copied);
      }
    }
    *plain_count = copied;

    if (run_info != nullptr && run_capacity > 0 && available > 0) {
      run_info[0].uCount = available;
      run_info[0].type = TS_RT_PLAIN;
      *run_count = 1;
    } else {
      *run_count = 0;
    }

    *next = (plain != nullptr && plain_capacity > 0)
                ? start + static_cast<LONG>(copied)
                : end;
    return S_OK;
  }

  STDMETHODIMP SetText(DWORD flags, LONG start, LONG end,
                       const WCHAR *new_text, ULONG cch,
                       TS_TEXTCHANGE *change) override {
    if (!HasWriteLock()) {
      return TS_E_NOLOCK;
    }
    if (change == nullptr || (new_text == nullptr && cch != 0)) {
      return E_INVALIDARG;
    }
    if (!ValidRange(start, end)) {
      return TS_E_INVALIDPOS;
    }

    ReplaceRange(start, end, new_text, cch, change);
    return S_OK;
  }

  STDMETHODIMP GetFormattedText(LONG start, LONG end,
                                IDataObject **data_object) override {
    if (data_object != nullptr) {
      *data_object = nullptr;
    }
    return E_NOTIMPL;
  }

  STDMETHODIMP GetEmbedded(LONG position, REFGUID service,
                           REFIID riid, IUnknown **object) override {
    if (object != nullptr) {
      *object = nullptr;
    }
    return E_NOTIMPL;
  }

  STDMETHODIMP QueryInsertEmbedded(const GUID *service,
                                   const FORMATETC *format,
                                   BOOL *insertable) override {
    if (insertable == nullptr) {
      return E_POINTER;
    }
    *insertable = FALSE;
    return S_OK;
  }

  STDMETHODIMP InsertEmbedded(DWORD flags, LONG start, LONG end,
                              IDataObject *data_object,
                              TS_TEXTCHANGE *change) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP InsertTextAtSelection(DWORD flags,
                                     const WCHAR *new_text, ULONG cch,
                                     LONG *start, LONG *end,
                                     TS_TEXTCHANGE *change) override {
    if ((new_text == nullptr && cch != 0)) {
      return E_INVALIDARG;
    }

    const LONG old_start = selection_start_;
    const LONG old_end = selection_end_;
    const LONG new_end = old_start + static_cast<LONG>(cch);

    if ((flags & TS_IAS_NOQUERY) == 0) {
      if (start == nullptr || end == nullptr) {
        return E_POINTER;
      }
      *start = old_start;
      *end = new_end;
    }

    if ((flags & TS_IAS_QUERYONLY) != 0) {
      return S_OK;
    }

    if (!HasWriteLock()) {
      return TS_E_NOLOCK;
    }
    if (change == nullptr) {
      return E_POINTER;
    }

    ReplaceRange(old_start, old_end, new_text, cch, change);
    return S_OK;
  }

  STDMETHODIMP InsertEmbeddedAtSelection(DWORD flags,
                                         IDataObject *data_object,
                                         LONG *start, LONG *end,
                                         TS_TEXTCHANGE *change) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP RequestSupportedAttrs(DWORD flags, ULONG count,
                                     const TS_ATTRID *attrs) override {
    return S_OK;
  }

  STDMETHODIMP RequestAttrsAtPosition(LONG position, ULONG count,
                                      const TS_ATTRID *attrs,
                                      DWORD flags) override {
    return S_OK;
  }

  STDMETHODIMP RequestAttrsTransitioningAtPosition(
      LONG position, ULONG count, const TS_ATTRID *attrs,
      DWORD flags) override {
    return S_OK;
  }

  STDMETHODIMP FindNextAttrTransition(
      LONG start, LONG halt, ULONG count, const TS_ATTRID *attrs,
      DWORD flags, LONG *next, BOOL *found,
      LONG *found_offset) override {
    if (next == nullptr || found == nullptr || found_offset == nullptr) {
      return E_POINTER;
    }
    *next = halt;
    *found = FALSE;
    *found_offset = 0;
    return S_OK;
  }

  STDMETHODIMP RetrieveRequestedAttrs(ULONG count,
                                      TS_ATTRVAL *attrs,
                                      ULONG *fetched) override {
    if (fetched == nullptr) {
      return E_POINTER;
    }
    *fetched = 0;
    return S_OK;
  }

  STDMETHODIMP GetEndACP(LONG *end) override {
    if (!HasReadLock()) {
      return TS_E_NOLOCK;
    }
    if (end == nullptr) {
      return E_POINTER;
    }
    *end = static_cast<LONG>(text_.size());
    return S_OK;
  }

  STDMETHODIMP GetActiveView(TsViewCookie *view) override {
    if (view == nullptr) {
      return E_POINTER;
    }
    *view = kViewCookie;
    return S_OK;
  }

  STDMETHODIMP GetACPFromPoint(TsViewCookie view, const POINT *point,
                               DWORD flags, LONG *position) override {
    if (!HasReadLock()) {
      return TS_E_NOLOCK;
    }
    return TS_E_NOLAYOUT;
  }

  STDMETHODIMP GetTextExt(TsViewCookie view, LONG start, LONG end,
                          RECT *rect, BOOL *clipped) override {
    if (!HasReadLock()) {
      return TS_E_NOLOCK;
    }
    if (rect == nullptr || clipped == nullptr) {
      return E_POINTER;
    }
    ::SetRectEmpty(rect);
    *clipped = FALSE;
    return TS_E_NOLAYOUT;
  }

  STDMETHODIMP GetScreenExt(TsViewCookie view, RECT *rect) override {
    if (rect == nullptr) {
      return E_POINTER;
    }
    ::SetRectEmpty(rect);
    return S_OK;
  }

  STDMETHODIMP GetWnd(TsViewCookie view, HWND *window) override {
    if (window == nullptr) {
      return E_POINTER;
    }
    *window = nullptr;
    return S_OK;
  }

  // ITfContextOwnerCompositionSink
  STDMETHODIMP OnStartComposition(ITfCompositionView *composition,
                                  BOOL *ok) override {
    if (ok == nullptr) {
      return E_POINTER;
    }
    composition_started_ = true;
    composition_ended_ = false;
    *ok = TRUE;
    return S_OK;
  }

  STDMETHODIMP OnUpdateComposition(ITfCompositionView *composition,
                                   ITfRange *new_range) override {
    composition_updated_ = true;
    return S_OK;
  }

  STDMETHODIMP OnEndComposition(ITfCompositionView *composition) override {
    composition_ended_ = true;
    return S_OK;
  }

 private:
  ~TextStore() {
    if (sink_ != nullptr) {
      sink_->Release();
      sink_ = nullptr;
    }
  }

  bool HasReadLock() const {
    return lock_type_ == TS_LF_READ || lock_type_ == TS_LF_READWRITE;
  }

  bool HasWriteLock() const {
    return lock_type_ == TS_LF_READWRITE;
  }

  bool ValidRange(LONG start, LONG end) const {
    const LONG size = static_cast<LONG>(text_.size());
    return start >= 0 && end >= start && end <= size;
  }

  void GrantLock(DWORD lock_type, HRESULT *session_result) {
    lock_type_ = lock_type;
    *session_result = sink_->OnLockGranted(lock_type);
    lock_type_ = 0;
  }

  void ReplaceRange(LONG start, LONG end,
                    const WCHAR *new_text, ULONG cch,
                    TS_TEXTCHANGE *change) {
    const std::wstring replacement =
        (new_text == nullptr || cch == 0)
            ? std::wstring()
            : std::wstring(new_text, new_text + cch);

    text_.replace(static_cast<size_t>(start),
                  static_cast<size_t>(end - start), replacement);

    change->acpStart = start;
    change->acpOldEnd = end;
    change->acpNewEnd = start + static_cast<LONG>(cch);

    selection_start_ = change->acpNewEnd;
    selection_end_ = change->acpNewEnd;
  }

  std::atomic<ULONG> ref_count_{1};
  ITextStoreACPSink *sink_ = nullptr;
  DWORD sink_mask_ = 0;
  DWORD lock_type_ = 0;
  DWORD pending_lock_ = 0;

  std::wstring text_;
  LONG selection_start_ = 0;
  LONG selection_end_ = 0;

  bool composition_started_ = false;
  bool composition_updated_ = false;
  bool composition_ended_ = false;
};

void PumpMessages(std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  MSG message = {};
  while (std::chrono::steady_clock::now() < deadline) {
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

HRESULT ActivateInstalledProfile() {
  ITfInputProcessorProfiles *profiles = nullptr;
  HRESULT hr = ::TF_CreateInputProcessorProfiles(&profiles);
  if (FAILED(hr) || profiles == nullptr) {
    if (profiles != nullptr) {
      profiles->Release();
    }
    return FAILED(hr) ? hr : E_FAIL;
  }

  ITfInputProcessorProfileMgr *profile_mgr = nullptr;
  hr = profiles->QueryInterface(IID_PPV_ARGS(&profile_mgr));
  profiles->Release();

  if (FAILED(hr) || profile_mgr == nullptr) {
    if (profile_mgr != nullptr) {
      profile_mgr->Release();
    }
    return FAILED(hr) ? hr : E_NOINTERFACE;
  }

  hr = profile_mgr->ActivateProfile(
      TF_PROFILETYPE_INPUTPROCESSOR,
      mozc::win32::TsfProfile::GetLangId(),
      mozc::win32::TsfProfile::GetTextServiceGuid(),
      mozc::win32::TsfProfile::GetProfileGuid(),
      nullptr,
      TF_IPPMF_FORPROCESS | TF_IPPMF_FORSESSION);

  profile_mgr->Release();
  return hr;
}

void PrintHr(const wchar_t *name, HRESULT hr) {
  std::wprintf(L"%ls=0x%08lX\n", name,
               static_cast<unsigned long>(hr));
}


bool SameComIdentity(IUnknown *lhs, IUnknown *rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }

  IUnknown *lhs_identity = nullptr;
  IUnknown *rhs_identity = nullptr;

  const HRESULT lhs_hr =
      lhs->QueryInterface(IID_PPV_ARGS(&lhs_identity));
  const HRESULT rhs_hr =
      rhs->QueryInterface(IID_PPV_ARGS(&rhs_identity));

  const bool same =
      SUCCEEDED(lhs_hr) && SUCCEEDED(rhs_hr) &&
      lhs_identity == rhs_identity;

  if (lhs_identity != nullptr) {
    lhs_identity->Release();
  }
  if (rhs_identity != nullptr) {
    rhs_identity->Release();
  }

  return same;
}

HRESULT GetActiveKeyboardProfile(TF_INPUTPROCESSORPROFILE *profile) {
  if (profile == nullptr) {
    return E_POINTER;
  }

  *profile = {};

  ITfInputProcessorProfiles *profiles = nullptr;
  HRESULT hr = ::TF_CreateInputProcessorProfiles(&profiles);
  if (FAILED(hr) || profiles == nullptr) {
    if (profiles != nullptr) {
      profiles->Release();
    }
    return FAILED(hr) ? hr : E_FAIL;
  }

  ITfInputProcessorProfileMgr *profile_mgr = nullptr;
  hr = profiles->QueryInterface(IID_PPV_ARGS(&profile_mgr));
  profiles->Release();

  if (FAILED(hr) || profile_mgr == nullptr) {
    if (profile_mgr != nullptr) {
      profile_mgr->Release();
    }
    return FAILED(hr) ? hr : E_NOINTERFACE;
  }

  hr = profile_mgr->GetActiveProfile(
      GUID_TFCAT_TIP_KEYBOARD, profile);
  profile_mgr->Release();
  return hr;
}

void PrintGuid(const wchar_t *name, REFGUID guid) {
  wchar_t value[64] = {};
  const int length = ::StringFromGUID2(
      guid, value,
      static_cast<int>(sizeof(value) / sizeof(value[0])));

  if (length <= 0) {
    std::wprintf(L"%ls=<StringFromGUID2 failed>\n", name);
    return;
  }

  std::wprintf(L"%ls=%ls\n", name, value);
}


}  // namespace

int wmain() {
#if !defined(_M_ARM64)
#error This smoke helper must be compiled as native ARM64.
#endif

  std::wprintf(L"process_architecture=ARM64\n");

  mozc::ScopedCOMInitializer com_initializer;
  HRESULT hr = com_initializer.error_code();
  PrintHr(L"CoInitialize", hr);
  if (FAILED(hr)) {
    return 10;
  }

  ITfThreadMgr *thread_mgr = nullptr;
  hr = ::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&thread_mgr));
  PrintHr(L"CoCreateInstance_ThreadMgr", hr);
  if (FAILED(hr) || thread_mgr == nullptr) {
    return 20;
  }

  TfClientId client_id = TF_CLIENTID_NULL;
  hr = thread_mgr->Activate(&client_id);
  PrintHr(L"ThreadMgr_Activate", hr);
  std::wprintf(L"client_id=%lu\n", static_cast<unsigned long>(client_id));
  if (FAILED(hr) || client_id == TF_CLIENTID_NULL) {
    thread_mgr->Release();
    return 30;
  }

  hr = ActivateInstalledProfile();
  PrintHr(L"ActivateInstalledProfile", hr);
  if (FAILED(hr)) {
    thread_mgr->Deactivate();
    thread_mgr->Release();
    return 40;
  }


  PumpMessages(std::chrono::milliseconds(250));

  TF_INPUTPROCESSORPROFILE active_profile = {};
  const HRESULT active_profile_hr =
      GetActiveKeyboardProfile(&active_profile);
  PrintHr(L"GetActiveProfile_keyboard", active_profile_hr);

  if (SUCCEEDED(active_profile_hr)) {
    PrintGuid(L"active_profile_clsid", active_profile.clsid);
    PrintGuid(L"active_profile_guid", active_profile.guidProfile);
    std::wprintf(
        L"active_profile_type=%lu\n",
        static_cast<unsigned long>(active_profile.dwProfileType));
    std::wprintf(
        L"active_profile_langid=0x%04X\n",
        static_cast<unsigned int>(active_profile.langid));
    std::wprintf(
        L"active_profile_flags=0x%08lX\n",
        static_cast<unsigned long>(active_profile.dwFlags));
  }

  const bool active_profile_is_mozkey =
      active_profile_hr == S_OK &&
      active_profile.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR &&
      ::IsEqualGUID(
          active_profile.clsid,
          mozc::win32::TsfProfile::GetTextServiceGuid()) &&
      ::IsEqualGUID(
          active_profile.guidProfile,
          mozc::win32::TsfProfile::GetProfileGuid());

  std::wprintf(
      L"active_profile_is_mozkey=%ls\n",
      active_profile_is_mozkey ? L"true" : L"false");

  ITfDocumentMgr *document_mgr = nullptr;
  hr = thread_mgr->CreateDocumentMgr(&document_mgr);
  PrintHr(L"CreateDocumentMgr", hr);
  if (FAILED(hr) || document_mgr == nullptr) {
    thread_mgr->Deactivate();
    thread_mgr->Release();
    return 50;
  }

  TextStore *store = new TextStore();

  ITfContext *context = nullptr;
  TfEditCookie edit_cookie = 0;
  hr = document_mgr->CreateContext(
      client_id, 0, static_cast<ITextStoreACP *>(store),
      &context, &edit_cookie);
  PrintHr(L"CreateContext", hr);
  std::wprintf(L"edit_cookie=%lu\n",
               static_cast<unsigned long>(edit_cookie));
  if (FAILED(hr) || context == nullptr) {
    store->Release();
    document_mgr->Release();
    thread_mgr->Deactivate();
    thread_mgr->Release();
    return 60;
  }

  hr = document_mgr->Push(context);
  PrintHr(L"DocumentMgr_Push", hr);
  if (FAILED(hr)) {
    context->Release();
    store->Release();
    document_mgr->Release();
    thread_mgr->Deactivate();
    thread_mgr->Release();
    return 70;
  }

  hr = thread_mgr->SetFocus(document_mgr);
  PrintHr(L"ThreadMgr_SetFocus", hr);
  if (FAILED(hr)) {
    document_mgr->Pop(TF_POPF_ALL);
    context->Release();
    store->Release();
    document_mgr->Release();
    thread_mgr->Deactivate();
    thread_mgr->Release();
    return 80;
  }


  ITfDocumentMgr *focused_document = nullptr;
  const HRESULT get_focus_hr =
      thread_mgr->GetFocus(&focused_document);
  PrintHr(L"ThreadMgr_GetFocus", get_focus_hr);

  const bool focused_document_is_ours =
      get_focus_hr == S_OK &&
      SameComIdentity(focused_document, document_mgr);

  std::wprintf(
      L"focused_document_is_ours=%ls\n",
      focused_document_is_ours ? L"true" : L"false");

  ITfContext *top_context = nullptr;
  const HRESULT get_top_hr =
      document_mgr->GetTop(&top_context);
  PrintHr(L"DocumentMgr_GetTop", get_top_hr);

  const bool top_context_is_ours =
      get_top_hr == S_OK &&
      SameComIdentity(top_context, context);

  std::wprintf(
      L"top_context_is_ours=%ls\n",
      top_context_is_ours ? L"true" : L"false");

  if (top_context != nullptr) {
    top_context->Release();
  }
  if (focused_document != nullptr) {
    focused_document->Release();
  }

  PumpMessages(std::chrono::milliseconds(500));

  const bool open_ok =
      mozc::win32::tsf::TipStatus::SetIMEOpen(thread_mgr, client_id, true);
  const bool mode_ok =
      mozc::win32::tsf::TipStatus::SetInputModeConversion(
          thread_mgr, client_id,
          TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE);

  std::wprintf(L"set_ime_open=%ls\n", open_ok ? L"true" : L"false");
  std::wprintf(L"set_hiragana_mode=%ls\n", mode_ok ? L"true" : L"false");

  PumpMessages(std::chrono::milliseconds(750));

  ITfKeystrokeMgr *keystroke_mgr = nullptr;
  hr = thread_mgr->QueryInterface(IID_PPV_ARGS(&keystroke_mgr));
  PrintHr(L"QueryInterface_ITfKeystrokeMgr", hr);
  if (FAILED(hr) || keystroke_mgr == nullptr) {
    document_mgr->Pop(TF_POPF_ALL);
    context->Release();
    store->Release();
    document_mgr->Release();
    thread_mgr->Deactivate();
    thread_mgr->Release();
    return 90;
  }


  CLSID foreground_clsid = CLSID_NULL;
  const HRESULT foreground_hr =
      keystroke_mgr->GetForeground(&foreground_clsid);
  PrintHr(L"KeystrokeMgr_GetForeground", foreground_hr);

  if (foreground_hr == S_OK) {
    PrintGuid(L"foreground_service_clsid", foreground_clsid);
  }

  const bool foreground_service_is_mozkey =
      foreground_hr == S_OK &&
      ::IsEqualGUID(
          foreground_clsid,
          mozc::win32::TsfProfile::GetTextServiceGuid());

  std::wprintf(
      L"foreground_service_is_mozkey=%ls\n",
      foreground_service_is_mozkey ? L"true" : L"false");

  const bool mozc_tip64arm_loaded =
      ::GetModuleHandleW(L"mozc_tip64arm.dll") != nullptr;
  const bool mozc_tip64_loaded =
      ::GetModuleHandleW(L"mozc_tip64.dll") != nullptr;
  const bool mozc_tip64x_loaded =
      ::GetModuleHandleW(L"mozc_tip64x.dll") != nullptr;

  std::wprintf(
      L"mozc_tip64arm_loaded=%ls\n",
      mozc_tip64arm_loaded ? L"true" : L"false");
  std::wprintf(
      L"mozc_tip64_loaded=%ls\n",
      mozc_tip64_loaded ? L"true" : L"false");
  std::wprintf(
      L"mozc_tip64x_loaded=%ls\n",
      mozc_tip64x_loaded ? L"true" : L"false");

  const bool sink_advised = store->sink_advised();
  std::wprintf(L"text_store_sink_advised=%ls\n",
               sink_advised ? L"true" : L"false");

  const bool arm64_tip_ok =
      mozc_tip64arm_loaded && !mozc_tip64_loaded;

  int result = 0;
  if (!active_profile_is_mozkey) {
    std::wprintf(L"gate_active_profile=FAIL\n");
    result = 100;
  } else {
    std::wprintf(L"gate_active_profile=PASS\n");
  }

  if (!focused_document_is_ours) {
    std::wprintf(L"gate_document_focus=FAIL\n");
    result = 110;
  } else {
    std::wprintf(L"gate_document_focus=PASS\n");
  }

  if (!top_context_is_ours) {
    std::wprintf(L"gate_top_context=FAIL\n");
    result = 120;
  } else {
    std::wprintf(L"gate_top_context=PASS\n");
  }

  if (!open_ok) {
    std::wprintf(L"gate_ime_open=FAIL\n");
    result = 130;
  } else {
    std::wprintf(L"gate_ime_open=PASS\n");
  }

  if (!mode_ok) {
    std::wprintf(L"gate_hiragana_mode=FAIL\n");
    result = 140;
  } else {
    std::wprintf(L"gate_hiragana_mode=PASS\n");
  }

  if (!sink_advised) {
    std::wprintf(L"gate_text_store_sink=FAIL\n");
    result = 150;
  } else {
    std::wprintf(L"gate_text_store_sink=PASS\n");
  }

  if (!foreground_service_is_mozkey) {
    std::wprintf(L"gate_foreground_service=FAIL\n");
    result = 160;
  } else {
    std::wprintf(L"gate_foreground_service=PASS\n");
  }

  if (!arm64_tip_ok) {
    std::wprintf(L"gate_arm64_tip=FAIL\n");
    result = 170;
  } else {
    std::wprintf(L"gate_arm64_tip=PASS\n");
  }

  keystroke_mgr->Release();

  document_mgr->Pop(TF_POPF_ALL);
  context->Release();
  store->Release();
  document_mgr->Release();
  thread_mgr->Deactivate();
  thread_mgr->Release();

  if (result == 0) {
    std::wprintf(L"TSF_APPLICATION_HOST_FOUNDATION=PASS\n");
  }
  return result;
}
