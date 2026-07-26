# American English spelling pass (2026-07-26)

CLAUDE.md gained the rule *"Use only American English spelling for
documentation."* Several documents written earlier by Claude Code used British
spelling. This pass corrects them. Done on branch `dev-doc-spelling`, cut from
`dev` at `b486c58`, and extended afterwards to three source-code comments
(§6) in a follow-up commit on `dev`.

## Executive summary

- **Scope.** `doc/*.md` and the top-level `CHANGES.md` — 87 word occurrences
  on 84 lines across 11 files (§1, §2) — followed by 3 occurrences on 3 lines
  in C++ comments (§6).
- **Rule applied.** Spelling only. No word was substituted for a different
  word, no sentence was rewritten, and no code, identifier, filename, or
  command-line option was touched.
- **No behavior change.** Everything outside §6 is documentation. §6 is
  comment text alone: no statement, declaration, or literal was touched, and
  `clang-format` reports no reflow.

## 1. Documentation files changed

| file | lines |
|---|---|
| `doc/MULTIPATH_FILTER_DESIGN_20260724.md` | 47 |
| `doc/OFFSET_MEASUREMENT_20260725.md` | 12 |
| `doc/PLL_ANALYSIS_3_20260724.md` | 7 |
| `doc/LATENCY_MEASUREMENT_20260725.md` | 4 |
| `doc/RTL_READ_ASYNC_20260713.md` | 4 |
| `doc/PLL_EXPERIMENT_2_20260724.md` | 3 |
| `doc/LPF_VOLK_20260725.md` | 1 |
| `doc/PLL_ANALYSIS_2_20260723.md` | 1 |
| `doc/SUMMARY-20260713-0.md` | 1 |
| `doc/SUMMARY-20260725-0.md` | 1 |
| `CHANGES.md` | 3 |

Three lines carried two corrections each, hence 87 occurrences on 84 lines.

## 2. Words corrected in the documentation

| British | American | n |
|---|---|---|
| `analysed` | `analyzed` | 10 |
| `synthesised` | `synthesized` | 10 |
| `behaviour` | `behavior` | 10 |
| `equaliser` | `equalizer` | 6 |
| `normalised` | `normalized` | 4 |
| `cancelled` | `canceled` | 4 |
| `analyse` | `analyze` | 3 |
| `synthesises` | `synthesizes` | 3 |
| `normalisation` | `normalization` | 3 |
| `optimised` / `Optimised` | `optimized` / `Optimized` | 3 + 1 |
| `behavioural` | `behavioral` | 2 |
| `unnormalised` | `unnormalized` | 2 |
| `prioritisation` | `prioritization` | 2 |
| `behaviours` | `behaviors` | 1 |
| `favourably` | `favorably` | 1 |
| `synthesise` | `synthesize` | 1 |
| `normalising` | `normalizing` | 1 |
| `renormalises` | `renormalizes` | 1 |
| `optimising` | `optimizing` | 1 |
| `quantise` | `quantize` | 1 |
| `quantises` | `quantizes` | 1 |
| `quantisation` | `quantization` | 1 |
| `penalised` | `penalized` | 1 |
| `amortised` | `amortized` | 1 |
| `Minimising` | `Minimizing` | 1 |
| `equalise` | `equalize` | 1 |
| `generalise` | `generalize` | 1 |
| `Centre` | `Center` | 1 |
| `Centring` | `Centering` | 1 |
| `artefacts` | `artifacts` | 1 |
| `defence` | `defense` | 1 |
| `signalling` | `signaling` | 1 |
| `signalled` | `signaled` | 1 |
| `levelling` | `leveling` | 1 |
| `modelled` | `modeled` | 1 |
| `towards` | `toward` | 1 |
| `analyses` (verb) | `analyzes` | 1 |

## 3. Judgment calls

**`analyses` had to be read in context.** Only the verb form changes
(`analyses` → `analyzes`); the plural noun of *analysis* is spelled identically
in American English. `doc/PLL_EXPERIMENT_20260724.md:369` and `:545` are plural
nouns and were left alone, as are the two in `CHANGES.md`. The one verb, in
`CHANGES.md`'s 20260725 clock-offset entry (`re-analyses saved captures`), was
corrected.

