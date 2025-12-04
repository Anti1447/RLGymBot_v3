#print_tree.py
#For RLGymBot_v3
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# Only show files that matter for CMake debugging
SHOW_EXTS = {".txt", ".cmake", ".py", ".cpp", ".h"}
MAX_DEPTH = 3  # only descend 3 levels

IGNORE_DIRS = {
    ".git", ".venv", "__pycache__", "data", "wandb",
    "logs", ".vscode", ".idea", "build", "Release",
    "Debug", "x64", "x86", "bin", "lib", "include"
}

def should_skip_dir(dirname):
    return dirname in IGNORE_DIRS

def should_show_file(filename):
    ext = os.path.splitext(filename)[1].lower()
    return ext in SHOW_EXTS or filename == "CMakeLists.txt"

def print_tree(path, prefix="", depth=0):
    if depth > MAX_DEPTH:
        return

    entries = list(path.iterdir())
    dirs = [e for e in entries if e.is_dir() and not should_skip_dir(e.name)]
    files = [e for e in entries if e.is_file() and should_show_file(e.name)]

    for d in sorted(dirs, key=lambda x: x.name.lower()):
        print(prefix + d.name + "/")
        print_tree(d, prefix + "    ", depth + 1)

    for f in sorted(files, key=lambda x: x.name.lower()):
        print(prefix + f.name)

if __name__ == "__main__":
    print(ROOT.name + "/")
    print_tree(ROOT, "    ", 1)
