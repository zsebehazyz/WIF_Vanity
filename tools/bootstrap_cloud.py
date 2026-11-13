#!/usr/bin/env python3
"""Reproduce this cloud toolchain without root or changes to system packages.
Run from the repository root. Downloads use TLS, NVIDIA SHA256 manifests,
and Debian signed package metadata. No GPU is required for compilation.
"""
import glob
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile

ROOT = Path('/workspace/.tools')
ROOT.mkdir(parents=True, exist_ok=True)

def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)

def download(url, path):
    run('curl', '-fsSL', '--retry', '2', '--max-time', '180', url, '-o', path)

if not (ROOT/'python/cmake/data/bin/cmake').exists():
    run(sys.executable, '-m', 'pip', 'install', '--target', ROOT/'python',
        'cmake==4.4.4', 'ninja==1.13.2')

cuda = ROOT/'cuda13'
metadata = ROOT/'cuda13-redist.json'
if not metadata.exists():
    download('https://developer.download.nvidia.com/compute/cuda/redist/redistrib_13.1.1.json', metadata)
manifest = json.loads(metadata.read_text())
for name in ('cuda_nvcc','cuda_cudart','cuda_cccl','cuda_crt','libnvvm'):
    spec = manifest[name]['linux-x86_64']
    package = ROOT/(name+'13.tar.xz')
    def verified():
        return package.exists() and hashlib.sha256(package.read_bytes()).hexdigest() == spec['sha256']
    if not verified():
        download('https://developer.download.nvidia.com/compute/cuda/redist/'+spec['relative_path'], package)
    if not verified():
        raise RuntimeError('SHA256 verification failed: '+name)
    cuda.mkdir(exist_ok=True)
    with tarfile.open(package) as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts[1:]
            if not parts:
                continue
            member.name = str(Path(*parts))
            archive.extract(member, cuda, filter='data')
    print(name, 'verified', flush=True)
if not (cuda/'lib64').exists():
    (cuda/'lib64').symlink_to('lib')

apt = ROOT/'apt'
(apt/'lists/partial').mkdir(parents=True,exist_ok=True)
(apt/'cache/archives/partial').mkdir(parents=True,exist_ok=True)
(apt/'sources.list').write_text('deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main\n')
opts = ['-o',f'Dir::Etc::sourcelist={apt}/sources.list','-o','Dir::Etc::sourceparts=-',
        '-o',f'Dir::State::lists={apt}/lists','-o',f'Dir::Cache={apt}/cache']
run('/usr/bin/apt-get',*opts,'update')
debs = ROOT/'debs'
debs.mkdir(exist_ok=True)
packages = ['binutils-mingw-w64-x86-64','mingw-w64-common','mingw-w64-x86-64-dev',
            'gcc-mingw-w64-base','gcc-mingw-w64-x86-64-posix-runtime',
            'gcc-mingw-w64-x86-64-posix','g++-mingw-w64-x86-64-posix']
run('/usr/bin/apt-get',*opts,'download',*(p+'/trixie' for p in packages),cwd=debs)
mingw = ROOT/'mingw'
mingw.mkdir(exist_ok=True)
for package in packages:
    for deb in debs.glob(package+'_*.deb'):
        run('dpkg-deb','-x',deb,mingw)
print('Ready: CMake, CUDA 13.1, MinGW x64 in', ROOT)
