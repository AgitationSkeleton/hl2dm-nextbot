#!/usr/bin/env python3
"""Pre-push hygiene scan of the files git would commit (tracked + staged + untracked, not ignored).

  python -I tools/secretscan.py

Flags LAN/public IPv4 addresses, e-mail addresses (except GitHub noreply), drive paths, rcon
passwords, and any line of the files listed in tools/local.json "scan_forbidden_files" (e.g. the
rcon secret, the server's local bot-names file). Known-harmless hits go in "scan_allow".
Exit code 1 if anything is found.
"""
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = json.load(open(os.path.join(ROOT, "tools", "local.json")))
ALLOW = set(CFG.get("scan_allow", []))
PATTERNS = [
    (r"\b(?!127\.0\.0\.1\b|0\.0\.0\.0\b)(?:\d{1,3}\.){3}\d{1,3}\b", "ipv4"),
    (r"[\w.+-]+@(?!users\.noreply\.github\.com)[\w-]+\.[\w.]+", "email"),
    (r"\b[A-Za-z]:[\\/](?!Program Files)", "drive path"),
    (r"rcon_password\s+\"?[^\s\"<]", "rcon password"),
] + [(re.escape(f), "local fragment") for f in CFG.get("forbid", [])]


def files():
    out = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=ROOT,
                         capture_output=True, text=True).stdout
    return [l for l in out.splitlines() if l]


def main():
    forbidden = []
    for p in CFG.get("scan_forbidden_files", []):
        if os.path.exists(p):
            forbidden += [l.strip() for l in open(p, errors="replace") if len(l.strip()) >= 4]
    hits = 0
    for f in files():
        try:
            text = open(os.path.join(ROOT, f), encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for pat, kind in PATTERNS:
            for m in re.finditer(pat, text):
                if m.group(0) not in ALLOW:
                    print("%s: %s: %s" % (f, kind, m.group(0)[:60])); hits += 1
        for line in text.splitlines():
            for bad in forbidden:
                if bad in line and bad not in ALLOW:
                    print("%s: forbidden local string: %s" % (f, bad)); hits += 1
    print("secretscan: %d finding(s)" % hits)
    sys.exit(1 if hits else 0)


if __name__ == "__main__":
    main()
