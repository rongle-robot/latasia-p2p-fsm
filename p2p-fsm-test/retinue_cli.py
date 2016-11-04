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

    ack = {}
    ack["type"] = "ack"

    retinue_master = ("192.168.160.4", 4036)
    ucm = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local_addr_master = (("0.0.0.0"), 7778)
    ucm.bind(local_addr_master)
    data = {}
    data["type"] = "2"
    data["auth"] = sys.argv[1]
    ucm.sendto(json.dumps(data), retinue_master)
    ucm.sendto(json.dumps(data), retinue_master)
    ucm.sendto(json.dumps(data), retinue_master)

    data, peer_addr = ucm.recvfrom(512)
    print data

    data = {}
    data["auth"] = sys.argv[1]
    data["back"] = "3"
    ucm.sendto(json.dumps(ack), peer_addr)
    ucm.close()

    retinue_vice = ("192.168.160.3", 4036)
    ucv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    local_addr_vice = (("0.0.0.0"), 7779)
    ucv.bind(local_addr_vice)
    data = {}
    data["type"] = "1"
    data["auth"] = sys.argv[1]
    ucv.sendto(json.dumps(data), retinue_vice)
    ucv.close()
