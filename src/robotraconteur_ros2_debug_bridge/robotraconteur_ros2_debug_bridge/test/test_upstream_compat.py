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
B2 compatibility: our definitions are a superset of the upstream bridge's.

The upstream bridge is robotraconteur-contrib/robotraconteur_ros2_bridge.
For every msg and srv of the B2 packages, the upstream bridge's generator
(ROSTypeAdapterManager, what its `msg`/`srv` modes run) registers its
definitions with the Robot Raconteur node singleton; nothing else of the
upstream bridge runs, and the node has no transports. Each upstream
definition must have a counterpart here with every struct, object and
member, member text identical, and every import.
"""

import os
import subprocess
import sys
import tempfile

import RobotRaconteur as RR

UPSTREAM_DIR = os.environ['UPSTREAM_BRIDGE_DIR']
GENERATE = os.environ['GENERATE_ROBDEF_EXE']
PACKAGES = ['action_tutorials_interfaces', 'example_interfaces', 'geometry_msgs',
            'nav_msgs', 'sensor_msgs', 'std_msgs', 'test_msgs', 'turtlesim']


def upstream_definitions():
    """Run upstream's generator; returns ({name: text}, {type: error})."""
    sys.path.insert(0, UPSTREAM_DIR)
    import robotraconteur_ros2_bridge as upstream  # noqa: E402
    from ament_index_python import get_resource

    manager = upstream.ROSTypeAdapterManager()
    failed = {}
    tried = 0
    for pkg in PACKAGES:
        content, _ = get_resource('rosidl_interfaces', pkg)
        for line in sorted(set(content.splitlines())):
            kind, _, file = line.partition('/')
            if not file.endswith('.idl') or kind not in ('msg', 'srv'):
                continue
            ros_type = pkg + '/' + file[:-len('.idl')]
            tried += 1
            try:
                if kind == 'msg':
                    manager.getMsgAdapter(ros_type)
                else:
                    manager.getSrvAdapter(ros_type)
            except Exception as e:  # upstream cannot bridge this type
                failed[ros_type] = '%s: %s' % (type(e).__name__, e)
    node = RR.RobotRaconteurNode.s
    defs = {}
    for name in node.GetRegisteredServiceTypes():
        if name != 'RobotRaconteurServiceIndex':
            defs[name] = node.GetServiceType(name).ToString()
    assert tried > 150
    return defs, failed


def our_definitions(extra_types=()):
    out = tempfile.mkdtemp()
    args = [GENERATE, '--out', out]
    for pkg in PACKAGES:
        args += ['--package', pkg]
    args += list(extra_types)
    subprocess.run(args, check=True)
    defs = {}
    for f in os.listdir(out):
        with open(os.path.join(out, f)) as fh:
            defs[f[:-len('.robdef')]] = fh.read()
    return defs


def parse(text):
    d = RR.ServiceDefinition()
    d.FromString(text)
    return d


def entries(d):
    out = {}
    for e in list(d.Structures) + list(d.Objects):
        out[e.Name] = {m.Name: m.ToString().strip() for m in e.Members}
    return out


def test_member_compatible_superset():
    upstream, failed = upstream_definitions()
    # Upstream also makes a topic definition for every nested message (here
    # generated on demand): generate those too.
    nested_topics = []
    for name in upstream:
        if name.startswith('rostopic_'):
            pkg, _, msg = name[len('rostopic_'):].partition('__')
            if pkg not in PACKAGES:
                nested_topics.append('%s/msg/%s' % (pkg, msg))
    ours = our_definitions(nested_topics)
    problems = []
    compared_members = 0
    for name, text in sorted(upstream.items()):
        if name not in ours:
            problems.append('%s: missing here' % name)
            continue
        u = parse(text)
        o = parse(ours[name])
        missing_imports = set(u.Imports) - set(o.Imports)
        if missing_imports:
            problems.append('%s: imports %s missing' % (name, sorted(missing_imports)))
        ue, oe = entries(u), entries(o)
        for entry, members in ue.items():
            if entry not in oe:
                problems.append('%s: %s missing' % (name, entry))
                continue
            for member, text_u in members.items():
                compared_members += 1
                text_o = oe[entry].get(member)
                if text_o != text_u:
                    problems.append('%s.%s.%s: upstream "%s", here "%s"'
                                    % (name, entry, member, text_u, text_o))
    print('compared %d upstream definitions, %d members'
          % (len(upstream), compared_members))
    print('types upstream cannot bridge (%d):' % len(failed))
    for t, why in sorted(failed.items()):
        print('  %s  (%s)' % (t, why))
    assert not problems, '\n'.join(problems)
    assert len(upstream) > 150
