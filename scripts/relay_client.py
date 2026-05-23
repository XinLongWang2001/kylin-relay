#!/usr/bin/env python3
"""
Kylin Relay Client - QClaw TCP client for controlling Kylin ARM64 machines.

Usage:
  python relay_client.py 192.168.1.7 exec "uname -a"
  python relay_client.py 192.168.1.7 status
  python relay_client.py 192.168.1.7 deploy --ssh-user d2000 --ssh-pass qilin@123
"""

import socket
import json
import time
import sys
import os
import subprocess
import argparse
import tempfile
import shutil
import uuid

DEFAULT_PORT = 12345
DEFAULT_TOKEN = "qclaw-relay"
DEFAULT_SSH_USER = "d2000"
DEFAULT_SSH_PASS = "qilin@123"
CONFIG_PATH = os.path.expanduser("~/.qclaw/kylin-relay.json")

# Path to pre-compiled ARM64 binary (bundled in skill)
_script_dir = os.path.dirname(os.path.abspath(__file__))
_skill_dir = os.path.dirname(_script_dir)
BUNDLED_BINARY = os.path.join(_skill_dir, "assets", "qt-relay-arm64")


# === Config management ===

def load_config():
    """Load saved config, or return defaults."""
    if os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH) as f:
            return json.load(f)
    return {"targets": {}}


def save_config(cfg):
    """Save config to disk."""
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    with open(CONFIG_PATH, "w") as f:
        json.dump(cfg, f, indent=2)


def get_target_config(host=None):
    """Get config for a host, merging defaults."""
    cfg = load_config()
    target = cfg.get("targets", {}).get(host or "default", {})
    return {
        "host": host or target.get("host", ""),
        "ssh_user": target.get("ssh_user", DEFAULT_SSH_USER),
        "ssh_pass": target.get("ssh_pass", DEFAULT_SSH_PASS),
        "port": target.get("port", DEFAULT_PORT),
        "token": target.get("token", DEFAULT_TOKEN),
    }


# === SSH helpers ===

def _ssh_env(ssh_pass):
    """Return environment dict with SSH password set up."""
    env = os.environ.copy()
    env["KREL_SSH_PASS"] = ssh_pass

    if sys.platform == "win32":
        # Use bundled askpass script
        askpass = os.path.join(_script_dir, "ssh_askpass.cmd")
        env["SSH_ASKPASS"] = askpass
    else:
        # Try sshpass first, fall back to Python helper
        if shutil.which("sshpass"):
            env["SSHPASS"] = ssh_pass
        else:
            askpass = os.path.join(_script_dir, "pw_helper.py")
            env["SSH_ASKPASS"] = askpass

    env["SSH_ASKPASS_REQUIRE"] = "force"
    env["DISPLAY"] = "dummy"
    return env


def run_ssh(host, ssh_user, ssh_pass, cmd, timeout=60):
    """Run a command on remote via SSH."""
    env = _ssh_env(ssh_pass)

    if shutil.which("sshpass") and sys.platform != "win32":
        ssh_cmd = ["sshpass", "-p", ssh_pass,
                   "ssh", "-o", "StrictHostKeyChecking=no",
                   f"{ssh_user}@{host}", cmd]
    else:
        ssh_cmd = ["ssh", "-o", "StrictHostKeyChecking=no",
                   "-o", "PasswordAuthentication=yes",
                   f"{ssh_user}@{host}", cmd]

    r = subprocess.run(ssh_cmd, capture_output=True, text=True,
                       timeout=timeout, env=env)
    return r.returncode, r.stdout, r.stderr


def scp_file(host, ssh_user, ssh_pass, local_path, remote_path, timeout=30):
    """SCP a file to remote."""
    env = _ssh_env(ssh_pass)

    if shutil.which("sshpass") and sys.platform != "win32":
        scp_cmd = ["sshpass", "-p", ssh_pass,
                   "scp", "-o", "StrictHostKeyChecking=no",
                   local_path, f"{ssh_user}@{host}:{remote_path}"]
    else:
        scp_cmd = ["scp", "-o", "StrictHostKeyChecking=no",
                   "-o", "PasswordAuthentication=yes",
                   local_path, f"{ssh_user}@{host}:{remote_path}"]

    r = subprocess.run(scp_cmd, capture_output=True, text=True,
                       timeout=timeout, env=env)
    return r.returncode, r.stdout, r.stderr


# === TCP Client ===

