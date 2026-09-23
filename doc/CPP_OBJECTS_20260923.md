# C++ object mutability review: `const`, references, and `std::move`

Date: 2026-09-23
Scope: `main.cpp`, `include/*.h`, `sfmbase/*.cpp` on branch **`dev`**
(commit `db75c95`), roughly 10.6k lines. `r8brain-free-src/`, `build/`,
`work/`, and `test-files/` are excluded (third-party / generated / scratch).
Editor backup files (`*~`) are excluded.
This is a read-only review report; no source code was changed.

Guiding principle throughout: **keep the number of mutable objects as small
as possible.** Every finding below is judged against that, not against a
generic style guide.

---

## Summary

| Category | Clear defect | Perf-relevant (no numerical change) | Style / improvement |
| --- | ---: | ---: | ---: |
| 1. `const` usage | 1 (meaningless `const` on by-value return) | 0 | ~90 (locals/params, grouped) + 19 member functions + ~28 member variables |
| 2. References | 0 | 1 (by-value hot-path copy) | ~12 cross-cutting sites (by-value `std::string`/`IQSampleCoeff&`) + a few output-param/range-for notes |
| 3. `std::move` | 1 (move into a `const&` parameter) | 6 (member-buffer draining) | 0 |

Total `std::move` call sites in scope: 15 (listed individually in §3, as
requested). Everything in §2 and §3, and every member function/member
variable candidate in §1, is listed individually below; only the very
numerous local-variable/by-value-scalar-parameter `const` opportunities in
§1.1 are grouped with representative examples and counts.

Nothing recommended here changes numerical behavior. Two items are flagged
explicitly as needing measurement before acting on them (§1.3's
Airspy/RTL-SDR string-cache members, and the member-buffer-draining `move`s
in §3), because their benefit depends on allocator/caller behavior that this
static read cannot fully settle.

---

## Prioritized top findings

1. **`main.cpp:1049`** — `audio_output->write(std::move(audiosamples))` moves
   into a parameter declared `const SampleVector &`. The move is a no-op;
   it should simply be `write(audiosamples)`. This is the exact "moving into
   a parameter taken by const reference" pattern called out in the review
   brief. Clear defect, trivial fix, zero risk. See §3.3.

2. **`AmDecoder::process` takes `samples_in` by value but never moves it on
   any reachable code path** (`sfmbase/AmDecode.cpp:98-153`), so every call
   pays a full heap allocation and copy of the IF block for nothing. The
   `std::move(samples_in)` at lines 141 and 151 is dead code (see §3.2 for
   why). `const IQSampleVector &samples_in` would remove the copy entirely.
   This is a real per-block cost in a hot path, only masked because the
   default demo path is FM, not AM. See §2.1 and §3.2.

3. **`FmDecoder`, `AmDecoder`, `NbfmDecoder` constructors take
   `IQSampleCoeff &fmfilter_coeff` / `&amfilter_coeff` / `&nbfmfilter_coeff`
   by non-const reference**, immediately bind them to a `const IQSampleCoeff&`
   member, and never write through them. Should be `const IQSampleCoeff &`.
   See §2.2.

4. **`Source::configure(std::string configuration)`** and its four overrides,
   plus `ConfigParser::parse_config_string`/`split_delimiter`/
   `split_equal_sign`, all take `std::string` by value along a call chain
   that copies the same short string two or three times per `configure()`
   call, purely to read it. None of the seven sites ever moves or mutates
   the parameter. Low absolute cost (config strings are short and this runs
   once at startup) but a clean, no-risk, cross-cutting fix. See §2.1.

5. **The `Source` interface's `get_sample_rate()`, `get_frequency()`,
   `is_low_if()`, and `print_specific_parms()` are declared non-`const`
   pure virtuals**, and all sixteen overrides across the four device classes
   are pure reads. Constifying this would require touching the base class
   and all four derived headers/`.cpp` files together — a real, low-risk
   improvement, but a coordinated one rather than a local edit. See §1.2.

6. **A recurring pattern of `std::move`-ing a persistent scratch member
   buffer into another persistent member or into an output parameter**
   (`FmDecode.cpp:112,128,132,224`, `AmDecode.cpp:218`, `NbfmDecode.cpp:96`)
   permanently discards that member's capacity, so its next `.process()`
   call must reallocate. This is most consequential for
   `m_samples_in_after_agc` in `FmDecoder`, which is drained on **every
   block** whenever the multipath filter is off — i.e. in the default
   configuration. `std::swap` would avoid the value copy without discarding
   capacity from either side, converging to a steady, non-reallocating state
   once block sizes stabilize (assuming the destination buffer is
   itself long-lived — see caveat in §3.4). Flagged as perf-relevant, not
   as a defect; would need a profile to confirm the benefit. See §3.4.

7. **`AudioOutput::get_device_name()`** (`include/AudioOutput.h:55`) is
   `const std::string get_device_name() { return m_device_name; }`: the
   `const` on the by-value return type is meaningless, and the method itself
   — which only reads `m_device_name` — is missing the `const` that would
   actually matter (on the member function). See §1.5.

8. **`ConfigParser` has no data members at all**, so `parse_config_string`,
   `split_delimiter`, and `split_equal_sign` could all be `const` (none of
   them touch `*this`). See §1.2.

