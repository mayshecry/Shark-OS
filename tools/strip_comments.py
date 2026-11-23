#!/usr/bin/env python3
"""
Strip comments from C/H sources without touching string or character literals.

Handles // line comments, /* block comments */ and preserves line structure so
#line-visible offsets and diffs stay readable.

    python3 tools/strip_comments.py FILE [FILE...]
    python3 tools/strip_comments.py --check FILE   # exit 1 if comments remain

The scanner is a small state machine: normal, line comment, block comment,
string, char. Escapes inside literals are honoured, so "http://x" survives.
"""

import sys


def strip(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            out.append(ch)
            i += 1
            continue

        if state == "line":
            if ch == "\n":
                state = "code"
                # keep the trailing newline, drop trailing spaces introduced
                while out and out[-1] in " \t":
                    out.pop()
                out.append("\n")
            i += 1
            continue

        if state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 2
                # a block comment that ended a line keeps that newline
                j = i
                while j < n and text[j] in " \t":
                    j += 1
                if j < n and text[j] == "\n":
                    i = j
                continue
            if ch == "\n":
                out.append("\n")
            i += 1
            continue

        if state in ("string", "char"):
            out.append(ch)
            if ch == "\\" and nxt:
                out.append(nxt)
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            i += 1
            continue

    return "".join(out)


def tidy(text: str) -> str:
    lines = []
    for line in text.split("\n"):
        stripped = line.rstrip()
        lines.append(stripped)
    # collapse runs of three or more blank lines
    collapsed = []
    blanks = 0
    for line in lines:
        if line == "":
            blanks += 1
            if blanks > 2:
                continue
        else:
            blanks = 0
        collapsed.append(line)
    while collapsed and collapsed[-1] == "":
        collapsed.pop()
    return "\n".join(collapsed) + "\n"


def has_comment(text: str) -> bool:
    return strip(text) != text.replace("\r\n", "\n")


def main(argv):
    check = False
    files = []
    for arg in argv[1:]:
        if arg == "--check":
            check = True
        else:
            files.append(arg)

    bad = []
    for path in files:
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
            original = fh.read()
        cleaned = tidy(strip(original))
        if check:
            if cleaned != original:
                bad.append(path)
            continue
        if cleaned != original:
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
                fh.write(cleaned)
            print("stripped " + path, file=sys.stderr)

    if check and bad:
        print("comments remain in: " + " ".join(bad))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
