#!/usr/bin/env python3
# Copyright 2026 Wason Technology, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Portability and no-networking checker for rmw_intraprocess (see CLAUDE.md).

Runs inside the container (scripts/check-portability.sh). Checks C/C++ sources for:
  POSIX-HEADER  POSIX/Unix-only headers (unistd.h, pthread.h, sys/*, ...)
  NET-HEADER    networking headers (winsock, asio, netinet, ...)
  NET-API       socket/networking API calls (socket, connect, bind, getaddrinfo, asio, ...)
  GCC-ONLY      GCC/Clang-only constructs (__attribute__, __builtin_*, typeof, ?:, ...)
  VISIBILITY    CMake libraries without hidden default visibility; packages with
                public headers but no visibility_control.h using __declspec
A finding on a line can be waived with a trailing comment
    // portability-ok(RULE): <reason>
The reason is mandatory. Waivers are reported so they stay visible in review.

--self-test runs the checker over scripts/portability/fixtures first: every file in
fixtures/bad must produce exactly the rules listed in its "// expect: RULE[,RULE]"
first line, and every file in fixtures/good must produce none.
"""

import argparse
import os
import re
import sys

SOURCE_EXT = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.ipp', '.inl'}
SKIP_DIRS = {'.git', 'build', 'install', 'log', '.cache', 'fixtures'}

POSIX_HEADERS = re.compile(
    r'^\s*#\s*include\s*[<"]('
    r'unistd\.h|pthread\.h|sys/[^>"]+|dlfcn\.h|fcntl\.h|poll\.h|semaphore\.h|sched\.h|'
    r'dirent\.h|termios\.h|syslog\.h|spawn\.h|strings\.h|libgen\.h|pwd\.h|grp\.h|'
    r'execinfo\.h|cxxabi\.h|ucontext\.h|mqueue\.h|aio\.h|glob\.h|fnmatch\.h|err\.h|'
    r'endian\.h|byteswap\.h|malloc\.h|alloca\.h|features\.h|link\.h|elf\.h'
    r')[>"]')
NET_HEADERS = re.compile(
    r'^\s*#\s*include\s*[<"]('
    r'sys/socket\.h|sys/un\.h|arpa/[^>"]+|netinet/[^>"]+|net/[^>"]+|netdb\.h|ifaddrs\.h|resolv\.h|'
    r'winsock2?\.h|ws2tcpip\.h|ws2def\.h|mswsock\.h|iphlpapi\.h|'
    r'asio\.hpp|asio/[^>"]+|boost/asio[^>"]*|zmq\.h|curl/[^>"]+'
    r')[>"]', re.IGNORECASE)
INCLUDE_LINE = re.compile(r'^\s*#\s*include\b')
# For pattern.match(text, pos): '^' would only match at the start of the string.
INCLUDE_AT = re.compile(r'[ \t]*#[ \t]*include\b')
# Text before a NET-API function-style match that makes it a declaration
# ("void connect(", "bool Foo::bind(") rather than a call.
DECL_PREFIX = re.compile(r'([A-Za-z_][\w:<>]*)[\s*&]+(?:[A-Za-z_]\w*::)*$')
NOT_A_TYPE = {'return', 'co_return', 'throw', 'else', 'case', 'do', 'new', 'delete', 'not',
              'and', 'or'}


def net_api_hits(code):
    """NET-API matches in a stripped line, ignoring member/function declarations."""
    for m in NET_API.finditer(code):
        if m.group(0).endswith('('):
            d = DECL_PREFIX.search(code[:m.start()])
            if d and d.group(1).split('::')[-1] not in NOT_A_TYPE:
                continue
        yield m

# Calls to free functions only: not preceded by '.', '->', '::' qualified by a
# namespace other than the global one (e.g. std::bind), or an identifier character.
_FREE = r'(?<![\w.>])(?<!\w::)(?:::)?'
NET_API = re.compile(
    _FREE + r'(?:socket|socketpair|connect|bind|listen|accept4?|getaddrinfo|freeaddrinfo|'
    r'gethostbyname\w*|gethostbyaddr|getnameinfo|sendto|recvfrom|sendmsg|recvmsg|'
    r'setsockopt|getsockopt|getsockname|getpeername|inet_\w+|htons|htonl|ntohs|ntohl|'
    r'getifaddrs|closesocket|ioctlsocket|shm_open|mq_open|sem_open)\s*\('
    r'|\bWSA[A-Z]\w*\b|\basio::|\bboost::asio\b|\bSOCK_(?:STREAM|DGRAM|RAW)\b|\bAF_INET6?\b')

GCC_ONLY = re.compile(
    r'\b__attribute__\b|\b__builtin_\w+|\b__typeof__\b|\btypeof\s*\(|#\s*pragma\s+GCC\b|'
    r'\b__extension__\b|\b__int128\b|\b__PRETTY_FUNCTION__\b|\b__thread\b|'
    r'#\s*include_next\b|\b__asm__\b|\basm\s*(?:volatile\s*)?\(|\b__restrict__\b|'
    r'\balloca\s*\(|(?:=|\breturn)\s*\(\s*\{|\?\s*:|\bcase\b[^:\n]*\.\.\.')
# visibility_control.h is the one place __attribute__ is expected (behind #ifdef).
VISIBILITY_HEADER = re.compile(r'visibility_control\.hp?p?$')

WAIVER = re.compile(r'//\s*portability-ok\(([A-Z-]+(?:\s*,\s*[A-Z-]+)*)\)\s*:\s*(\S.*)$')


def strip_code(text):
    """Blank out comments and string/char literals, preserving line structure.

    #include lines are kept verbatim so header names in quotes stay checkable.
    """
    out = []
    i, n = 0, len(text)
    state = None  # None, 'line', 'block', 'str', 'chr', 'raw'
    raw_end = ''
    line_start = True
    while i < n:
        c = text[i]
        if state is None:
            if line_start:
                m = INCLUDE_AT.match(text, i)
                if m:
                    j = text.find('\n', i)
                    j = n if j < 0 else j
                    line = text[i:j]
                    k = line.find('//')
                    out.append(line if k < 0 else line[:k])
                    i = j
                    continue
            if text.startswith('//', i):
                state = 'line'
                i += 2
                continue
            if text.startswith('/*', i):
                state = 'block'
                out.append('  ')
                i += 2
                continue
            m = re.match(r'(?:u8|u|U|L)?R"([^()\\ \n]{0,16})\(', text[i:i + 24])
            if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
                state = 'raw'
                raw_end = ')' + m.group(1) + '"'
                out.append('""')
                i += m.end()
                continue
            if c == '"':
                state = 'str'
                out.append('"')
                i += 1
                continue
            if c == "'" and not (i > 0 and text[i - 1].isalnum()):  # skip 1'000 digit sep
                state = 'chr'
                out.append("'")
                i += 1
                continue
            out.append(c)
            line_start = c == '\n'
            i += 1
            continue
        if state == 'line':
            if c == '\n':
                state = None
                out.append('\n')
                line_start = True
            i += 1
            continue
        if state == 'block':
            if text.startswith('*/', i):
                state = None
                out.append('  ')
                i += 2
                continue
            out.append('\n' if c == '\n' else ' ')
            i += 1
            continue
        if state == 'raw':
            if text.startswith(raw_end, i):
                state = None
                i += len(raw_end)
                continue
            if c == '\n':
                out.append('\n')
            i += 1
            continue
        # str / chr
        quote = '"' if state == 'str' else "'"
        if c == '\\':
            i += 2
            continue
        if c == quote:
            state = None
            out.append(quote)
        elif c == '\n':  # unterminated; resync at end of line
            state = None
            out.append('\n')
            line_start = True
        i += 1
    return ''.join(out)


def check_source(path, rel):
    with open(path, encoding='utf-8', errors='replace') as f:
        original = f.read()
    orig_lines = original.split('\n')
    code_lines = strip_code(original).split('\n')
    findings = []
    for idx, code in enumerate(code_lines):
        lineno = idx + 1
        hits = []
        if POSIX_HEADERS.search(code):
            hits.append(('POSIX-HEADER', 'POSIX-only header'))
        if NET_HEADERS.search(code):
            hits.append(('NET-HEADER', 'networking header'))
        if not INCLUDE_LINE.match(code):
            if any(True for _ in net_api_hits(code)):
                hits.append(('NET-API', 'networking API'))
            m = GCC_ONLY.search(code)
            if m and not (m.group(0) == '__attribute__' and VISIBILITY_HEADER.search(rel)):
                hits.append(('GCC-ONLY', 'GCC/Clang-only construct %r' % m.group(0)))
        if not hits:
            continue
        orig = orig_lines[idx] if idx < len(orig_lines) else ''
        w = WAIVER.search(orig)
        waived = {r.strip() for r in w.group(1).split(',')} if w else set()
        for rule, msg in hits:
            findings.append((rel, lineno, rule, msg, rule in waived, orig.strip()))
    return findings


def check_cmake(path, rel):
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    findings = []
    no_comments = re.sub(r'#[^\n]*', '', text)
    libs = re.findall(r'add_library\s*\(\s*([\w${}-]+)(?![^)]*\b(?:INTERFACE|IMPORTED|ALIAS|OBJECT)\b)',
                      no_comments)
    if libs:
        global_hidden = re.search(
            r'set\s*\(\s*CMAKE_CXX_VISIBILITY_PRESET\s+hidden\s*\)', no_comments) and re.search(
            r'set\s*\(\s*CMAKE_VISIBILITY_INLINES_HIDDEN\s+(?:ON|TRUE|1)\s*\)', no_comments)
        if not global_hidden:
            for lib in libs:
                per_target = re.search(
                    r'set_target_properties\s*\(\s*' + re.escape(lib) +
                    r'\b[^)]*CXX_VISIBILITY_PRESET\s+hidden[^)]*\)', no_comments)
                if not per_target:
                    findings.append((rel, 1, 'VISIBILITY',
                                     'library %s built without hidden default visibility '
                                     '(set CMAKE_CXX_VISIBILITY_PRESET hidden and '
                                     'CMAKE_VISIBILITY_INLINES_HIDDEN ON)' % lib, False, ''))
    return findings


def check_package(pkg_dir, rel):
    """A package with public headers must ship a visibility_control.h using __declspec."""
    inc = os.path.join(pkg_dir, 'include')
    if not os.path.isdir(inc):
        return []
    headers = []
    for d, _, files in os.walk(inc):
        headers += [os.path.join(d, f) for f in files if os.path.splitext(f)[1] in SOURCE_EXT]
    if not headers:
        return []
    vis = [h for h in headers if VISIBILITY_HEADER.search(os.path.basename(h))]
    if not vis:
        return [(rel, 1, 'VISIBILITY', 'package has public headers but no visibility_control.h',
                 False, '')]
    with open(vis[0], encoding='utf-8') as f:
        text = f.read()
    if '__declspec(dllexport)' not in text or '__declspec(dllimport)' not in text:
        return [(os.path.relpath(vis[0], os.path.dirname(pkg_dir)), 1, 'VISIBILITY',
                 'visibility_control.h lacks __declspec(dllexport)/(dllimport)', False, '')]
    return []


def scan(root, paths=None, skip_dirs=SKIP_DIRS):
    targets = [os.path.join(root, p) for p in paths] if paths else [root]
    findings = []
    nfiles = 0
    for target in targets:
        walker = [(os.path.dirname(target), [], [os.path.basename(target)])] \
            if os.path.isfile(target) else os.walk(target)
        for d, dirs, files in walker:
            dirs[:] = sorted(x for x in dirs if x not in skip_dirs)
            if 'package.xml' in files:
                findings += check_package(d, os.path.relpath(d, root))
            for fn in sorted(files):
                p = os.path.join(d, fn)
                rel = os.path.relpath(p, root)
                if os.path.splitext(fn)[1] in SOURCE_EXT:
                    nfiles += 1
                    findings += check_source(p, rel)
                elif fn == 'CMakeLists.txt':
                    nfiles += 1
                    findings += check_cmake(p, rel)
    return findings, nfiles


def self_test(fixture_root):
    ok = True
    count = 0
    for kind in ('bad', 'good'):
        d = os.path.join(fixture_root, kind)
        for fn in sorted(os.listdir(d)):
            p = os.path.join(d, fn)
            if os.path.isdir(p):
                findings, _ = scan(d, [fn], skip_dirs=set())
                expect_file = os.path.join(p, 'EXPECT')
                expected = set()
                if os.path.exists(expect_file):
                    with open(expect_file) as f:
                        expected = set(f.read().split())
            else:
                findings, _ = scan(d, [fn], skip_dirs=set())
                with open(p) as f:
                    first = f.readline()
                m = re.search(r'expect:\s*([A-Z, -]*)', first)
                expected = {r.strip() for r in m.group(1).split(',') if r.strip()} if m else set()
            got = {f[2] for f in findings if not f[4]}
            if kind == 'good':
                expected = set()
            count += 1
            if got != expected:
                ok = False
                print('self-test FAIL %s/%s: expected %s, got %s' %
                      (kind, fn, sorted(expected), sorted(got)))
                for f in findings:
                    print('    %s:%d %s %s' % (f[0], f[1], f[2], f[3]))
    print('self-test: %s (%d fixtures)' % ('OK' if ok else 'FAILED', count))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=os.path.join(os.path.dirname(__file__), '..', '..'))
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('paths', nargs='*', help='paths relative to --root (default: whole repo)')
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    if args.self_test and not self_test(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                     'fixtures')):
        return 2

    findings, nfiles = scan(root, args.paths)
    errors = [f for f in findings if not f[4]]
    waived = [f for f in findings if f[4]]
    for f in waived:
        print('waived  %s:%d [%s] %s' % (f[0], f[1], f[2], f[5]))
    for f in errors:
        print('ERROR   %s:%d [%s] %s%s' % (f[0], f[1], f[2], f[3],
                                          ('\n          ' + f[5]) if f[5] else ''))
    print('portability check: %s (%d files, %d errors, %d waived)' %
          ('OK' if not errors else 'FAILED', nfiles, len(errors), len(waived)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
