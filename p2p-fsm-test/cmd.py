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


def on_exit(cmd, s):
    return True


def on_help(cmd, s):
    print "exit    -- exit"
    print "help    -- show this message"
    return False


if __name__ == "__main__":
    exit = False
    cmdset = {"exit": on_exit, "help": on_help, }

    cli = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        cli.connect(("127.0.0.1", 4321))
    except socket.error as e:
        print e
        sys.exit(1)

    while not exit:
        print "> ",

        try:
            cmd = raw_input()
        except EOFError:
            print ""
            break
        except KeyboardInterrupt:
            print ""
            break

        try:
            exit = cmdset[cmd](cmd, cli)
        except KeyError:
            print "unknonw command"
