# Gotchas

Four non-obvious things that have all bitten us in this project's release
flow. Read this before doing anything; if a `gh` command in the skill
behaves unexpectedly, the answer is usually here.

## 1. `gh release create --draft` puts the release under `untagged-<id>`

This is the single biggest trap in the release flow.

**Symptom**: After `gh release create <tag> ... --draft --prerelease`, the
release exists, has the right `tag_name`, has assets, has notes — but:

- `gh api repos/<owner>/<repo>/releases/tags/<tag>` returns **404**.
- The `html_url` ends in `untagged-<hash>` instead of `<tag>`.

**Root cause**: GitHub stores draft releases under a synthetic `untagged-<id>`
URL even when the underlying git tag exists on origin. The flag combination
matters more than the tag's actual presence in the refs.

**Fix**: Do NOT pass `--draft` to `gh release create`. Create the release
"live" (still as `draft:false, prerelease:false`) and let `verify-release.sh`
confirm. If you actually want a pre-publish review, create it without
`--draft`, then use `gh release edit <tag> --draft=true` to flip the flag
afterward — the URL stays properly bound because the release was created
without `--draft`.

**Why this matters for this project**: The Build & Release workflow has
inputs `prerelease` and `draft`, both default `true`. Every prior release
(v0.1.0 / v0.3.0 / v0.4.0 / v0.4.1 / v0.4.2) was actually dispatched with
`prerelease=false, draft=false` — see the v0.4.0 workflow log where the
conditional expansion evaluated to empty. Always dispatch the workflow with
`-f prerelease=false -f draft=false` to avoid the trap.

## 2. Tag at the merge commit, not the feature commit

Every prior release tag in this repo points at a merge commit on `main`,
not at the feature commit:

| tag | commit | subject |
|-----|--------|---------|
| v0.4.0 | `5e07da1` | Merge pull request #94 from alondero/gh78-widescreen-3p4p-... |
| v0.4.1 | `1f489fb` | Merge pull request #112 from alondero/inbred-rebellious-sphinx |
| v0.4.2 | `03e533a` | Merge pull request #113 from alondero/significant-significant-bolt |

The merge commit is the smallest unit that includes the new code AND the
issue/PR reference in the subject. Feature commits come and go; merge
commits mark the "this PR landed on main" moment, which is what release
notes link to.

The `tag-and-dispatch.sh` script enforces this — it refuses to tag if HEAD
isn't a merge commit.

## 3. The release's `prerelease` flag is `false` — "Pre-release quality" is body copy

This is a contradiction in the project's release naming that's confused
every new contributor (and me, twice):

- The release notes' opening blockquote says "**Pre-release quality.**"
- The GitHub release's `prerelease` flag is **false**.

The blockquote is the *project's* self-description (mirror of semver's
"anything <1.0 is pre-release"). The flag is GitHub's mechanism for showing
the release with a "Pre-release" badge in the UI. The project has decided
not to use the badge — confirmed by inspecting the v0.1.0/v0.3.0/v0.4.0
releases via the GitHub API, all of which have `prerelease: false`.

`verify-release.sh` checks this and will fail loudly if a release gets
flagged `prerelease: true` — that's a deliberate gate so we don't
accidentally introduce a different convention.

## 4. The workflow's release-notes placeholder must be replaced after the build

The `Build & Release` workflow's release job runs:

```bash
gh release create "$tag" <assets> \
  --notes "Automated build from commit $GITHUB_SHA. ..."
```

That placeholder is fine for the in-flight workflow but is the user-facing
release body. Step 5 of the skill (`update-notes.sh`) replaces it with the
real release notes drafted from `release-notes-template.md`. If you skip
step 5, users downloading the binary see a useless placeholder.

## Quick diagnostic flow

If a release looks wrong, walk down this list:

1. `git ls-remote --tags origin <tag>` — does the tag exist?
2. `gh api repos/.../releases/tags/<tag>` — does the API see it?
   - 404 → workflow hasn't finished OR `--draft` trap (gotcha #1)
3. `gh release view <tag> --json htmlUrl,isDraft,isPrerelease` — sanity check
   - `untagged-` in url → `--draft` trap (gotcha #1)
   - `isDraft: true` → publish via UI or `gh release edit <tag> --draft=false`
   - `isPrerelease: true` → almost certainly a misconfig (gotcha #3)
4. `gh release view <tag> --json body --jq '.body' | head -n 3` — placeholder
   notes? Run `update-notes.sh`.

`verify-release.sh` automates all of this; trust its output.