class KylinRelayClient:
    """TCP client that talks to qt-relay service on Kylin."""

    def __init__(self, host, port=DEFAULT_PORT, token=DEFAULT_TOKEN, timeout=10):
        self.host = host
        self.port = port
        self.token = token
        self.timeout = timeout
        self.sock = None
        self.reader = None

    def connect(self):
        """Connect and authenticate."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        self.reader = self.sock.makefile("r", encoding="utf-8")
        self._send({"type": "auth", "token": self.token})
        resp = self._recv()
        if resp.get("type") != "auth_ok":
            raise ConnectionError(f"Auth failed: {resp}")
        return self

    def exec(self, command, timeout=30):
        """Execute command, yield (type, data) tuples."""
        cmd_id = str(uuid.uuid4())[:8]
        self._send({"type": "exec", "id": cmd_id, "command": command, "timeout": timeout})
        exit_code = None
        deadline = time.time() + timeout + 5
        while time.time() < deadline:
            try:
                line = self.reader.readline()
            except socket.timeout:
                break
            if not line:
                break
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            t = msg.get("type")
            if t == "stdout":
                yield ("stdout", msg.get("data", ""))
            elif t == "stderr":
                yield ("stderr", msg.get("data", ""))
            elif t == "exit":
                exit_code = msg.get("code", -1)
                break
        yield ("exit", exit_code if exit_code is not None else -1)

    def exec_text(self, command, timeout=30):
        """Execute and return (stdout, stderr, exit_code)."""
        stdout_parts, stderr_parts, exit_code = [], [], -1
        for typ, data in self.exec(command, timeout):
            if typ == "stdout":
                stdout_parts.append(data)
            elif typ == "stderr":
                stderr_parts.append(data)
            elif typ == "exit":
                exit_code = data
        return "".join(stdout_parts), "".join(stderr_parts), exit_code

    def close(self):
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass

    def _send(self, obj):
        data = json.dumps(obj, ensure_ascii=False) + "\n"
        self.sock.sendall(data.encode("utf-8"))

    def _recv(self):
        line = self.reader.readline()
        if not line:
            raise ConnectionError("Connection closed")
        return json.loads(line)


# === CLI commands ===

def cmd_exec(args):
    """Execute a command on Kylin."""
    target = get_target_config(args.host)
    client = KylinRelayClient(args.host, target["port"], target["token"])
    try:
        client.connect()
        for typ, data in client.exec(args.command, args.timeout):
            if typ == "stdout":
                sys.stdout.write(data)
                sys.stdout.flush()
            elif typ == "stderr":
                sys.stderr.write(data)
                sys.stderr.flush()
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        client.close()


def cmd_status(args):
    """Check relay connection status."""
    target = get_target_config(args.host)
    client = KylinRelayClient(args.host, target["port"], target["token"], timeout=5)
    try:
        client.connect()
        stdout, _, _ = client.exec_text("uname -a && whoami && hostname", 5)
        lines = stdout.strip().splitlines()
        print("[OK] Kylin Relay online")
        print(f"  system:   {lines[0] if len(lines) > 0 else '?'}")
        print(f"  user:     {lines[1] if len(lines) > 1 else '?'}")
        print(f"  hostname: {lines[2] if len(lines) > 2 else '?'}")
    except Exception as e:
        print(f"[ERROR] Not connected: {e}")
        sys.exit(1)
    finally:
        client.close()


def cmd_deploy(args):
    """Deploy qt-relay to a Kylin machine. Uses pre-compiled binary if available."""
    host = args.host
    ssh_user = args.ssh_user
    ssh_pass = args.ssh_pass

    print(f"[DEPLOY] Deploying Qt Relay to {host}...")

    # Step 1: Check connectivity
    print("  [1/4] Checking SSH connectivity...")
    code, out, _ = run_ssh(host, ssh_user, ssh_pass, "echo OK && uname -m", timeout=10)
    if code != 0:
        print(f"  [FAIL] SSH failed. Check IP/user/password.")
        sys.exit(1)
    arch = out.strip().splitlines()[-1] if out.strip() else "unknown"
    print(f"  [OK] Connected, arch: {arch}")

    if arch != "aarch64":
        print(f"  [WARN] Expected aarch64, got {arch}. Continuing anyway...")

    # Step 2: Deploy binary
    print("  [2/4] Deploying binary...")

    if os.path.exists(BUNDLED_BINARY) and os.path.getsize(BUNDLED_BINARY) > 10000:
        # Use pre-compiled binary
        print("  Using pre-compiled ARM64 binary...")
        code, out, err = scp_file(host, ssh_user, ssh_pass,
                                  BUNDLED_BINARY,
                                  "/tmp/qt-relay-arm64",
                                  timeout=15)
        if code != 0:
            print(f"  [FAIL] SCP failed: {err}")
            sys.exit(1)
        print("  [OK] Binary transferred")

        # Move to proper location and chmod
        run_ssh(host, ssh_user, ssh_pass,
                "mkdir -p ~/qt-relay-project && "
                "mv /tmp/qt-relay-arm64 ~/qt-relay-project/qt-relay && "
                "chmod +x ~/qt-relay-project/qt-relay")
    else:
        # Fall back to source compilation
        print("  No pre-compiled binary, compiling from source...")
        source_dir = os.path.join(_skill_dir, "source")
        if not os.path.exists(os.path.join(source_dir, "qt-relay.pro")):
            print("  [FAIL] Source not found")
            sys.exit(1)

        run_ssh(host, ssh_user, ssh_pass, "mkdir -p ~/qt-relay-project/source/build")
        src_files = [f for f in os.listdir(source_dir)
                     if f.endswith(('.cpp', '.h', '.pro'))]
        for fname in src_files:
            code, out, err = scp_file(host, ssh_user, ssh_pass,
                                      os.path.join(source_dir, fname),
                                      f"~/qt-relay-project/source/{fname}")
            if code != 0:
                print(f"  [FAIL] SCP {fname}: {err}")
                sys.exit(1)

        code, out, err = run_ssh(host, ssh_user, ssh_pass,
                                 "cd ~/qt-relay-project/source/build && "
                                 "qmake ~/qt-relay-project/source/qt-relay.pro -o Makefile && "
                                 "make -j4 2>&1",
                                 timeout=120)
        if code != 0:
            print(f"  [FAIL] Build failed:\n{err}")
            sys.exit(1)
        run_ssh(host, ssh_user, ssh_pass,
                "cp ~/qt-relay-project/source/build/qt-relay ~/qt-relay-project/qt-relay && "
                "chmod +x ~/qt-relay-project/qt-relay")

    # Step 3: Check Qt libs
    print("  [3/4] Checking Qt libraries...")
    code, out, _ = run_ssh(host, ssh_user, ssh_pass,
                           "ldd ~/qt-relay-project/qt-relay 2>&1 | grep 'not found' | head -5",
                           timeout=10)
    if out.strip():
        print(f"  [WARN] Missing libs: {out.strip()}")
        print("  Attempting to install Qt5 libs...")
        code2, _, err2 = run_ssh(host, ssh_user, ssh_pass,
                                 "echo {0} | sudo -S apt-get install -y libqt5widgets5 libqt5network5 libqt5gui5 2>&1 | tail -3".format(ssh_pass),
                                 timeout=60)
    else:
        print("  [OK] All Qt libs available")

    # Step 4: Start service
    print("  [4/4] Starting service...")
    run_ssh(host, ssh_user, ssh_pass, "pkill -f qt-relay 2>/dev/null; sleep 1")
    run_ssh(host, ssh_user, ssh_pass,
            "nohup DISPLAY=:0 ~/qt-relay-project/qt-relay "
            "--bind 0.0.0.0 --token qclaw-relay "
            "&>/tmp/qt-relay.log &")
    time.sleep(3)

    code, out, _ = run_ssh(host, ssh_user, ssh_pass,
                           "ss -tlnp 2>/dev/null | grep 12345 || netstat -tlnp 2>/dev/null | grep 12345",
                           timeout=5)
    if code != 0:
        print("  [WARN] Port check failed, but service may still be starting...")
        code2, log, _ = run_ssh(host, ssh_user, ssh_pass,
                                "cat /tmp/qt-relay.log 2>/dev/null | tail -3")
        if log:
            print(f"  Log: {log}")
    else:
        print("  [OK] Service started, port 12345 listening")

    # Save config
    print("  Saving configuration...")
    cfg = load_config()
    cfg["targets"][host] = {
        "host": host,
        "ssh_user": ssh_user,
        "ssh_pass": ssh_pass,
        "port": DEFAULT_PORT,
        "token": DEFAULT_TOKEN,
    }
    cfg.setdefault("default", cfg["targets"][host])
    save_config(cfg)

    print(f"\n[DONE] {host}:12345 ready! Config saved to {CONFIG_PATH}")


# === Main ===

def main():
    parser = argparse.ArgumentParser(description="Kylin Relay Client")
    parser.add_argument("host", help="Kylin IP address")
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--token", default=None)
    parser.add_argument("--timeout", type=int, default=30)

    subparsers = parser.add_subparsers(dest="action", required=True)

    p_exec = subparsers.add_parser("exec", help="Execute a command")
    p_exec.add_argument("command", help="Shell command")
    p_exec.set_defaults(func=cmd_exec)

    p_status = subparsers.add_parser("status", help="Check connection")
    p_status.set_defaults(func=cmd_status)

    p_deploy = subparsers.add_parser("deploy", help="Deploy to new Kylin")
    p_deploy.add_argument("--ssh-user", default=None)
    p_deploy.add_argument("--ssh-pass", default=None)
    p_deploy.set_defaults(func=cmd_deploy)

    args = parser.parse_args()

    # Merge CLI args with saved config
    if hasattr(args, 'ssh_user') and args.ssh_user is None:
        target = get_target_config(args.host)
        args.ssh_user = target["ssh_user"]
    if hasattr(args, 'ssh_pass') and args.ssh_pass is None:
        target = get_target_config(args.host)
        args.ssh_pass = target["ssh_pass"]

    args.func(args)


if __name__ == "__main__":
    main()
