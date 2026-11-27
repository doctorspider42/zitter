// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_ASSISTANT_CODEX_SETUP_H_
#define ZITTER_CHROMIUM_ASSISTANT_CODEX_SETUP_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/process/process.h"
#include "base/synchronization/lock.h"
#include "build/build_config.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_handle.h"
#endif

namespace zitter::assistant {

// Checks a downloaded Codex .zst against the pinned size and SHA-256 digest,
// decompresses it and puts the executable at ManagedCodexPath(). Removes the
// executables of older managed versions. Blocking: call on a MayBlock thread.
bool InstallManagedCodex(const base::FilePath& archive);

// True when `url` is an OpenAI sign-in page Codex may ask Zitter to open.
bool IsCodexSignInUrl(const GURL& url);

struct CodexSignInResult {
  bool ok = false;
  // Codex already had a saved login; no browser page was opened.
  bool already_signed_in = false;
  // The ChatGPT plan type Codex reports, for example "plus", when known.
  std::string plan;
  std::string error;
};

// Signs Codex in with a ChatGPT account through `codex app-server`: Codex
// gives a sign-in URL, the browser opens it, and Codex saves its login in its
// own home (CODEX_HOME or ~/.codex) when the user finishes. Run() blocks; call
// it on a MayBlock thread. Cancel() may be called from any thread.
class CodexSignIn : public base::RefCountedThreadSafe<CodexSignIn> {
 public:
  CodexSignIn();
  CodexSignIn(const CodexSignIn&) = delete;
  CodexSignIn& operator=(const CodexSignIn&) = delete;

  // `open_url` runs on the calling thread with the validated sign-in URL.
  CodexSignInResult Run(const base::FilePath& codex,
                        base::OnceCallback<void(GURL)> open_url);
  void Cancel();

 private:
  friend class base::RefCountedThreadSafe<CodexSignIn>;
  ~CodexSignIn();

  base::Lock lock_;
  bool cancelled_ = false;
  base::Process process_;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle job_;
#endif
};

}  // namespace zitter::assistant

#endif  // ZITTER_CHROMIUM_ASSISTANT_CODEX_SETUP_H_