---

## 1. `const` usage

### 1.1 Local variables and by-value scalar parameters never modified after init

This is by far the largest and most repetitive category, exactly as
expected in a DSP codebase full of `unsigned int n = samples_in.size();`
at the top of every `process()`. Per the review instructions, these are
grouped with representative examples and approximate counts rather than
listed exhaustively; none of them are large objects (that's §2), so none
of them have a performance angle — this is pure `const`-correctness /
readability.

**Pattern A — size/count locals taken once and never reassigned**
(`unsigned int n = ...size();`, `size_t input_size = ...`, etc.), roughly
27 instances:
- `sfmbase/Filter.cpp:53-54,110-111` (`order`, `n` in both `process()`
  overloads) — `order` is a straight copy of `m_order` and never changes.
- `sfmbase/FmDecode.cpp:141` (`decoded_size`), `234` (`n` in `demod_stereo`),
  `249` (`n` in `mono_to_left_right`), `263`, `281`.
- `sfmbase/AmDecode.cpp:184` (`decoded_size`), `224`, `232` (`n` in
  `demodulate_am`/`demodulate_dsb`).
- `sfmbase/NbfmDecode.cpp:63` (`decoded_size`).
- `sfmbase/AfSimpleAgc.cpp:38`, `sfmbase/IfSimpleAgc.cpp:39` (`n`).
- `sfmbase/FineTuner.cpp:56-57` (`tblsiz`, `n`).
- `sfmbase/PhaseDiscriminator.cpp:35` (`n`).
- `sfmbase/PilotPhaseLock.cpp:67` (`n`).
- `sfmbase/MultipathFilter.cpp:231` (`n`).
- `sfmbase/AudioResampler.cpp:41` (`input_size`), `sfmbase/IfResampler.cpp:41`
  (`input_size`).
- `main.cpp:975` (`if_samples_size`), `1028` (`audiosamples_size`).

**Pattern B — booleans/doubles/floats computed once and read-only afterward**,
roughly 20+ instances, e.g.:
- `main.cpp:691` (`freq`), `693` (`tuner_freq`), `703`
  (`enable_fs_fourth_downconverter`), `707-709` (`fm_target_rate`,
  `am_target_rate`, `nbfm_target_rate`), `730-731` (`stat_rate`, could be
  combined with its `std::max` initializer and made `const` directly),
  `770-772` (`demodulator_rate`, `total_decimation_ratio`,
  `audio_decimation_ratio`), `976` (`if_exists`), `1000` (`if_level_db`),
  `1029` (`audio_exists`), `1061` (`pilot_level`), `1063` (`stereo_status`),
  `1082` (`audio_level_db`), `1107-1108` (`if_agc_gain_db`).
- `sfmbase/Filter.cpp:239` (`w`), `244` (`p1s`), `247` (`p1z`), `266` (`g`) in
  `HighPassFilterIir::HighPassFilterIir`.
- `sfmbase/PilotPhaseLock.cpp:71` (`was_locked`), `87-88` (`psin`, `pcos`),
  `102-104` (`x`, `phasor_i`, `phasor_q`), `107-108` (`new_phasor_i`,
  `new_phasor_q`), `116` (`phase_err`), `127` (`new_phase_err`) — all in the
  per-sample hot loop; making these `const` is pure readability, the
  compiler already treats them as such.
- `sfmbase/AudioOutput.cpp:77` (`filetype`), `160` (`size`), `172-174`
  (`length` could be folded into its assignment and made `const`), `182`
  (`logmsg`), `206` (`index`), `302` (`sample_size`), `325` (`addmsg`).
- `sfmbase/FileSource.cpp:423` (`expected`), `427` (`one_us`) — `auto`
  locals of a small `std::chrono::microseconds` type, never reassigned.
- `sfmbase/ConfigParser.cpp:83` (`tokens`), `85` (`element`).

**Not recommended for `const`**, and correctly so: loop counters that are
read after the loop (e.g. `AirspySource.cpp:315-322`'s `i`), variables built
up across branches before a single later use (`main.cpp:504-509`
`squelch_level`, `701-726` `if_blocksize` — could be made `const` by
restructuring the assignment into a single ternary/switch-expression, but
that is a larger, more debatable rewrite than a one-line `const` add, so it
is called out here rather than folded into the count above), and anything
already mutated in a visible way.

### 1.2 Member functions that do not modify state but are not `const`

Every instance found, listed individually as requested.

| Site | Function | Notes |
| --- | --- | --- |
| `include/DataBuffer.h:71` / `.h:90` / `.h:117` | `dropped_blocks()`, `queue_size()`, `pull_end_reached()` | Each only reads a member under `m_mutex`. Could be `const` if `m_mutex` were declared `mutable` (a `std::mutex` lock is the standard justification for `mutable`). `push()`/`push_end()`/`pull()` correctly stay non-`const` since they mutate. |
| `include/AudioOutput.h:55` | `AudioOutput::get_device_name()` | Only reads `m_device_name`. See §1.5 for the accompanying return-type issue. |
| `include/Source.h:38,41,44,50` (pure virtuals) and all 16 overrides: `include/AirspySource.h:45,48,51,54` + `sfmbase/AirspySource.cpp:184,186,189,191`; `include/AirspyHFSource.h:45,48,51,54` + `sfmbase/AirspyHFSource.cpp:163,165,167,169`; `include/RtlSdrSource.h:44,47,50,53` + `sfmbase/RtlSdrSource.cpp:274,279,282,284`; `include/FileSource.h:51,54,57,60` + `sfmbase/FileSource.cpp:287,289,291,293` | `get_sample_rate()`, `get_frequency()`, `is_low_if()`, `print_specific_parms()` | Every override in every device backend is a pure read (of a member, or in `RtlSdrSource`'s case, of the hardware via `m_dev`, itself unmodified). This is a single design decision applied in 20 places at once: constifying the four base-class pure virtuals would let all 16 overrides become `const` too. Verified none of the 16 overrides write any member. |
| `include/RtlSdrSource.h:81,84` + `sfmbase/RtlSdrSource.cpp:298,301` | `get_tuner_gain()`, `get_tuner_gains()` | Both only read `m_dev` / build a local return value. |
| `include/FileSource.h:109,111,112,114` + `sfmbase/FileSource.cpp:303,330,354,269` | `to_sf_format()`, `get_major_format()`, `get_sub_type()`, `round_power()` | `to_sf_format` touches no member at all (pure function of its argument). `get_major_format`/`get_sub_type` only read `m_sfp` (never write it) via `sf_command()`. `round_power` touches no member. |
| `include/ConfigParser.h:35,44,49` + `sfmbase/ConfigParser.cpp:81,25,51` | `parse_config_string()`, `split_delimiter()`, `split_equal_sign()` | `ConfigParser` declares **no data members at all** — every method is a pure function of its arguments and could be `const` (or `static`). |
| `include/FmDecode.h:109,113,117,123` + `sfmbase/FmDecode.cpp:229,247,260,279` | `demod_stereo()`, `mono_to_left_right()`, `stereo_to_left_right()`, `zero_to_left_right()` | None of the four touch any `FmDecoder` member; they operate solely on their by-reference parameters. Could be `const` (or `static`). |
| `include/AmDecode.h:67,70` + `sfmbase/AmDecode.cpp:222,230` | `demodulate_am()`, `demodulate_dsb()` | Same as above — no member access. |

### 1.3 Member variables set only in the constructor that could be `const`

Every instance found, listed individually. Where noted, adding `const`
disables copy/move **assignment** for the class (construction is
unaffected); in every case checked here the class is only ever
default/parameter-constructed once and never assigned, so the trade-off is
free in practice — but it is called out per the review brief regardless.

| Class | Candidate members | Already-mutated members (correctly left non-`const`) |
| --- | --- | --- |
| `AfSimpleAgc` (`include/AfSimpleAgc.h:56-60`) | `m_initial_gain`, `m_max_gain`, `m_reference`, `m_distortion_rate` | `m_current_gain` (rewritten every sample in `process()` and by `reset_gain()`) |
| `IfSimpleAgc` (`include/IfSimpleAgc.h:56-59`) | `m_initial_gain`, `m_max_gain`, `m_distortion_rate` | `m_current_gain` |
| `PilotPhaseLock` (`include/PilotPhaseLock.h:85,88`) | `m_minfreq`, `m_maxfreq`, `m_lock_delay` — all set once in the constructor and only ever read in `process()`/`locked()` | `m_freq`, `m_phase`, `m_pilot_level`, `m_lock_cnt`, `m_pilot_periods`, `m_pps_cnt`, `m_sample_cnt`, `m_freq_err`, `m_pps_events`, and the filter member objects (all mutated) |
| `LowPassFilterFirIQ` (`include/Filter.h:44,48,49`) | `m_coeff_reversed`, `m_order`, `m_downsample` | `m_state`, `m_scratch`, `m_pos` (all rewritten in `process()`) |
| `LowPassFilterFirAudio` (`include/Filter.h:68,72`) | `m_coeff_reversed`, `m_order` | `m_state`, `m_scratch`, `m_pos` |
| `FirstOrderIirFilter` (`include/Filter.h:92`) | `m_b0`, `m_b1`, `m_a1` — verified: every actual instantiation in the codebase (`LowPassFilterRC::m_filter0`/`m_filter1`, `PilotPhaseLock::m_first_phase_err`) constructs these with final values via the parameterized constructor; none is default-constructed and later re-seeded. | `m_x0`, `m_x1` (recursion state) |
| `LowPassFilterRC` (`include/Filter.h:120-122`) | `m_timeconst`, `m_a1`, `m_b0` | `m_filter0`, `m_filter1` (their own `.process()` mutates their internal state, so they cannot be `const` members themselves) |

**Checked and explicitly *not* recommended**, because the members are
genuinely reassigned after construction (see §"Not flagged" below for the
mechanism): `BiquadIirFilter::m_b0,m_b1,m_b2,m_a1,m_a2` — these are
`protected`, not `private`, specifically so that `HighPassFilterIir`'s
constructor body can overwrite them after the base subobject is built with
placeholder zeros.

**Lower-confidence / would need restructuring, not a simple `const` add**
(the values are computed conditionally inside the constructor *body*, not
the initializer list, so making them `const` requires moving that logic
into a helper invoked from the init list):
- `AirspySource` (`include/AirspySource.h:101-105`): `m_srates`,
  `m_lgainsStr`, `m_mgainsStr`, `m_vgainsStr`, `m_sratesStr` — all populated
  once in the constructor body (`sfmbase/AirspySource.cpp:116,121,133-135`)
  and never touched again by `configure()`.
- `AirspyHFSource` (`include/AirspyHFSource.h:89-90`): `m_srates`,
  `m_sratesStr` (`sfmbase/AirspyHFSource.cpp:107,112`).
- `RtlSdrSource` (`include/RtlSdrSource.h:97-98`): `m_gains`, `m_gainsStr`
  (`sfmbase/RtlSdrSource.cpp:55,59`).

These three classes are already non-copy-assignable in practice (they hold
a `std::unique_ptr<std::thread>`), so adding `const` here would not newly
restrict anything that isn't already restricted — but the early-`return`
error-handling style in these constructors (device-open failure aborts
mid-constructor) makes the init-list rewrite non-trivial, so this is listed
as a real opportunity rather than a one-line fix.

**Already done well** (for context/credit, not a finding): `FmDecoder`,
`AmDecoder`, `NbfmDecoder`, and `MultipathFilter` already `const`-qualify
most of their constructor-only members (e.g. `FmDecode.h:127-133`'s
`m_fmfilter_enable`, `m_pilot_shift`, `m_enable_multipath_filter`,
`m_multipath_stages`, `m_stereo_enabled`, and the `const IQSampleCoeff&`
reference member; `MultipathFilter.h:120-123`'s `m_stages`,
`m_index_reference_point`, `m_filter_order`, `m_alpha`). This is exactly
the right trade-off given these objects are constructed once and never
assigned.

### 1.4 `static const` vs `static constexpr`

No misuse found. All numeric compile-time constants in the codebase already
use `static constexpr` (e.g. every constant in `FmDecoder`, `AmDecoder`,
`NbfmDecoder`, `MultipathFilter.h:38,53,56,63,67,76`,
`PilotPhaseLock.h:31,33,35,37`, `AudioResampler.h:31,41-42`,
`IfResampler.h:31,42-43`, `main.cpp:779` `max_if_upsampling_ratio`,
`FileSource.cpp:518` `kMaxBlockSamples`, `RtlSdrSource.cpp:413`
`USB_STRING_MAX`). `static const` is used only where the type cannot be
`constexpr` — non-literal container types such as
`FilterParameters.h:31-49`'s `static const IQSampleCoeff`/`SampleCoeff`
tables and `AirspySource.h:98-100`'s `static const std::vector<int>` gain
tables — which is the correct choice (`std::vector` is not a literal type,
so `constexpr` is not available here pre-C++20 `constexpr` allocator
support, and this codebase doesn't rely on that).

### 1.5 `const` misused or meaningless

- **`include/AudioOutput.h:55`**:
  ```cpp
  const std::string get_device_name() { return m_device_name; }
  ```
  Two independent issues in one line: (1) `const` on a **by-value return
  type** is meaningless — the caller receives an independent copy regardless,
  and the qualifier only blocks moving from the returned temporary at the
  call site; (2) the member function itself, which only reads
  `m_device_name`, is **missing** the `const` that would actually matter.
  Should be `std::string get_device_name() const { return m_device_name; }`.
  (Contrast with `include/Source.h:64`'s `get_device_name()`, an unrelated,
  differently-named method on a different class, which is already correctly
  `std::string get_device_name() const { ... }`.)

- **Third-party, not this project's code, noted for completeness**:
  `include/git.h` (vendored from
  `andrew-hardin/cmake-git-version-tracking`, MIT license) returns
  `const StringOrView` by value from `InitString()` and from
  `CommitDate()`/`CommitSubject()`/`CommitBody()`/`Describe()`/`Branch()`
  (lines 97, 121, 125, 129, 133, 137) — same meaningless-`const`-on-by-value-
  return pattern as above. Since this file is a vendored drop-in with its own
  upstream, fixing it means patching the vendored copy rather than this
  project's style; listed for completeness, not as an actionable item here.

No instances of `const` applied to a pointer where pointer-to-const was
intended-but-missed were found beyond what's covered in §2 (the
`IQSampleCoeff&` constructor parameters, which are reference, not pointer,
issues).

---

## 2. Reference (`&`) usage

Every instance is listed, as requested.

### 2.1 Large objects passed by value where `const T&` (or plain `const&`) would do

| Site | Signature | Assessment |
| --- | --- | --- |
| `include/Source.h:35` (pure virtual) + all four overrides: `include/AirspySource.h:42` / `sfmbase/AirspySource.cpp:288`, `include/AirspyHFSource.h:42` / `sfmbase/AirspyHFSource.cpp:275`, `include/RtlSdrSource.h:41` / `sfmbase/RtlSdrSource.cpp:81`, `include/FileSource.h:48` / `sfmbase/FileSource.cpp:56` | `virtual bool configure(std::string configuration) = 0;` | Every implementation only reads the string (passes it into `ConfigParser::parse_config_string`); none mutates or moves it. Should be `const std::string &configuration`. |
| `include/FileSource.h:94` / `sfmbase/FileSource.cpp:164` | `bool configure(std::string fname, ...)` | `fname` is only copy-assigned into `m_devname` (`m_devname = fname;`); never moved. Should be `const std::string &fname`. |
| `include/ConfigParser.h:35` / `sfmbase/ConfigParser.cpp:81` | `void parse_config_string(std::string text, map_type &output);` | `text` is only read (passed again by value into `split_delimiter`). Should be `const std::string &text`. |
| `include/ConfigParser.h:44` / `sfmbase/ConfigParser.cpp:25` | `std::vector<std::string> split_delimiter(const std::string str);` | `str` (already `const`-qualified, but still copied because it's by value) is only iterated with a range-`for`. Should be `const std::string &str`. |
| `include/ConfigParser.h:49` / `sfmbase/ConfigParser.cpp:51` | `pair_type split_equal_sign(const std::string str);` | Same as above. |
| `include/FmDecode.h:63` / `sfmbase/FmDecode.cpp:27` | `FmDecoder(bool fmfilter_enable, IQSampleCoeff &fmfilter_coeff, ...)` | Non-`const` reference, bound straight to `const IQSampleCoeff &m_fmfilter_coeff` (`FmDecode.h:128`) and never written. Should be `const IQSampleCoeff &fmfilter_coeff`. |
| `include/AmDecode.h:46` / `sfmbase/AmDecode.cpp:27` | `AmDecoder(IQSampleCoeff &amfilter_coeff, const ModType mode);` | Same pattern, bound to `const IQSampleCoeff &m_amfilter_coeff` (`AmDecode.h:74`). |
| `include/NbfmDecode.h:46` / `sfmbase/NbfmDecode.cpp:26` | `NbfmDecoder(IQSampleCoeff &nbfmfilter_coeff, const double freq_dev);` | Same pattern, bound to `const IQSampleCoeff &m_nbfmfilter_coeff` (`NbfmDecode.h:62`). |
| `include/FmDecode.h:74` / `sfmbase/FmDecode.cpp:87` | `void process(IQSampleVector samples_in, SampleVector &audio);` | By-value **is** justified here: the "no IF filter" branch (`FmDecode.cpp:100`) genuinely moves `samples_in` into `m_samples_in_iffiltered`, and that branch is live whenever `-f default` or `-f wide` is selected (`main.cpp:832-835,850-854`), which is the tool's default filter setting. Not a finding — see §3.1. |
| `include/AmDecode.h:51` / `sfmbase/AmDecode.cpp:98` | `void process(IQSampleVector samples_in, SampleVector &audio);` | By-value is **not** justified here — see the top-priority finding above and §3.2: the move is only reached on an unreachable code path, so every real call pays a full copy for nothing. This is the one entry in this table with an actual measured behavioral consequence (a guaranteed allocation+copy every AM/DSB/SSB/CW/WSPR block) rather than a purely theoretical one. |

`NbfmDecoder::process` (`include/NbfmDecode.h:49`) already takes
`const IQSampleVector &samples_in` — correctly done, and a useful contrast
with the two above.

### 2.2 Non-const `T&` parameters that are never written

Beyond the constructor parameters already covered in §2.1 (which are
reference-to-non-const specifically), no additional standalone instances
were found: every other non-`const` reference parameter in scope
(`SampleVector &audio`, `IQSampleVector &samples_out`,
`ConfigParser::map_type &output`, `std::vector<std::string> &devices`, the
`float &mean, float &rms` pair in `Utility::samples_mean_rms`, etc.) is
genuinely written to — these are output parameters, covered next.

### 2.3 Output parameters that could instead be return values

- **`include/Utility.h:112-113` / definition inline**:
  `samples_mean_rms(const IQSampleDecodedVector &samples, float &mean, float &rms)`
  writes two output parameters. Call sites: `sfmbase/FmDecode.cpp:153`,
  `sfmbase/AmDecode.cpp:208`, `sfmbase/NbfmDecode.cpp:84`, `main.cpp:1041`.
  Could return `std::pair<float,float>` or a small struct instead; the
  payload is two `float`s, so there is no performance argument either way —
  purely a style call. Not changed here because it touches four call sites
  for no measurable benefit.

- **`include/Utility.h:38,73`**: `parse_dbl(const char *s, double &v)` and
  `parse_int(const char *s, int &v, bool allow_unit)` combine a `bool`
  success return with an output parameter. Since the project targets C++20,
  `std::optional<double>`/`std::optional<int>` is available and would fold
  the two into one return value. This has 19 call sites
  (`main.cpp:410,437,447,453,471,480`;
  `sfmbase/AirspySource.cpp:312,334,352,369,387`;
  `sfmbase/AirspyHFSource.cpp:304,320,337`;
  `sfmbase/RtlSdrSource.cpp:97,108,129,152`; `sfmbase/FileSource.cpp:81,94,105`).
  Flagged as a design observation, not a recommendation to act on: the
  invasiveness (19 call sites, all in argument-parsing code that is not
  performance-sensitive) outweighs the benefit, and it is a value-preserving
  refactor rather than a `const`/reference fix. Mentioned per the review's
  "output parameter could be a return value" criterion.

- **`include/Source.h:64`-adjacent pattern**: `get_device_names(std::vector<std::string> &devices)`
  is `static` on all four device classes (`AirspySource.cpp:150`,
  `AirspyHFSource.cpp:128`, `RtlSdrSource.cpp:410`, `FileSource.cpp:298`) and
  in the free function `main.cpp:204`'s `get_device()`. A single shared
  `devnames` vector is filled by whichever of the four static methods
  matches `devtype`, then reused; the output-parameter design fits that
  call pattern (one shared destination, dispatched by a `switch`) reasonably
  well, so a return-value redesign is a style preference, not an
  improvement, given it runs once at startup.

### 2.4 Range-`for` loops copying elements of non-trivial types

- **`sfmbase/ConfigParser.cpp:84`**:
  ```cpp
  for (std::string str : tokens) {
  ```
  Copies every token string; should be `const std::string &str` (or
  `const auto &`). This is the only such instance in the reviewed scope —
  every other range-`for` over a non-trivial-type container already uses a
  reference (e.g. `main.cpp:1140`'s
  `for (const PilotPhaseLock::PpsEvent &ev : fm.get_pps_events())`).

### 2.5 Dangling-reference risk

No live bugs found. One pattern is worth flagging as a **latent** risk that
is currently safe only because of how the one caller is structured:

- `FmDecoder`, `AmDecoder`, and `NbfmDecoder` each store
  `const IQSampleCoeff &m_fmfilter_coeff` (etc.) — a reference to a vector
  owned by the caller. The only caller, `main.cpp:826-876`, declares the
  three `IQSampleCoeff` locals (`amfilter_coeff`, `fmfilter_coeff`,
  `nbfmfilter_coeff`) in the same scope as, and before, the three decoder
  objects (`am`, `fm`, `nbfm`), so C++'s reverse-destruction-order guarantee
  keeps the referenced vectors alive for the decoders' entire lifetime.
  This is correct today, but it is a scope-coupling invariant rather than
  something the type system enforces — a future change that moves the
  decoder construction earlier, or into a helper function that returns the
  decoder by value while the coefficient vector stays behind, would silently
  dangle. Not a defect; flagged because the review brief asks for this
  specifically.

- `main.cpp:1140`'s
  `for (const PilotPhaseLock::PpsEvent &ev : fm.get_pps_events())` binds a
  reference to elements of a temporary vector returned by value from
  `get_pps_events()` (`FmDecode.h:92-94`, itself forwarding
  `PilotPhaseLock::get_pps_events()`, `PilotPhaseLock.h:72`). This is safe:
  range-`for`'s hidden reference to the range-init-expression extends the
  temporary's lifetime for the whole loop, per the standard. Verified not a
  bug; included in §"Not flagged" for completeness since it looks
  superficially risky.

---

## 3. Redundant / incorrect `std::move`

All 15 `std::move` call sites in the reviewed scope, individually:

### 3.1 Correct, intentional uses (not flagged)

1. **`main.cpp:964`**: `if_shifted_samples = std::move(iqsamples);` —
   `iqsamples` is a fresh per-iteration local (`main.cpp:935`,
   `source_buffer.pull()`), not read again in that iteration. Correct.
2. **`main.cpp:971`**: `if_samples = std::move(if_shifted_samples);` — same
   reasoning, `if_shifted_samples` not read again. Correct.
3. **`sfmbase/AirspySource.cpp:508`**,
   **`sfmbase/AirspyHFSource.cpp:428`**,
   **`sfmbase/RtlSdrSource.cpp:405`**,
   **`sfmbase/FileSource.cpp:443`**: all `m_buf->push(std::move(iqsamples))`
   — `iqsamples` is a local built freshly in each `callback()`/`run()`
   invocation and handed to the cross-thread queue; the queue must take
   ownership, so this move is required, not optional. Correct.
4. **`sfmbase/FmDecode.cpp:100`**: `m_samples_in_iffiltered = std::move(samples_in);`
   — see §2.1: this branch is live under the default/wide filter setting,
   and `samples_in` is a by-value parameter not read again on this path.
   Correct and, per the constructor doc comment
   (`FmDecode.h:72-73`), intentional.
5. **`include/DataBuffer.h:54`**: `m_queue.push(std::move(samples));` —
   `samples` is a named rvalue-reference parameter (`std::vector<Element> &&samples`);
   a named rvalue reference is itself an lvalue in expressions, so
   `std::move` is required here to actually trigger the move constructor.
   Textbook-correct use.

### 3.2 Dead-code moves (symptom of an unneeded by-value parameter)

6. **`sfmbase/AmDecode.cpp:141`**: `m_buf_filtered2 = std::move(samples_in);`
   (inside the inner `switch (m_mode)`'s `default:` case,
   `AmDecode.cpp:109-143`)
7. **`sfmbase/AmDecode.cpp:151`**: `m_buf_filtered2 = std::move(samples_in);`
   (the outer `switch (m_mode)`'s `default:` case, `AmDecode.cpp:99-153`)

   Both `default:` branches are unreachable in practice: `m_mode` is fixed
   at construction from the caller's `ModType`, and the only call site
   (`main.cpp:1014-1022`) invokes `am.process()` exclusively for
   `AM`/`DSB`/`USB`/`LSB`/`CW`/`WSPR` — exactly the values the outer
   `switch` and the inner `switch` (for the four non-AM/DSB modes) already
   handle explicitly. `ModType::FM`/`NBFM` are asserted unreachable a few
   lines further down (`AmDecode.cpp:163-170`). So these two `std::move`
   calls never execute, and the `samples_in`-by-value design that the
   `AmDecode.h:49-50` doc comment justifies ("taken by value so that
   `std::move(samples_in)` inside the body actually moves") is not actually
   exercised anywhere live. Net effect: every real call to
   `AmDecoder::process()` still pays the by-value parameter's copy
   (`main.cpp:1021`'s `am.process(if_samples, audiosamples)` passes an
   lvalue), for zero benefit. Fix: change the parameter to
   `const IQSampleVector &samples_in` (removing the two dead `move`s along
   the way, or leaving them as unreachable no-ops — either is fine once the
   parameter is a reference, since `std::move` on a `const&` local doesn't
   compile as a move anyway and would need to be deleted regardless).

### 3.3 Move into a `const&` parameter (clear defect)

8. **`main.cpp:1049`**:
   ```cpp
   if (!audio_output->write(std::move(audiosamples))) {
   ```
   `AudioOutput::write()` is declared
   `virtual bool write(const SampleVector &samples) = 0;`
   (`include/AudioOutput.h:40`), and both overrides
   (`SndfileOutput::write`, `include/AudioOutput.h:84`;
   `PortAudioOutput::write`, `include/AudioOutput.h:128`) keep the same
   `const SampleVector &` signature. Binding a `const&` to an xvalue is
   legal and behaves identically to binding it to an lvalue — no move
   constructor or assignment is ever invoked. The `std::move` here does
   nothing but suggest, incorrectly, that ownership transfers. Neither
   implementation needs ownership (`SndfileOutput::write` reads
   `samples.data()` into `sf_write_float`; `PortAudioOutput::write` copies
   into `m_floatbuf` via `std::copy_n`), so the fix is simply to drop the
   `std::move` and pass `audiosamples` directly. This is the literal example
   given in the review brief ("moving into a parameter taken by const
   reference").

### 3.4 Moves that drain a persistent member buffer (perf observation)

These four sites are all functionally correct (no use-after-move, no
observable value change) but share a pattern worth flagging under the
"keep mutable objects small / hot-path performance" lens: each moves a
long-lived **member** buffer's contents away, which — because
`std::vector` move construction/assignment transfers the source's heap
allocation rather than copying into the destination's existing one — also
discards that member's capacity. The buffer must then be fully reallocated
the next time it is filled via `.resize()`.

9. **`sfmbase/FmDecode.cpp:112`**: `m_samples_in_multipathfiltered = std::move(m_samples_in_after_agc);`
   (inside `if (m_wait_multipath_blocks > 0)`, the ~20-block warm-up window)
10. **`sfmbase/FmDecode.cpp:128`**: same assignment, on the rare multipath-filter-divergence-reset path
11. **`sfmbase/FmDecode.cpp:132`**: same assignment, in the `else` branch taken on **every block** whenever the multipath filter is disabled (`m_enable_multipath_filter == false`) — i.e., whenever `-E` is not passed, which is the tool's default. Traced against `FmDecode.cpp:109-134`: the one branch that does *not* move (`FmDecode.cpp:116-117`, `m_multipathfilter.process(m_samples_in_after_agc, m_samples_in_multipathfiltered)`, a real read-only pass) is only taken once the multipath filter is both enabled and successfully processing. So in the default configuration, `m_samples_in_after_agc` (`FmDecode.h:140`) is fully drained on every single block, forcing `IfSimpleAgc::process()`'s internal `samples_out.resize(n)` (`IfSimpleAgc.cpp:40`) to reallocate every block instead of reusing a stable capacity.
12. **`sfmbase/FmDecode.cpp:224`**: `audio = std::move(m_buf_mono);` (mono-mode branch, always taken when `m_stereo_enabled == false`)
13. **`sfmbase/AmDecode.cpp:218`**: `audio = std::move(m_buf_baseband);` (always executed — `AmDecoder` has no stereo path)
14. **`sfmbase/NbfmDecode.cpp:96`**: `audio = std::move(m_buf_baseband_filtered);` (always executed)

**Recommendation and caveat.** `std::swap(destination_member, source_member)`
(or, for the output-parameter cases, `std::swap(audio, m_buf_...)`) would
avoid the value copy exactly as `std::move` does, but — unlike move-assignment
— it hands the *emptied* side's previous buffer back to the other side
instead of destroying it, so once block sizes stabilize both buffers
settle into a fixed capacity and neither side ever reallocates again. This
is a real, no-numerical-effect improvement for #9–#11 (`m_samples_in_after_agc`
/ `m_samples_in_multipathfiltered` are both persistent `FmDecoder` members
with no external interference).

For #12–#14, the caveat is important: the benefit depends on the *caller's*
buffer also being long-lived. Currently it is not —
`main.cpp:935-942` declares `iqsamples`, `if_shifted_samples`, `if_samples`,
and `audiosamples` **freshly inside the loop body** on every iteration
(`SampleVector audiosamples(0);` at `main.cpp:942`), so the output parameter
`audio` always arrives at `FmDecoder::process()`/`AmDecoder::process()`/
`NbfmDecoder::process()` with zero capacity regardless of whether the
decoder uses `move` or `swap` internally. Swapping into an always-empty
destination gives back nothing to the member buffer, so for #12–#14 alone
`swap` would not measurably help *unless* `main.cpp`'s loop-body buffer
declarations were also hoisted out of the loop and reused across
iterations — a materially larger, main-loop-level change that is outside
the const/reference/move scope of this review, but is the reason this
whole cluster is presented as an observation requiring profiling rather
than a confident recommendation. #9–#11 stand on their own regardless of
what `main.cpp` does, since both sides are internal `FmDecoder` members.

No numerical-behavior change is implied by any of the above: `swap`
transfers the same values as `move`, just via a different mechanism.

---

## Not flagged — patterns that look suspicious but are correct

- **`include/DataBuffer.h:108`**: `pull()` uses
  `std::swap(ret, m_queue.front());` rather than a move-assignment. This is
  exactly the swap-based buffer-donation pattern recommended in §3.4,
  already precedented in this codebase — cited here as evidence the
  technique is idiomatic for this project, not a novel suggestion.

- **C-API out-parameters that cannot be `const`**: `sfmbase/AudioOutput.cpp:109-110`
  (`int constant_mode`, `double compression_level`, addresses taken and
  passed to `sf_command()` as non-`const void*`), `sfmbase/FileSource.cpp:339,366`
  (`int count`, filled by `sf_command(..., &count, ...)`), and
  `sfmbase/AudioResampler.cpp:52`/`sfmbase/IfResampler.cpp:56` (`double *output0[...]`,
  filled by-reference by `r8b::CDSPResampler::process()`). All of these look
  like §1.1 candidates at first glance but cannot be `const`-qualified: their
  address is taken and handed to a C/third-party API as a mutable pointer or
  mutable-reference out-parameter.

- **`include/Filter.h:144-148`'s `BiquadIirFilter::m_b0,m_b1,m_b2,m_a1,m_a2`**
  look like straightforward "set once in the constructor" candidates (like
  `FirstOrderIirFilter`'s equivalent members, which *are* flagged in §1.3),
  but they are `protected` specifically so that
  `HighPassFilterIir::HighPassFilterIir()` (`sfmbase/Filter.cpp:234-270`) can
  overwrite them in its constructor *body*, after the `BiquadIirFilter` base
  subobject has already been constructed with placeholder zeros. They are
  genuinely mutated post-construction (just not by `HighPassFilterIir`'s own
  instance) and correctly cannot be `const`.

- **The `const IQSampleCoeff&` reference members** discussed in §2.5 look
  like a dangling-reference risk on first read; verified safe given the
  current single call site's scope ordering (see §2.5 for the caveat).

- **`main.cpp:1140`'s range-`for` over a by-value-returned temporary**
  (`fm.get_pps_events()`) looks like a dangling-reference risk; verified
  safe under the range-`for` temporary-lifetime-extension rule (§2.5).

- **`AudioOutput::error()` (`include/AudioOutput.h:46`) and
  `Source::error()` (`include/Source.h:67`)** are non-`const` and might look
  like candidates for §1.2 at a glance, but both mutate state
  (`m_error.clear()`) as part of "read and clear" semantics — correctly
  non-`const`.

- **`sfmbase/FileSource.cpp:407`'s `IQSampleVector iqsamples;`**, declared
  once above `FileSource::run()`'s `while` loop and then
  `std::move()`-d into the cross-thread queue every iteration
  (`FileSource.cpp:443`), looks like the same member-buffer-draining pattern
  flagged in §3.4. It is not: ownership must genuinely transfer to the
  consumer thread through `DataBuffer::push()`, so the reallocation this
  causes is the unavoidable cost of a zero-copy producer/consumer handoff,
  not a wasted internal churn. (The consumer side already reclaims a buffer
  via `swap` in `DataBuffer::pull()`, per the first bullet above.)

---

## Methodology

- Read every file in scope in full: `main.cpp` (1188 lines); all 25 headers
  under `include/`, including the vendored `git.h`; and all 19 `.cpp` files
  under `sfmbase/` (6844 lines), including the pure-data
  `FilterParameters.cpp` (2740 lines, confirmed to contain only coefficient
  table definitions with no logic).
- Confirmed the language standard from `CMakeLists.txt:231`
  (`-std=c++20`), which is why the C++20-only `std::optional` suggestion in
  §2.3 is offered.
- For every `std::move` and every non-`const` reference/by-value parameter,
  traced actual call sites (via `grep` plus manual reading) rather than
  assuming reachability — this is how the AmDecoder dead-`move` finding
  (§3.2) and the "which filter-type branch is actually default"
  determination (§2.1, §3.4) were established, and why `ModType`/`FilterType`
  dispatch in `main.cpp` was read alongside the decoder classes rather than
  reviewed in isolation.
- For member-variable `const` candidates, checked every constructor and
  every other method of the owning class for a write to that member, and
  additionally checked (via `grep` across the whole scope) whether the
  owning class is ever copy- or move-assigned anywhere, since `const`
  members disable those two special member functions.
- Did not open `compile_commands.json` or attempt a build; all findings are
  from static reading of the source. Nothing here changes numerical
  behavior — every recommendation is either a qualifier addition
  (`const`), a parameter-type change (`T&`/`const T&`), or a
  value-preserving substitution (`std::swap` for `std::move`,
  `write(audiosamples)` for `write(std::move(audiosamples))`).
