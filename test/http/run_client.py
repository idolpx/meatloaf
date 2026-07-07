#!/usr/bin/env python3
"""Inject the openai_chat_client.bas, run it, ask a question."""
import sys, time, subprocess
sys.path.insert(0, "/home/qus/.claude/skills/ultimate64-debug/scripts")
from c64_remote import C64Remote

c64 = C64Remote("http://192.168.1.176")

print("=== RESET ===")
c64.reset_machine()
time.sleep(6)

with open("/home/qus/dev/_c/meatloaf/test/http/openai_chat_client.bas") as f:
    basic = f.read()

print("=== INJECT ===")
c64.inject_basic(basic)
time.sleep(0.5)

# Clear screen and run
c64.clear_screen()
time.sleep(0.3)
c64.type_text("RUN", press_enter=True)

# Wait for title screen
time.sleep(3)

# Press any key to advance
c64.type_text(" ", press_enter=False)

# Wait for INPUT prompt
time.sleep(2)

# Type a question and press enter
c64.type_text("SAY HI", press_enter=True)

# Wait for response
for i in range(20):
    time.sleep(2)
    s = c64.read_screen()
    lines = [l.rstrip() for l in s.split('\n') if l.rstrip()]
    if lines:
        print(f"--- {i*2+2}s ---")
        for l in lines[-8:]:
            print(f"  [{l}]")
    # Check for "again?" prompt (ASCII mode fallback)
    if any('AGAIN?' in l for l in lines):
        break
else:
    print("=== TIMEOUT ===")

# Check Ollama log
r = subprocess.run(["journalctl", "-u", "ollama", "--since", "30 seconds ago", "--no-pager"],
                   capture_output=True, text=True)
for l in r.stdout.split('\n'):
    if "192.168.1.238" in l:
        print(f"OLLAMA: {l.strip()}")
