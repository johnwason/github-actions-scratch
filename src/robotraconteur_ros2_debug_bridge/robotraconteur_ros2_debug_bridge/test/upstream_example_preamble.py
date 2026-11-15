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
Safe stand-in for the upstream examples' client import.

The upstream bridge's examples start with a star import of Robot
Raconteur's client module, which sets up the node with default flags (TCP
and discovery). Tests must not do that (CLAUDE.md S2), so
test_upstream_examples.py runs the examples with that line replaced by a
star import of this module: the same names (RR, RRN), and the node set up
through the safety helper with local transports only.
"""

import atexit
import os
import sys

import RobotRaconteur as RR

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr_test_safety  # noqa: E402

RRN = RR.RobotRaconteurNode.s
_setup = rr_test_safety.make_node_setup(
    'upstream_example_%d' % os.getpid(), rr_test_safety.client_flags(), node=RRN)
atexit.register(_setup.close)

__all__ = ['RR', 'RRN']
