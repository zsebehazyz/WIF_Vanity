#!/usr/bin/env python3
"""Package an already built and tested Windows x64 release. Build tool only."""
from pathlib import Path
import hashlib
import shutil
import sys
import zipfile
project=Path(__file__).resolve().parents[1]
build=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else project/'build-win'
output=project/'downloads'
output.mkdir(exist_ok=True)
executables=['SolanaVanity.exe','solana-cli.exe','solana-tests.exe','solana-gui-smoke.exe']
for name in executables:
 if not (build/name).is_file():raise SystemExit('Missing built executable: '+name)
shutil.copy2(build/'SolanaVanity.exe',output/'SolanaVanity.exe')
with zipfile.ZipFile(output/'SolanaVanity-Windows-x64.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
 for name in executables:archive.write(build/name,'SolanaVanity/'+name)
 archive.write(project/'README.md','SolanaVanity/README.md')
 for p in (project/'docs').rglob('*'):
  if p.is_file():archive.write(p,'SolanaVanity/'+str(p.relative_to(project)))
 for p in (project.parent/'docs/licenses').glob('*'):
  if p.is_file():archive.write(p,'SolanaVanity/licenses/'+p.name)
 archive.write(project/'third_party/ed25519/LICENSE.txt','SolanaVanity/licenses/orlp-ed25519.txt')
 archive.write(project/'third_party/monocypher/LICENSE.md','SolanaVanity/licenses/Monocypher.md')
lines=[]
for name in ['SolanaVanity.exe','SolanaVanity-Windows-x64.zip']:
 p=output/name;lines.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+name)
(output/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
