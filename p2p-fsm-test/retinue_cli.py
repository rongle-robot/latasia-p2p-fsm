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
    data["type"] = "auth"
    data["auth"] = sys.argv[1]
    ack = {}
    ack["type"] = "ack"

    retinue_master = ("127.0.0.1", 9998)
    ucm = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local_addr_master = (("0.0.0.0"), 7778)
    ucm.bind(local_addr_master)
    ucm.sendto(json.dumps(data), retinue_master)
    data, peer_addr = ucm.recvfrom(512)
    ucm.sendto(json.dumps(ack), peer_addr)
    ucm.close()

    retinue_vice = ("127.0.0.1", 9999)
    ucv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local_addr_vice = (("0.0.0.0"), 7779)
    ucv.bind(local_addr_vice)
    ucv.sendto(json.dumps(data), retinue_vice)
    ucv.close()
