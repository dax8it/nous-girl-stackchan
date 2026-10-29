#!/usr/bin/env python3
"""Safety scan for this repo. Fails (exit 1) on:
- secret or token patterns
- email addresses
- private domains (read from the PRIVATE_DOMAINS env var, one per line, set from a
  repository secret; never written into the repo)
- LAN IP addresses other than the documented examples
- files over the size limit
- broken relative links in Markdown files
Run locally: python3 .github/scripts/safety_scan.py
"""
import os, re, subprocess, sys

MAX_BYTES = 2 * 1024 * 1024
ALLOWED_IPS = {"192.168.1.10", "192.168.1.25", "192.168.4.1", "127.0.0.1", "0.0.0.0"}
SECRET_PATTERNS = {
    "private key": r"-----BEGIN [A-Z ]*PRIVATE KEY-----",
    "GitHub token": r"\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}|\bgithub_pat_[A-Za-z0-9_]{40,}",
    "OpenAI-style key": r"\bsk-[A-Za-z0-9_-]{20,}",
    "Slack token": r"\bxox[abprs]-[A-Za-z0-9-]{10,}",
    "AWS key": r"\bAKIA[0-9A-Z]{16}\b",
    "Google API key": r"\bAIza[0-9A-Za-z_-]{35}\b",
    "assigned secret": r"(?i)\b(api[_-]?key|secret|token|password|passwd)\b\s*[:=]\s*['\"]?[A-Za-z0-9_\-./+]{16,}",
}
EMAIL = r"\b[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}\b"
LAN_IP = r"\b(10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3})\b"
SKIP_SELF = {".github/scripts/safety_scan.py"}

files = subprocess.check_output(["git", "ls-files", "-z"]).decode().split("\0")
files = [f for f in files if f and os.path.isfile(f)]
private = [d.strip().lower() for d in os.environ.get("PRIVATE_DOMAINS", "").splitlines() if d.strip()]
problems = []

def add(f, line_no, kind):
    problems.append(f"{f}:{line_no}: {kind}")   # never print the match itself

for f in files:
    size = os.path.getsize(f)
    if size > MAX_BYTES:
        add(f, 0, f"file is {size} bytes (limit {MAX_BYTES})")
    data = open(f, "rb").read()
    if b"\0" in data[:8000]:
        low = data.lower()
        for d in private:
            if d.encode() in low:
                add(f, 0, "private domain in binary file")
        continue
    text = data.decode("utf-8", errors="ignore")
    for n, line in enumerate(text.splitlines(), 1):
        if f not in SKIP_SELF:
            for kind, pat in SECRET_PATTERNS.items():
                if re.search(pat, line):
                    add(f, n, kind)
            if re.search(EMAIL, line):
                add(f, n, "email address")
            for m in re.finditer(LAN_IP, line):
                if m.group(0) not in ALLOWED_IPS:
                    add(f, n, "LAN IP that is not a documented example")
        for d in private:
            if d in line.lower():
                add(f, n, "private domain")
    if f.endswith(".md"):
        for link in re.findall(r"\]\(([^)\s]+)\)", text):
            if link.startswith(("http://", "https://", "#", "mailto:")):
                continue
            target = os.path.normpath(os.path.join(os.path.dirname(f), link.split("#")[0]))
            if not os.path.exists(target):
                add(f, 0, "broken relative link: " + link)

if not private:
    print("note: PRIVATE_DOMAINS is not set (normal for pull requests from forks); private domain check skipped")
print(f"scanned {len(files)} files")
if problems:
    print("\n".join(problems))
    print(f"FAILED: {len(problems)} problem(s)")
    sys.exit(1)
print("OK: no problems found")
