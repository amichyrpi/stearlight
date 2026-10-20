"""Exercise the real V4L2 adapter with a deterministic decoder transport."""
import os
import mmap
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[3]


class VideoQueueTest(unittest.TestCase):
    def test_reused_low_buffer_index_cannot_overtake_older_frames(self):
        for shared in (False, True):
            with self.subTest(shared_memory=shared):
                self.exercise_transport(shared)

    def exercise_transport(self, shared):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            library, executable = folder / 'video.so', folder / 'test'
            subprocess.run(['g++', '-O2', '-shared', '-fPIC', '-std=c++17',
                            '-I' + str(ROOT / 'packages/quest-runtime/app/src/main/cpp'),
                            str(ROOT / 'packages/quest-runtime/linux/video-bridge.cpp'),
                            '-ldl', '-pthread', '-o', str(library)], check=True)
            subprocess.run(['g++', '-O2', '-std=c++17', str(Path(__file__).with_suffix('.cpp')),
                            '-o', str(executable)], check=True)
            server = socket.socket(socket.AF_UNIX)
            self.addCleanup(server.close)
            path = str(folder / 'decoder.sock')
            server.bind(path)
            server.listen(1)
            server.settimeout(10)
            errors = []
            payload_bytes = []

            def decode():
                memory = None
                try:
                    connection, _ = server.accept()
                    with connection:
                        connection.settimeout(5)

                        def receive(size):
                            result = bytearray()
                            while len(result) < size:
                                part = connection.recv(size - len(result))
                                if not part:
                                    return None
                                result.extend(part)
                            return result

                        while True:
                            header, ancillary, _, _ = connection.recvmsg(32, socket.CMSG_SPACE(4))
                            if not header:
                                break
                            if len(header) < 32:
                                header += receive(32 - len(header))
                            magic, operation, size, _, _, _, timestamp = struct.unpack('<6IQ', header)
                            if size:
                                receive(size)
                            frame = b''
                            status = 1
                            if operation == 4:
                                descriptor = struct.unpack('i', ancillary[0][2])[0]
                                if shared:
                                    memory = mmap.mmap(descriptor, 4096 * 4096 * 3 // 2)
                                else:
                                    status = -1
                                os.close(descriptor)
                            if operation == 2:
                                frame = bytes([40 + timestamp]) * (128 * 128) + bytes([128]) * (128 * 64)
                                status = 0
                                if memory:
                                    memory[:len(frame)] = frame
                            payload = frame if memory is None else b''
                            payload_bytes.append(len(payload))
                            connection.sendall(struct.pack('<Ii4IQ', magic, status,
                                                           len(frame), 128, 128, 21, timestamp) + payload)
                except Exception as error:
                    errors.append(error)
                finally:
                    if memory is not None:
                        memory.close()

            worker = threading.Thread(target=decode, daemon=True)
            worker.start()
            environment = dict(os.environ, LD_PRELOAD=str(library), QUEST_VIDEO_SOCKET=path)
            environment.pop('QUEST_VIDEO_STATEFUL', None)
            result = subprocess.run([str(executable)], env=environment, timeout=15, capture_output=True, text=True)
            worker.join(timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)
            self.assertEqual(sum(payload_bytes), 0 if shared else 10 * 128 * 128 * 3 // 2)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
