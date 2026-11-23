#!/usr/bin/env python3
"""hl2mp-nextbot test harness for the LAN test copy (never the live server).

  start [map] [extra srcds args]   start the test copy (plain, new console); pid -> <testsrv>/srcds.pid
  kill                             stop every srcds whose command line names the test copy
  deploy                           copy build/hl2mpnextbot.dll (+pdb), the .vdf and package/ into the test copy
  rcon <cmd...>                    run one rcon command (password read from <testsrv>/rcon.secret)
  log [n]                          last n lines of the test copy's console.log
  status                           a2s player list (humans check)
  dumps                            crash minidumps in the test copy

Local settings live in tools/local.json (git-ignored):
  {"host": "127.0.0.1", "port": 27015, "testsrv": "<path to the test copy>",
   "forbid": ["<path fragment of every live install>"]}
"""
import glob, json, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = json.load(open(os.path.join(ROOT, "tools", "local.json")))
TS = os.path.abspath(CFG["testsrv"])
GAME = os.path.join(TS, "hl2mp")
ADDON = os.path.join(GAME, "addons", "hl2mpnextbot")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import rcon as _rcon  # noqa: E402


FORBID = [f.lower() for f in CFG.get("forbid", [])]   # path fragments of live installs (local.json)


def forbidden(path):
    p = os.path.realpath(path).lower()
    return any(f in p for f in FORBID)


def guard():
    if not FORBID:
        sys.exit("refusing: set \"forbid\" (live install path fragments) in tools/local.json")
    if forbidden(GAME) or not os.path.isfile(os.path.join(TS, "rcon.secret")):
        sys.exit("refusing: %s does not look like the test copy" % GAME)
    for sub in ("maps", "addons", "cfg", "bin"):
        p = os.path.join(GAME, sub)
        if os.path.exists(p) and forbidden(p):
            sys.exit("refusing: %s resolves into a live install" % p)


def srcds_args(mapname, extra):
    return [os.path.join(TS, "srcds.exe"), "-console", "-game", "hl2mp", "-condebug", "-nocrashdialog",
            "-nohltv", "+ip", CFG["host"], "+port", str(CFG["port"]), "+clientport", str(CFG["port"] + 1),
            "+sv_lan", "1", "+sv_pure", "-1", "+maxplayers", "16", "+servercfgfile", "server_test.cfg",
            "+map", mapname] + list(extra)


def start(mapname, extra):
    guard()
    p = subprocess.Popen(srcds_args(mapname, extra), cwd=TS, creationflags=subprocess.CREATE_NEW_CONSOLE)
    open(os.path.join(TS, "srcds.pid"), "w").write(str(p.pid))
    print("started pid", p.pid)


def test_pids():
    """srcds/cdb processes whose command line points at the test copy (never anything else)."""
    ps = ("Get-CimInstance Win32_Process -Filter \"Name='srcds.exe' or Name='cdb.exe'\" | "
          "ForEach-Object { \"$($_.ProcessId)`t$($_.CommandLine)\" }")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        if "\t" not in line:
            continue
        pid, cmd = line.split("\t", 1)
        if TS.lower().replace("/", "\\") in cmd.lower().replace("/", "\\") and not any(f in cmd.lower() for f in FORBID):
            pids.append(int(pid))
    return pids


def kill():
    guard()
    for pid in test_pids():
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
        print("killed", pid)
    pidf = os.path.join(TS, "srcds.pid")
    if os.path.exists(pidf):
        os.remove(pidf)


def deploy():
    guard()
    os.makedirs(os.path.join(ADDON, "bin"), exist_ok=True)
    for f in ("hl2mpnextbot.dll", "hl2mpnextbot.pdb"):
        src = os.path.join(ROOT, "build", f)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(ADDON, "bin", f))
    pkg = os.path.join(ROOT, "package", "hl2mp")
    if os.path.isdir(pkg):
        shutil.copytree(pkg, GAME, dirs_exist_ok=True)
    print("deployed to", ADDON)


def rcon(cmd, timeout=15.0):
    guard()
    pw = open(os.path.join(TS, "rcon.secret")).read().strip()
    return _rcon.run("%s:%d" % (CFG["host"], CFG["port"]), pw, [cmd], timeout=timeout)


def log(n):
    p = os.path.join(GAME, "console.log")
    lines = open(p, errors="replace").read().splitlines() if os.path.exists(p) else []
    return "\n".join(lines[-n:])


if __name__ == "__main__":
    a = sys.argv[1:] or ["help"]
    if a[0] == "start":
        start(a[1] if len(a) > 1 else "dm_lockdown", a[2:])
    elif a[0] == "kill":
        kill()
    elif a[0] == "deploy":
        deploy()
    elif a[0] == "rcon":
        sys.stdout.write(rcon(" ".join(a[1:])) + "\n")
    elif a[0] == "log":
        print(log(int(a[1]) if len(a) > 1 else 60))
    elif a[0] == "pids":
        print(test_pids())
    elif a[0] == "dumps":
        for d in glob.glob(os.path.join(TS, "*.mdmp")):
            print(d, os.path.getsize(d), time.ctime(os.path.getmtime(d)))
    else:
        print(__doc__)
