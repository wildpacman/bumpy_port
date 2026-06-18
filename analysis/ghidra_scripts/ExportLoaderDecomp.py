# @category Bumpy
# Lean decompilation export: dumps decompiled C for every function, a list of
# defined strings, and any references Ghidra resolved to resource-filename
# strings. Robust to 16-bit auto-analysis missing string xrefs, because the full
# decompiled C can be grepped directly for the loader/decompressor.
#
# Usage (headless): -postScript ExportLoaderDecomp.py <absolute_output_dir>
#
# Jython (Python 2.7) syntax.

import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor


EXT_MARKERS = (".VEC", ".BUM", ".DEC", ".PAV", ".BIN", ".CAR", ".BNK", ".MID")


def main():
    args = getScriptArgs()
    if len(args) != 1:
        raise ValueError("expected one argument: output directory")
    out_dir = args[0]
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    listing = currentProgram.getListing()
    fm = currentProgram.getFunctionManager()
    refmgr = currentProgram.getReferenceManager()
    monitor = ConsoleTaskMonitor()

    # --- defined strings ---
    string_rows = []
    filename_addrs = []
    for d in listing.getDefinedData(True):
        try:
            rep = d.getDefaultValueRepresentation()
        except:
            rep = ""
        if rep and rep.startswith('"'):
            addr = d.getAddress()
            string_rows.append("%s\t%s" % (addr, rep))
            up = rep.upper()
            if any(m in up for m in EXT_MARKERS):
                filename_addrs.append((addr, rep))

    _write(os.path.join(out_dir, "strings.txt"), "\n".join(string_rows) + "\n")

    # --- references Ghidra resolved to filename strings ---
    xref_lines = []
    for addr, rep in filename_addrs:
        refs = refmgr.getReferencesTo(addr)
        any_ref = False
        for r in refs:
            any_ref = True
            frm = r.getFromAddress()
            fn = fm.getFunctionContaining(frm)
            fn_name = fn.getName() + " @" + str(fn.getEntryPoint()) if fn else "(no function)"
            xref_lines.append("%s %s  <- from %s in %s" % (addr, rep, frm, fn_name))
        if not any_ref:
            xref_lines.append("%s %s  <- (no references resolved)" % (addr, rep))
    _write(os.path.join(out_dir, "filename_xrefs.txt"), "\n".join(xref_lines) + "\n")

    # --- decompile every function ---
    decomp = DecompInterface()
    decomp.openProgram(currentProgram)

    index_lines = []
    parts = []
    funcs = list(fm.getFunctions(True))
    total = len(funcs)
    done = 0
    for fn in funcs:
        entry = fn.getEntryPoint()
        name = fn.getName()
        body_size = fn.getBody().getNumAddresses()
        index_lines.append("%s\t%s\tsize=%d" % (entry, name, body_size))
        code = None
        try:
            res = decomp.decompileFunction(fn, 60, monitor)
            if res is not None and res.decompileCompleted():
                code = res.getDecompiledFunction().getC()
        except:
            code = None
        parts.append("// ===== %s  @ %s  (size=%d) =====\n%s\n" % (
            name, entry, body_size, code if code else "// <decompilation failed>"))
        done += 1
        if done % 50 == 0:
            print("decompiled %d/%d" % (done, total))

    _write(os.path.join(out_dir, "index.txt"), "\n".join(index_lines) + "\n")
    _write(os.path.join(out_dir, "all_functions.c"), "\n".join(parts))
    decomp.dispose()
    print("export complete: %d functions, %d strings, %d filename strings" % (
        total, len(string_rows), len(filename_addrs)))


def _write(path, text):
    f = open(path, "w")
    try:
        f.write(text)
    finally:
        f.close()


main()
