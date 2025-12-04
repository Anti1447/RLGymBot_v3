import os
from pathlib import Path

# Assumes this file lives in: C:\Projects\RLGymBot_v3\GigaLearnCPP
PARENT = Path(__file__).resolve().parent              # ...\GigaLearnCPP
CHILD = PARENT / "GigaLearnCPP"                       # ...\GigaLearnCPP\GigaLearnCPP

MAX_DEPTH = 4  # go a bit deeper than before

IGNORE_DIRS = {
    ".git", ".venv", "__pycache__",
    # big externals – we already know they exist
    "libtorch",
    "pybind11",
}

SHOW_EXTS = {".cpp", ".h", ".hpp", ".txt", ".md", ".cmake", ".exe"}


def should_skip_dir(name: str) -> bool:
    return name in IGNORE_DIRS


def should_show_file(name: str) -> bool:
    if name == "CMakeLists.txt":
        return True
    if name.startswith("CMakeLists"):
        return True  # e.g. backup copies you made
    ext = os.path.splitext(name)[1].lower()
    return ext in SHOW_EXTS


def print_tree(root: Path, prefix: str = "", depth: int = 0):
    if depth > MAX_DEPTH:
        return

    try:
        entries = list(root.iterdir())
    except PermissionError:
        return

    dirs = [e for e in entries if e.is_dir() and not should_skip_dir(e.name)]
    files = [e for e in entries if e.is_file() and should_show_file(e.name)]

    for d in sorted(dirs, key=lambda x: x.name.lower()):
        print(prefix + d.name + "/")
        print_tree(d, prefix + "    ", depth + 1)

    for f in sorted(files, key=lambda x: x.name.lower()):
        print(prefix + f.name)


if __name__ == "__main__":
    # Print parent view
    print("=== GigaLearnCPP PARENT ===")
    print(PARENT.name + "/")
    print_tree(PARENT, "    ", 1)

    # Print child engine view
    if CHILD.exists():
        print("\n=== GigaLearnCPP CHILD (Engine) ===")
        print(CHILD.name + "/")
        print_tree(CHILD, "    ", 1)
    else:
        print("\n[WARN] Child GigaLearnCPP directory not found at:", CHILD)
