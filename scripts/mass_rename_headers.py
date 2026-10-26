#!/usr/bin/env python3
"""Mass-rename C++ files and rewrite every textual reference to them.

Modes (--mode):
  pascal-to-snake  Rename PascalCase .cpp/.hpp files to snake_case.
  h-to-hpp         Rename .h headers to .hpp.

Only the rename plan differs between modes. Reference rewriting is shared:
every old file name (and root-relative / include-relative path) found in
sources, CMake files and Markdown is replaced by the new one. References are
matched textually, not resolved, so an external header with the same basename
is changed too. Run on a clean tree and inspect the diff.

--docs-dir DIR additionally renames file names that appear in DIR/**/*.md but
match no real file (names planned in the roadmap). No file on disk is renamed
for those; only the Markdown text changes. Add --docs-only to do just that
(e.g. after the real rename already ran). Examples:

  scripts/mass_rename_headers.py --mode h-to-hpp --docs-dir docs --docs-only --dry-run
  scripts/mass_rename_headers.py --mode pascal-to-snake --docs-dir docs --docs-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path


EXCLUDED_DIRS = {
    ".git",
    ".idea",
    ".junie",
    ".vscode",
    "vcpkg_installed",
}
EXCLUDED_DIR_PREFIXES = ("cmake-build",)

REFERENCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cmake", ".md"}
REFERENCE_FILENAMES = {"CMakeLists.txt"}

MODES = ("pascal-to-snake", "h-to-hpp")

# Whole file-name match: not glued to a longer identifier, extension or path.
BOUNDARY_BEFORE = r"(?<![\w.\-+])"
BOUNDARY_AFTER = r"(?![\w.\-+/\\])"


@dataclass(frozen=True)
class RenameEntry:
    old_path: Path
    new_path: Path
    old_rel: str
    new_rel: str


def make_entry(root: Path, old_path: Path, new_path: Path) -> RenameEntry:
    return RenameEntry(
        old_path=old_path,
        new_path=new_path,
        old_rel=old_path.relative_to(root).as_posix(),
        new_rel=new_path.relative_to(root).as_posix(),
    )


def iter_project_files(root: Path):
    for directory, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = sorted(
            d
            for d in dirs
            if d not in EXCLUDED_DIRS
            and not d.startswith(EXCLUDED_DIR_PREFIXES)
            and not (Path(directory) / d).is_symlink()
        )
        for name in sorted(names):
            path = Path(directory) / name
            if path.is_file() and not path.is_symlink():
                yield path


# --- mode-specific rename plans -------------------------------------------


def pascal_to_snake(name: str) -> str:
    first_pass = re.sub(r"([A-Z]+)([A-Z][a-z0-9])", r"\1_\2", name)
    second_pass = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first_pass)
    return second_pass.lower()


def plan_pascal_to_snake(root: Path) -> list[RenameEntry]:
    plan = []
    for path in iter_project_files(root):
        if path.suffix not in {".cpp", ".hpp"}:
            continue
        if not any(ch.isupper() for ch in path.stem):
            continue
        new_path = path.with_name(f"{pascal_to_snake(path.stem)}{path.suffix}")
        if new_path.name != path.name:
            plan.append(make_entry(root, path, new_path))
    return plan


def plan_h_to_hpp(root: Path) -> list[RenameEntry]:
    return [
        make_entry(root, path, path.with_suffix(".hpp"))
        for path in iter_project_files(root)
        if path.suffix == ".h"
    ]


PLANNERS = {
    "pascal-to-snake": plan_pascal_to_snake,
    "h-to-hpp": plan_h_to_hpp,
}


# --- shared machinery -----------------------------------------------------


def validate_plan(plan: list[RenameEntry]):
    old_paths = {entry.old_path for entry in plan}
    new_path_to_old: dict[Path, Path] = {}

    for entry in plan:
        if entry.new_path in new_path_to_old:
            existing = new_path_to_old[entry.new_path]
            raise RuntimeError(
                f"Rename collision: {existing} and {entry.old_path} both map to {entry.new_path}"
            )
        new_path_to_old[entry.new_path] = entry.old_path

        if os.path.lexists(entry.new_path) and entry.new_path not in old_paths:
            raise RuntimeError(
                f"Destination already exists and is not being renamed: {entry.new_path}"
            )


def execute_renames(plan: list[RenameEntry]):
    """Two-phase rename so case-only renames work on case-insensitive disks."""
    temporary_moves: list[tuple[Path, Path]] = []

    for index, entry in enumerate(plan):
        temp_path = entry.old_path.with_name(f".__rename_tmp__{index}__{entry.old_path.name}")
        while temp_path.exists():
            temp_path = temp_path.with_name(f"{temp_path.name}_")
        entry.old_path.rename(temp_path)
        temporary_moves.append((temp_path, entry.new_path))

    for temp_path, final_path in temporary_moves:
        temp_path.rename(final_path)


def add_mapping(mapping: dict[str, str], old_value: str, new_value: str):
    if old_value == new_value:
        return
    existing = mapping.get(old_value)
    if existing is not None and existing != new_value:
        raise RuntimeError(
            f"Conflicting replacement mapping for '{old_value}': '{existing}' vs '{new_value}'"
        )
    mapping[old_value] = new_value


def build_replacement_map(plan: list[RenameEntry]) -> dict[str, str]:
    replacement_map: dict[str, str] = {}
    for entry in plan:
        add_mapping(replacement_map, entry.old_rel, entry.new_rel)
        add_mapping(replacement_map, entry.old_path.name, entry.new_path.name)

        if entry.old_rel.startswith("include/") and entry.new_rel.startswith("include/"):
            add_mapping(
                replacement_map,
                entry.old_rel.removeprefix("include/"),
                entry.new_rel.removeprefix("include/"),
            )
    return replacement_map


def build_reference_pattern(replacement_map: dict[str, str]) -> re.Pattern[bytes]:
    # Longest first so a full path wins over its own basename.
    alternatives = "|".join(
        re.escape(old) for old in sorted(replacement_map, key=len, reverse=True)
    )
    return re.compile(
        (BOUNDARY_BEFORE + "(?:" + alternatives + ")" + BOUNDARY_AFTER).encode("utf-8")
    )


def update_references(root: Path, replacement_map: dict[str, str], dry_run: bool) -> tuple[int, int]:
    """Rewrite references in all eligible files; returns (files, references)."""
    pattern = build_reference_pattern(replacement_map)
    byte_map = {k.encode("utf-8"): v.encode("utf-8") for k, v in replacement_map.items()}
    files_changed = 0
    references = 0

    for path in iter_project_files(root):
        if path.suffix not in REFERENCE_SUFFIXES and path.name not in REFERENCE_FILENAMES:
            continue
        # Bytes keep encoding and line endings untouched.
        original = path.read_bytes()
        updated, hits = pattern.subn(lambda m: byte_map[m.group()], original)
        if not hits:
            continue
        files_changed += 1
        references += hits
        print(f"  UPDATE {path.relative_to(root).as_posix()} ({hits})")
        if not dry_run:
            path.write_bytes(updated)

    return files_changed, references


# --- docs-only (planned, not yet real) file names ---------------------------

# Optional ./ or ../ prefix, directories, stem, extension. The lookbehind
# rejects URLs and absolute paths; the lookahead tolerates a sentence-ending dot.
DOC_TOKEN = re.compile(
    rb"(?<![\w.\-+/:\\])"
    rb"((?:\.{1,2}/)*(?:[\w\-]+/)*)([\w\-]+)\.(hpp|cpp|h)"
    rb"(?![\w\-+/\\]|\.\w)"
)

# C / POSIX headers that must never be turned into .hpp.
SYSTEM_HEADERS = {
    "assert.h", "ctype.h", "errno.h", "fcntl.h", "float.h", "inttypes.h",
    "limits.h", "locale.h", "math.h", "netdb.h", "setjmp.h", "signal.h",
    "stdarg.h", "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "string.h", "strings.h", "time.h", "unistd.h", "wchar.h", "poll.h",
    "pthread.h", "dirent.h", "dlfcn.h", "syslog.h", "termios.h",
}


def rename_doc_name(mode: str, stem: str, ext: str) -> str | None:
    """Return the new file name for a docs token, or None to leave it alone."""
    if mode == "h-to-hpp":
        if ext == "h" and f"{stem}.{ext}" not in SYSTEM_HEADERS:
            return f"{stem}.hpp"
        return None
    if ext in ("cpp", "hpp") and any(ch.isupper() for ch in stem):
        return f"{pascal_to_snake(stem)}.{ext}"
    return None


def project_dir_names(root: Path) -> set[str]:
    """First path components that identify a project-relative path in docs."""
    names: set[str] = set()
    for base in (root, root / "include", root / "src"):
        if base.is_dir():
            names.update(
                d.name
                for d in base.iterdir()
                if d.is_dir() and not d.name.startswith(".")
            )
    return names


def update_docs_only_names(
    root: Path, docs_dir: Path, mode: str, dry_run: bool
) -> tuple[int, int]:
    """Rename file names found in docs that match no real file.

    Covers names planned in the roadmap but not created yet. Nothing on disk is
    renamed; only *.md files under docs_dir are rewritten. Names that exist as
    real files are skipped: the real rename plan owns those. Returns
    (files, references).
    """
    real_names = {path.name for path in iter_project_files(root)}
    known_dirs = project_dir_names(root)
    files_changed = 0
    references = 0

    for path in iter_project_files(docs_dir):
        if path.suffix != ".md":
            continue
        found: dict[str, int] = {}

        def replace(match: re.Match[bytes]) -> bytes:
            directory = match.group(1).decode("ascii")
            stem = match.group(2).decode("ascii")
            ext = match.group(3).decode("ascii")
            if f"{stem}.{ext}" in real_names:
                return match.group()
            first = directory.replace("./", "").replace("../", "").split("/")[0]
            if first and first not in known_dirs:
                return match.group()
            new_name = rename_doc_name(mode, stem, ext)
            if new_name is None:
                return match.group()
            key = f"{directory}{stem}.{ext} -> {directory}{new_name}"
            found[key] = found.get(key, 0) + 1
            return f"{directory}{new_name}".encode("ascii")

        original = path.read_bytes()
        updated = DOC_TOKEN.sub(replace, original)
        if not found:
            continue
        files_changed += 1
        references += sum(found.values())
        shown = path.relative_to(root).as_posix() if path.is_relative_to(root) else str(path)
        print(f"  DOCS   {shown}")
        for key, count in sorted(found.items()):
            print(f"           {key} ({count})")
        if not dry_run:
            path.write_bytes(updated)

    return files_changed, references


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--mode", required=True, choices=MODES, help="Rename plan to apply")
    parser.add_argument(
        "--docs-dir",
        type=Path,
        help="Also rewrite file names found in *.md under this directory that "
        "match no real file (planned in the roadmap). Relative to --root.",
    )
    parser.add_argument(
        "--docs-only",
        action="store_true",
        help="With --docs-dir: skip the real rename and reference rewrite; "
        "only rewrite docs-only names",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="Project root path (default: current working directory)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned changes without modifying files",
    )
    args = parser.parse_args()

    if args.docs_only and args.docs_dir is None:
        parser.error("--docs-only requires --docs-dir")

    root = args.root.expanduser().resolve()
    docs_dir = None
    if args.docs_dir is not None:
        docs_dir = (root / args.docs_dir.expanduser()).resolve()
        if not docs_dir.is_dir():
            raise RuntimeError(f"--docs-dir is not a directory: {docs_dir}")

    prefix = "Dry run: " if args.dry_run else "Done: "
    renamed = files_changed = references = 0
    plan: list[RenameEntry] = []

    if not args.docs_only:
        plan = PLANNERS[args.mode](root)
        plan.sort(key=lambda entry: entry.old_rel)
        validate_plan(plan)

        if not plan and docs_dir is None:
            print(f"Mode {args.mode}: no files to rename.")
            return 0

        print(f"Planned renames: {len(plan)}")
        for entry in plan:
            print(f"  RENAME {entry.old_rel} -> {entry.new_rel}")

        if plan:
            replacement_map = build_replacement_map(plan)
            # Rewrite before renaming: scanning only needs the old tree to be
            # consistent, and a dry run then reports exactly what a real run
            # would touch.
            files_changed, references = update_references(
                root, replacement_map, args.dry_run
            )
        renamed = len(plan)

    # Docs pass runs before the on-disk rename so every real file is still
    # visible under its old name and is skipped as "handled by the plan".
    docs_files = docs_refs = 0
    if docs_dir is not None:
        print("Docs-only names (no matching real file):")
        docs_files, docs_refs = update_docs_only_names(
            root, docs_dir, args.mode, args.dry_run
        )

    if not args.dry_run and plan:
        execute_renames(plan)

    print(f"{prefix}{renamed} files renamed, {files_changed} files updated, {references} references.")
    if docs_dir is not None:
        print(f"{prefix}docs-only: {docs_files} files updated, {docs_refs} references.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
