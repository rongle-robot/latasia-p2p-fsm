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

    # master
    retinue_master = ("192.168.160.3", 4036)
    ucm = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local_addr_master = (("0.0.0.0"), 7778)
    ucm.bind(local_addr_master)

    data = {}
    data["type"] = "2"
    data["auth"] = sys.argv[1]
    ucm.sendto(json.dumps(data), retinue_master)

    data, peer_addr = ucm.recvfrom(512)
    print data, peer_addr

    data = {}
    data["auth"] = sys.argv[1]
    data["back"] = "3"
    ucm.sendto(json.dumps(data), peer_addr)

    # vice
    retinue_vice = ("192.168.160.4", 4036)
    data = {}
    data["type"] = "1"
    data["auth"] = sys.argv[1]
    ucm.sendto(json.dumps(data), retinue_vice)
    ucm.close()
