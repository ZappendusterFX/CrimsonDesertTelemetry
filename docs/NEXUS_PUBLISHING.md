# Publishing to Nexus Mods

The mod page is <https://www.nexusmods.com/crimsondesert/mods/3374>. Releases can
be pushed to it through the Nexus Mods v3 API instead of the website upload form,
either from a GitHub workflow or from a local PowerShell session.

Nothing in this repository uploads anything on its own. Both paths are dry run by
default and need an explicit opt-in.

## What the API can and cannot do

The v3 API is a *file* API, not a page editor.

| Task | Supported |
| --- | --- |
| Create a new file entry on an existing mod page | yes (`POST /mod-files`) |
| Add a new version to an existing file | yes (`POST /mod-files/{id}/versions`) |
| Archive the previous version while uploading | yes (`archive_existing_file`) |
| Move the mod page version to the new release | yes (`update_mod_version`) |
| Append changelog text for a version | yes (`POST /mods/{id}/changelogs`) |
| Rename a file entry | yes (`PUT /mod-files/{id}`) |
| Read files, versions, categories | yes |
| Create the mod page itself | **no** |
| Edit the mod page description, images, tags, category | **no** |
| Delete a file or version | **no** |
| Report whether a file passed the virus scan | **no** |

There is no scan status anywhere in the v3 API. A file version object carries only
`id`, `file`, `position`, `name`, `version`, `category`, `uploaded_at`, and
`category: main` means the page rubric, NOT that the scan cleared. A freshly
uploaded file shows "Virus scanning is in progress" on the page and is not
downloadable until Nexus finishes; the page is the only place that says so.

So the page text, screenshots and requirements stay manual edits on the site. The
API only ever adds files, versions and changelog entries.

## Known identifiers

Read off the mod page on 2026-09-11 under **Files -> Advanced**, for the
`CrimsonDesertTelemetry-ModManagers` file entry:

| Shown as | Value | Use |
|---|---|---|
| File ID | `7891854` | `NEXUSMODS_FILE_ID` |
| Short Unique File ID | `R07hI2hXi` | share links only; the API does not take it |
| Unique Mod ID | `38521561681198` | `NEXUSMODS_MOD_ID` |

The file id is what `POST /mod-files/{id}/versions` wants. The mod id was worth
checking rather than assuming, because a wrong one appends release notes to
another page and changelogs cannot be deleted -- but it checks out: run against
`GET /games/crimsondesert/mods/3374` on 2026-09-11, `$mod.id` is exactly the
site's 14-digit "Unique Mod ID". Game id is `8969`.

All three live in `.env`, which the scripts load themselves.

## One-time setup

1. Create a personal API key at <https://www.nexusmods.com/settings/api-keys>.
   Treat it like a password: it can upload files under your account.
2. Put it in `.env` as `NEXUSMODS_API_KEY` for local runs -- the file is
   git-ignored and both scripts load it without anything being exported by hand.
   For CI, add it to the GitHub repository as the secret `NEXUSMODS_API_KEY`
   (Settings -> Secrets and variables -> Actions -> Secrets); an exported
   environment variable always wins over the file, so CI stays authoritative.
3. Find the two ids and add them as repository *variables* (same page, Variables
   tab):

   ```powershell
   $env:NEXUSMODS_API_KEY = '<key>'
   .\scripts\Get-NexusModStatus.ps1
   ```

   The output labels which value is `NEXUSMODS_MOD_ID` (the internal mod id, not
   `3374`) and which is `NEXUSMODS_FILE_ID`. The file id is also on the mod page
   under Files -> API Info.

4. If the mod page has no file yet, there is no file id to add versions to.
   Create the first file entry once:

   ```powershell
   .\scripts\Publish-NexusRelease.ps1 -CreateModFile          # dry run, shows the plan
   .\scripts\Publish-NexusRelease.ps1 -CreateModFile -Execute # actually creates it
   ```

   Then store the printed file id as `NEXUSMODS_FILE_ID`.

