// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "cookie_encryption_win.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "components/os_crypt/async/browser/dpapi_key_provider.h"
#include "components/os_crypt/async/browser/os_crypt_win.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/in_memory_pref_store.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "cookie_store_verifier.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace zitter::content_embedder {
namespace {

// Use the preference layout understood by Chromium's DPAPIKeyProvider.
constexpr char kEncryptedKey[] = "os_crypt.encrypted_key";
constexpr int64_t kMaxLocalStateBytes = 64 * 1024;

std::vector<std::pair<os_crypt_async::OSCryptAsync::Precedence,
                      std::unique_ptr<os_crypt_async::KeyProvider>>>
MakeProviders(PrefService* local_state) {
  std::vector<std::pair<os_crypt_async::OSCryptAsync::Precedence,
                        std::unique_ptr<os_crypt_async::KeyProvider>>> providers;
  providers.emplace_back(
      1, std::make_unique<os_crypt_async::DPAPIKeyProvider>(local_state));
  return providers;
}

}  // namespace

// static
std::unique_ptr<CookieEncryptionWin> CookieEncryptionWin::Open(
    const base::FilePath& local_state_path,
    bool can_create_key,
    const std::vector<base::FilePath>& network_paths) {
  if (base::IsLink(local_state_path))
    return nullptr;

  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  os_crypt_async::RegisterLocalPrefs(registry.get());
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<InMemoryPrefStore>());
  auto local_state = factory.Create(registry);
  base::File state_file(local_state_path,
                        base::File::FLAG_OPEN | base::File::FLAG_READ);
  const bool existing = state_file.IsValid();
  if (existing) {
    const auto length = state_file.GetLength();
    if (length <= 0 || length > kMaxLocalStateBytes)
      return nullptr;
    std::string json(static_cast<size_t>(length), '\0');
    if (!state_file.ReadAndCheck(0, base::as_writable_byte_span(json)))
      return nullptr;
    auto values = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    const auto* key = values ? values->FindStringByDottedPath(kEncryptedKey)
                             : nullptr;
    if (!key || key->empty())
      return nullptr;
    local_state->SetString(kEncryptedKey, *key);
  } else if (state_file.error_details() != base::File::FILE_ERROR_NOT_FOUND ||
             !can_create_key || !os_crypt_async::Init(local_state.get())) {
    return nullptr;
  }

  auto encryption = std::unique_ptr<CookieEncryptionWin>(
      new CookieEncryptionWin(std::move(local_state)));
  // The pinned DPAPI provider calls back synchronously. If that changes,
  // reject startup until readiness can be established before opening a store.
  using Result = base::RefCountedData<scoped_refptr<os_crypt_async::Encryptor>>;
  auto result = base::MakeRefCounted<Result>();
  encryption->os_crypt_.GetInstance(base::BindOnce(
      [](scoped_refptr<Result> result,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        result->data = std::move(encryptor);
      },
      result));
  if (!result->data || !result->data->IsEncryptionAvailable())
    return nullptr;
  if (!CheckCookieStores(network_paths, *result->data)) {
    LOG(ERROR) << "The Content workspace could not verify its cookie stores";
    return nullptr;
  }
  std::vector<base::FilePath> profile_paths;
  profile_paths.reserve(network_paths.size());
  for (const auto& network_path : network_paths)
    profile_paths.push_back(network_path.DirName());
  if (!CheckLoginStores(profile_paths, *result->data)) {
    LOG(ERROR) << "The Content workspace could not verify its login stores";
    return nullptr;
  }

  if (!existing) {
    auto json = base::WriteJson(
        encryption->local_state_->GetPreferenceValues(
            PrefService::EXCLUDE_DEFAULTS));
    if (!json || !base::ImportantFileWriter::WriteFileAtomically(
                     local_state_path, *json)) {
      return nullptr;
    }
  }
  return encryption;
}

CookieEncryptionWin::CookieEncryptionWin(
    std::unique_ptr<PrefService> local_state)
    : local_state_(std::move(local_state)),
      os_crypt_(MakeProviders(local_state_.get())),
      provider_(&os_crypt_) {}

CookieEncryptionWin::~CookieEncryptionWin() = default;

void CookieEncryptionWin::Configure(
    network::mojom::NetworkContextParams& params) {
  params.enable_encrypted_cookies = true;
  params.cookie_encryption_provider = provider_.BindNewRemote();
}

}  // namespace zitter::content_embedder