**`towards` → `toward`** — `doc/OFFSET_MEASUREMENT_20260725.md:56`. This is a
variant-form difference rather than a letter swap; `toward` is the American
form and `towards` chiefly British, so it is included here. It is the one
change in this pass that a reader could reasonably call a word substitution.

**One change lands inside a fenced code block** —
`doc/MULTIPATH_FILTER_DESIGN_20260724.md:1483`, the shell comment
`# 1. synthesise the channel into test-files/`. It is prose in a comment, so
the command it annotates is unaffected. Every other corrected occurrence sits
outside code fences, and none was inside a verbatim quotation of a source-code
comment (checked explicitly — for example §8.2's quotation of
`MultipathFilter.h:40-44` ends before the word `unnormalised`, which is the
document's own prose).

## 4. Anchors

`doc/PLL_ANALYSIS_3_20260724.md` has two table-of-contents links whose targets
contain the corrected word:

```
1. [The loop being analysed](#1-the-loop-being-analysed)
4. [Comparison across the loops analysed to date](#4-comparison-across-the-loops-analysed-to-date)
```

Heading text and anchor were corrected together, so both links still resolve.

## 5. Method

Detection was a case-insensitive scan of `doc/*.md` for the standard
British/American divergence classes, not a fixed word list alone:

- `-ise`/`-isation`/`-yse` verb and noun endings (`[a-z]{4,}(ise|ised|ises|ising|isation|yse|ysed|yses|ysing)`),
  then hand-triaged, since many hits are spelled the same in both variants
  (`exercise`, `promise`, `premises`, `compromise`, `expertise`, `advertised`,
  `likewise`, `pointwise`, `otherwise`, `precise`, `surprise`).
- `-our` (`behaviour`, `favour`, `colour`, …).
- `-re` (`centre`, `metre`, `fibre`, `calibre`, …).
- `-ce`/`-se` noun pairs (`defence`, `offence`, `licence`, `pretence`).
- Doubled final `l` before a suffix (`cancelled`, `signalling`, `levelling`,
  `modelled`, `travelled`, `labelled`, …), excluding the many words that
  double in both variants (`called`, `controlled`, `installed`, `falling`,
  `stalling`, `filled`, `rolled`, `pulled`, `compelling`, `telling`).
- Miscellaneous single words (`artefact`, `programme`, `grey`, `whilst`,
  `amongst`, `aluminium`, `manoeuvre`, `sceptical`, `judgement`, `learnt`,
  `spelt`, `mould`, `focussed`, …) — of these only `artefacts` was present.

A repeat of the same scan after the edit reports no remaining hits other than
the four `analyses` plural nouns noted in §3.

## 6. Source-code comments

The same scan of §5 was run over `include/`, `sfmbase/`, and `main.cpp`. It
found four hits, three of which were corrected in a follow-up commit:

| file:line | British | American |
|---|---|---|
| `sfmbase/RtlSdrSource.cpp:383` | `cancelled` | `canceled` |
| `sfmbase/RtlSdrSource.cpp:387` | `cancelled` | `canceled` |
| `main.cpp:612` | `signalling` | `signaling` |

All three are comment prose. No statement, declaration, string literal, or
identifier was touched, so the binary is unchanged apart from recompilation.
Both corrections shorten their line, and `clang-format --dry-run -Werror`
reports no diff on either file, so no comment reflow was triggered.

The fourth hit is **not** a defect and was left alone:
`sfmbase/MultipathFilter.cpp:26` cites *"Automatic Cancelling of FM …"* by
Mochizuki and Hatori. That is the published title of the paper, quoted
verbatim, and a citation keeps its source's spelling regardless of house style.

## 7. Not changed

- **Top-level `README.md`.** Scanned; no British spelling found, so it is
  unchanged.
- **The `analyses` plural nouns** of §3, and the paper title above.

Sections 1–5 were committed together with the CLAUDE.md rule that prompted
them; §6 followed once the rule's reach was extended past documentation to
comments.