## Releasing from GitHub Actions

`.github/workflows/nexus-publish.yml` builds the mod-manager package and uploads
it with the official [`Nexus-Mods/upload-action`](https://github.com/Nexus-Mods/upload-action),
pinned to the commit behind `v1.0.0-beta.10`.

Two ways in:

* **Manual** — Actions -> Publish to Nexus Mods -> Run workflow. `dry_run` is
  checked by default, which builds, validates and uploads the archive as a
  workflow artifact without touching Nexus Mods. Uncheck it to publish.
* **On release** — publishing a GitHub release uploads that tag automatically,
  but only once the repository variable `NEXUS_AUTO_PUBLISH` is set to `true`.
  Until then the release trigger is inert, so the existing tag -> draft release
  flow in `release.yml` stays unchanged.

Every run performs a read-only preflight first (resolve the mod, confirm the file
id belongs to it, confirm the version is not already published) and fails before
uploading if anything is off. That preflight deliberately runs under Windows
PowerShell 5.1, the same host used locally.

## Releasing from a local PowerShell 5.1 session

```powershell
$env:NEXUSMODS_API_KEY = '<key>'
$env:NEXUSMODS_FILE_ID = '<file id>'

.\scripts\Build-ModManagerPackage.ps1 -Version 1.2.1
.\scripts\Publish-NexusRelease.ps1 -Version 1.2.1                     # dry run
.\scripts\Publish-NexusRelease.ps1 -Version 1.2.1 -Execute -UpdateModVersion -ArchiveExistingFile
```

Without `-Execute` the script only reads. It prints the package, its MD5, the
resolved mod, every existing file with its id, and exactly what it would do.

Useful switches:

| Switch | Effect |
| --- | --- |
| `-UpdateModVersion` | moves the version shown on the mod page to this release |
| `-ArchiveExistingFile` | archives the previous version instead of leaving both active |
| `-PrimaryModManagerDownload` | makes this the default mod manager download |
| `-Category optional` | uploads as an optional file rather than a main file |
| `-SkipChangelog` | does not append changelog text |
| `-CreateModFile` | creates a new file entry instead of a new version |

The changelog text comes from `docs/releases/v<version>.md` with the H1 title
stripped. Changelog entries are **append only** — running the same publish twice
for one version duplicates the text on the page.

## Constraints worth knowing

* **Version strings** must match `^[a-zA-Z0-9.-]+$`, max 50 characters. A semver
  build suffix such as `1.2.1+abc` is rejected. `1.2.1-preview.1` is fine.
* **File names** must match `^[a-zA-Z0-9 _'().-]+$`, max 50 characters.
* **100 MiB** is the single part upload limit. Check the archive size before each
  upload; if it crosses 100 MiB, the multipart flow has to be implemented — the
  module raises a clear error rather than silently truncating.
* **MD5 becomes mandatory on 2026-12-01.** The module already sends the hex digest
  in the create-upload body and the base64 digest in the `Content-MD5` header, so
  no change is needed then.
* `Content-Disposition` and `Content-MD5` are part of the presigned URL signature.
  Storage rejects the `PUT` if either is missing or altered.
* Most mod endpoints are marked **Experimental** in the spec and may change.
* The API key authenticates as you personally. Anyone with the GitHub secret can
  upload files to your account, so keep the workflow's permissions minimal and
  rotate the key if it leaks.

## Files

| Path | Purpose |
| --- | --- |
| `tools/nexus/NexusMods.psm1` | PowerShell 5.1 client for the v3 API |
| `scripts/Get-NexusModStatus.ps1` | read-only view of the page, files and versions |
| `scripts/Publish-NexusRelease.ps1` | dry-run-by-default publish |
| `.github/workflows/nexus-publish.yml` | CI publish, manual or on release |

Reference: <https://api-docs.nexusmods.com/> and the local `openapi.yaml`.
