import json
import socket

SOCKET = "/run/armada/control.sock"


def call(action, **payload):
    request = {"action": action, **payload}
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(30)
        sock.connect(SOCKET)
        sock.sendall((json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8"))
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    response = json.loads(data.decode("utf-8"))
    if not response.get("ok"):
        raise RuntimeError(response.get("error") or "privileged call failed")
    return response.get("result", {})
