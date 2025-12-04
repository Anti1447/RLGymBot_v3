# render_main.py
import sys
from render_receiver import render_state

def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        render_state(line)

if __name__ == "__main__":
    main()
