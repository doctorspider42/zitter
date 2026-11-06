// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include <csignal>
#include <cstdlib>
#include <map>
#include <set>
#include "base/at_exit.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chromium/assistant/native_transport.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#if BUILDFLAG(IS_POSIX)
#include <unistd.h>
#endif
#include "net/base/ip_endpoint.h"
#include "net/log/net_log.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
namespace zitter::assistant {
namespace {
constexpr auto kRpcTraffic =
    net::DefineNetworkTrafficAnnotation("zitter_native_assistant_rpc", R"(
  semantics { sender: "Zitter native assistant helper"
    description: "Returns answers, progress and tool requests to the browser over authenticated loopback RPC."
    trigger: "The user sends a message or runs a configured assistant task."
    data: "Assistant answers, session state and browser tool requests." destination: LOCAL }
  policy { cookies_allowed: NO setting: "The optional AI assistant can be disabled in Settings."
    policy_exception_justification: "Not implemented." })");
}

rust::String runtime_request(rust::Str path, rust::Str body);
bool BeginOperation(std::string_view id);
void EndOperation(std::string_view id);
void CancelOperation(std::string_view id);
void CancelAllOperations();
std::string Progress(std::string_view id);
class Server : public net::HttpServer::Delegate {
 public:
  Server(std::unique_ptr<net::ServerSocket> socket, std::string token)
      : server_(std::move(socket), this), token_("Bearer " + token) {}
  void OnConnect(int connection) override {
    server_.SetReceiveBufferSize(connection, 17 * 1024 * 1024);
    server_.SetSendBufferSize(connection, 17 * 1024 * 1024);
  }
  void OnWebSocketRequest(int connection,
                          const net::HttpServerRequestInfo&) override {
    server_.Close(connection);
  }
  void OnWebSocketMessage(int connection, std::string) override {
    server_.Close(connection);
  }
  void OnClose(int connection) override {
    auto it = connections_.find(connection);
    if (it != connections_.end()) {
      CancelOperation(it->second);
      connections_.erase(it);
    }
  }
  void Send(int connection,
            std::string body,
            net::HttpStatusCode status = net::HTTP_OK) {
    net::HttpServerResponseInfo reply(status);
    reply.AddHeader("Cache-Control", "no-store");
    reply.SetBody(body, "application/json");
    server_.SendResponse(connection, reply, kRpcTraffic);
  }
  void OnHttpRequest(int connection,
                     const net::HttpServerRequestInfo& info) override {
    const auto supplied = info.headers.find("authorization");
    // Compare every byte without an early exit for matching prefixes.
    unsigned mismatch = supplied == info.headers.end()
                            ? 1
                            : supplied->second.size() ^ token_.size();
    if (supplied != info.headers.end())
      for (size_t i = 0; i < token_.size(); ++i)
        mismatch |=
            token_[i] ^ (i < supplied->second.size() ? supplied->second[i] : 0);
    if (info.headers.contains("origin") || mismatch) {
      Send(connection, "{\"error\":\"Forbidden\"}", net::HTTP_FORBIDDEN);
      return;
    }
    if (info.method == "GET" && info.path == "/health") {
      Send(connection, "{\"ready\":true}");
      return;
    }
    if (info.method != "POST") {
      Send(connection, "{}", net::HTTP_METHOD_NOT_ALLOWED);
      return;
    }
    if (info.data.size() > 16 * 1024 * 1024) {
      Send(connection, "{\"error\":\"Request too large\"}",
           net::HTTP_REQUEST_ENTITY_TOO_LARGE);
      return;
    }
    if (!base::IsStringUTF8(info.data)) {
      Send(connection, "{\"error\":\"Invalid UTF-8 request\"}",
           net::HTTP_BAD_REQUEST);
      return;
    }
    const auto value =
        base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
    const auto* id_ptr = value ? value->FindString("id") : nullptr;
    if (!id_ptr || id_ptr->empty() || id_ptr->size() > 100) {
      Send(connection, "{\"error\":\"Invalid session\"}",
           net::HTTP_BAD_REQUEST);
      return;
    }
    const auto id = *id_ptr;
    if (info.path == "/progress") {
      Send(connection,
           base::WriteJson(base::DictValue().Set("partial", Progress(id)))
               .value());
      return;
    }
    if (info.path == "/cancel" || info.path == "/delete") {
      CancelOperation(id);
      if (info.path == "/delete")
        runtime_request("/delete", info.data);
      Send(connection, "{}");
      return;
    }
    if (info.path != "/turn" && info.path != "/continue") {
      Send(connection, "{}", net::HTTP_NOT_FOUND);
      return;
    }
    if (connections_.contains(connection) || !BeginOperation(id)) {
      Send(connection, "{\"error\":\"Session is already running\"}",
           net::HTTP_BAD_REQUEST);
      return;
    }
    connections_[connection] = id;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives()},
        base::BindOnce(
            [](std::string path, std::string body) {
              return std::string(runtime_request(path, body));
            },
            info.path, info.data),
        base::BindOnce(&Server::Finished, weak_.GetWeakPtr(), connection, id));
  }
  void Finished(int connection, std::string id, std::string body) {
    EndOperation(id);
    auto it = connections_.find(connection);
    if (it == connections_.end() || it->second != id)
      return;
    connections_.erase(it);
    Send(connection, std::move(body));
  }

 private:
  net::HttpServer server_;
  std::string token_;
  std::map<int, std::string> connections_;
  base::WeakPtrFactory<Server> weak_{this};
};
}  // namespace zitter::assistant
namespace {
volatile std::sig_atomic_t stopping = 0;
void StopSignal(int) {
  stopping = 1;
}
bool InitializeAssistantICU() {
#if ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE
  base::FilePath executable;
  if (!base::PathService::Get(base::DIR_EXE, &executable))
    return false;
  auto data = executable.AppendASCII("icudtl.dat");
#if BUILDFLAG(IS_MAC)
  if (!base::PathExists(data) && executable.BaseName().value() == "MacOS" &&
      executable.DirName().BaseName().value() == "Contents") {
    data = executable.DirName()
               .AppendASCII("Frameworks")
               .AppendASCII("Chromium Framework.framework")
               .AppendASCII("Resources")
               .AppendASCII("icudtl.dat");
  }
#endif
  base::File file(data, base::File::FLAG_OPEN | base::File::FLAG_READ |
                            base::File::FLAG_WIN_SHARE_DELETE);
  if (!file.IsValid())
    return false;
  return base::i18n::InitializeICUWithFileDescriptor(
      file.TakePlatformFile(), base::MemoryMappedFile::Region::kWholeFile);
#else
  return base::i18n::InitializeICU();
#endif
}
bool ParentAlive(int pid) {
#if BUILDFLAG(IS_POSIX)
  return getppid() == pid;
#else
  auto process = base::Process::Open(pid);
  int code = 0;
  return process.IsValid() &&
         base::GetTerminationStatus(process.Handle(), &code) ==
             base::TERMINATION_STATUS_STILL_RUNNING;
#endif
}
}  // namespace
int main(int argc, const char** argv) {
  base::AtExitManager exit;
  base::CommandLine::Init(argc, argv);
  const auto* command = base::CommandLine::ForCurrentProcess();
  int port = 0, pid = 0;
  const auto token = command->GetSwitchValueASCII("token");
  if (!base::StringToInt(command->GetSwitchValueASCII("port"), &port) ||
      port < 1024 || port > 65535 || token.size() != 48 ||
      !base::StringToInt(command->GetSwitchValueASCII("parent"), &pid) ||
      pid <= 0)
    return 2;
  for (char c : token)
    if (!base::IsHexDigit(c))
      return 2;
  if (!InitializeAssistantICU())
    return 3;
  base::SingleThreadTaskExecutor tasks(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("ZitterAssistant");
  auto socket = std::make_unique<net::TCPServerSocket>(net::NetLog::Get(),
                                                       net::NetLogSource());
  if (socket->Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), port), 16,
                     std::nullopt) != net::OK)
    return 1;
  zitter::assistant::Server server(std::move(socket), token);
  base::RunLoop loop;
  std::signal(SIGTERM, StopSignal);
  std::signal(SIGINT, StopSignal);
  base::RepeatingTimer parent;
  parent.Start(FROM_HERE, base::Milliseconds(100),
               base::BindRepeating(
                   [](base::RepeatingTimer* timer, int pid) {
                     if (stopping || !ParentAlive(pid)) {
                       zitter::assistant::CancelAllOperations();
                       timer->Stop();
                       base::SingleThreadTaskRunner::GetCurrentDefault()
                           ->PostDelayedTask(
                               FROM_HERE, base::BindOnce([] { std::_Exit(0); }),
                               base::Seconds(1));
                     }
                   },
                   base::Unretained(&parent), pid));
  loop.Run();
  return 0;
}
