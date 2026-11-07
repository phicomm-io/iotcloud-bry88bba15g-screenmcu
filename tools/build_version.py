#!/usr/bin/env python3
"""Derive screen application identity from its independent Git repository."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TAG = re.compile(r'v\d+\.\d+\.\d+(?:-rc(?:\.\d+)?)?')

def metadata():
    def git(*args):
        return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()
    commit = git('rev-parse', 'HEAD')
    dirty = bool(git('status', '--porcelain', '--untracked-files=normal'))
    tags = [t for t in git('tag', '--merged', 'HEAD', '--list', 'v*').splitlines() if TAG.fullmatch(t)]
    tag = git('describe', '--tags', '--abbrev=0', *[a for t in tags for a in ('--match', t)]) if tags else None
    if tag and git('rev-parse', tag+'^{commit}') == commit and dirty:
        raise ValueError('Tagged screen application requires a clean source tree')
    version = f'{tag[1:] if tag else "0.0.0"}-{commit[:6]}' + ('-dirty' if dirty else '')
    return dict(repository='phicomm-io/iotcloud-bry88bba15g-screenmcu', commit=commit,
                tag=tag, dirty=dirty, version=version)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = metadata()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'screen_version.h').write_text('#pragma once\n#define SCREEN_VERSION '+json.dumps(data['version'])+'\n')
    (args.output/'version.json').write_text(json.dumps(data, indent=2)+'\n')
