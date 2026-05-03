#!/usr/bin/env python3
"""Interactive serial terminal for gauge control with continuous output and command history."""

from utils import connect_serial
import sys
import threading
import time
import readline  # Provides command history and up arrow support

# Command history
command_history = []

def read_serial_continuous(ser, stop_event):
    """Continuously read from serial port in background thread."""
    buffer = ""
    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                # Read available data
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                buffer += data
                
                # Process complete lines
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    if line:
                        print(f"  {line}")
                        sys.stdout.flush()  # Ensure immediate output
            else:
                time.sleep(0.01)  # Small delay when no data
        except Exception as e:
            if not stop_event.is_set():
                print(f"\nSerial read error: {e}")
            break
    
    # Print any remaining buffer
    if buffer.strip():
        print(f"  {buffer.strip()}")

def completer(text, state):
    """Tab completion for commands."""
    commands = ['HOME', 'TEMP', 'MODE', 'CAL', 'GOTO', 'STEP', 'LIST', 'STATUS', 
                'SUGGEST', 'CLEAR', 'SAVE', 'OFFSET', 'A1', 'TOUCH', 'BASELINE', 
                'THRESHOLD', 'CAL', 'WORK', 'RUN']
    matches = [cmd for cmd in commands if cmd.startswith(text.upper())]
    try:
        return matches[state]
    except IndexError:
        return None

# Set up readline for command history
readline.set_completer(completer)
readline.parse_and_bind('tab: complete')

# Load history from file if it exists
history_file = '.gauge_history'
try:
    readline.read_history_file(history_file)
except FileNotFoundError:
    pass

ser = connect_serial('/dev/cu.usbmodem2101')

# Start background thread for continuous serial reading
stop_event = threading.Event()
serial_thread = threading.Thread(target=read_serial_continuous, args=(ser, stop_event), daemon=True)
serial_thread.start()

print("\nSerial terminal ready. Type commands (Up arrow for history, Tab for completion)")
print("Type 'quit' or 'exit' to stop\n")

try:
    while True:
        # Get command from user (readline handles history)
        try:
            cmd = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if cmd.lower() in ['quit', 'exit', 'q']:
            break

        if not cmd:
            continue

        # Add to history
        command_history.append(cmd)
        readline.add_history(cmd)

        # Send to Arduino
        ser.write(f"{cmd}\n".encode())
        ser.flush()  # Ensure command is sent immediately

except KeyboardInterrupt:
    print("\n\nInterrupted by user")

finally:
    # Stop serial reading thread
    stop_event.set()
    time.sleep(0.1)  # Give thread time to stop
    
    # Save history
    try:
        readline.write_history_file(history_file)
    except:
        pass
    
    ser.close()
    print("\nSerial terminal closed.")
