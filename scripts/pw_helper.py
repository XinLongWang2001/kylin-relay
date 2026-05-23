#!/usr/bin/env python3
"""SSH password helper - prints password when SSH_ASKPASS calls it."""
import os, sys

def main():
    pw = os.environ.get("KREL_SSH_PASS", "")
    if not pw:
        # Try reading from config
        config_path = os.path.expanduser("~/.qclaw/kylin-relay.json")
        if os.path.exists(config_path):
            import json
            with open(config_path) as f:
                cfg = json.load(f)
            target = cfg.get("targets", {}).get("default", {})
            pw = target.get("ssh_pass", "")
    print(pw)
    sys.exit(0)

if __name__ == "__main__":
    main()
