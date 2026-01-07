#!/usr/bin/env python3
"""
Rocket League Rewards Linter
Checks for common compilation issues in reward header files
"""

import re
import sys
from pathlib import Path
from typing import List, Tuple, Dict
from dataclasses import dataclass

@dataclass
class LintError:
    file: str
    line: int
    severity: str  # 'error', 'warning', 'info'
    message: str
    code: str

class RewardLinter:
    def __init__(self):
        self.errors: List[LintError] = []
        self.class_definitions: Dict[str, List[Tuple[str, int]]] = {}
        
    def lint_file(self, filepath: Path) -> List[LintError]:
        """Lint a single reward header file"""
        self.errors = []
        
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        
        self.check_duplicate_classes(filepath, lines)
        self.check_namespace_usage(filepath, lines)
        self.check_enum_scoping(filepath, lines)
        self.check_forward_declarations(filepath, lines)
        self.check_include_guards(filepath, lines)
        self.check_override_keywords(filepath, lines)
        self.check_const_correctness(filepath, lines)
        
        return self.errors
    
    def check_duplicate_classes(self, filepath: Path, lines: List[str]):
        """Check for duplicate class definitions"""
        class_pattern = re.compile(r'^\s*class\s+(\w+)\s*[:{]')
        
        for i, line in enumerate(lines, 1):
            match = class_pattern.match(line)
            if match:
                class_name = match.group(1)
                if class_name not in self.class_definitions:
                    self.class_definitions[class_name] = []
                self.class_definitions[class_name].append((str(filepath), i))
        
        # Report duplicates
        for class_name, locations in self.class_definitions.items():
            if len(locations) > 1:
                for file, line in locations[1:]:
                    self.errors.append(LintError(
                        file=file,
                        line=line,
                        severity='error',
                        message=f"Duplicate class definition '{class_name}' (also defined in {locations[0][0]}:{locations[0][1]})",
                        code='E001'
                    ))
    
    def check_namespace_usage(self, filepath: Path, lines: List[str]):
        """Check for classes outside RLGC namespace"""
        in_namespace = False
        namespace_level = 0
        
        for i, line in enumerate(lines, 1):
            if re.match(r'^\s*namespace\s+RLGC\s*{', line):
                in_namespace = True
                namespace_level += 1
            elif re.match(r'^\s*namespace\s+\w+\s*{', line):
                namespace_level += 1
            elif re.match(r'^\s*}\s*//.*namespace', line):
                namespace_level -= 1
                if namespace_level == 0:
                    in_namespace = False
            
            # Check for class definitions outside namespace
            if re.match(r'^\s*class\s+\w+.*:', line) and not in_namespace:
                self.errors.append(LintError(
                    file=str(filepath),
                    line=i,
                    severity='warning',
                    message="Class defined outside RLGC namespace (may cause ambiguity)",
                    code='W001'
                ))
    
    def check_enum_scoping(self, filepath: Path, lines: List[str]):
        """Check for enum declarations appearing after usage in structs"""
        class_content = ""
        current_class = None
        class_start = 0
        
        for i, line in enumerate(lines, 1):
            if re.match(r'^\s*class\s+(\w+)', line):
                # Check previous class if any
                if current_class and class_content:
                    self._check_enum_order(filepath, current_class, class_start, class_content)
                
                # Start new class
                match = re.match(r'^\s*class\s+(\w+)', line)
                current_class = match.group(1)
                class_content = line
                class_start = i
            elif current_class:
                class_content += line
        
        # Check last class
        if current_class and class_content:
            self._check_enum_order(filepath, current_class, class_start, class_content)
    
    def _check_enum_order(self, filepath: Path, class_name: str, start_line: int, content: str):
        """Check if enums are declared before structs that use them"""
        # Find enum declarations
        enum_pattern = re.compile(r'enum\s+class\s+(\w+)')
        enums = {m.group(1): m.start() for m in enum_pattern.finditer(content)}
        
        # Find struct declarations
        struct_pattern = re.compile(r'struct\s+(\w+)\s*{([^}]+)}', re.DOTALL)
        
        for struct_match in struct_pattern.finditer(content):
            struct_body = struct_match.group(2)
            struct_pos = struct_match.start()
            
            # Find enum usages in struct
            for enum_name, enum_pos in enums.items():
                if enum_name in struct_body:
                    # Check if enum is declared AFTER struct
                    if enum_pos > struct_pos:
                        self.errors.append(LintError(
                            file=str(filepath),
                            line=start_line + content[:struct_pos].count('\n'),
                            severity='error',
                            message=f"Enum '{enum_name}' used in struct but declared after it",
                            code='E002'
                        ))
    
    def check_forward_declarations(self, filepath: Path, lines: List[str]):
        """Check for incomplete forward declarations"""
        forward_decls = set()
        full_definitions = set()
        
        for i, line in enumerate(lines, 1):
            # Forward declaration
            if re.match(r'^\s*struct\s+(\w+);', line):
                match = re.match(r'^\s*struct\s+(\w+);', line)
                forward_decls.add(match.group(1))
            
            # Full definition
            if re.match(r'^\s*struct\s+(\w+)\s*{', line):
                match = re.match(r'^\s*struct\s+(\w+)\s*{', line)
                full_definitions.add(match.group(1))
        
        # Check for forward decls without definitions
        undefined = forward_decls - full_definitions
        for struct_name in undefined:
            self.errors.append(LintError(
                file=str(filepath),
                line=0,
                severity='warning',
                message=f"Forward declaration of '{struct_name}' without full definition",
                code='W002'
            ))
    
    def check_include_guards(self, filepath: Path, lines: List[str]):
        """Check for proper include guards"""
        if not lines:
            return
        
        # Should start with #pragma once or #ifndef guard
        has_pragma = any('#pragma once' in line for line in lines[:10])
        has_ifndef = any(line.strip().startswith('#ifndef') for line in lines[:10])
        
        if not (has_pragma or has_ifndef):
            self.errors.append(LintError(
                file=str(filepath),
                line=1,
                severity='warning',
                message="Missing include guard (#pragma once or #ifndef)",
                code='W003'
            ))
    
    def check_override_keywords(self, filepath: Path, lines: List[str]):
        """Check for missing override keywords"""
        virtual_pattern = re.compile(r'virtual\s+\w+.*\(.*\)(?!\s*override)')
        
        for i, line in enumerate(lines, 1):
            if 'virtual' in line and 'override' not in line and '= 0' not in line:
                # Check if it's overriding a base method
                if any(keyword in line for keyword in ['GetReward', 'Reset']):
                    self.errors.append(LintError(
                        file=str(filepath),
                        line=i,
                        severity='warning',
                        message="Virtual method likely missing 'override' keyword",
                        code='W004'
                    ))
    
    def check_const_correctness(self, filepath: Path, lines: List[str]):
        """Check for const correctness in member functions"""
        # Methods that should likely be const
        getter_pattern = re.compile(r'(Get|Calculate|Is|Has)\w+\s*\([^)]*\)\s*{')
        
        for i, line in enumerate(lines, 1):
            match = getter_pattern.search(line)
            if match and 'const' not in line:
                self.errors.append(LintError(
                    file=str(filepath),
                    line=i,
                    severity='info',
                    message="Getter/calculator method should probably be const",
                    code='I001'
                ))
    
    def print_report(self):
        """Print lint report"""
        if not self.errors:
            print("✓ No linting errors found!")
            return
        
        # Group by severity
        errors = [e for e in self.errors if e.severity == 'error']
        warnings = [e for e in self.errors if e.severity == 'warning']
        infos = [e for e in self.errors if e.severity == 'info']
        
        print(f"\n{'='*80}")
        print(f"LINT REPORT: {len(self.errors)} issues found")
        print(f"{'='*80}\n")
        
        if errors:
            print(f"❌ ERRORS ({len(errors)}):")
            for e in errors:
                print(f"  {e.file}:{e.line} [{e.code}] {e.message}")
            print()
        
        if warnings:
            print(f"⚠️  WARNINGS ({len(warnings)}):")
            for w in warnings:
                print(f"  {w.file}:{w.line} [{w.code}] {w.message}")
            print()
        
        if infos:
            print(f"ℹ️  INFO ({len(infos)}):")
            for i in infos:
                print(f"  {i.file}:{i.line} [{i.code}] {i.message}")
            print()
        
        return len(errors) > 0


def main():
    if len(sys.argv) < 2:
        print("Usage: python reward_linter.py <reward_header_file> [...]")
        sys.exit(1)
    
    linter = RewardLinter()
    has_errors = False
    
    for filepath in sys.argv[1:]:
        path = Path(filepath)
        if not path.exists():
            print(f"Error: File not found: {filepath}")
            continue
        
        print(f"Linting {filepath}...")
        linter.lint_file(path)
    
    has_errors = linter.print_report()
    
    sys.exit(1 if has_errors else 0)


if __name__ == '__main__':
    main()
