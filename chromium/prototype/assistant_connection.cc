// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/assistant_connection.h"

#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"

#if BUILDFLAG(IS_POSIX)
#include <unistd.h>
#endif

namespace zitter::prototype {

std::u16string AssistantConnectionError(std::string_view message) {
  if (message.starts_with("Install native Codex"))
    return T(u"Zainstaluj Codex i zaloguj się na swoje konto.");
  if (message.starts_with("Install native Claude"))
    return T(u"Zainstaluj Claude Code i zaloguj się na swoje konto.");
  if (message.starts_with("Sign in to the provider CLI"))
    return T(u"Zaloguj się w terminalu i ponów test. Wskazówki znajdziesz w sekcji „Pomoc i konfiguracja”.");
  if (message.starts_with("The provider CLI does not support"))
    return T(u"Zaktualizuj CLI dostawcy i ponów test.");
  if (message.starts_with("The service did not find this model"))
    return T(u"Sprawdź ID modelu i dostęp do niego na swoim koncie.");
  if (message.starts_with("OpenAI rejected the API key"))
    return T(u"OpenAI odrzucił klucz API. Sprawdź klucz w ustawieniach.");
  if (message.starts_with("The connection test timed out"))
    return T(u"Test nie dostał odpowiedzi na czas. Sprawdź połączenie i spróbuj ponownie.");
  if (message.starts_with("Could not list models"))
    return T(u"Uruchom Ollama i sprawdź adres lokalnego serwera albo dodaj model ręcznie.");
  return message.empty() ? T(u"Sprawdź konfigurację i spróbuj ponownie.") : base::UTF8ToUTF16(message);
}
base::DictValue CheckAssistantProvider(std::string provider, std::string model, bool discover,
                                     base::EnvironmentMap environment, std::string api_key) {
  auto error = [](std::string_view message) { return base::DictValue().Set("error", message); };
  base::FilePath exe;
  base::PathService::Get(base::DIR_EXE, &exe);
#if BUILDFLAG(IS_WIN)
  base::CommandLine command(exe.AppendASCII("zitter_assistant.exe"));
#else
  base::CommandLine command(exe.AppendASCII("zitter_assistant"));
#endif
  command.AppendSwitchASCII("check-provider", provider);
  command.AppendSwitchASCII("check-model", model);
  if (discover) command.AppendSwitch("list-models");
  base::ScopedTempDir temp;
  if (!temp.CreateUniqueTempDir()) return error("Could not prepare the connection test.");
  const auto output_path = temp.GetPath().AppendASCII("result");
  base::File output(output_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  base::LaunchOptions options;
  options.environment = std::move(environment);
#if BUILDFLAG(IS_WIN)
  base::File input(temp.GetPath().AppendASCII("stdin"),
                   base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ | base::File::FLAG_WRITE);
  base::File diagnostics(temp.GetPath().AppendASCII("stderr"),
                         base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!input.IsValid()) return error("Could not prepare the connection test.");
  options.start_hidden = true;
  options.stdin_handle = input.GetPlatformFile();
  options.stdout_handle = output.GetPlatformFile();
  options.stderr_handle = diagnostics.GetPlatformFile();
  options.handles_to_inherit = {input.GetPlatformFile(), output.GetPlatformFile(), diagnostics.GetPlatformFile()};
  if (!api_key.empty()) options.environment[L"ZITTER_OPENAI_API_KEY"] = base::UTF8ToWide(api_key);
#else
  base::File diagnostics(base::FilePath("/dev/null"), base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  options.fds_to_remap = {{output.GetPlatformFile(), STDOUT_FILENO}, {diagnostics.GetPlatformFile(), STDERR_FILENO}};
  if (!api_key.empty()) options.environment["ZITTER_OPENAI_API_KEY"] = api_key;
#endif
  if (!output.IsValid() || !diagnostics.IsValid()) return error("Could not prepare the connection test.");
  auto process = base::LaunchProcess(command, options);
  if (!process.IsValid()) return error("Could not start the Zitter assistant helper. Check the installation.");
  int code = 1;
  if (!process.WaitForExitWithTimeout(base::Seconds(60), &code)) {
    process.Terminate(1, true);
    return error("The connection test timed out. Check the connection and try again.");
  }
  std::string result;
  if (base::ReadFileToStringWithMaxSize(output_path, &result, 8192)) {
    auto parsed = base::JSONReader::ReadDict(result, base::JSON_PARSE_RFC);
    if (parsed) return std::move(*parsed);
  }
  return error("The Zitter assistant helper did not return a result. Check the installation.");
}

}  // namespace zitter::prototype
