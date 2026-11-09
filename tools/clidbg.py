#!/usr/bin/env python3
# clidbg.py <command line> -- <debugger command>...: runs the Debugger's
# command line interface on the C2 over SSH (it needs a terminal) and sends
# the commands one by one, printing everything. "{tid}" in a command is the
# thread the Debugger stopped first; "sleep:<s>" waits. Test password.
import sys
import time

import pexpect

split = sys.argv.index("--")
target = " ".join(sys.argv[1:split])
commands = sys.argv[split + 1:]
child = pexpect.spawn("sshpass -p haiku ssh -tt -o StrictHostKeyChecking=no "
	"-o UserKnownHostsFile=/dev/null -o LogLevel=ERROR baron@10.42.0.2 "
	"/boot/system/apps/Debugger --cli " + target, encoding="utf-8",
	timeout=30)
child.logfile_read = sys.stdout
prompt = r"debugger\([^)]*\)> |> $"
import re
thread = "0"
try:
	child.expect(r"thread stopped: (\d+)", timeout=40)
	thread = child.match.group(1)
	child.sendline("")
	child.expect(prompt, timeout=40)
	for command in commands:
		command = command.replace("{tid}", thread)
		if command.startswith("sleep:"):
			time.sleep(float(command[6:]))
			continue
		child.sendline(command)
		child.expect(prompt, timeout=40)
except pexpect.TIMEOUT:
	print("\n[clidbg: timeout]")
except pexpect.EOF:
	print("\n[clidbg: eof]")
child.sendline("quit")
time.sleep(1)
child.close(force=True)
