"""Checks Hyrule Before the Third Storm's generated data (places, chests on floors, water boxes, collision, drawing windows): no Blender needed."""
import subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
r = subprocess.run([sys.executable, str(root / 'tools/maps/hyrule_third_storm/validate.py'), '--report', str(root / 'build/hyrule-third-storm-validation.json')], capture_output=True, text=True)
print(r.stdout[-3000:], r.stderr[-1500:])
sys.exit(r.returncode)
