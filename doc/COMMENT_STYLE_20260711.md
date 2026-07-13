# Comment Style Conversion Verification — 2026-07-11

## Purpose

Verify that the uncommitted working-tree changes on branch `dev` (against
HEAD `3a70402`) alter **only comments** and leave all code untouched.
Analysis was performed by a C++ expert review with token-level
verification.

## Verdict

**All uncommitted modifications are comment-only.** No executable code,
declarations, macro bodies, preprocessor directives, or string/character
literal contents changed in any of the 15 modified files.

## Verification method

For each modified file, the HEAD version and the working-tree version
were extracted, comments were stripped in a token-preserving way, and
whitespace was normalized. Two independently implemented comment
strippers were used as a cross-check:

1. A hand-rolled Python state machine (tracks string/char literals so
   comment markers inside literals are not misinterpreted).
2. A classic Perl regex-based C/C++ comment stripper.

Under **both** strippers, the diff between the stripped HEAD and
working-tree versions was empty for every file, and the MD5 digests of
the stripped outputs matched.

Edge cases explicitly checked:

- Comment markers inside string literals — none affected.
- Preprocessor directives — only trailing comments (e.g. on `#endif`
  lines) changed; directive text itself untouched.
- Line continuations (`\`) — none affected.
- Previously commented-out code (e.g. the usage example in
  `include/ConfigParser.h`) — text untouched, only the enclosing comment
  delimiters changed.

## Per-file results

| File | Result |
|------|--------|
| `include/AirspyHFSource.h` | comment-only |
| `include/AirspySource.h` | comment-only |
| `include/AmDecode.h` | comment-only |
| `include/AudioOutput.h` | comment-only |
| `include/ConfigParser.h` | comment-only |
| `include/FileSource.h` | comment-only |
| `include/FmDecode.h` | comment-only |
| `include/NbfmDecode.h` | comment-only |
| `include/PhaseDiscriminator.h` | comment-only |
| `include/RtlSdrSource.h` | comment-only |
| `include/Source.h` | comment-only |
| `include/Utility.h` | comment-only |
| `sfmbase/AudioOutput.cpp` | comment-only |
| `sfmbase/Filter.cpp` | comment-only |
| `sfmbase/RtlSdrSource.cpp` | comment-only |

## Nature of the conversion

C-style `/* ... */` comments were converted to C++-style `//` line
comments. Doxygen block comments (`/** ... */`, `/*! ... */`) became
`///` line comments. Indentation and Doxygen semantics are preserved;
only the delimiter style changes.

Representative examples:

- `include/Utility.h:158` — trailing comment on a `#define`:

  ```c
  #define TAN_MAP_RES 0.003921569 /* (smallest non-zero value in table) */
  ```

  ```cpp
  #define TAN_MAP_RES 0.003921569 // (smallest non-zero value in table)
  ```

- `include/AirspyHFSource.h:36` — single-line Doxygen block:

  ```c
  /** Open Airspy device. */
  ```

  ```cpp
  /// Open Airspy device.
  ```

- `sfmbase/AudioOutput.cpp:323` (same pattern in `Filter.cpp:313` and
  `RtlSdrSource.cpp:404`) — trailing file-end marker:
  `/* end */` → `// end`

- Include-guard trailers (also in `AirspySource.h`, `ConfigParser.h`,
  `FileSource.h`, `Source.h`):
  `#endif /* INCLUDE_UTILITY_H_ */` → `#endif // INCLUDE_UTILITY_H_`

## Conversion tool

The untracked script `convert-comment.sh` in the repository root drove
the conversion:

```sh
#!/bin/sh
for i in sfmbase/*.cpp include/*.h main.cpp; do
  c-comments-to-cpp.py ${i} ${i}.new && mv ${i}.new ${i}
done
```

`c-comments-to-cpp.py` (MIT/zlib-licensed tool by Marcus Geelnard)
state-machine parses each line, skips comment recognition inside string
literals, and rewrites `/* */` blocks line-by-line as `//` (plain),
`///` (Doxygen `/**` / `/*!`), or `///<` (Doxygen after-member `/**<`)
while preserving indentation. Existing `//` comments and non-comment
code are left untouched.

The observed diff is exactly consistent with this tool. Files listed in
the script's loop but absent from the diff (e.g. `main.cpp` and most
`.cpp` sources) simply contained no C-style comments to convert;
`main.cpp` was confirmed to contain no `/*` sequences.
