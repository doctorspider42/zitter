"""Exercise the packaged Assistant against a loopback-only model fixture."""

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import subprocess
from threading import Thread


def verify_assistant_helper(executable, working_directory, environment):
    model = "zitter-package-probe"
    requests = []
    isolated_environment = dict(environment)
    for name in ("OPENAI_API_KEY", "ZITTER_OPENAI_API_KEY"):
        isolated_environment.pop(name, None)

    class Provider(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            size = int(self.headers.get("Content-Length", 0))
            if self.path != "/api/chat" or not 0 < size <= 256 * 1024:
                self.send_error(400)
                return
            request = json.loads(self.rfile.read(size))
            requests.append(request)
            if request.get("model") != model or "Authorization" in self.headers:
                self.send_error(400)
                return
            answer = json.dumps({"answer": "Packaged helper ready", "actions": []})
            body = (json.dumps({"message": {"content": answer}, "done": True}) + "\n").encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with ThreadingHTTPServer(("127.0.0.1", 0), Provider) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run(
                [executable, "--check-provider=ollama", f"--check-model={model}"],
                cwd=working_directory,
                env=dict(isolated_environment,
                         ZITTER_OLLAMA_URL=f"http://127.0.0.1:{server.server_port}"),
                capture_output=True, text=True, timeout=30,
            )
        finally:
            server.shutdown()
            thread.join()
    if result.returncode or result.stdout.strip() != '{"ok":true}' or len(requests) != 1:
        raise ValueError(f"Packaged Assistant provider check failed: {result.stdout}{result.stderr}")

    for provider, check_model, expected in (
        ("openai", "gpt-5", "Add your OpenAI API key in Settings → AI assistant."),
        ("ollama", model, "Ollama must use a local HTTP endpoint."),
    ):
        rejected = subprocess.run(
            [executable, f"--check-provider={provider}", f"--check-model={check_model}"],
            cwd=working_directory,
            env=dict(isolated_environment, ZITTER_OLLAMA_URL="http://example.invalid:11434"),
            capture_output=True, text=True, timeout=30,
        )
        if rejected.returncode != 1 or json.loads(rejected.stdout) != {
            "ok": False, "error": expected,
        }:
            raise ValueError(f"Packaged Assistant did not reject the invalid {provider} configuration")
    print("Packaged Assistant answered through Ollama and rejected invalid provider configurations")
