# Python RR client (RR core's own Python binding) exercising every member kind of the Lua
# service in test_python_client.lua. Prints "ok <check>" lines; exits non-zero on failure.
import sys
import time

from RobotRaconteur.Client import *  # noqa: F401,F403 (sets up RRN and the client transports)
import RobotRaconteur as RR

RRN = RR.RobotRaconteurNode.s


def wait_for(cond, what, timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return
        time.sleep(0.01)
    raise AssertionError("timed out waiting for " + what)


def main():
    port = int(sys.argv[1])
    c = RRN.ConnectService("rr+tcp://localhost:%d?service=svc" % port)

    # service attributes, set by the Lua service with svc:setAttributes
    a = RRN.GetServiceAttributes(c)
    assert a["description"].datatype == "string" and a["description"].data == "lua service", a
    assert a["version"].datatype == "int32[]" and list(a["version"].data) == [3], a

    # properties
    assert c.speed == 1.5
    c.speed = 2.25
    assert c.speed == 2.25
    assert c.name == "lua service"
    p = RRN.NewStructure("experimental.lua_server_test.Point", c)
    p.x = 1.0
    p.y = 2.0
    c.pos = p
    assert c.pos.x == 1.0 and c.pos.y == 2.0
    try:
        c.speed = -1.0
        raise AssertionError("expected an error")
    except RR.OperationFailedException as e:
        assert "speed must not be negative" in str(e), str(e)
    print("ok properties")

    # functions
    assert c.add(2.0, 3.5) == 5.5
    a = RRN.NewStructure("experimental.lua_server_test.Point", c)
    a.x, a.y = 0.0, 0.0
    b = RRN.NewStructure("experimental.lua_server_test.Point", c)
    b.x, b.y = 2.0, 4.0
    m = c.midpoint(a, b)
    assert m.x == 1.0 and m.y == 2.0
    try:
        c.fail("boom")
        raise AssertionError("expected an error")
    except RR.OperationFailedException as e:
        assert "boom" in str(e)
    print("ok functions")

    # events
    got = []
    c.moved += lambda pt, tm: got.append((pt.x, pt.y, tm))
    c.ping()
    wait_for(lambda: len(got) > 0, "the event")
    assert got[0] == (3.0, 4.0, 0.5), got
    print("ok events")

    # objrefs
    assert c.get_child().id == 0
    assert c.get_kids(7).hello("py") == "hello py from 7"
    print("ok objrefs")

    # generators
    assert list(c.count(3).NextAll()) == [1, 2, 3]
    g = c.running_sum()
    assert g.Next(1.0) == 1.0 and g.Next(2.5) == 3.5
    g.Close()
    print("ok generators")

    # memories (served by the core from the Lua service's native arrays)
    import numpy as np
    assert c.buf.Length == 10
    c.buf.Write(2, np.array([1.5, 2.5]), 0, 2)
    r = np.zeros(10)
    c.buf.Read(0, r, 0, 10)
    assert r[3] == 2.5
    assert list(c.grid.Dimensions) == [3, 4]
    c.grid.Write([0, 0], np.array([[5, 6], [7, 8]], dtype=np.int32), [0, 0], [2, 2])
    print("ok memories")

    # wires: the broadcaster (updated by the Lua service every step) and the receiver
    w = c.state.Connect()
    wait_for(lambda: w.InValueValid, "the broadcast value")
    assert len(w.InValue) == 3
    cw = c.command.Connect()
    cw.OutValue = [7.0, 8.0]
    print("ok wires")

    # pipes: the Lua service echoes each packet back doubled
    e = c.frames.Connect(-1)
    e.SendPacket([9.0, 10.0])
    wait_for(lambda: e.Available > 0, "the echoed packet")
    assert list(e.ReceivePacket()) == [18.0, 20.0]
    print("ok pipes")

    # tell the service we are done and what the receiver should hold
    c.speed = 99.0
    time.sleep(0.2)
    RRN.DisconnectService(c)
    print("ok done")


if __name__ == "__main__":
    try:
        main()
    except Exception as ex:  # report and fail
        import traceback

        traceback.print_exc()
        print("FAIL", ex)
        sys.exit(1)
    finally:
        RRN.Shutdown()
