<p align="left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/branding/TrueScope_Logo_dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/branding/TrueScope_Logo_light.svg">
    <img alt="TrueScope — Viewport Scopes for Unreal Engine" src="docs/branding/TrueScope_Logo_dark.svg" width="45%">
  </picture>
</p>

# Contributing to TrueScope

TrueScope is maintained by one person with limited time. Small, focused
contributions are welcome; larger ones need a discussion issue first.

## Before you open a pull request

- **Open an issue** describing the change and why. For anything touching capture
  or analysis, say how you verified the result — a synthetic automation test, a
  DaVinci Resolve cross-check, or both.
- **Read [`docs/architecture.md`](docs/architecture.md).** Settled decisions are
  not re-litigated inside a pull request. If you disagree with one, open an issue
  and make the case there.
- **Keep pull requests small and single-purpose.** One change, one reason.

## Requirements for a mergeable pull request

- **It builds** on the platform you changed. Say which platforms you tested on,
  and name the engine version.
- **The automation tests pass.** Run the `TrueScope.*` suite headless, with the
  editor closed. The invocation, its two run modes and when each one is needed
  are under *"Build & test hygiene"* in [`docs/dev-rules.md`](docs/dev-rules.md).
  Report the test count you got, and which mode you ran — the count is this
  project's stale-code tripwire, so a number lower than expected means something
  did not rebuild.
- **New behaviour comes with a test.** For a bug fix, the regression test must be
  shown to fail on the reintroduced bug and pass after the fix — see *"Prove a
  regression test has teeth"* in [`docs/architecture.md`](docs/architecture.md).
  A test that has never failed proves nothing.
- **Docs follow behaviour.** If behaviour changed, the README and
  `docs/architecture.md` change with it in the same pull request.
- **Every commit is signed off** under the
  [Developer Certificate of Origin](https://developercertificate.org/) with
  `git commit -s`. The sign-off certifies that you wrote the contribution, or
  otherwise have the right to submit it under Apache-2.0.
- **No GPL-derived code.** TrueScope is Apache-2.0 and deliberately carries no
  code derived from GPL scope projects. A GPL-licensed tool may be used as a
  *behavioural* reference — what a scope should show for a given input — but
  nothing may be copied or transliterated from it. See *"Licensing red line"* in
  [`docs/architecture.md`](docs/architecture.md).

## Feature requests

Feature request issues are closed with a pointer to the backlog — that is just
how this project stays finishable. The backlog in
[`docs/backlog.md`](docs/backlog.md) says what is planned, what was deferred and
why.

## Review policy

The maintainer merges only what they can understand and verify themselves. A
pull request may be labelled `needs-verification` and wait for a cross-check, or
be declined with thanks. That is a statement about reviewing capacity, not a
judgement of your work.

## Support policy

- **Bugs:** GitHub Issues, using the bug report template. A report without the
  template fields is usually impossible to act on.
- **GPU and driver-specific problems:** the maintainer cannot buy every GPU.
  Please fill in the full bug report, and try the **CPU (compatibility)**
  analysis path before reporting — if the problem disappears there, say so, as
  that narrows it down considerably. Expect some driver-specific issues to stay
  open under the `help wanted` label.
- **Engine versions:** 5.8 is supported, and 5.9 if that release arrives. Unreal
  Engine 6 is explicitly not covered. Newer minor versions are ported when time
  allows.
- **No response-time commitment.** This is a side project.

## Working with an AI coding agent

If you use an AI coding assistant on this repository, point it at `docs/` first —
particularly `docs/dev-rules.md`, which is written for exactly that case, and
`docs/architecture.md`, which records which decisions are settled and why.

Two rules from those documents matter most for a contribution, because they are
where this project has actually been burned:

- **Read the real engine source instead of guessing.** Engine APIs differ from
  what a model remembers. Verify against the installed engine source before
  using an unfamiliar or version-sensitive API.
- **Test the mapping layer, not just the analysis.** Two real bugs lived not in
  the maths but in which data landed in which drawn element. A test that only
  asserts on bin arrays would have missed both.
