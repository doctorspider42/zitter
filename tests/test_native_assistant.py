"""Offline contracts for the shipped native assistant (no Node or model account)."""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import shutil
import sys
import subprocess
import threading
import tempfile
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('ZITTER_ASSISTANT_TEST_BINARY', ROOT / '.chromium/src/out/ZitterAssistant/zitter_assistant'))


class Model(BaseHTTPRequestHandler):
    requests = []
    lock = threading.Lock()
    attempts = {}
    active = 0
    maximum = 0

    def log_message(self, *_):
        pass

    def do_POST(self):
        value = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        with self.lock:
            self.requests.append(value)
        model = value['model']
        messages = value['messages']
        last = messages[-1]['content']
        if model == 'retry':
            with self.lock:
                attempt = Model.attempts[last] = Model.attempts.get(last, 0) + 1
            if attempt < 3:
                self.send_response(429)
                self.send_header('Retry-After', '.25')
                self.end_headers()
                self.wfile.write(b'{"error":"private diagnostics"}')
                return
        if model == 'concurrency':
            with self.lock:
                Model.active += 1
                Model.maximum = max(Model.maximum, Model.active)
            time.sleep(.3)
            with self.lock:
                Model.active -= 1
        answer = {'answer': 'Integration complete', 'actions': []}
        if model == 'browser' and 'untrusted_tool_results' not in last:
            answer = {'answer': '', 'actions': [{'tool': 'list_tabs', 'args': '{}'}]}
        if model in ['web', 'weburl'] and 'untrusted_tool_results' not in last:
            url = json.loads(last)['prompt'] if model == 'weburl' else 'https://127.0.0.1/'
            answer = {'answer': '', 'actions': [{'tool': 'web_read', 'args': json.dumps({'url': url})}]}
        if model == 'slow':
            time.sleep(1)
        if model == 'malformed':
            answer = {'answer': 'broken', 'actions': [{'tool': 'click', 'args': 'null'}]}
        self.send_response(200)
        self.send_header('Content-Type', 'application/x-ndjson')
        self.end_headers()
        if model == 'invalid-utf8':
            self.wfile.write(b'\xff\n')
            return
        encoded = json.dumps(answer, ensure_ascii=False)
        try:
            for piece in [encoded[:12], encoded[12:]]:
                frame = json.dumps({'message': {'content': piece}}).encode() + b'\n'
                self.wfile.write(frame[:7])
                self.wfile.flush()
                self.wfile.write(frame[7:])
                self.wfile.flush()
                if model == 'stream':
                    time.sleep(.25)
        except (BrokenPipeError, ConnectionResetError):
            pass


class FixtureServer(ThreadingHTTPServer):
    request_queue_size = 64


