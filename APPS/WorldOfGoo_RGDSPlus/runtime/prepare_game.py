"""Prepare a private direct-rendering copy of the verified GOG executable."""
import hashlib
from pathlib import Path
import sys

root = Path(sys.argv[1])
original = root / "game/WorldOfGoo.bin.x86_64"
target = original.with_name("WorldOfGoo.rgds.x86_64")
original_sha = "9f8e6a907f6cc2ea78b695fce9a4683d7836d3e83b0af9ef0af422ec6fdea898"
# Direct rendering disables FBO allocation. Mouse conversion still calls the
# FBO dimension getters, so give those callers the current drawable dimensions.
edits = (
    (0x1A719D, "0f 94 83 38 01 00 00", "c6 83 38 01 00 00 00"),
    (0x1A5290, "8b 87 3c 01 00 00 c3", "8b 87 2c 01 00 00 c3"),
    (0x1A52A0, "8b 87 40 01 00 00 c3", "8b 87 30 01 00 00 c3"),
)
data = original.read_bytes()
if hashlib.sha256(data).hexdigest() != original_sha:
    sys.exit("Unsupported World of Goo executable for the direct-rendering patch")
patched = bytearray(data)
for offset, expected, replacement in edits:
    before, after = bytes.fromhex(expected), bytes.fromhex(replacement)
    if len(before) != len(after) or data[offset:offset+len(before)] != before:
        sys.exit(f"Unexpected game instruction at {offset:#x}")
    patched[offset:offset+len(before)] = after
if not target.is_file() or target.read_bytes() != patched:
    staging = target.with_suffix(".tmp")
    staging.write_bytes(patched)
    staging.chmod(0o755)
    staging.replace(target)
print("Verified direct-rendering executable ready", flush=True)
