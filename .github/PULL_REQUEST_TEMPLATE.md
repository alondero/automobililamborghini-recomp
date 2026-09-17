<!-- Keep the summary and evidence concrete. The human maintainer owns the
support and architecture decisions represented by this change. -->

## Summary

What changed, and why is it the smallest useful change?

## Scope and authority

- [ ] This change is documentation-only.
- [ ] This change changes runtime behavior.
- [ ] This change changes a dependency patch or renderer boundary.
- Relevant issue or design question:
- Trade-offs needing review:

## Verification

- Operating system:
- Compiler/toolchain:
- Graphics backend:
- Release or commit tested:
- ROM region and SHA-256, if ROM-backed:
- Exact commands run:
- Test results:
- Manual checks or captures:

## Generated and dependency state

- [ ] No generated files were edited.
- [ ] Generated inputs were changed and output was regenerated.
- [ ] Generated output was freshly created for verification.
- [ ] Dependency patches still apply through the supported scripts.
- [ ] Upstream comparison/status was updated when a dependency behavior changed.

## Documentation and limitations

- Documentation files changed:
- User-facing claim changed:
- Known limitations:
- Checks not run and why:
- Logs, screenshots, or captures:

## Review checklist

- [ ] Guest/host ownership and thread boundaries are documented where needed.
- [ ] Fixed guest addresses include units, byte order, and evidence.
- [ ] Comments explain purpose or invariants rather than repeating code.
- [ ] No private-session links, absolute machine paths, ROM bytes, or
      unexplained foreign issue references were added.
- [ ] python tools/check_docs.py passes when documentation changed.
- [ ] git diff --check passes.
