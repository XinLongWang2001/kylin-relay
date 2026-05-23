---
name: kylin-relay
description: "Remote control Kylin/Feiteng ARM64 Linux computers from QClaw via TCP relay. Single command deploy + instant reconnect. Use when: (1) user wants to connect to or control a remote Kylin/ARM64 machine, (2) deploy relay to a new Kylin target, (3) execute shell commands on a remote ARM64 Linux, (4) manage multiple Kylin machines. Triggers: 连接麒麟, 操控麒麟, 部署麒麟, deploy to Kylin, ARM64 remote, Kylin relay."
---

# Kylin Relay Skill

## What This Skill Does

Lets QClaw remotely control any Kylin/Feiteng ARM64 Linux computer.
From a new Windows machine, one command deploys everything. From then on, instant reconnect.

`
QClaw (Windows)          TCP:12345 (JSON)          Kylin ARM64
  relay_client.py  ──── exec/status ────────→  qt-relay (GUI)
                   ←── stdout/stderr/exit ────  QProcess → bash
`

## Quick Reference For The Agent

### Already deployed - just connect and use:
`
python scripts/relay_client.py <IP> status
python scripts/relay_client.py <IP> exec "<command>"
`

### First time on a new Kylin - one deploy command:
`
python scripts/relay_client.py <IP> deploy --ssh-user <user> --ssh-pass <password>
`

Credentials are saved to ~/.qclaw/kylin-relay.json automatically.
After first deploy, just use status and exec without credentials.

### Multi-machine:
`
python scripts/relay_client.py 192.168.1.7 status
python scripts/relay_client.py 192.168.1.7 exec "df -h"
python scripts/relay_client.py 192.168.1.100 status
python scripts/relay_client.py 192.168.1.100 exec "uname -a"
`

## Setup On A New Windows Machine

1. Copy the entire kylin-relay directory to ~/.qclaw/skills/
2. That's it. The skill is self-contained:
   - Pre-compiled ARM64 binary in ssets/qt-relay-arm64 (194KB)
   - Source code in source/ (fallback if binary incompatible)
   - SSH helpers in scripts/ (cross-platform)

## First Deployment To A New Kylin

Step by step what happens:
`
User: "部署麒麟 192.168.1.100"

Agent calls:
  python scripts/relay_client.py 192.168.1.100 deploy --ssh-user root --ssh-pass MyPass123

The deploy command:
  [1/4] SSH to Kylin, verify connectivity and architecture
  [2/4] SCP the pre-compiled ARM64 binary (~/qt-relay-project/qt-relay)
  [3/4] Check Qt5 libraries (auto-install if missing)
  [4/4] Start service: nohup DISPLAY=:0 ./qt-relay --bind 0.0.0.0 &
  → Save credentials to ~/.qclaw/kylin-relay.json
`

Time: ~5 seconds (binary transfer). No compilation needed.

## Daily Usage

`
User: "连麒麟看看状态"
Agent: python scripts/relay_client.py 192.168.1.7 status
→ [OK] Kylin Relay online / system / user / hostname

User: "麒麟上看看磁盘"
Agent: python scripts/relay_client.py 192.168.1.7 exec "df -h"

User: "麒麟安装nginx"
Agent: python scripts/relay_client.py 192.168.1.7 exec "sudo apt install nginx -y"
`

## Config File

~/.qclaw/kylin-relay.json:
`json
{
  "targets": {
    "192.168.1.7": {
      "host": "192.168.1.7",
      "ssh_user": "d2000",
      "ssh_pass": "qilin@123",
      "port": 12345,
      "token": "qclaw-relay"
    },
    "192.168.1.100": {
      "host": "192.168.1.100",
      "ssh_user": "root",
      "ssh_pass": "MyPass",
      "port": 12345,
      "token": "qclaw-relay"
    }
  },
  "default": "192.168.1.7"
}
`

Agent should write to this file when deploying or when user provides new credentials.

## Files In This Skill

| Path | Size | Purpose |
|------|------|---------|
| assets/qt-relay-arm64 | 195KB | Pre-compiled ARM64 binary (ready to run) |
| scripts/relay_client.py | 15KB | Main client: status, exec, deploy |
| scripts/config.template.json | 0.2KB | Config template |
| scripts/pw_helper.py | 0.6KB | SSH password helper (Linux/macOS) |
| scripts/ssh_askpass.cmd | 0.03KB | SSH password helper (Windows) |
| source/ | 22KB | Qt5.12+ source (fallback compilation) |
| SKILL.md | this file | Skill description |

## Requirements

### QClaw side (Windows):
- Python 3
- SSH client (built into Windows 10+)
- No other dependencies

### Kylin side:
- SSH server (openssh-server)
- Qt5 libraries: libqt5widgets5, libqt5network5, libqt5gui5
  (deploy step [3/4] auto-installs these via apt if missing)
- X11 display (:0) for the GUI (service runs headless if no display)

## Protocol

TCP connection to port 12345, JSON messages separated by newline:

`
Client → {"type":"auth","token":"qclaw-relay"}
Server ← {"type":"auth_ok"}

Client → {"type":"exec","id":"abc123","command":"uname -a","timeout":30}
Server ← {"id":"abc123","type":"stdout","data":"Linux ...\n"}
Server ← {"id":"abc123","type":"exit","code":0}
`

## Troubleshooting

- **"Not connected"**: qt-relay may have stopped. SSH in and restart: ~/qt-relay-project/qt-relay --bind 0.0.0.0 &
- **"Auth failed"**: Token mismatch. Check config or restart with --token qclaw-relay
- **Deploy fails**: Check SSH connectivity first. Qt5 libs auto-install, but may need manual pt install if offline.
- **Missing display**: If DISPLAY=:0 fails, service still starts but GUI won't show. Commands still work.
- **Need to change password**: Edit ~/.qclaw/kylin-relay.json directly, or re-run deploy with new credentials.
