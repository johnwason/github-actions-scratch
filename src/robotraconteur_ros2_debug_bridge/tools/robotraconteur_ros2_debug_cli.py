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

"""
Debug ROS 2 through robotraconteur_ros2_debug_bridge, like the ros2 CLI.

    robotraconteur_ros2_debug_cli.py URL graph
    robotraconteur_ros2_debug_cli.py URL echo TOPIC [--type T] [--wire|--pipe] [-n N]
    robotraconteur_ros2_debug_cli.py URL pub TOPIC TYPE JSON [--via function|pipe|wire]
    robotraconteur_ros2_debug_cli.py URL call SERVICE TYPE JSON
    robotraconteur_ros2_debug_cli.py URL param list|get|set|describe NODE [...]
    robotraconteur_ros2_debug_cli.py URL action ACTION TYPE JSON [--cancel-after N]
    robotraconteur_ros2_debug_cli.py URL load CONTAINER PACKAGE PLUGIN [--name N]
    robotraconteur_ros2_debug_cli.py URL unload CONTAINER ID
    robotraconteur_ros2_debug_cli.py URL components CONTAINER

URL is the bridge's, for example
rr+local:///?nodename=ros2_debug_bridge&service=ros2_debug_bridge (same
machine and user) or rr+tcp://host:34573/?service=ros2_debug_bridge. Only
the transport the URL needs is started; the tool never runs node discovery
or announces itself. Messages are JSON, in and out.
"""

import argparse
import json
import sys
import time

import numpy as np
import RobotRaconteur as RR

NUMPY = {
    'double': np.float64, 'single': np.float32, 'int8': np.int8, 'uint8': np.uint8,
    'int16': np.int16, 'uint16': np.uint16, 'int32': np.int32, 'uint32': np.uint32,
    'int64': np.int64, 'uint64': np.uint64, 'bool': np.bool_,
}
# rcl_interfaces/msg/ParameterType
PARAM_FIELDS = {1: 'bool_value', 2: 'integer_value', 3: 'double_value', 4: 'string_value',
                5: 'byte_array_value', 6: 'bool_array_value', 7: 'integer_array_value',
                8: 'double_array_value', 9: 'string_array_value'}


class Bridge:
    """A connection to the bridge; only the URL's transport, no discovery."""

    def __init__(self, url, timeout):
        self.node = RR.RobotRaconteurNode()
        self.node.Init()
        if url.startswith('rr+local:'):
            self.node.RegisterTransport(RR.LocalTransport(self.node))
        elif url.startswith(('rr+tcp:', 'rr+ws:', 'rrs+tcp:', 'rrs+ws:')):
            self.node.RegisterTransport(RR.TcpTransport(self.node))
        else:
            raise SystemExit('unsupported URL scheme: ' + url)
        self.node.RequestTimeout = timeout
        self.root = self.node.ConnectService(url)

    def close(self):
        try:
            self.node.DisconnectService(self.root)
        finally:
            self.node.Shutdown()

    # ----- JSON <-> Robot Raconteur values (using the pulled definitions)

    def _struct(self, obj, qualified):
        definition, _, name = qualified.rpartition('.')
        sdef = self.node.GetPulledServiceType(obj, definition)
        for e in sdef.Structures:
            if e.Name == name:
                return sdef, e
        raise ValueError('no structure ' + qualified)

    def _member_types(self, entry):
        out = []
        for m in entry.Members:
            # A structure's fields print as "property <type> <name>" (or
            # "field ..." in the definition text).
            text = m.ToString().strip()
            kind, _, rest = text.partition(' ')
            if kind not in ('field', 'property'):
                continue
            t = RR.TypeDefinition()
            t.FromString(rest)
            out.append(t)
        return out

    def _qualify(self, sdef, type_string):
        return type_string if '.' in type_string else sdef.Name + '.' + type_string

    def from_json(self, obj, qualified, value):
        """Build a structure of type `qualified` from a dict."""
        sdef, entry = self._struct(obj, qualified)
        s = self.node.NewStructure(qualified, obj)
        for t in self._member_types(entry):
            if t.Name not in value:
                continue
            v = value[t.Name]
            nested = (self._qualify(sdef, t.TypeString)
                      if t.Type == RR.DataTypes_namedtype_t else None)
            if t.ContainerType == RR.DataTypes_ContainerTypes_list:
                v = [self.from_json(obj, nested, i) if nested else i for i in v]
            elif nested:
                v = self.from_json(obj, nested, v)
            elif t.Type == RR.DataTypes_string_t:
                v = str(v)
            elif t.ArrayType != RR.DataTypes_ArrayTypes_none:
                v = np.array(v, dtype=NUMPY[RR.TypeDefinition.StringFromDataType(t.Type)])
            setattr(s, t.Name, v)
        return s


