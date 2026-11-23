#!/usr/bin/env python3
"""Run the LAN test copy under cdb and log the first access violation (or exit) with a symbolized stack.

  python -I tools/cdbrun.py [map] [extra srcds args...]   -> writes build/cdb.log
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testsrv  # noqa: E402

ROOT = testsrv.ROOT
CDB = r"C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe"
log = os.path.join(ROOT, "build", "cdb.log")
mapname = sys.argv[1] if len(sys.argv) > 1 else "dm_lockdown"
testsrv.guard()
os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
cmds = os.path.join(ROOT, "build", "cdb.cmd")
open(cmds, "w").write("sxd ld\nsxd -c \"\" eh\n"
                      "sxe -c \".echo FIRSTCHANCE; r; kn 40; q\" av\n"
                      "g\n.lastevent\nkn 40\nq\n")
symp = "srv*;" + os.path.join(testsrv.ADDON, "bin")
args = [CDB, "-g", "-G", "-lines", "-y", symp, "-logo", log, "-cf", cmds] + testsrv.srcds_args(mapname, sys.argv[2:])
p = subprocess.Popen(args, cwd=testsrv.TS, creationflags=subprocess.CREATE_NEW_CONSOLE)
open(os.path.join(testsrv.TS, "srcds.pid"), "w").write(str(p.pid))
print("cdb pid", p.pid, "log", log)
