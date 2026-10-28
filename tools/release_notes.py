#!/usr/bin/env python3
"""A release's notes, and their announcement on Discord, for the GitHub
workflow's release job (.github/workflows/build.yml):

    python tools/release_notes.py notes TAG SHA > notes.md
    python tools/release_notes.py discord TAG SHA

notes writes the release's notes: every commit since the build-* release
before TAG (merges left out, oldest first), then the release's downloads
and its build. discord posts the same changes to the Discord webhook in
DISCORD_RELEASES_WEBHOOK as an embed (the release's title, a link to it,
the changes and the downloads); with no webhook it posts nothing. Discord
limits a webhook's posts (HTTP 429, retry_after): a post is tried again a
few times, and one that still fails exits 1.

Both read GITHUB_REPOSITORY, GITHUB_SERVER_URL and GITHUB_RUN_ID, as a
workflow sets them, and git's history (the checkout's, fetched whole).
"""

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

# the most commits listed (the rest are counted), and the most characters of
# them a Discord embed's description takes (Discord allows 4096)
MAXIMUM_COMMITS = 40
MAXIMUM_DISCORD_CHANGES = 3500
# when there is no release before: the commits listed of the history
FIRST_RELEASE_COMMITS = 20
DISCORD_ATTEMPTS = 5
DISCORD_GREEN = 5763719
PLATFORMS = (("Windows", "windows"), ("Linux", "linux"), ("Android", "android"))


def git(*arguments):
    return subprocess.run(["git", *arguments], check=True, capture_output=True, text=True).stdout


def previous_tag(tag, sha):
    """the newest build-* tag other than tag whose commit is an ancestor of
    sha's (a release made before a history was rewritten is passed over), or
    None"""
    for candidate in git("tag", "--list", "build-*", "--sort=-v:refname").split():
        if candidate == tag:
            continue
        if subprocess.run(["git", "merge-base", "--is-ancestor", candidate, sha], capture_output=True).returncode == 0:
            return candidate
    return None


def changes(tag, sha):
    """the release's commits, oldest first: (short hash, subject); how many more
    there are than are listed; and the release before, or None"""
    previous = previous_tag(tag, sha)
    if previous:
        log = git("log", "--no-merges", "--reverse", "--format=%h%x09%s", f"{previous}..{sha}")
    else:
        log = git("log", "--no-merges", "--reverse", f"-{FIRST_RELEASE_COMMITS}", "--format=%h%x09%s", sha)
    commits = [tuple(line.split("\t", 1)) for line in log.splitlines() if "\t" in line]
    return commits[-MAXIMUM_COMMITS:], max(0, len(commits) - MAXIMUM_COMMITS), previous


def repository_url():
    return f"{os.environ.get('GITHUB_SERVER_URL', 'https://github.com')}/{os.environ['GITHUB_REPOSITORY']}"


def change_lines(commits, more):
    lines = [f"- {subject} ([{short}]({repository_url()}/commit/{short}))" for short, subject in commits]
    if more:
        lines.append(f"- and {more} earlier changes")
    return lines


def downloads():
    latest = f"{repository_url()}/releases/latest/download"
    return " · ".join(f"[{name}]({latest}/halo-{platform}-release.zip)" for name, platform in PLATFORMS)


def notes(tag, sha):
    commits, more, previous = changes(tag, sha)
    since = f" since {previous}" if previous else ""
    lines = [f"## What's changed{since}", ""]
    lines += change_lines(commits, more) or ["- No changes since the last release."]
    lines += ["", f"**Downloads:** {downloads()}"]
    run_id = os.environ.get("GITHUB_RUN_ID")
    if run_id:
        lines.append(f"**Build:** {repository_url()}/actions/runs/{run_id}")
    lines += ["", "The release builds are for playing; the debug builds stop at the first failed assertion and log it."]
    return "\n".join(lines) + "\n"


def discord_payload(tag, sha):
    commits, more, _ = changes(tag, sha)
    lines = change_lines(commits, more) or ["- No changes since the last release."]
    # (whole lines only, within the embed's limit)
    description, kept = "", 0
    for line in lines:
        if len(description) + len(line) + 1 > MAXIMUM_DISCORD_CHANGES:
            break
        description += line + "\n"
        kept += 1
    if kept < len(lines):
        description += f"- and {len(lines) - kept} more: see the release\n"
    number = tag.split("-", 1)[-1]
    return {
        "embeds": [{
            "title": f"SilentCartographer Build {number}",
            "url": f"{repository_url()}/releases/tag/{tag}",
            "color": DISCORD_GREEN,
            "description": "**What's changed**\n" + description,
            "fields": [{"name": "Downloads", "value": downloads()}],
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }],
    }


def discord(tag, sha):
    webhook = os.environ.get("DISCORD_RELEASES_WEBHOOK", "").strip()
    if not webhook:
        print("no DISCORD_RELEASES_WEBHOOK secret: nothing to post")
        return 0
    body = json.dumps(discord_payload(tag, sha)).encode()
    for attempt in range(1, DISCORD_ATTEMPTS + 1):
        request = urllib.request.Request(webhook, data=body, method="POST", headers={
            "Content-Type": "application/json",
            # (Discord refuses the default urllib agent)
            "User-Agent": "SilentCartographer-release-notes (https://github.com/GuvenHuso/SilentCartographer)",
        })
        try:
            with urllib.request.urlopen(request, timeout=15):
                print(f"posted {tag}'s notes to Discord")
                return 0
        except urllib.error.HTTPError as error:
            if error.code == 429:
                try:
                    wait = float(json.loads(error.read() or b"{}").get("retry_after", 5))
                except ValueError:
                    wait = 5.0
                wait = min(max(wait, 1.0), 30.0)
                print(f"Discord is rate limiting the webhook; trying again in {wait:.0f}s (attempt {attempt} of {DISCORD_ATTEMPTS})")
            elif error.code >= 500:
                wait = 5.0
                print(f"Discord answered HTTP {error.code}; trying again in {wait:.0f}s (attempt {attempt} of {DISCORD_ATTEMPTS})")
            else:
                print(f"::error::Discord answered HTTP {error.code}: {error.read().decode(errors='replace')[:300]}")
                return 1
        except (urllib.error.URLError, TimeoutError) as error:
            wait = 5.0
            print(f"could not reach Discord ({error}); trying again in {wait:.0f}s (attempt {attempt} of {DISCORD_ATTEMPTS})")
        if attempt < DISCORD_ATTEMPTS:
            time.sleep(wait)
    print(f"::warning::the release notes were not posted to Discord after {DISCORD_ATTEMPTS} attempts")
    return 1


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("notes", "discord"):
        print(__doc__, file=sys.stderr)
        return 2
    command, tag, sha = sys.argv[1:]
    if command == "notes":
        sys.stdout.write(notes(tag, sha))
        return 0
    return discord(tag, sha)


if __name__ == "__main__":
    sys.exit(main())
