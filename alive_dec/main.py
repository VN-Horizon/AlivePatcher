# https://ida-domain.docs.hex-rays.com/llms.txt

from __future__ import annotations

import atexit
import shutil
import tempfile
from pathlib import Path

from ida_domain import Database
from ida_domain.database import IdaCommandOptions

PACKED_DB = Path(__file__).resolve().parent.parent / "game" / "alive.exe.i64"
_tmp_dirs: list[Path] = []


def _cleanup() -> None:
    for d in _tmp_dirs:
        shutil.rmtree(d, ignore_errors=True)


atexit.register(_cleanup)


def open_packed_db() -> Database:
    tmp = Path(tempfile.mkdtemp(prefix="alive_i64_"))
    _tmp_dirs.append(tmp)
    shutil.copy2(PACKED_DB, tmp / PACKED_DB.name)
    opts = IdaCommandOptions(auto_analysis=False, new_database=False)
    return Database.open(str(tmp / PACKED_DB.name), args=opts, save_on_close=False)


def main() -> None:
    with open_packed_db() as db:
        print("packed db", db)


if __name__ == "__main__":
    main()
