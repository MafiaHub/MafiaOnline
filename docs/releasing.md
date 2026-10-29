# Releasing

Releases follow Kingdoms Connected's model: the launcher patches itself from
MafiaHub Services with the Framework's build patcher, and GitHub keeps a
release with the client and server ZIPs for each tag.

## How players get updates

`Mafia1OnlineLauncher.exe` runs `Framework::Services::Builds::BuildPatcher`
before it loads Game.exe (`code/launcher/src/patcher.cpp`). It asks Services
for the newest build on the installation's channel, downloads only the files
whose SHA-256 changed, and hands the swap to `MafiaHubUpdater.exe`, which
replaces the files the launcher holds open and starts it again. Offline, it
starts the installed version without a prompt.

- The channel comes from `.mafiahub/channel` in the package (`stable` for a
  release, `testing` for a release candidate). Once an installation has
  patched, Services answers which channel its build is on, so a promotion
  reaches it without that file changing.
- A tester follows release candidates by writing `testing` into
  `.mafiahub/channel-override`; deleting it returns to the current release.
- A package with `.mafiahub/distribution` is never patched (for a storefront
  that updates it itself).
- Developer builds have the patcher compiled out (`M1O_PATCHER_ENABLED` is OFF
  unless a release build turns it on), so a local build never patches itself.

## Cutting a release

From an up-to-date `main` with VERSION set:

```bat
set M1O_SERVICES_API_URL=...
set M1O_SERVICES_MOD_ID=...
set M1O_BUILD_DOWNLOAD_TOKEN=...
set MAFIAHUB_BUILD_UPLOAD_TOKEN=...
python tools\release.py --rc 1     :: v8.0.0-rc.1 for testers
python tools\release.py            :: v8.0.0
```

`tools/release.py`:

1. Builds the Release client, launcher (patcher compiled in with the download
   token) and server with `builds\build.bat`.
2. Packages `builds/releases/Mafia1OnlineClient-<version>-windows-x86.zip`
   (stamped with the channel) and `Mafia1OnlineServer-<version>-windows-x64.zip`.
3. Publishes the client folder to the Services `testing` channel with
   `@mafiahub/services-cli` (0.10.0, as KCDC; `M1O_SERVICES_CLI_VERSION` overrides).
4. Tags `v<version>` (or `-rc.N`), pushes the tag and creates the GitHub
   release (a prerelease for a candidate) with both ZIPs.
5. Builds the launcher again without the patcher.

Players on `stable` get the release only once the build is promoted to
`stable` in MafiaHub Services; that step is deliberately manual.

`python tools\release.py --dry-run` builds and packages without publishing;
without the environment above it builds without the patcher.
`--dry-run --skip-build` packages the Release trees as they are.

Client and server must share the version: a MAJOR bump (netcode, sync flow)
needs both, see the version rules in the project's CLAUDE.md.