def to_json(value):
    """A plain, JSON-able form of a Robot Raconteur value."""
    if value is None or isinstance(value, (str, bool, int, float)):
        return value
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, (list, tuple)):
        return [to_json(v) for v in value]
    if isinstance(value, dict):
        return {k: to_json(v) for k, v in value.items()}
    # A structure: its public, non-callable attributes (RR's Python
    # structures use slots, not a __dict__).
    fields = {}
    for k in dir(value):
        if k.startswith('_'):
            continue
        v = getattr(value, k)
        if not callable(v):
            fields[k] = to_json(v)
    return fields


def emit(value):
    print(json.dumps(to_json(value), sort_keys=True), flush=True)


def ros_type(t):
    return t.replace('/msg/', '/').replace('/srv/', '/').replace('/action/', '/')


def def_name(prefix, t):
    pkg, _, name = ros_type(t).partition('/')
    return '%s_%s__%s' % (prefix, pkg, name), name


# ----- subcommands

def cmd_graph(b, args):
    emit(b.root.graph)


def cmd_echo(b, args):
    sub = b.root.get_subscribers(b.root.subscribe(args.topic, args.type or ''))
    try:
        if args.wire:
            w = sub.subscriberwire.Connect()
            last = None  # (seconds, nanoseconds) of the last value shown
            got = 0
            deadline = time.monotonic() + args.timeout
            while got < args.count and time.monotonic() < deadline:
                t = w.LastValueReceivedTime if w.InValueValid else None
                stamp = (t.seconds, t.nanoseconds) if t is not None else None
                if stamp is not None and stamp != last:
                    last = stamp
                    emit(w.InValue)
                    got += 1
                time.sleep(0.01)
            w.Close()
        else:
            ep = sub.subscriberpipe.Connect(-1)
            got = 0
            deadline = time.monotonic() + args.timeout
            while got < args.count and time.monotonic() < deadline:
                while ep.Available > 0 and got < args.count:
                    emit(ep.ReceivePacket())
                    got += 1
                time.sleep(0.01)
            ep.Close()
        if got < args.count:
            raise SystemExit('timed out after %d message(s)' % got)
    finally:
        sub.unsubscribe()


def cmd_pub(b, args):
    pub = b.root.get_publishers(b.root.publish(args.topic, args.type))
    defn, name = def_name('rosmsg', args.type)
    msg = b.from_json(pub, defn + '.' + name, json.loads(args.json))
    if args.via == 'pipe':
        ep = pub.publisherpipe.Connect(-1)
    elif args.via == 'wire':
        w = pub.publisherwire.Connect()
    for i in range(args.count):
        if args.via == 'function':
            pub.publish(msg)
        elif args.via == 'pipe':
            ep.SendPacket(msg)
        else:
            w.OutValue = msg
        if i + 1 < args.count:
            time.sleep(1.0 / args.rate)
    time.sleep(0.2)  # let the last one go before disconnecting
    print('published %d' % args.count, flush=True)


def cmd_call(b, args):
    cl = b.root.get_clients(b.root.client(args.service, args.type))
    if not cl.wait_for_service(args.timeout):
        raise SystemExit('service not available: ' + args.service)
    defn, name = def_name('rosservice', args.type)
    emit(cl.call(b.from_json(cl, '%s.%sRequest' % (defn, name), json.loads(args.json))))


def parameter_value(b, p, text):
    """A ParameterValue from JSON text (type from the JSON value)."""
    value = json.loads(text)
    v = b.node.NewStructure('rosmsg_rcl_interfaces__ParameterValue.ParameterValue', p)
    if isinstance(value, bool):
        v.type, v.bool_value = 1, value
    elif isinstance(value, int):
        v.type, v.integer_value = 2, value
    elif isinstance(value, float):
        v.type, v.double_value = 3, value
    elif isinstance(value, str):
        v.type, v.string_value = 4, value
    elif isinstance(value, list) and value and all(isinstance(x, bool) for x in value):
        v.type, v.bool_array_value = 6, np.array(value, dtype=np.bool_)
    elif isinstance(value, list) and all(isinstance(x, int) for x in value):
        v.type, v.integer_array_value = 7, np.array(value, dtype=np.int64)
    elif isinstance(value, list) and all(isinstance(x, (int, float)) for x in value):
        v.type, v.double_array_value = 8, np.array(value, dtype=np.float64)
    elif isinstance(value, list):
        v.type, v.string_array_value = 9, [str(x) for x in value]
    else:
        raise SystemExit('unsupported parameter value: ' + text)
    return v


