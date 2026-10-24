"""Check the shared stylesheet and all page sources, including feature includes."""
import subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/css_lint.py'),str(root)],check=True)
