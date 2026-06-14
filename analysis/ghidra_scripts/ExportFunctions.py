# @category Bumpy

import csv


def export_functions():
    args = getScriptArgs()
    if len(args) != 1:
        raise ValueError("expected output CSV path")

    output = args[0]
    functions = list(currentProgram.getFunctionManager().getFunctions(True))

    with open(output, "w", encoding="utf-8", newline="") as stream:
        csv_writer = csv.writer(stream, lineterminator="\n")
        csv_writer.writerow(["address", "name", "status", "evidence", "cpp_symbol"])
        for function in functions:
            csv_writer.writerow([
                str(function.getEntryPoint()),
                function.getName(),
                "unknown",
                "ghidra initial analysis",
                "",
            ])


export_functions()
