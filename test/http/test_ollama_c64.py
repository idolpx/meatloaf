#!/usr/bin/env python3
"""Simple Ollama test - reset, inject, run."""
import sys, time
sys.path.insert(0, "/home/qus/.claude/skills/ultimate64-debug/scripts")
from c64_remote import C64Remote

c64 = C64Remote("http://192.168.1.176")

print("=== RESET ===")
c64.reset_machine()
time.sleep(6)

prog = (
    '10 open 1,8,2,"http://192.168.1.131:11434/v1/chat/completions"\n'
    '12 if (st and 128)<>0 then print"fail":end\n'
    '15 print#1,"m post"\n'
    '20 print#1,"h content-type: application/json"\n'
    '25 print#1,"b {""model"":""qwen2.5:0.5b"",""messages"":[{""role"":""user"",""content"":""say hi back""}],""max_tokens"":50}"\n'
    '30 print#1,"s"\n'
    '35 print#1,"status"\n'
    '36 st$=""\n'
    '37 get#1,a$\n'
    # Use explicit line for each step - chr$ in BASIC string
    '38 if a$=chr$(13) then 45\n'
    '39 st$=st$+a$:goto 37\n'
    '45 print"status: ";st$\n'
    '50 print#1,"r-b"\n'
    '55 print"answer:"\n'
    '60 for t=1 to 300\n'
    '61 get#1,a$\n'
    '62 if (st and 64)<>0 then 80\n'
    '63 if (st and 128)<>0 then 80\n'
    '65 print chr$(asc(a$));\n'
    '70 next t\n'
    '75 print\n'
    '80 print"<<done>>"\n'
    '85 close 1\n'
)

print("=== INJECT ===")
c64.inject_basic(prog)
time.sleep(0.5)

print("=== LIST ===")
r = c64.type_command("LIST", wait=2)
for l in r.split('\n'):
    if l.strip():
        print(f"  {l.strip()}")

print("=== RUN ===")
c64.clear_screen()
time.sleep(0.3)
c64.type_text("RUN", press_enter=True)
time.sleep(20)

print("=== RESULT ===")
for l in [l.rstrip() for l in c64.read_screen().split('\n') if l.rstrip()]:
    print(f"  [{l}]")
