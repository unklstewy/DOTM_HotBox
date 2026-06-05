import struct
import time
import sys
import os
import subprocess
import threading
import glob

def main():
    # Struct format
    EVENT_FORMAT = 'IhBB'
    EVENT_SIZE = struct.calcsize(EVENT_FORMAT)

    def gamepad_thread_func():
        while True:
            js_devices = glob.glob('/dev/input/js*')
            if not js_devices:
                time.sleep(1)
                continue
            js_path = sorted(js_devices)[0]
            try:
                print(f"[INFO] Opening gamepad device: {js_path}", flush=True)
                with open(js_path, 'rb') as f:
                    while True:
                        chunk = f.read(EVENT_SIZE)
                        if not chunk:
                            break
                        t_ms, val, ev_type, num = struct.unpack(EVENT_FORMAT, chunk)
                        # ev_type: 1=button, 2=axis. Bit 0x80 is set for init events.
                        is_init = bool(ev_type & 0x80)
                        actual_type = ev_type & ~0x80
                        
                        if is_init:
                            continue
                        
                        t_str = time.strftime('%H:%M:%S') + f'.{int(time.time() * 1000) % 1000:03d}'
                        if actual_type == 1:
                            # Convert 0-indexed js button to 1-indexed Gamepad button
                            action = "PRESSED" if val else "RELEASED"
                            print(f"\033[92m[{t_str}] [GAMEPAD] Button {num + 1} -> {action}\033[0m", flush=True)
                        elif actual_type == 2:
                            print(f"\033[96m[{t_str}] [GAMEPAD] Axis {num} -> {val}\033[0m", flush=True)
            except PermissionError:
                print(f"[GAMEPAD ERROR] Permission denied to read '{js_path}'. Please run 'sudo chmod a+r {js_path}' to grant read permission.", flush=True)
                time.sleep(2)
            except Exception as e:
                print(f"[GAMEPAD ERROR] {e}, retrying in 1s...", flush=True)
                time.sleep(1)

    def serial_thread_func():
        print("[INFO] Starting idf.py monitor...", flush=True)
        # Use PAGER=cat to avoid paging and get clean stdout lines
        env = os.environ.copy()
        env['PAGER'] = 'cat'
        proc = subprocess.Popen(
            ['idf.py', 'monitor'],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd='/home/sannis/ESPSCar',
            env=env
        )
        
        try:
            for line in iter(proc.stdout.readline, ''):
                clean_line = line.rstrip()
                if not clean_line:
                    continue
                t_str = time.strftime('%H:%M:%S') + f'.{int(time.time() * 1000) % 1000:03d}'
                print(f"[{t_str}] [SERIAL] {clean_line}", flush=True)
        except Exception as e:
            print(f"[SERIAL ERROR] {e}", flush=True)
        finally:
            try:
                proc.terminate()
            except:
                pass

    t_gp = threading.Thread(target=gamepad_thread_func, daemon=True)
    t_ser = threading.Thread(target=serial_thread_func, daemon=True)

    t_gp.start()
    t_ser.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[INFO] Exiting alignment logger.", flush=True)

if __name__ == '__main__':
    main()
