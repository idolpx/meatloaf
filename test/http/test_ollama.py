#!/usr/bin/env python3
"""Test Ollama on C64 via Meatloaf. Captures serial debug output."""
import sys, time, subprocess, threading, re, os
sys.path.insert(0, "/home/qus/.claude/skills/ultimate64-debug/scripts")
from c64_remote import C64Remote

SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 2000000
U64 = "http://192.168.1.176"
OLLAMA = "http://192.168.1.131:11434/v1/chat/completions"

c64 = C64Remote(U64)

# Simple inline test: POST to Ollama with JSON body
program = '\n'.join([
    '10 open 1,8,2,"' + OLLAMA + '"',
    '12 if (st and 128)<>0 then print"fail":end',
    '15 print#1,"m post"',
    '20 print#1,"h content-type: application/json"',
    '25 print#1,"b {""model"":""qwen2.5:0.5b"",""messages"":[{""role"":""user"",""content"":""hi""}],""max_tokens"":20}"',
    '30 print#1,"s"',
    '35 print#1,"status"',
    '36 st$=""',
    '37 get#1,a$',
    '38 if a$=chr$(13) then 45',
    '39 st$=st$+a$:goto 37',
    '45 print"status: ";st$',
    '50 print#1,"r-b"',
    '55 print"body:"',
    '60 for t=1 to 200',
    '61 get#1,a$',
    '62 if (st and 64)<>0 then 80',
    '63 if (st and 128)<>0 then 80',
    '65 print chr$(asc(a$));',
    '70 next t',
    '75 print',
    '80 print"done"',
    '85 close 1',
])

# Capture serial output in background
serial_data = []
stop_capture = threading.Event()

def capture_serial():
    try:
        import serial
        ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.5)
        while not stop_capture.is_set():
            try:
                d = ser.read(4096)
                if d:
                    # Strip ANSI
                    clean = re.sub(b'\x1b\\[[0-9;]*[a-zA-Z]', b'', d)
                    serial_data.append(clean)
            except:
                time.sleep(0.1)
        ser.close()
    except Exception as e:
        serial_data.append(f"[SERIAL ERROR: {e}]".encode())

t = threading.Thread(target=capture_serial, daemon=True)
t.start()

time.sleep(0.5)

print("=== CLEAR SCREEN ===")
c64.clear_screen()
time.sleep(0.3)

print("=== INJECT PROGRAM ===")
c64.inject_basic(program)
time.sleep(0.5)

print("=== LIST ===")
r = c64.type_command("LIST", wait=2)
for l in r.split('\n'):
    if l.strip():
        print(f"  {l.strip()}")

print("=== RUN ===")
c64.type_text("RUN", press_enter=True)

for i in range(20):
    time.sleep(2)
    screen = c64.read_screen()
    for l in [l.rstrip() for l in screen.split('\n') if l.rstrip()]:
        print(f"  [{l}]")
    if any("READY." in l for l in screen.split('\n')):
        break

stop_capture.set()
time.sleep(0.5)

print("\n=== SERIAL DEBUG OUTPUT (filtered) ===")
all_data = b"".join(serial_data)
text = all_data.decode('utf-8', errors='replace')
for line in text.split('\n'):
    l = line.strip()
    if not l: continue
    # Show lines with key terms
    if any(kw in l.lower() for kw in ['post', 'body', 'capture', 'perform', 'buffer',
                                        'status', 'header', 'error', 'wrote', 'open',
                                        'sendreq', 'handle']):
        print(f"  {l}")

# Also check Ollama log
print("\n=== OLLAMA LOG ===")
r = subprocess.run(["journalctl", "-u", "ollama", "--since", "1 min ago", "--no-pager"],
                   capture_output=True, text=True)
for line in r.stdout.split('\n'):
    if "192.168.1.238" in line:
        print(f"  {line.strip()}")
