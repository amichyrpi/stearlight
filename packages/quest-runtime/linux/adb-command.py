#!/usr/bin/python3
import base64
import os
import sys

command = base64.b64decode(sys.argv[1], validate=True).decode('utf-8')
os.execv('/bin/bash', ['/bin/bash', '/opt/quest/desktop-shell.sh', '-lc', command])
