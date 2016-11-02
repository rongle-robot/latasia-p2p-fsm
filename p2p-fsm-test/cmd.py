#! /usr/bin/python


import sys
import json
import struct
import socket


def pack_sjsonb(cargo):
    str_cargo = json.dumps(cargo, separators=(",", ":"))
    package = struct.pack(
        "!5I{}s".format(len(str_cargo)), 0xE78f8A9D, 1000, 20, len(str_cargo), 0, str_cargo
    )
    return package


def unpack_sjsonb(package):
    magic_no, _, ent_offset, ent_sz, _ = struct.unpack("!5I", package[0:20])
    str_cargo = package[ent_offset : ent_offset + ent_sz]
    cargo = json.loads(str_cargo)
    return cargo


def on_heartbeat(arglist, skt):
    skt.sendall(pack_sjsonb({"interface": "heartbeat",}))
    print unpack_sjsonb(skt.recv(1024))
    return False


def on_exit(arglist, skt):
    return True


def on_help(arglist, skt):
    print "exit    -- exit"
    print "help    -- show this message"
    print "heartbeat    -- keep tcp connection alive"
    print "login auth   -- login"
    print "logout auth  -- logout"
    print "talkto auth peer_auth    -- talk"
    print "listen       -- listen"
    print "p2p_init auth peer_auth -- init p2p"
    return False


def on_login(arglist, skt):
    try:
        auth = arglist[0]
    except IndexError:
        print "argument 'auth' missing"
        return False

    skt.sendall(pack_sjsonb({"interface": "login", "auth": auth}))
    print unpack_sjsonb(skt.recv(1024))

    return False


def on_logout(arglist, skt):
    try:
        auth = arglist[0]
    except IndexError:
        print "argument 'auth' missing"
        return False

    skt.sendall(pack_sjsonb({"interface": "logout", "auth": auth}))
    print unpack_sjsonb(skt.recv(1024))

    return False


def on_talkto(arglist, skt):
    try:
        auth = arglist[0]
    except IndexError:
        print "argument 'auth' missing"
        return False

    try:
        peer_auth = arglist[1]
    except IndexError:
        print "argument 'peer_auth' missing"
        return False

    try:
        message = arglist[2]
    except IndexError:
        print "argument 'message' missing"
        return False

    if auth == peer_auth:
        print "talk to yourself? No..."
        return False

    skt.sendall(pack_sjsonb({"interface": "talkto", "auth": auth,
                             "peer_auth": peer_auth, "message": message}))
    recv_data = skt.recv(1024)
    print unpack_sjsonb(recv_data)

    return False


def on_listen(arglist, skt):
    print unpack_sjsonb(skt.recv(1024))
    return False


def on_p2p_init(arglist, skt):
    try:
        auth = arglist[0]
    except IndexError:
        print "argument 'auth' missing"
        return False

    try:
        peer_auth = arglist[1]
    except IndexError:
        print "argument 'peer_auth' missing"
        return False

    skt.sendall(pack_sjsonb({"interface": "p2p_init", "auth": auth,
                             "peer_auth": peer_auth}))
    recv_data = skt.recv(1024)
    print unpack_sjsonb(recv_data)

    return False


if __name__ == "__main__":
    exit = False
    cmdset = {
        "heartbeat": on_heartbeat,
        "exit": on_exit, "help": on_help,
        "login": on_login, "logout": on_logout,
        "talkto": on_talkto, "listen": on_listen,
        "p2p_init": on_p2p_init,
    }

    cli = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        cli.connect(("127.0.0.1", 4321))
    except socket.error as e:
        print e
        sys.exit(1)

    while not exit:
        print "> ",

        try:
            cmd = raw_input().split()
        except EOFError:
            print ""
            break
        except KeyboardInterrupt:
            print ""
            break

        if len(cmd) == 0:
            continue

        try:
            exit = cmdset[cmd[0]](cmd[1:], cli)
        except KeyError:
            print "unknonw command"

    cli.close()
