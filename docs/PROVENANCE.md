# Source provenance

## Native tools

The code under `include/dosbase`, `src/devtools`, and `tests` is an independent
clean-room implementation. Compatibility behavior was established by comparing
small, purpose-built inputs and outputs with legitimately obtained historical
tools. Historical executable files are not linked into, embedded in, or needed
at runtime by the native tools.

## MS-DOS 4 source snapshot

Microsoft published the MS-DOS 4.0 source at:

- <https://github.com/microsoft/MS-DOS>

Some files in the published archive contain Unicode replacement characters
where the original byte stream was damaged. This repository preserves the
repair release maintained at:

- Repository: <https://github.com/chasonr/MS-DOS.git>
- Tag: `release-1`
- Commit: `95deabd0d309ac8157f0c738a22910508de64050`
- Tree: `a74039742f9f419a138d3eb613a285ef7ef43ccb`

That repair restores damaged bytes, original line endings, and build scripts;
it is not presented as a new operating-system implementation. The vendored
snapshot's MIT license is retained at `third_party/ms-dos-4/LICENSE`.

The snapshot includes historical build executables because they are part of
the licensed upstream tree. CK-DOS Base CI does not redistribute them in native
tool packages, and the native tools do not invoke them.

## Clean public boundary

This repository is self-contained. Its source files, tests, workflows, and
documentation must not depend on private downstream repositories. A downstream
product may import a released CK-DOS Base version; dependencies never point in
the reverse direction.
