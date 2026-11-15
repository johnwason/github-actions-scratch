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

"""
Every test and tool goes through the safety helpers (CLAUDE.md S2).

Fails if any C++ or Python file under test/ (and the repo's tools/) creates a
Robot Raconteur node setup directly, imports RobotRaconteur.Client (which
sets up TCP and discovery on import), or starts discovery, except the
helpers themselves.
"""

import os
import re

PACKAGE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_DIR = os.path.dirname(PACKAGE_DIR)
HELPERS = {'rr_test_safety.hpp', 'rr_test_safety.cpp', 'rr_test_safety.py',
           os.path.basename(__file__)}

FORBIDDEN = [
    # The default-flag setup classes never belong in a test, in any form.
    (re.compile(r'\b(Server|Client|SecureServer)NodeSetup\b'),
     'uses a default node setup class; use rr_test_safety.make_node_setup'),
    # Constructing the generic one (a call, a declaration, make_shared/unique).
    (re.compile(r'\bRobotRaconteurNodeSetup\b\s*(\w+\s*)?[({]'
                r'|make_(shared|unique)\s*<[^>]*RobotRaconteurNodeSetup\s*>'),
     'creates a node setup directly; use rr_test_safety.make_node_setup'),
    (re.compile(r'RobotRaconteur\.Client\b|from\s+RobotRaconteur\s+import\s+Client\b'),
     'imports RobotRaconteur.Client (default TCP + discovery)'),
    (re.compile(r'\b(EnableNodeAnnounce|EnableNodeDiscoveryListening|'
                r'FindServiceByType|FindNodeByName|FindNodeByID|'
                r'UpdateDetectedNodes|SubscribeServiceByType|SubscribeServiceInfo2)\b'),
     'uses node announce/discovery'),
]


def scanned_files():
    roots = [os.path.join(PACKAGE_DIR, 'test'), os.path.join(REPO_DIR, 'tools')]
    for root in roots:
        for dirpath, _, filenames in os.walk(root):
            for name in filenames:
                if name in HELPERS:
                    continue
                if name.endswith(('.cpp', '.hpp', '.h', '.py')):
                    yield os.path.join(dirpath, name)


def test_no_direct_node_setup_or_discovery():
    problems = []
    count = 0
    for path in scanned_files():
        count += 1
        with open(path, encoding='utf-8') as f:
            for lineno, line in enumerate(f, 1):
                for pattern, why in FORBIDDEN:
                    if pattern.search(line):
                        rel = os.path.relpath(path, REPO_DIR)
                        problems.append('%s:%d: %s' % (rel, lineno, why))
    assert count > 0, 'no test files found'
    assert not problems, '\n'.join(problems)


def test_the_checker_catches_violations():
    bad = [
        'auto s = RobotRaconteur::ServerNodeSetup(types, "x", 0);',
        'RobotRaconteur::ClientNodeSetup setup{types};',
        'from RobotRaconteur.Client import *',
        'node->EnableNodeAnnounce();',
        'RRN.FindServiceByType("x", ["rr+tcp"])',
        'auto s = std::make_shared<RR::RobotRaconteurNodeSetup>(node, types, "n", 0, f);',
        'RobotRaconteur::RobotRaconteurNodeSetup setup(node, types, "n", 0, f);',
        'setup = RR.RobotRaconteurNodeSetup("n", 0, flags)',
    ]
    for line in bad:
        assert any(p.search(line) for p, _ in FORBIDDEN), line
    assert not any(p.search(line) for p, _ in FORBIDDEN
                   for line in [
                       'rr_test_safety::make_node_setup(node, types, "n", flags);',
                       'std::unique_ptr<RR::RobotRaconteurNodeSetup> setup;',
                   ])
