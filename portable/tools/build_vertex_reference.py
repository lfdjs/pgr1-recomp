#!/usr/bin/env python3
"""Build-tree-only original SDK vertex assembler for differential tests.

EN: Rename public symbols, leaving the pinned implementation independent.
PT-BR: Renomeie símbolos públicos, mantendo a implementação fixada independente.
"""
import pathlib
import subprocess
import sys

PIN = "97b1654736f744a3966b5dff3d49be6d61ac7244"


def main():
    sdk, output = map(pathlib.Path, sys.argv[1:])
    source = subprocess.check_output(
        ["git", "-C", str(sdk), "show", f"{PIN}:runtime/gpu/vertex.cpp"],
        text=True,
    )
    source = source.replace("g_raw_vertices", "g_reference_raw_vertices")
    source = source.replace("FvfInputs(", "FvfInputsReference(")
    source = source.replace("bool Assemble(", "bool AssembleReference(")
    source = source.replace(
        "namespace gpu {",
        "namespace gpu {\nvoid FvfInputsReference(uint32_t, VertexShader&);",
        1,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(source)


if __name__ == "__main__":
    main()
