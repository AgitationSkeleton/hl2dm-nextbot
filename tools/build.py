#!/usr/bin/env python3
"""Incremental, parallel MSVC x86 build of hl2mpnextbot.dll (Metamod:Source plugin).

AMBuild is not used: its daemon and probe executables are blocked by the antivirus on the build
machine. This applies the flags the Metamod hl2sdk manifest uses for the hl2dm SDK.

  python -I tools/build.py [clean] [-j N]

Needs VS2022 Build Tools (x86), the alliedmodders hl2sdk (branch hl2dm) and metamod-source
(plugin API 16). Paths come from HL2SDK / MMSOURCE / VCVARS or tools/local.json
{"hl2sdk": ..., "mmsource": ..., "vcvars": ...}.
"""
import concurrent.futures as cf, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_local = os.path.join(ROOT, "tools", "local.json")
CFG = json.load(open(_local)) if os.path.exists(_local) else {}
SDK = os.environ.get("HL2SDK", CFG.get("hl2sdk", ""))
MMS = os.environ.get("MMSOURCE", CFG.get("mmsource", ""))
VCVARS = os.environ.get("VCVARS", CFG.get("vcvars",
         r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"))
OBJ = os.path.join(ROOT, "build", "obj")
OUT = os.path.join(ROOT, "build", "hl2mpnextbot.dll")

SRC_DIRS = ["src"]
INCLUDES = ["src"]
SDK_INCLUDES = ["public", "public/engine", "public/mathlib", "public/vstdlib", "public/tier0", "public/tier1",
                "public/game/server", "public/toolframework", "game/shared", "common"]
DEFINES = ("SOURCE_ENGINE=7 SE_EPISODEONE=1 SE_ORANGEBOX=3 SE_CSS=6 SE_HL2DM=7 SE_DODS=8 SE_SDK2013=9 SE_TF2=12 "
           "SE_LEFT4DEAD=13 SE_NUCLEARDAWN=14 SE_LEFT4DEAD2=16 SE_ALIENSWARM=17 SE_PORTAL2=18 SE_CSGO=23 "
           "GAME_DLL RAD_TELEMETRY_DISABLED COMPILER_MSVC COMPILER_MSVC32 WIN32 _WINDOWS NDEBUG "
           "_CRT_SECURE_NO_DEPRECATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE HL2MPNB").split()
CFLAGS = ["/nologo", "/c", "/MT", "/EHsc", "/TP", "/std:c++17", "/W3", "/O2", "/Oy-", "/Zi", "/GR",
          "/wd4005", "/wd4996", "/wd4244", "/wd4305", "/wd4267", "/wd4018", "/wd4800", "/wd4838", "/wd4091",
          "/Zc:threadSafeInit-", "/MP1"]
LIBS = [os.path.join(SDK, "lib", "public", "x86", l + ".lib") for l in ("mathlib", "tier1", "tier0", "vstdlib")] + \
       ["legacy_stdio_definitions.lib", "kernel32.lib", "user32.lib", "advapi32.lib"]


def vc_env():
    cache = os.path.join(ROOT, "build", "vcenv.json")
    if os.path.exists(cache):
        return json.load(open(cache))
    out = subprocess.run('cmd /c ""%s" x86 >nul && set"' % VCVARS, capture_output=True, text=True, shell=True).stdout
    env = dict(l.split("=", 1) for l in out.splitlines() if "=" in l)
    # cmd's "set" may print the search path as "Path": normalise it so env["PATH"] works
    path_keys = [k for k in env if k.upper() == "PATH"]
    if not path_keys:
        sys.exit("vcvarsall did not produce an environment: " + VCVARS)
    env["PATH"] = env.pop(path_keys[0])
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    json.dump(env, open(cache, "w"))
    return env


def sources():
    for d in SRC_DIRS:
        for base, _, files in os.walk(os.path.join(ROOT, d)):
            for f in files:
                if f.endswith(".cpp"):
                    yield os.path.join(base, f)


def objname(src):
    rel = os.path.relpath(src, ROOT).replace("\\", "/")
    return os.path.join(OBJ, rel.replace("/", "__")[:-4] + ".obj")


def headers_mtime():
    m = 0
    for d in SRC_DIRS:
        for base, _, files in os.walk(os.path.join(ROOT, d)):
            for f in files:
                if f.endswith((".h", ".inc")):
                    m = max(m, os.path.getmtime(os.path.join(base, f)))
    return m


def compile_one(env, src):
    obj = objname(src)
    inc = []
    for i in INCLUDES:
        inc += ["/I", os.path.join(ROOT, i)]
    for i in SDK_INCLUDES:
        inc += ["/I", os.path.join(SDK, i)]
    inc += ["/I", os.path.join(MMS, "core"), "/I", os.path.join(MMS, "core", "sourcehook")]
    cmd = [shutil.which("cl", path=env["PATH"])] + CFLAGS + ["/D" + d for d in DEFINES] + inc + \
          [src, "/Fo" + obj, "/Fd" + obj[:-4] + ".pdb"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace")
    return src, r.returncode, r.stdout + r.stderr


def main():
    if not SDK or not MMS:
        sys.exit("set HL2SDK and MMSOURCE (or tools/local.json)")
    args = sys.argv[1:]
    if "clean" in args and os.path.isdir(OBJ):
        shutil.rmtree(OBJ)
    jobs = int(args[args.index("-j") + 1]) if "-j" in args else os.cpu_count()
    os.makedirs(OBJ, exist_ok=True)
    env = vc_env()
    hm = headers_mtime()
    srcs = list(sources())
    todo = [s for s in srcs if not os.path.exists(objname(s)) or
            os.path.getmtime(objname(s)) < max(os.path.getmtime(s), hm)]
    failed = 0
    with cf.ThreadPoolExecutor(jobs) as ex:
        for src, rc, out in ex.map(lambda s: compile_one(env, s), todo):
            lines = [l for l in out.splitlines() if (" error " in l or "fatal error" in l or (rc and "warning" not in l))
                     and not l.strip().endswith(".cpp")]
            if rc:
                failed += 1
                print("FAIL", os.path.relpath(src, ROOT))
                for l in lines[:25]:
                    print("   ", l.replace(ROOT + "\\", ""))
    print("compiled %d, failed %d" % (len(todo), failed))
    if failed:
        sys.exit(1)
    objs = [objname(s) for s in srcs]
    cmd = [shutil.which("link", path=env["PATH"]), "/nologo", "/DLL", "/DEBUG", "/OPT:REF", "/SUBSYSTEM:WINDOWS",
           "/MACHINE:X86", "/OUT:" + OUT, "/PDB:" + OUT[:-4] + ".pdb", "/NODEFAULTLIB:libcmtd"] + objs + LIBS
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace")
    if r.returncode:
        lines = [l for l in r.stdout.splitlines() if "error" in l]
        print("LINK FAILED (%d errors)" % len(lines))
        for l in lines[:60]:
            print("   ", l)
        sys.exit(1)
    print("linked", OUT, os.path.getsize(OUT))


if __name__ == "__main__":
    main()
