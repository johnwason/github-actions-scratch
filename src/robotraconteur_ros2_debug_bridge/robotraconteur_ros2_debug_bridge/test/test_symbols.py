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
The component library exports its public API and none of Robot Raconteur.

Robot Raconteur stays behind the library boundary: if the static Robot
Raconteur core's symbols were exported, another copy in the process
(an executable or another component) would share its globals by symbol
interposition and destroy them twice at unload or exit.
"""

import os
import subprocess

LIBRARY = os.environ['COMPONENT_LIBRARY']


def exported_symbols():
    """Return the exported names: demangled (nm) on Linux, decorated (dumpbin) on Windows."""
    if os.name == 'nt':
        out = subprocess.run(
            ['dumpbin', '/nologo', '/exports', LIBRARY], check=True, capture_output=True,
            text=True).stdout
        # Export table rows: ordinal hint RVA name
        return [f[3] for f in (line.split() for line in out.splitlines())
                if len(f) >= 4 and f[0].isdigit()]
    out = subprocess.run(
        ['nm', '-D', '--defined-only', '-C', LIBRARY], check=True, capture_output=True,
        text=True).stdout
    return [line.split(' ', 2)[2] for line in out.splitlines() if line.count(' ') >= 2]


def in_namespace(symbol, namespace):
    """Tell whether a (demangled or MSVC-decorated) symbol is in the namespace."""
    if namespace + '::' in symbol:
        return True
    # MSVC decoration lists the scopes innermost first: ?name@inner@outer@@...
    return ('@' + '@'.join(reversed(namespace.split('::'))) + '@') in symbol


def test_no_robotraconteur_exports():
    # Some Boost symbols stay exported on Linux, by Boost's design: Asio's
    # header-only statics (service ids, call stacks) are STB_GNU_UNIQUE, which
    # --exclude-libs cannot hide and which is safe (one definition per
    # process, initialized and destroyed once), and Boost.System marks its
    # error category classes visible so categories and exceptions work across
    # libraries. The process already shares the system Boost libraries. The
    # hazard is Robot Raconteur's own globals, which each copy would
    # initialize and destroy separately. A Windows DLL exports only what is
    # marked dllexport.
    symbols = exported_symbols()
    assert symbols, 'no exported symbols found'
    leaked = [s for s in symbols if in_namespace(s, 'RobotRaconteur')
              or in_namespace(s, 'experimental::ros2_debug_bridge')]
    assert not leaked, '\n'.join(leaked[:20])


def test_public_api_is_exported():
    symbols = exported_symbols()
    assert any(in_namespace(s, 'robotraconteur_ros2_debug_bridge') and 'make_bridge_node' in s
               for s in symbols), '\n'.join(symbols[:50])
