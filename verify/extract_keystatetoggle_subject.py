"""Copy the actual synchronous reader bodies into a pure-memory test subject.

This tool does not compile or execute the subject and calls no Windows API.
"""
from pathlib import Path
import argparse
import hashlib
import json


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source = args.core.read_bytes()
    first = b"SHORT WINAPI h_GetKeyState(int vk) {"
    last = b"BOOL WINAPI h_GetCursorPos(LPPOINT p) {"
    bulk = b"BOOL WINAPI h_GetKeyboardState(PBYTE ks) {"
    if source.count(first) != 1 or source.count(last) != 1:
        raise ValueError("expected one exact GetKeyState/GetCursorPos boundary")
    start, end = source.index(first), source.index(last)
    if start >= end:
        raise ValueError("reader source boundaries are reversed")
    subject = source[start:end]
    if subject.count(bulk) != 1 or b"h_GetAsyncKeyState" in subject:
        raise ValueError("expected only synchronous single/bulk reader section")
    core_hash = hashlib.sha256(source).hexdigest().upper()
    body_hash = hashlib.sha256(subject).hexdigest().upper()
    args.out.mkdir(parents=True, exist_ok=False)
    header = (f'#define VI_KEY_STATE_CORE_SHA "{core_hash}"\n'
              f'#define VI_KEY_STATE_BODY_SHA "{body_hash}"\n').encode() + subject
    (args.out / "vi_keystatetoggle_subject.inl").write_bytes(header)
    receipt = {"source": str(args.core.resolve()), "core_sha256": core_hash,
               "body_sha256": body_hash, "body_bytes": len(subject),
               "native_api_execution": False}
    (args.out / "subject.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
