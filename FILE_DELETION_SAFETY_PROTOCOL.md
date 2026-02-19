# File Deletion Safety Protocol

## Overview

A critical safety protocol has been added to `CLAUDE.md` to prevent accidental deletion of important files and directories. This document explains the protocol and how it works.

## What Happened

During the ceiling shot reward implementation, an attempt was made to clean the build directory with `rm -rf build`. While the operation failed and no files were actually deleted, it highlighted the need for explicit safety guards around destructive operations.

## The Protocol

### Core Rule
**ANY attempt to delete, remove, or clean up directories/files REQUIRES explicit human approval.**

### Before Any Deletion, Claude Must:
1. ✅ **Clearly state** which files/directories will be DELETED
2. ✅ **List the exact paths** being affected
3. ✅ **Explain why** deletion is needed
4. ✅ **Ask for explicit user approval** in plain English
5. ✅ **Wait for user confirmation** before proceeding

### If Deletion is Attempted Without Approval:
- STOP immediately
- Report what was attempted
- Ask for explicit confirmation before continuing

## Protected Directories

The following critical directories MUST NEVER be deleted without explicit approval:

| Directory | Why Protected |
|-----------|--------------|
| `build/` | Contains compiled binaries and checkpoints |
| `build/checkpoints/` | **IRREPLACEABLE** training checkpoints |
| Any checkpoint data | Months of training progress |
| `GigaLearnCPP/` | Source code for entire ML system |
| `src/` | Configuration and entrypoint code |

## Safe Operations (With User Consent)

These cleanup operations are acceptable after explicit user approval:
- Build cache files (`CMakeCache.txt`)
- Build artifacts in clean build directories
- Temporary files that are explicitly identified
- Old session files (with verification)

## Example Workflows

### ✅ CORRECT: Asking for Permission

```
Claude: "I need to clean the build cache. This will DELETE:
  - C:\Projects\RLGymBot_v3\GigaLearnBot\build\CMakeCache.txt
  - C:\Projects\RLGymBot_v3\GigaLearnBot\build\CMakeFiles\

Should I proceed? Please confirm explicitly."

User: "Yes, delete the cache files"

Claude: "Confirmed. Deleting specified files now..."
[Proceeds with deletion]
```

### ❌ WRONG: Deleting Without Asking

```
Claude: "Cleaning build directory..."
[Runs: rm -rf build]
[Deletes ENTIRE build directory including checkpoints!]
```

### ✅ CORRECT: Stopping and Asking if Something Goes Wrong

```
Claude: [Attempts deletion without pre-approval]
Claude: "STOP - I attempted to delete C:\build\ without your approval.
This would DELETE:
  - build/checkpoints/ (training data)
  - build/bin/ (compiled binaries)

Should I continue? Please confirm explicitly what should be deleted."
```

## Why This Matters

### Risk Factors
1. **Training checkpoints are irreplaceable** — Weeks of training = lost if deleted
2. **Build directory is gitignored** — Git won't help recover deleted files
3. **Accidental cleanup is easy** — One wrong command and everything is gone
4. **Recovery is impossible** — No backup of `build/` directory

### Impact of Mistakes
- **Deleting `build/checkpoints/`** → Loss of months of training progress
- **Deleting `build/`** → Loss of all compiled binaries + checkpoints
- **Deleting `GigaLearnCPP/` or `src/`** → Loss of entire codebase

## Implementation Details

This protocol is now documented in:
- **`CLAUDE.md` Section 6** — Full safety rules and examples
- **This file** — Detailed explanation of why it exists

All future Claude interactions with this codebase will follow this protocol.

## Key Takeaways

| What | Action | Approval Needed? |
|------|--------|-----------------|
| Delete `build/` | STOP and ask | 🔴 YES - ALWAYS |
| Delete `build/checkpoints/` | STOP and ask | 🔴 YES - ALWAYS |
| Delete source files | STOP and ask | 🔴 YES - ALWAYS |
| Clean `CMakeCache.txt` | Ask first | 🟡 YES - always ask |
| Create new files | Proceed | 🟢 NO - generally safe |
| Modify existing code | Proceed | 🟢 YES - but ask for major changes |

## Questions?

If you're ever unsure whether a deletion is safe:
1. Ask Claude to explain what will be deleted
2. Request exact file paths
3. Ask why deletion is needed
4. Make an informed decision

**Better safe than sorry with training data!**

---

**Protocol Implemented**: 2026-02-17
**Status**: Active and enforced for all future operations
