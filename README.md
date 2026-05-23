# kylin-relay

One-click remote control of Kylin/Feiteng ARM64 Linux computers via TCP relay.

## What it does

Deploy a lightweight TCP relay service to any Kylin ARM64 machine, then control it from QClaw — execute shell commands, check system status, install software, all without SSH after initial setup.

## Quick Start

`ash
# First deploy to a new Kylin machine (SSH required once)
python scripts/relay_client.py 192.168.1.100 deploy --ssh-user root --ssh-pass xxx

# From then on — instant connect
python scripts/relay_client.py 192.168.1.100 status
python scripts/relay_client.py 192.168.1.100 exec "df -h"
`

## Install as QClaw Skill

`ash
# Copy to skills directory
cp -r kylin-relay ~/.qclaw/skills/

# Then in QClaw, just say "连接麒麟"
`

## Architecture

`
QClaw (Windows/macOS)          TCP:12345 (JSON)          Kylin ARM64
  relay_client.py   ----- exec / status ------>   qt-relay (GUI + QProcess)
                    <---- stdout / stderr -------           |
                                                        /bin/sh -c <cmd>
`

## Files

| Path | Purpose |
|------|---------|
| ssets/qt-relay-arm64 | Pre-compiled ARM64 binary (195KB) |
| scripts/relay_client.py | Main client: status, exec, deploy |
| source/ | Qt 5.12+ source code (fallback compilation) |
| SKILL.md | QClaw skill manifest |

## Requirements

- **Client**: Python 3, SSH client (built into Windows 10+)
- **Target**: Kylin/ARM64 Linux, SSH server, Qt5 libs (auto-installed by deploy)
