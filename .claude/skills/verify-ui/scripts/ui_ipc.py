"""JSON-line client for the Zitter development automation channel."""

import json
import os
import socket


def request(endpoint, command, timeout=130):
    message = (json.dumps(command) + "\n").encode()
    if os.name == "nt":
        import ctypes

        pipe = endpoint.read_text(encoding="utf-8").strip()
        if not pipe.startswith(r"\\.\pipe\zitter.automation."):
            raise OSError("Invalid Zitter automation endpoint")
        if not ctypes.windll.kernel32.WaitNamedPipeW(pipe, int(timeout * 1000)):
            raise ctypes.WinError()
        with open(pipe, "r+b", buffering=0) as connection:
            connection.write(message)
            data = connection.readline()
    else:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(timeout)
            connection.connect(str(endpoint))
            connection.sendall(message)
            with connection.makefile("rb") as stream:
                data = stream.readline()
    return json.loads(data or b'{"ok": false, "error": "no answer"}')
