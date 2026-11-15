# Compatibility status

CK-DOS Base focuses on the historical build formats and behavior exercised by
the preserved MS-DOS 4 source tree. Compatibility claims are deliberately
tree-scoped unless a broader behavior has its own test.

## Available natively

- MASM-compatible assembly and OMF object generation (`ckmasm`)
- OMF linking and map generation (`cklink`)
- OMF library creation (`cklib`)
- MZ-to-binary conversion (`ckexe2bin`)
- DBOF and message-generation utilities
- MZ, OMF, and library inspection tools
- NMAKE macro parsing and dependency planning (`cknmake /N`)

The automated test suite contains isolated format and behavior fixtures. The
assembler has additionally been checked against all 311 standalone assembly
objects in the preserved source tree, with byte-identical output for that set.

## Not complete yet

- The Microsoft C compiler (`CL`) is not reimplemented.
- `cknmake` does not execute recipes; it emits the planned command sequence.
- The complete MS-DOS 4 build has not yet been promoted to a portable,
  byte-identical CI gate.
- Compatibility outside the exercised MS-DOS 4 inputs is not implied.

## Road to a complete source build

1. Add a reproducible replacement for the C compiler behavior used by the tree.
2. Promote the command planner into a safely sandboxed build executor.
3. Run the complete source build on all supported hosts.
4. Compare every produced object, library, executable, and final image with a
   recorded reference manifest.
5. Publish only artifacts whose manifest passes the byte-for-byte gate.

Until those steps are complete, GitHub Actions publishes the native tool suite,
not a claimed rebuilt MS-DOS distribution.

For the prioritized, task-level breakdown, see [docs/WORK_PACKAGES.md](WORK_PACKAGES.md).
