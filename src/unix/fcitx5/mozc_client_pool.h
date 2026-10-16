// Copyright 2023-2023, Weng Xuetian <wengxt@gmail.com>
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

#ifndef UNIX_FCITX5_MOZC_CLIENT_POOL_H_
#define UNIX_FCITX5_MOZC_CLIENT_POOL_H_

#include <fcitx-utils/trackableobject.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "unix/fcitx5/mozc_client_interface.h"

namespace fcitx {

class MozcClientPool : public TrackableObject<MozcClientPool> {
  friend class MozcClientInterface;

 public:
  MozcClientPool(PropertyPropagatePolicy initialPolicy);

  void setPolicy(PropertyPropagatePolicy policy);
  PropertyPropagatePolicy policy() const { return policy_; }

  std::shared_ptr<MozcClientInterface> requestClient(InputContext* ic);

 private:
  std::shared_ptr<MozcClientInterface> registerClient(
      const std::string& key, std::unique_ptr<MozcClientInterface> client);
  void unregisterClient(const std::string& key);
  PropertyPropagatePolicy policy_;
  std::unordered_map<std::string, std::weak_ptr<MozcClientInterface>> clients_;
};

}  // namespace fcitx

#endif
