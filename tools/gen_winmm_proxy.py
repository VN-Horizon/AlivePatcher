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
    "int InitWinmmProxy(void) {",
    "    if (g_ready) return 1;",
    '    g_log = CreateLogger("WinmmProxy");',
    "    if (!EmbedEnsureWinmmOrg()) return 0;",
    "",
    "    HMODULE self = NULL;",
    "    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |",
    "                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,",
    "                           (LPCSTR)&InitWinmmProxy, &self) || !self) {",
    '        g_log.Err(&g_log, "cannot resolve self module");',
    "        return 0;",
    "    }",
    "",
    "    char dllPath[MAX_PATH];",
    "    if (!GetModuleFileNameA(self, dllPath, MAX_PATH)) return 0;",
    "    char *slash = strrchr(dllPath, '\\\\');",
    "    if (!slash) slash = strrchr(dllPath, '/');",
    "    if (!slash) return 0;",
    "    slash[1] = 0;",
    '    strncat(dllPath, "winmm_org.dll", MAX_PATH - strlen(dllPath) - 1);',
    "",
    "    g_org = LoadLibraryA(dllPath);",
    "    if (!g_org) {",
    '        g_log.Err(&g_log, "LoadLibrary(%s) failed (%lu)", dllPath, GetLastError());',
    "        return 0;",
    "    }",
    "",
    "    int missing = 0;",
    "    for (int i = 0; i < WINMM_FN_COUNT; ++i) {",
    "        g_fn[i] = (void *)GetProcAddress(g_org, g_names[i]);",
    "        if (!g_fn[i]) {",
    '            g_log.Wrn(&g_log, "missing export: %s", g_names[i]);',
    "            ++missing;",
    "        }",
    "    }",
    '    g_log.Suc(&g_log, "winmm_org loaded (%d exports, %d missing)", WINMM_FN_COUNT, missing);',
    "    g_ready = 1;",
    "    return 1;",
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

/* Extract embedded winmm_org.dll and bind all of its exports into jmp stubs. */
int InitWinmmProxy(void);
HMODULE WinmmOrgModule(void);

#endif
""",
    encoding="utf-8",
)
print(f"generated {len(names)} stubs; timeGetDevCaps ok")
