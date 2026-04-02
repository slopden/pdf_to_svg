"""Generate MSVC import libraries (.lib) from MinGW DLLs.

MSYS2/ucrt64 packages only ship .dll.a import libraries (MinGW format).
MSVC's linker needs .lib files. This script uses dumpbin (MSVC) to extract
exports from each DLL, writes a .def file, then uses lib.exe to create
a .lib import library that MSVC can link against.
"""

import os
import re
import subprocess
import sys


def gen_implib(dll_path, lib_name, out_dir):
    result = subprocess.run(
        ["dumpbin", "/exports", dll_path],
        capture_output=True, text=True, check=True,
    )

    exports = []
    for line in result.stdout.splitlines():
        m = re.match(r"\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)", line)
        if m:
            exports.append(m.group(1))

    if not exports:
        print(f"WARNING: no exports found in {dll_path}", file=sys.stderr)
        return

    def_path = os.path.join(out_dir, f"{lib_name}.def")
    with open(def_path, "w") as f:
        f.write(f"LIBRARY {os.path.basename(dll_path)}\nEXPORTS\n")
        for exp in exports:
            f.write(f"    {exp}\n")

    lib_path = os.path.join(out_dir, f"{lib_name}.lib")
    subprocess.run(
        ["lib", f"/def:{def_path}", f"/out:{lib_path}", "/machine:x64"],
        check=True,
    )
    print(f"Created {lib_path} ({len(exports)} exports)")


if __name__ == "__main__":
    msys2 = "C:/msys64/ucrt64"
    dlls = [
        (f"{msys2}/bin/librsvg-2-2.dll", "rsvg-2"),
        (f"{msys2}/bin/libgobject-2.0-0.dll", "gobject-2.0"),
        (f"{msys2}/bin/libglib-2.0-0.dll", "glib-2.0"),
    ]
    for dll_path, lib_name in dlls:
        gen_implib(dll_path, lib_name, f"{msys2}/lib")
