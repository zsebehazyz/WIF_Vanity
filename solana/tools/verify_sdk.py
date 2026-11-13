#!/usr/bin/env python3
"""Independent integration check. Development only: pip install solders==0.29.0.
Takes the CLI executable and a directory of PUBLIC RFC8032 test key fixtures.
Never use these published fixture keys for funds.
"""
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
from solders.keypair import Keypair
from solders.pubkey import Pubkey
cli, fixtures = sys.argv[1:]
count = 0
for path in sorted(Path(fixtures).glob('*.json')):
    values=json.loads(path.read_text())
    assert len(values)==64
    pair=Keypair.from_bytes(bytes(values))
    assert bytes(pair.pubkey())==bytes(values[32:])
    assert bytes(Keypair.from_seed(bytes(values[:32])))==bytes(values)
    actual=subprocess.check_output([cli,'verify',str(path)],text=True).strip()
    assert actual==str(pair.pubkey())
    signature=pair.sign_message(b'SolanaVanity SDK interoperability test')
    assert signature.verify(pair.pubkey(),b'SolanaVanity SDK interoperability test')
    count+=1
assert count==3,'Expected three published RFC8032 fixtures'
rng=random.Random(0x534f4c414e41) # Public test data only, never the application's RNG.
samples=[bytes(rng.randrange(256) for _ in range(32)) for _ in range(64)]
samples += [bytes(i)+bytes([1])+bytes(31-i) for i in range(32)]
samples += [bytes(32),bytes([255])*32]
for data in samples:
    actual=subprocess.check_output([cli,'encode-public',data.hex()],text=True).strip()
    assert actual==str(Pubkey.from_bytes(data)),'Base58 SDK mismatch'
print(f'PASS solders 0.29.0: {count} keypair JSON imports, seed/public roundtrips and signatures; {len(samples)} public-key Base58 cases')
