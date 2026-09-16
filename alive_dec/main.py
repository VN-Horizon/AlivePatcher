# https://ida-domain.docs.hex-rays.com/llms.txt

from pathlib import Path

from ida_domain import Database
from ida_domain.database import IdaCommandOptions

DB_PATH = Path(__file__).resolve().parent.parent / "game" / "alive.exe.i64"


def decomp(ea: int) -> str:
    import ida_hexrays

    c = ida_hexrays.decompile(ea)
    return str(c) if c else "<fail>"


def main() -> None:
    ida_options = IdaCommandOptions(auto_analysis=True, new_database=False)
    with Database.open(str(DB_PATH), args=ida_options, save_on_close=False) as db:
        import ida_bytes
        import struct

        print("===== BGM name table off_6C5294 =====")
        ea = 0x6C5294
        for i in range(40):
            ptr = ida_bytes.get_dword(ea + i * 4)
            if not ptr:
                print(f"  [{i}] NULL")
                break
            s = ida_bytes.get_strlit_contents(ptr, -1, 0)
            print(f"  [{i}] {hex(ptr)} {s!r}")

        print("\naM30 =", hex(ida_bytes.get_dword(0x6C5294 + 32 * 4)) if False else "end marker")
        # find aM30 address
        for item in db.strings:
            if item.contents in (b"M30", b"M01", b"M02", b"AR_BGM"):
                print(hex(item.address), item.contents)

        print("\n===== sub_68A3E0 (play track) =====")
        print(decomp(0x68A3E0))

        print("\n===== vtable at this+23500 methods used =====")
        # offset 5875 * 4 = 23500 - the CD player object
        # methods +20, +28, +72, +76
        print("this+23500 is CD audio device object")

        print("\n===== prologue 42DCF0 =====")
        raw = ida_bytes.get_bytes(0x42DCF0, 16)
        print(raw.hex())
        for insn in db.instructions.get_between(0x42DCF0, 0x42DD10):
            print(f"  {hex(insn.ea)}: {db.instructions.get_disassembly(insn)}")


if __name__ == "__main__":
    main()
