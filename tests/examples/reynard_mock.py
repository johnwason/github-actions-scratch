# A headless stand-in for Reynard the Robot (the example service in RR core's
# examples/reynard_the_robot), serving its robdef with RR's Python binding. It records the calls
# it receives and prints them on exit, so the Lua example's test can check what the client did.
#
#     python reynard_mock.py <robdef path> <port> <log path>
import sys
import threading
import time

import numpy as np
import RobotRaconteur as RR

RRN = RR.RobotRaconteurNode.s


class Reynard:
    def __init__(self, log):
        self._log = log
        self._lock = threading.Lock()
        self._robot = np.zeros((2,))
        self._arm = np.zeros((3,))
        self._color = np.array([0.929, 0.49, 0.192])
        self._t0 = time.time()
        self.state = None  # set by RR (wire broadcaster below)
        self.new_message = RR.EventHook()

    def _record(self, *args):
        with self._lock:
            self._log.write(" ".join(str(a) for a in args) + "\n")
            self._log.flush()

    def RRServiceObjectInit(self, ctx, service_path):
        self._state_b = RR.WireBroadcaster(self.state)
        threading.Thread(target=self._publish, daemon=True).start()

    def _publish(self):
        state_type = RRN.GetStructureType("experimental.reynard_the_robot.ReynardState")
        while True:
            s = state_type()
            s.time = time.time() - self._t0
            s.robot_position = np.copy(self._robot)
            s.arm_position = np.copy(self._arm)
            s.robot_velocity = np.zeros((2,))
            s.arm_velocity = np.zeros((3,))
            self._state_b.OutValue = s
            time.sleep(0.05)

    def teleport(self, x, y):
        self._robot = np.array([x, y])
        self._record("teleport", x, y)

    def setf_arm_position(self, q1, q2, q3):
        self._arm = np.array([q1, q2, q3])
        self._record("setf_arm_position", round(q1, 4), round(q2, 4), round(q3, 4))

    def getf_arm_position(self):
        return self._arm

    @property
    def robot_position(self):
        return self._robot

    def drive_robot(self, vel_x, vel_y, timeout, wait):
        self._record("drive_robot", vel_x, vel_y, timeout, wait)

    def drive_arm(self, q1, q2, q3, timeout, wait):
        self._record("drive_arm", round(q1, 4), round(q2, 4), round(q3, 4), timeout, wait)

    def say(self, message):
        self._record("say", message)
        self.new_message.fire(message)

    @property
    def color(self):
        return self._color

    @color.setter
    def color(self, value):
        self._color = np.array(value)
        self._record("color", list(np.round(value, 3)))


def main():
    robdef_path, port, log_path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    with open(robdef_path) as f:
        robdef = f.read()
    with open(log_path, "w") as log, RR.ServerNodeSetup("experimental.reynard_mock", port):
        RRN.RegisterServiceType(robdef)
        RRN.RegisterService("reynard", "experimental.reynard_the_robot.Reynard", Reynard(log))
        print("READY", flush=True)
        while True:
            time.sleep(1)


if __name__ == "__main__":
    main()
