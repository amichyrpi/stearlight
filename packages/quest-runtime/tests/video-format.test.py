"""The real adapter must announce sequence headers before requiring a picture."""
from pathlib import Path
import os
import socket
import struct
import subprocess
import tempfile
import threading

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    library, executable = folder / 'video.so', folder / 'test'
    subprocess.run(['g++', '-O2', '-shared', '-fPIC', '-std=c++17', '-pthread',
                    '-I' + str(root / 'packages/quest-runtime/app/src/main/cpp'),
                    str(root / 'packages/quest-runtime/linux/video-bridge.cpp'), '-ldl', '-o', str(library)], check=True)
    subprocess.run(['g++', '-O2', '-std=c++17', str(Path(__file__).with_suffix('.cpp')), '-o', str(executable)], check=True)
    marker, path = folder / 'format-ready', str(folder / 'decoder.sock')
    errors = []
    with socket.socket(socket.AF_UNIX) as server:
        server.bind(path)
        server.listen(1)
        server.settimeout(5)

        def decode():
            try:
                connection, _ = server.accept()
                with connection:
                    connection.settimeout(5)
                    pending = False
                    while True:
                        header, controls, _, _ = connection.recvmsg(32, socket.CMSG_SPACE(4), socket.MSG_WAITALL)
                        if not header:
                            break
                        assert len(header) == 32
                        magic, operation, size, config_width, config_height, _, timestamp = struct.unpack('<6IQ', header)
                        for _, _, data in controls:
                            for fd in struct.unpack('i' * (len(data) // 4), data):
                                os.close(fd)
                        if size:
                            assert connection.recv(size, socket.MSG_WAITALL).startswith(b'\x00\x00\x00\x01\x42\x01')
                        frame, status, width, height = b'', 1, 0, 0
                        if operation == 1:
                            assert (config_width, config_height) == (1152, 4608)
                        elif operation == 4:
                            status = -1  # Exercise the supported legacy transport.
                        elif operation == 2:
                            assert timestamp == 713000000
                            status, width, height, pending = 2, 1152, 4608, True
                        elif operation == 3 and pending:
                            assert marker.exists(), 'Decoder polled for pictures before announcing the format'
                            frame = bytes([81]) * (1152 * 4608) + bytes([90, 240]) * (1152 * 1152)
                            status, width, height, timestamp, pending = 0, 1152, 4608, 713000000, False
                        connection.sendall(struct.pack('<Ii4IQ', magic, status, len(frame), width, height, 21, timestamp) + frame)
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=decode, daemon=True)
        worker.start()
        result = subprocess.run([str(executable), str(marker)], env={**os.environ, 'LD_PRELOAD': str(library),
                                'QUEST_VIDEO_SOCKET': path, 'QUEST_VIDEO_STATEFUL': '1'}, capture_output=True, text=True, timeout=15)
        worker.join(timeout=5)
        assert not worker.is_alive() and not errors, (errors,result.returncode,result.stdout,result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip())
