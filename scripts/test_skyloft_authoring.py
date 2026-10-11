"""Builds Skyloft Isles from its authoring scripts and runs the automated walkability and collision checks (no Blender, no generated headers needed)."""
import subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
r = subprocess.run([sys.executable, str(root / 'tools/maps/skyloft/validate.py'), '--report', str(root / 'build/skyloft-validation.json')], capture_output=True, text=True)
print(r.stdout[-3000:], r.stderr[-1500:])
sys.exit(r.returncode)
