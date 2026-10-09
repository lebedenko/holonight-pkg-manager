"""Generate the deterministic extra-only five-upgrade repository fixture."""

import gzip
import io
import tarfile
from pathlib import Path

archive_bytes = io.BytesIO()
with tarfile.open(fileobj=archive_bytes, mode="w", format=tarfile.USTAR_FORMAT) as archive:
    for suffix in "abcde":
        name = f"pending-{suffix}"
        data = (
            f"%NAME%\n{name}\n\n%VERSION%\n2.0-1\n\n"
            f"%FILENAME%\n{name}-2.0-1-x86_64.pkg.tar.zst\n\n"
            "%CSIZE%\n100\n\n%ISIZE%\n100\n\n%ARCH%\nx86_64\n\n"
        ).encode()
        entry = tarfile.TarInfo(f"{name}-2.0-1/desc")
        entry.size = len(data)
        entry.mtime = 0
        archive.addfile(entry, io.BytesIO(data))
Path(__file__).with_name("extra-five.db").write_bytes(gzip.compress(archive_bytes.getvalue(), mtime=0))
