#! /usr/bin/env python
# -*- coding:utf-8 -*-


import sys
import time
import json
import socket


if "__main__" == __name__:
    if len(sys.argv) == 1:
        print "[ERROR] no auth"
        sys.exit(1)

    data = {}
    data["auth"] = sys.argv[1]
    addr = ("192.168.160.4", 1234)
    # addr = ("127.0.0.1", 1234)
    uc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while (True):
        try:
            uc.sendto(json.dumps(data), addr)
            time.sleep(3)
        except KeyboardInterrupt:
            break
    uc.close()
