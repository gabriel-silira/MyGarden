"""Compila e executa os testes C++ locais, sem ESP32 conectado.

Uso: python tools/test_host.py
Opcional: MYGARDEN_CXX = caminho absoluto para g++/clang++.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
candidates = [
    os.environ.get('MYGARDEN_CXX'), shutil.which('g++'), shutil.which('clang++'),
    'C:/msys64/mingw64/bin/g++.exe', 'C:/msys64/ucrt64/bin/g++.exe',
]
compiler = next((Path(p) for p in candidates if p and Path(p).is_file()), None)
if compiler is None:
    raise SystemExit('Compilador C++ ausente. Configure MYGARDEN_CXX para g++ ou clang++.')

build = root / 'build' / 'host'
build.mkdir(parents=True, exist_ok=True)
binary = build / ('test_safety.exe' if os.name == 'nt' else 'test_safety')
environment = dict(os.environ)
environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
command = [str(compiler), '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
           '-I', str(root / 'lib' / 'GardenSafety'),
           str(root / 'test' / 'host' / 'test_safety.cpp'),
           str(root / 'lib' / 'GardenSafety' / 'GardenSafety.cpp'), '-o', str(binary)]
print('Compilador:', compiler, flush=True)
subprocess.run(command, cwd=root, env=environment, check=True)
subprocess.run([str(binary)], cwd=root, env=environment, check=True)