def plain_value(v):
    field = PARAM_FIELDS.get(v.type)
    return to_json(getattr(v, field)) if field else None


def cmd_param(b, args):
    p = b.root.get_parameters()
    if args.op == 'list':
        emit(sorted(p.list(args.node, [], 0).names))
    elif args.op == 'get':
        values = p.get(args.node, args.names)
        emit({n: plain_value(v) for n, v in zip(args.names, values)})
    elif args.op == 'describe':
        emit(p.describe(args.node, args.names))
    elif args.op == 'set':
        if len(args.names) != 2:
            raise SystemExit('param set NODE NAME VALUE')
        m = b.node.NewStructure('rosmsg_rcl_interfaces__Parameter.Parameter', p)
        m.name = args.names[0]
        m.value = parameter_value(b, p, args.names[1])
        r = p.set(args.node, [m])[0]
        emit(r)
        if not r.successful:
            raise SystemExit(1)


def cmd_action(b, args):
    ac = b.root.get_action_clients(b.root.action_client(args.action, args.type))
    if not ac.wait_for_server(args.timeout):
        raise SystemExit('action server not available: ' + args.action)
    defn, name = def_name('rosaction', args.type)
    gen = ac.send_goal(b.from_json(ac, '%s.%sGoal' % (defn, name), json.loads(args.json)))
    feedback = 0
    while True:
        try:
            s = gen.Next()
        except Exception as e:
            if 'StopIteration' in type(e).__name__:
                break
            raise
        emit(s)
        if s.feedback is not None:
            feedback += 1
            if args.cancel_after and feedback >= args.cancel_after:
                gen.Abort()
                print('canceled', flush=True)
                break


def cmd_load(b, args):
    c = b.root.get_components()
    r = b.node.NewStructure(
        'rosservice_composition_interfaces__LoadNode.LoadNodeRequest', c)
    r.package_name = args.package
    r.plugin_name = args.plugin
    r.node_name = args.name or ''
    res = c.load(args.container, r)
    emit(res)
    if not res.success:
        raise SystemExit(1)


def cmd_unload(b, args):
    res = b.root.get_components().unload(args.container, args.id)
    emit(res)
    if not res.success:
        raise SystemExit(1)


def cmd_components(b, args):
    emit(b.root.get_components().list(args.container))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('url')
    ap.add_argument('--timeout', type=float, default=10.0, help='seconds (default 10)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('graph')
    p = sub.add_parser('echo')
    p.add_argument('topic')
    p.add_argument('--type', default='')
    g = p.add_mutually_exclusive_group()
    g.add_argument('--wire', action='store_true', help='latest values (default: --pipe)')
    g.add_argument('--pipe', action='store_true', help='every message')
    p.add_argument('-n', '--count', type=int, default=1)
    p = sub.add_parser('pub')
    p.add_argument('topic')
    p.add_argument('type')
    p.add_argument('json')
    p.add_argument('--via', choices=['function', 'pipe', 'wire'], default='function')
    p.add_argument('-n', '--count', type=int, default=1)
    p.add_argument('-r', '--rate', type=float, default=10.0)
    p = sub.add_parser('call')
    p.add_argument('service')
    p.add_argument('type')
    p.add_argument('json')
    p = sub.add_parser('param')
    p.add_argument('op', choices=['list', 'get', 'set', 'describe'])
    p.add_argument('node')
    p.add_argument('names', nargs='*')
    p = sub.add_parser('action')
    p.add_argument('action')
    p.add_argument('type')
    p.add_argument('json')
    p.add_argument('--cancel-after', type=int, default=0, help='feedback messages')
    p = sub.add_parser('load')
    p.add_argument('container')
    p.add_argument('package')
    p.add_argument('plugin')
    p.add_argument('--name', default='')
    p = sub.add_parser('unload')
    p.add_argument('container')
    p.add_argument('id', type=int)
    p = sub.add_parser('components')
    p.add_argument('container')
    args = ap.parse_args(argv)
    b = Bridge(args.url, int(args.timeout * 1000))
    try:
        globals()['cmd_' + args.cmd](b, args)
    finally:
        b.close()


if __name__ == '__main__':
    sys.exit(main())