@unittest.skipUnless(BINARY.is_file(), 'Build //zitter:zitter_assistant first')
class NativeAssistantTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = FixtureServer(('127.0.0.1', 0), Model)
        cls.thread = threading.Thread(target=cls.model.serve_forever, daemon=True)
        cls.thread.start()
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            cls.port = reservation.getsockname()[1]
        cls.token = secrets.token_hex(24)
        env = dict(os.environ, ZITTER_OLLAMA_URL=f'http://127.0.0.1:{cls.model.server_port}')
        cls.provider_home = tempfile.TemporaryDirectory()
        env['CODEX_HOME'] = cls.provider_home.name
        env['OPENAI_API_KEY'] = 'invalid\nfixture-key'
        fixture = BINARY.parent / ('assistant_cli_fixture.exe' if os.name == 'nt' else 'assistant_cli_fixture')
        if fixture.is_file():
            env['ZITTER_CODEX_PATH'] = str(fixture)
            env['ZITTER_CLAUDE_PATH'] = str(fixture)
        cls.fixture = fixture
        env.pop('ZITTER_NODE_PATH', None)
        env['PATH'] = '/usr/bin:/bin'
        cls.process = subprocess.Popen([str(BINARY), f'--port={cls.port}', f'--token={cls.token}', f'--parent={os.getpid()}'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError(cls.process.stderr.read().decode())
            try:
                if cls.rpc('/health', method='GET')[0] == 200:
                    return
            except OSError:
                pass
            time.sleep(.05)
        raise RuntimeError('Native helper did not become ready')

    @classmethod
    def tearDownClass(cls):
        if cls.process.poll() is not None:
            print(cls.process.stderr.read().decode())
        else:
            cls.process.terminate()
        cls.process.wait(timeout=5)
        cls.process.stderr.close()
        cls.model.shutdown()
        cls.model.server_close()
        cls.thread.join(timeout=2)
        cls.provider_home.cleanup()

    def test_connection_check_isolated_and_reports_failure(self):
        env = dict(os.environ, ZITTER_OLLAMA_URL=f'http://127.0.0.1:{self.model.server_port}')
        result = subprocess.run([str(BINARY), '--check-provider=ollama', '--check-model=probe'], env=env, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(json.loads(result.stdout), {'ok': True})
        with Model.lock:
            request = Model.requests[-1]
        self.assertIn('Connection test.', json.dumps(request))
        self.assertNotIn('browsing_history', json.dumps(request))
        env['ZITTER_OLLAMA_URL'] = 'http://127.0.0.1:1'
        failed = subprocess.run([str(BINARY), '--check-provider=ollama', '--check-model=probe'], env=env, capture_output=True, timeout=30)
        self.assertEqual(failed.returncode, 1)
        self.assertEqual(json.loads(failed.stdout), {'ok': False, 'error': 'Network request failed'})

    @classmethod
    def rpc(cls, path, value=None, method='POST', headers=None, raw=None, port=None):
        connection = http.client.HTTPConnection('127.0.0.1', port or cls.port, timeout=15)
        try:
            body = raw if raw is not None else json.dumps(value or {})
            connection.request(method, path, body, headers if headers is not None else {'Authorization': f'Bearer {cls.token}'})
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def request(self, model='plain', **extra):
        return {'id': secrets.token_hex(8), 'config': {'provider': 'ollama', 'model': model},
                'permissions': {'tabs': True, 'pages': False, 'actions': False, 'history': False, 'web': False},
                'prompt': 'Help me', 'context': {}, 'date': {'unix_ms': 1793872800000, 'utc_offset_minutes': 60}, **extra}

    def test_health_auth_and_origin(self):
        self.assertEqual(self.rpc('/health', method='GET', headers={})[0], 403)
        self.assertEqual(self.rpc('/health', method='GET', headers={'Authorization': 'Bearer bad'})[0], 403)
        self.assertEqual(self.rpc('/health', method='GET', headers={'Authorization': f'Bearer {self.token}', 'Origin': 'https://evil.test'})[0], 403)
        self.assertEqual(self.rpc('/health', method='GET')[1], {'ready': True})

    def test_turn_and_native_tool_continuation(self):
        request = self.request('browser')
        first = self.rpc('/turn', request)[1]
        self.assertEqual(first['actions'], [{'tool': 'list_tabs', 'args': {}}])
        final = self.rpc('/continue', {'id': request['id'], 'results': [{'tool': 'list_tabs', 'result': [{'title': 'Example'}]}]})[1]
        self.assertEqual(final['answer'], 'Integration complete')
        self.assertEqual(final['actions'], [])

    def test_changed_permissions_and_model_require_new_session(self):
        request = self.request()
        self.assertNotIn('error', self.rpc('/turn', request)[1])
        request['permissions']['history'] = True
        self.assertIn('new session', self.rpc('/turn', request)[1]['error'])
        request['permissions']['history'] = False
        request['config']['model'] = 'changed'
        self.assertIn('new session', self.rpc('/turn', request)[1]['error'])

    def test_history_is_restored_without_images_or_tool_results(self):
        request = self.request(history=[{'role': 'user', 'text': 'Solaris?'}, {'role': 'assistant', 'text': 'Lem.'}])
        self.assertNotIn('error', self.rpc('/turn', request)[1])
        with Model.lock:
            messages = Model.requests[-1]['messages']
        self.assertIn('Solaris?', messages[1]['content'])
        self.assertIn('Lem.', messages[2]['content'])

    def test_invalid_model_ids_never_reach_a_provider(self):
        for model in ['', '--flag', '-p', 'model\n--flag']:
            self.assertIn('error', self.rpc('/turn', self.request(model))[1])

    def test_invalid_api_key_is_rejected_without_network_or_secret_echo(self):
        request = self.request()
        request['config']['provider'] = 'openai'
        result = self.rpc('/turn', request)[1]
        self.assertIn('API key', result['error'])
        self.assertNotIn('fixture-key', result['error'])
        self.assertEqual(self.rpc('/health', method='GET')[1], {'ready': True})

    def test_invalid_provider_utf8_is_sanitized(self):
        self.assertIn('error', self.rpc('/turn', self.request('invalid-utf8'))[1])
        self.assertEqual(self.rpc('/health', method='GET')[1], {'ready': True})

    def test_invalid_cli_utf8_does_not_crash_helper(self):
        request = self.request('invalid-utf8')
        request['config']['provider'] = 'codex'
        self.assertIn('error', self.rpc('/turn', request)[1])
        self.assertEqual(self.rpc('/health', method='GET')[1], {'ready': True})

    def test_malformed_tool_arguments_are_rejected(self):
        self.assertIn('error', self.rpc('/turn', self.request('malformed'))[1])

    def test_disabled_web_does_not_issue_network_reads(self):
        self.assertEqual(self.rpc('/turn', self.request('web'))[1]['answer'], 'Integration complete')
        with Model.lock:
            self.assertIn('Web permission is disabled', Model.requests[-1]['messages'][-1]['content'])

    def test_research_blocks_private_https_addresses(self):
        request = self.request('web')
        request['permissions']['web'] = True
        self.assertEqual(self.rpc('/turn', request)[1]['answer'], 'Integration complete')
        with Model.lock:
            self.assertIn('Private network', Model.requests[-1]['messages'][-1]['content'])

    def test_research_mode_requires_web_permission(self):
        self.assertIn('Enable web research', self.rpc('/turn', self.request(research=True))[1]['error'])

    def test_cancel_and_duplicate_turn(self):
        request = self.request('slow')
        with concurrent.futures.ThreadPoolExecutor() as executor:
            pending = executor.submit(self.rpc, '/turn', request)
            time.sleep(.15)
            self.assertIn('already running', self.rpc('/turn', request)[1]['error'])
            self.assertEqual(self.rpc('/cancel', {'id': request['id']})[1], {})
            self.assertIn('error', pending.result()[1])

    def test_progress_streams_before_final_answer(self):
        request = self.request('stream')
        seen = []
        with concurrent.futures.ThreadPoolExecutor() as executor:
            pending = executor.submit(self.rpc, '/turn', request)
            while not pending.done():
                seen.append(self.rpc('/progress', {'id': request['id']})[1]['partial'])
                time.sleep(.03)
            self.assertEqual(pending.result()[1]['answer'], 'Integration complete')
        self.assertTrue(any(seen), seen)

    def test_deleted_session_cannot_continue(self):
        request = self.request()
        self.rpc('/turn', request)
        self.rpc('/delete', {'id': request['id']})
        for _ in range(20):
            result = self.rpc('/continue', {'id': request['id'], 'results': []})[1]
            if 'error' in result:
                break
            time.sleep(.01)
        self.assertIn('expired', result['error'])

    def test_public_reader_rejects_unsafe_url_classes(self):
        for url in ['http://example.com', 'https://user:secret@example.com', 'https://example.com:444', 'https://żółw.example:444', 'https://10.0.0.1', 'https://[::1]', 'https://[::ffff:127.0.0.1]', 'https://[fc00::1]']:
            request = self.request('weburl', prompt=url)
            request['permissions']['web'] = True
            self.assertEqual(self.rpc('/turn', request)[1].get('answer'), 'Integration complete', url)
            with Model.lock:
                self.assertIn('error', Model.requests[-1]['messages'][-1]['content'], url)

    def test_retryable_provider_status_uses_bounded_retries(self):
        request = self.request('retry', prompt=secrets.token_hex(8))
        self.assertEqual(self.rpc('/turn', request)[1]['answer'], 'Integration complete')
        with Model.lock:
            content = Model.requests[-1]['messages'][-1]['content']
            self.assertEqual(Model.attempts[content], 3)

    def test_global_model_concurrency_is_bounded(self):
        requests = [self.request('concurrency', prompt=secrets.token_hex(8)) for _ in range(18)]
        with Model.lock:
            Model.maximum = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=18) as executor:
            turns = [executor.submit(self.rpc, '/turn', request) for request in requests]
            for turn in turns:
                result = turn.result()[1]
                self.assertEqual(result.get('answer'), 'Integration complete', result)
        with Model.lock:
            self.assertGreater(Model.maximum, 1)
            self.assertLessEqual(Model.maximum, 12)

    def test_exact_inflight_model_requests_are_shared(self):
        prompt = secrets.token_hex(8)
        first = self.request('slow', prompt=prompt)
        second = self.request('slow', prompt=prompt)
        with Model.lock:
            before = len(Model.requests)
        with concurrent.futures.ThreadPoolExecutor() as executor:
            turns = [executor.submit(self.rpc, '/turn', request) for request in [first, second]]
            for turn in turns:
                self.assertEqual(turn.result()[1]['answer'], 'Integration complete')
        with Model.lock:
            self.assertEqual(len(Model.requests) - before, 1)

    def test_canceling_cache_owner_keeps_other_session_usable(self):
        prompt = secrets.token_hex(8)
        first, second = self.request('slow', prompt=prompt), self.request('slow', prompt=prompt)
        with concurrent.futures.ThreadPoolExecutor() as executor:
            owner = executor.submit(self.rpc, '/turn', first)
            time.sleep(.15)
            follower = executor.submit(self.rpc, '/turn', second)
            time.sleep(.15)
            self.rpc('/cancel', {'id': first['id']})
            self.assertIn('error', owner.result()[1])
            self.assertEqual(follower.result()[1].get('answer'), 'Integration complete')

    def test_only_newest_screenshots_remain_in_context(self):
        request = self.request('browser', prompt=secrets.token_hex(8))
        self.rpc('/turn', request)
        for image in ['b2xk', 'bmV3']:
            self.rpc('/continue', {'id': request['id'], 'results': [{'tool': 'screenshot', 'result': {'image': image}}]})
        with Model.lock:
            messages = Model.requests[-1]['messages']
        self.assertEqual([m['images'] for m in messages if 'images' in m], [['bmV3']])
        self.assertNotIn('bmV3', messages[-1]['content'])

    def test_native_cli_backends_without_node(self):
        self.assertTrue(self.fixture.is_file(), 'Build //zitter:assistant_cli_fixture')
        for provider in ['codex', 'claude']:
            request = self.request()
            request['config']['provider'] = provider
            self.assertEqual(self.rpc('/turn', request)[1].get('answer'), 'CLI integration complete')

    @unittest.skipUnless(sys.platform == 'darwin', 'macOS CLI locations')
    def test_native_cli_discovery_without_shell_path(self):
        self.assertTrue(self.fixture.is_file(), 'Build //zitter:assistant_cli_fixture')
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            # Chromium resolves DIR_HOME through Foundation on macOS.
            env = dict(HOME=str(home), CFFIXED_USER_HOME=str(home),
                       CODEX_HOME=str(home / '.codex'), PATH='/usr/bin:/bin:/usr/sbin:/sbin')
            for provider, location in [
                ('claude', '.local/bin/claude'),
                ('codex', 'Applications/ChatGPT.app/Contents/Resources/codex-cli/'
                          'CodexCLI.app/Contents/MacOS/codex'),
            ]:
                with self.subTest(provider=provider):
                    executable = home / location
                    executable.parent.mkdir(parents=True)
                    executable.symlink_to(self.fixture.resolve())
                    result = subprocess.run(
                        [str(BINARY), f'--check-provider={provider}', '--check-model=probe'],
                        env=env, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout.decode())
                    self.assertEqual(json.loads(result.stdout), {'ok': True})

    def test_native_cli_screenshots_and_cleanup(self):
        for provider in ['codex', 'claude']:
            request = self.request(prompt='Native CLI image test')
            request['config']['provider'] = provider
            self.rpc('/turn', request)
            result = self.rpc('/continue', {'id': request['id'], 'results': [{'tool': 'screenshot', 'result': {'image': 'aW1hZ2U=', 'width': 10, 'height': 10}}]})[1]
            self.assertEqual(result.get('answer'), 'CLI integration complete', result)

    def test_stop_terminates_native_cli(self):
        request = self.request('slow-cli')
        request['config']['provider'] = 'codex'
        with concurrent.futures.ThreadPoolExecutor() as executor:
            pending = executor.submit(self.rpc, '/turn', request)
            time.sleep(.2)
            self.rpc('/cancel', {'id': request['id']})
            self.assertIn('error', pending.result(timeout=3)[1])

    @unittest.skipUnless(sys.platform == 'darwin', 'macOS bundle resource layout')
    def test_packaged_helper_finds_framework_icu_data(self):
        with tempfile.TemporaryDirectory() as directory:
            contents = Path(directory) / 'Zitter.app/Contents'
            macos = contents / 'MacOS'
            macos.mkdir(parents=True)
            helper = macos / 'zitter_assistant'
            shutil.copy2(BINARY, helper)
            for library in BINARY.parent.glob('*.dylib'):
                (macos / library.name).symlink_to(library)
            resources = contents / 'Frameworks/Chromium Framework.framework/Resources'
            resources.mkdir(parents=True)
            (resources / 'icudtl.dat').symlink_to(BINARY.parent / 'icudtl.dat')
            with socket.socket() as reservation:
                reservation.bind(('127.0.0.1', 0))
                port = reservation.getsockname()[1]
            env = dict(os.environ, ZITTER_OLLAMA_URL=f'http://127.0.0.1:{self.model.server_port}')
            process = subprocess.Popen([str(helper), f'--port={port}', f'--token={self.token}', f'--parent={os.getpid()}'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                for _ in range(100):
                    if process.poll() is not None:
                        self.fail(process.stderr.read().decode())
                    try:
                        self.rpc('/health', method='GET', port=port)
                        break
                    except OSError:
                        time.sleep(.03)
                request = self.request('weburl', prompt='https://żółw.example:444')
                request['permissions']['web'] = True
                self.assertEqual(self.rpc('/turn', request, port=port)[1].get('answer'), 'Integration complete')
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=5)
                process.stderr.close()

    def test_bad_rpc_requests(self):
        self.assertEqual(self.rpc('/turn', raw='{')[0], 400)
        self.assertEqual(self.rpc('/turn', raw=b'\xff')[0], 400)
        self.assertEqual(self.rpc('/turn', {'id': 'x' * 101})[0], 400)
        self.assertEqual(self.rpc('/unknown', {'id': 'valid'})[0], 404)
        self.assertEqual(self.rpc('/turn', method='GET')[0], 405)


if __name__ == '__main__':
    unittest.main()
