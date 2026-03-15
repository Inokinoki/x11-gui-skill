# Claude Code Skills for GUI Automation

Collection of Claude Code skills for Linux GUI automation and testing.

## Skills

| Skill | Description |
|-------|-------------|
| [xvfb-gui-loop](xvfb-gui-loop/) | Xvfb GUI automation - capture screenshots, simulate input |

## Installation

### Install Dependencies

```bash
# Required for all skills
sudo apt install xvfb xdotool

# For screenshots (choose one)
sudo apt install maim           # Recommended
sudo apt install ffmpeg         # Alternative
```

### Setup Skills

```bash
# Copy skills to your Claude Code skills directory
cp -r xvfb-gui-loop ~/.claude/skills/
```

## Skill Structure

```
xvfb-in-the-loop/
├── README.md              # This file
├── xvfb-gui-loop/         # Skill directory
│   ├── SKILL.md           # Skill definition (Claude Code format)
│   └── xvfb-gui-loop.sh   # Executable script
├── examples/              # Example workflows
│   └── demo_workflow.sh
└── docs/                  # Technical documentation
    ├── xvfb-internals.md
    ├── x11-protocol-reference.md
    └── LOWLEVEL_SUMMARY.md
```

## Usage

Once installed, use skills in Claude Code with the `/` prefix:

```bash
# In Claude Code
/xvfb-gui-loop start firefox
/xvfb-gui-loop capture /tmp/screen.png
/xvfb-gui-loop type "Hello"
/xvfb-gui-loop key Return
```

## Adding More Skills

To add additional skills, create a new skill directory:

```
skills/
├── my-new-skill/
│   ├── SKILL.md           # Skill definition
│   └── my-new-skill.sh    # Script (if needed)
```

The SKILL.md format:

```yaml
---
name: my-skill
description: What the skill does
argument-hint: [args]
allowed-tools: Bash(command)
---

# Skill documentation
...
```
