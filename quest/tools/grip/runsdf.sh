#!/bin/bash
cd /home/claude/tfevr/tools/grip
for n in "$@"; do python3 sdf.py /home/claude/wout/geo/$n.npz $n.sdf >> sdf.log 2>&1; done
