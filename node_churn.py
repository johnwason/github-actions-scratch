# Stress node creation, connection and teardown in one Python process, to look for native crashes in the
# Python suite's shared-process pattern (many RobotRaconteurNode objects created and shut down in one process).
# Usage: python3 node_churn.py [batches] [pairs_per_batch]
import RobotRaconteur as RR
import random
import socket
import sys
import time

_robdef = """
service experimental.node_churn

object testobj
    function int32 add(int32 a, int32 b)
    pipe int32 broadcastpipe [readonly]
    wire int32 broadcastwire [readonly]
end
"""


class _testobj_impl(object):
    # Same pattern as test/python/RobotRaconteurTest/ServiceTest.py: two 25 ms timers with Python callbacks that
    # broadcast on a pipe and a wire, started in RRServiceObjectInit and never stopped
    def __init__(self):
        self._pipe_b = None
        self._wire_b = None

    def add(self, a, b):
        return a + b

    @property
    def broadcastpipe(self):
        return self._pipe_b

    @broadcastpipe.setter
    def broadcastpipe(self, value):
        self._pipe_b = RR.PipeBroadcaster(value, 3)

    @property
    def broadcastwire(self):
        return self._wire_b

    @broadcastwire.setter
    def broadcastwire(self, value):
        self._wire_b = RR.WireBroadcaster(value)

    def RRServiceObjectInit(self, ctx, service_path):
        node = ctx.GetNode()

        def timer_func(ev):
            try:
                for i in range(100):
                    self._pipe_b.AsyncSendPacket(i, lambda: None)
            except:
                pass
        t = node.CreateTimer(.025, timer_func, oneshot=False)
        t.Start()

        def timer_func2(ev):
            try:
                for i in range(100):
                    self._wire_b.OutValue = i
            except:
                pass
        t2 = node.CreateTimer(.025, timer_func2, oneshot=False)
        t2.Start()


def ws_handshake(port, origin):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    try:
        req = ("GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
               "Sec-WebSocket-Protocol: robotraconteur.robotraconteur.com\r\nOrigin: %s\r\n\r\n") % (port, origin)
        s.sendall(req.encode("ascii"))
        resp = b""
        while b"\r\n" not in resp:
            d = s.recv(4096)
            if not d:
                break
            resp += d
        return resp.split(b"\r\n")[0]
    finally:
        s.close()


def make_server():
    node = RR.RobotRaconteurNode()
    node.Init()
    t = RR.TcpTransport(node)
    t.StartServer(0, True)
    node.RegisterTransport(t)
    node.RegisterServiceType(_robdef)
    node.RegisterService("svc", "experimental.node_churn.testobj", _testobj_impl())
    return node, t.GetListenPort()


def make_client():
    node = RR.RobotRaconteurNode()
    node.Init()
    t = RR.TcpTransport(node)
    node.RegisterTransport(t)
    return node


def batch(n, rnd):
    servers = [make_server() for _ in range(n)]
    clients = [make_client() for _ in range(n)]
    objs = []
    for (snode, port), cnode in zip(servers, clients):
        o = cnode.ConnectService("rr+tcp://127.0.0.1:%d?service=svc" % port)
        assert o.add(2, 3) == 5
        p = o.broadcastpipe.Connect(-1)
        w = o.broadcastwire.Connect()
        objs.append((cnode, o, p, w))
    for (snode, port) in servers:
        for origin in ("https://robotraconteur.com", "chrome-extension://x"):
            ws_handshake(port, origin)
    # Disconnect some clients cleanly, leave the rest connected when the nodes shut down
    # Let the broadcast timers run against the connected endpoints
    time.sleep(0.2)
    for cnode, o, p, w in objs:
        if rnd.random() < 0.5:
            cnode.DisconnectService(o)
    allnodes = [s for s, _ in servers] + clients
    rnd.shuffle(allnodes)
    for node in allnodes:
        node.Shutdown()


def main():
    batches = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    pairs = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    rnd = random.Random(1)
    for b in range(batches):
        t0 = time.time()
        batch(pairs, rnd)
        print("batch %d: %d pairs in %.1f s" % (b + 1, pairs, time.time() - t0), flush=True)
    print("done", flush=True)


if __name__ == "__main__":
    main()
