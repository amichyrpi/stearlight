"""Verify Steam's owner query against live sockets, not invented process data."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / "linux/lsof-launcher.c"
with tempfile.TemporaryDirectory() as work:
    binary = Path(work) / "lsof"
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", str(source),
                    "-o", str(binary)], check=True)
    server = subprocess.Popen(["python3", "-u", "-c", """
import socket, sys
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen()
print(s.getsockname()[1], flush=True)
c, _ = s.accept()
sys.stdin.read()
"""], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        port = int(server.stdout.readline())
        with socket.create_connection(("127.0.0.1", port)) as client:
            output = subprocess.check_output([str(binary), "-P", "-F", "upnR",
                "-i", f"TCP@127.0.0.1:{port}"], text=True)
            owners = {}
            for line in output.splitlines():
                if line.startswith("p"):
                    owner = owners.setdefault(int(line[1:]), {})
                elif line.startswith("n"):
                    owner.setdefault("endpoints", []).append(line[1:])
                else:
                    owner[line[0]] = int(line[1:])
            assert set(owners) == {os.getpid(), server.pid}, output
            for pid, parent in [(os.getpid(), os.getppid()), (server.pid, os.getpid())]:
                assert owners[pid]["u"] == os.getuid(), output
                assert owners[pid]["R"] == parent, output
            local = f"127.0.0.1:{client.getsockname()[1]}"
            remote = f"127.0.0.1:{port}"
            assert f"{local}->{remote}" in owners[os.getpid()]["endpoints"], output
            assert f"{remote}->{local}" in owners[server.pid]["endpoints"], output
    finally:
        server.communicate(timeout=5)
print("Live socket owner, parent, UID and both endpoint checks passed")
