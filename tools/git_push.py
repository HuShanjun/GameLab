#!/usr/bin/env python3
"""Push current branch to origin/<same-name>; optionally squash unpushed commits."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Sequence


def run_git(
    *args: str,
    check: bool = False,
    capture: bool = True,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        check=check,
        capture_output=capture,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def git_out(*args: str) -> str:
    cp = run_git(*args, check=False)
    return (cp.stdout or "").strip()


def git_ok(*args: str) -> bool:
    return run_git(*args, check=False).returncode == 0


def ask_yes_no(prompt: str, default: bool = False) -> bool:
    suffix = " [Y/n]: " if default else " [y/N]: "
    try:
        raw = input(prompt + suffix).strip().lower()
    except EOFError:
        return default
    if not raw:
        return default
    return raw in ("y", "yes")


def ask_text(prompt: str) -> str:
    try:
        return input(prompt).strip()
    except EOFError:
        return ""


def build_squash_message(title: str, subjects: Sequence[str], append: bool) -> str:
    lines = [title]
    if append and subjects:
        lines.append("")
        for i, subject in enumerate(subjects, start=1):
            lines.append(f"  {i}. {subject}")
    return "\n".join(lines) + "\n"


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Push current branch to origin/<branch>; optional squash of unpushed commits.",
    )
    p.add_argument("--squash", action="store_true", help="Squash unpushed commits into one before push.")
    p.add_argument("--title", default=None, help="Squash commit title (prompted if omitted with --squash).")
    p.add_argument(
        "--append-subjects",
        action="store_true",
        help="Append numbered unpushed subjects to the squash message body.",
    )
    p.add_argument(
        "--yes",
        action="store_true",
        help="Continue squash despite dirty tracked files without prompting.",
    )
    return p.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    toplevel = git_out("rev-parse", "--show-toplevel")
    if not toplevel or not git_ok("rev-parse", "--show-toplevel"):
        print("[ERROR] Not inside a git repository.")
        return 1

    os.chdir(toplevel)
    print(f"[INFO] Repo:   {toplevel}")

    branch = git_out("rev-parse", "--abbrev-ref", "HEAD")
    if not branch or branch == "HEAD":
        print("[ERROR] Detached HEAD. Checkout a branch first.")
        return 1
    print(f"[INFO] Branch: {branch}")

    fetch = run_git("fetch", "origin")
    if fetch.returncode != 0:
        print("[WARN] git fetch origin failed; using local remote-tracking info.")

    remote_ref = f"origin/{branch}"
    has_remote = git_ok("rev-parse", "--verify", "--quiet", remote_ref)

    if has_remote:
        rev_range = f"{remote_ref}..HEAD"
        ahead_s = git_out("rev-list", "--count", rev_range)
    else:
        print(f"[INFO] No {remote_ref} yet - this will be a first push.")
        rev_range = None
        ahead_s = git_out("rev-list", "--count", "HEAD")

    try:
        ahead = int(ahead_s or "0")
    except ValueError:
        ahead = 0

    if ahead == 0:
        print("[INFO] No commits to push.")
        return 0

    print()
    print(f"[INFO] Commits to push ({ahead}):")
    if has_remote:
        log = run_git("log", "--oneline", rev_range, capture=True)
    else:
        log = run_git("log", "--oneline", "-n", str(ahead), capture=True)
    if log.stdout:
        print(log.stdout.rstrip())
    print()

    do_squash = bool(args.squash)
    if not args.squash and sys.stdin.isatty():
        do_squash = ask_yes_no("Merge into one commit before push?", default=False)
    elif not args.squash and not sys.stdin.isatty():
        do_squash = False

    if not do_squash:
        print("[INFO] Pushing without squash...")
        push = run_git("push", "-u", "origin", "HEAD", capture=False)
        if push.returncode != 0:
            print("[ERROR] git push failed.")
            return 1
        print(f"[OK] Pushed {branch} to {remote_ref}.")
        return 0

    if not has_remote:
        print(f"[ERROR] Cannot squash on first push - {remote_ref} does not exist.")
        print("        Push once without squash, or create the remote branch first.")
        return 1

    dirty = bool(git_out("status", "--porcelain", "-uno"))
    if dirty:
        print("[WARN] Working tree has tracked modifications.")
        if args.yes:
            cont = True
        elif sys.stdin.isatty():
            cont = ask_yes_no("Continue squash anyway?", default=False)
        else:
            print("[ERROR] Dirty tree and no TTY; pass --yes to continue.")
            return 1
        if not cont:
            print("[INFO] Aborted.")
            return 1

    title = args.title
    if not title:
        if not sys.stdin.isatty():
            print("[ERROR] --title is required for non-interactive squash.")
            return 1
        title = ask_text("Commit title (required): ")
    if not title:
        print("[ERROR] Title is required.")
        return 1

    # --append-subjects wins; --title on CLI means non-interactive append (off unless flag);
    # fully interactive squash asks.
    if args.append_subjects:
        append = True
    elif args.title is not None:
        append = False
    elif sys.stdin.isatty():
        append = ask_yes_no("Append numbered unpushed subjects to body?", default=False)
    else:
        append = False

    subjects: List[str] = []
    if append:
        subj_out = git_out("log", "--reverse", "--format=%s", rev_range)
        subjects = [line for line in subj_out.splitlines() if line.strip()]

    message = build_squash_message(title, subjects, append)
    print()
    print("[INFO] Commit message preview:")
    print(message.rstrip())
    print()

    print(f"[INFO] Soft-reset to {remote_ref} and recreate one commit...")
    if not git_ok("reset", "--soft", remote_ref):
        print("[ERROR] git reset --soft failed.")
        return 1

    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="\n",
        suffix=".txt",
        delete=False,
    ) as fh:
        fh.write(message)
        msg_path = fh.name

    try:
        commit = run_git("commit", "--allow-empty", "-F", msg_path)
        if commit.returncode != 0:
            print("[ERROR] git commit failed. Tip: git reflog to recover pre-reset HEAD.")
            if commit.stderr:
                print(commit.stderr.rstrip())
            return 1
    finally:
        try:
            Path(msg_path).unlink(missing_ok=True)
        except OSError:
            pass

    print("[INFO] Pushing squashed commit...")
    push = run_git("push", "-u", "origin", "HEAD", capture=False)
    if push.returncode != 0:
        print("[ERROR] git push failed. Tip: git reflog to recover if needed.")
        return 1

    print(f"[OK] Squashed and pushed {branch} to {remote_ref}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
