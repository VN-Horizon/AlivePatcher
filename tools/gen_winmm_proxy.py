from pathlib import Path
import re
import subprocess

root = Path(r"D:\DNV\Documents\Github\VisualNovelHorizon\AlivePatcher")
objdump = Path(r"D:\DNV\tiny_softs\cli-bin\mingw32\bin\objdump.exe")
compat = root / "src" / "compat"
org = root / "assets" / "native" / "winmm_org.dll"

out = subprocess.check_output([str(objdump), "-p", str(org)], text=True, errors="replace")
names = []
for line in out.splitlines():
    m = re.search(r"\+base\[\s*\d+\]\s+[0-9A-Fa-f]+\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", line)
    if m:
        names.append(m.group(1))

names = sorted(set(names), key=str.lower)
if not names:
    raise SystemExit("failed to parse exports from winmm_org.dll")
if "timeGetDevCaps" not in names:
    raise SystemExit("timeGetDevCaps missing from parsed exports")

(compat / "winmm.def").write_text(
    "LIBRARY winmm.dll\nEXPORTS\n" + "".join(f"    {n} = Proxy_{n}\n" for n in names),
    encoding="utf-8",
)

lines = [
    "#ifndef WIN32_LEAN_AND_MEAN",
    "#define WIN32_LEAN_AND_MEAN",
    "#endif",
    '#include "WinmmProxy.h"',
    "",
    '#include "Embed.h"',
    '#include "Logger.h"',
    "",
    "#include <stdio.h>",
    "#include <string.h>",
    "",
    "/*",
    " * Full winmm_org export surface.",
    " * Other modules in-process (e.g. nvd3dum.dll) also bind to this winmm.dll,",
    " * so a trimmed export set breaks GPU/driver loads.",
    " */",
    f"enum {{ WINMM_FN_COUNT = {len(names)} }};",
    "",
    "static HMODULE g_org;",
    "static void *g_fn[WINMM_FN_COUNT];",
    "static Logger g_log;",
    "static int g_ready;",
    "",
    "static const char *const g_names[WINMM_FN_COUNT] = {",
]
for n in names:
    lines.append(f'    "{n}",')
lines += [
    "};",
    "",
    "HMODULE WinmmOrgModule(void) { return g_org; }",
    "",
    "static int BindWinmmExports(void) {",
    "    int missing = 0;",
    "    for (int i = 0; i < WINMM_FN_COUNT; ++i) {",
    "        g_fn[i] = (void *)GetProcAddress(g_org, g_names[i]);",
    "        if (!g_fn[i]) {",
    '            g_log.Wrn(&g_log, "missing export: %s", g_names[i]);',
    "            ++missing;",
    "        }",
    "    }",
    '    g_log.Suc(&g_log, "winmm loaded (%d exports, %d missing)", WINMM_FN_COUNT, missing);',
    "    g_ready = 1;",
    "    return 1;",
    "}",
    "",
    "int InitWinmmProxy(void) {",
    "    if (g_ready) return 1;",
    '    g_log = CreateLogger("WinmmProxy");',
    "",
    "    char dllPath[MAX_PATH];",
    "    if (EmbedEnsureWinmmOrg(dllPath, sizeof(dllPath))) {",
    "        g_org = LoadLibraryA(dllPath);",
    "        if (g_org) return BindWinmmExports();",
    '        g_log.Wrn(&g_log, "LoadLibrary(%s) failed (%lu)", dllPath, GetLastError());',
    "    }",
    "",
    "    char sys[MAX_PATH];",
    "    if (GetSystemDirectoryA(sys, MAX_PATH)) {",
    '        const int n = _snprintf(dllPath, sizeof(dllPath), "%s\\\\winmm.dll", sys);',
    "        if (n > 0 && (size_t)n < sizeof(dllPath)) {",
    "            g_org = LoadLibraryA(dllPath);",
    "            if (g_org) {",
    '                g_log.Wrn(&g_log, "using system winmm: %s", dllPath);',
    "                return BindWinmmExports();",
    "            }",
    "        }",
    "    }",
    "",
    '    g_log.Err(&g_log, "cannot load real winmm");',
    "    return 0;",
    "}",
    "",
]

for i, n in enumerate(names):
    lines += [
        f"__attribute__((naked)) void Proxy_{n}(void) {{",
        "    __asm__ __volatile__(",
        '        "jmp *%0"',
        "        :",
        f'        : "m"(g_fn[{i}])',
        "    );",
        "}",
        "",
    ]

(compat / "WinmmProxy.c").write_text("\n".join(lines), encoding="utf-8")
(compat / "WinmmProxy.h").write_text(
    """#ifndef WINMMPROXY_H
#define WINMMPROXY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Load real winmm (extracted cache, else System32) and bind exports into jmp stubs. */
int InitWinmmProxy(void);
HMODULE WinmmOrgModule(void);

#endif
""",
    encoding="utf-8",
)
print(f"generated {len(names)} stubs; timeGetDevCaps ok")
