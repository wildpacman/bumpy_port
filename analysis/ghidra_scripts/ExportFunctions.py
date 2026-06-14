# @category Bumpy

import csv
import os
import tempfile


def export_functions():
    args = getScriptArgs()
    if len(args) != 1:
        raise ValueError("expected output CSV path")

    output = args[0]
    functions = list(currentProgram.getFunctionManager().getFunctions(True))
    output_directory = os.path.dirname(os.path.abspath(output))
    descriptor, temporary = tempfile.mkstemp(
        dir=output_directory,
        prefix="." + os.path.basename(output) + ".",
        suffix=".tmp",
    )
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="") as stream:
            csv_writer = csv.writer(stream, lineterminator="\n")
            csv_writer.writerow(["address", "name"])
            for function in functions:
                csv_writer.writerow([
                    str(function.getEntryPoint()),
                    function.getName(),
                ])
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary is not None and os.path.exists(temporary):
            os.unlink(temporary)


export_functions()
