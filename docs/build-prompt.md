# Build BurnLight

You are an AI coding agent working in a local checkout of BurnLight. Build the native Mac app from the current source, run the available GUI validation, and open the resulting app. Carry out the work; do not stop at a plan or a list of commands for the user to run.

Produce a **read-only app by default**. If the user has requested recording support, produce a write-enabled app after validation. Enabling recording in the build does not authorize an actual disc operation.

## 1. Check the checkout and build environment

Work from the repository root. Read [README.en.md](../README.en.md), [the repository guide](repository.md), and the current build scripts. Historical reports describe earlier builds; use the current scripts to determine the actual requirements and output paths.

Check the working tree and preserve the user's changes. Build the existing application without changing its features or recording behavior.

Confirm these prerequisites:

- An Apple Silicon Mac running macOS 26 or 27. The app has been confirmed working on macOS 26.6 and macOS 27.
- Homebrew, Python 3, and Xcode Command Line Tools, including a working `xcrun swiftc` and C++ compiler.
- The SDK directory `/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`. The VAT backend script currently hardcodes this path. The GUI script accepts `BURNLIGHT_SDK`, but that override does not change the backend script's requirement.
- Homebrew under `/opt/homebrew`. The packaging script currently uses fixed paths there for `readcd`, libraries, and third-party notices.

If a required tool or SDK is missing, identify the missing component and use the normal installation process where authorized. Do not disguise another SDK as `MacOSX26.sdk`, change global toolchain selection, or disable system security protections to get past a build error. Running the app on macOS 27 does not by itself verify building it with the macOS 27 SDK.

Before rebuilding, make sure BurnLight is idle and quit it normally if it is running. The scripts refuse to proceed when `/private/tmp/burnlight-physical-<uid>/active.json` indicates an active or unknown physical task. Respect that refusal; do not remove the marker or force-terminate a recording worker.

## 2. Prepare dependencies and reference files

Install any missing dependencies:

```sh
brew install cdrtools libisofs libudfread cmake pkgconf smake
```

Restore the reference inputs required by packaging:

```sh
python3 gui/scripts/restore-reference.py
```

This checks the committed reference archive and restores only missing files: the historical `build/appendcd`, the media6 and initial7 backends under `~/.local/libexec/appendcd/`, and their provenance records. The GUI's current CLI is compiled separately from the current source.

If restoration reports an existing file with a different hash, investigate and report the conflict. Do not overwrite the file, change the expected hash, or replace the reference archive to make the check pass. Keep `build/appendcd` as the packaging reference; any separate current-source CLI build belongs in `build-cli/`.

## 3. Prepare the VAT backend

The app should include `appendcd-vat-cdrecord` version `3.02a09-appendcd-vat9`, with its matching `provenance.json`. For a fresh checkout, build it with:

```sh
python3 scripts/build-cdrecord.py --vat --output-dir build-vat-backend-v9
```

The script downloads pinned cdrtools source, verifies its SHA-256, applies the repository's patches, and builds the isolated backend. It does not perform a disc operation.

If a valid backend and matching provenance already exist, reuse them. If the same output directory contains an existing patched source tree that needs rebuilding, use:

```sh
python3 scripts/build-cdrecord.py --vat --output-dir build-vat-backend-v9 --resume
```

Use `--resume` only for the existing patched VAT source tree. Diagnose download, checksum, patch, or compiler errors rather than repeatedly retrying an unchanged command. Do not substitute Homebrew's unmodified `cdrecord`, an older vat8 backend, or another recording backend.

## 4. Build and validate the app

Use the repository's default GUI build workflow. It compiles the C++ adapter and Swift sources, bundles the runtime dependencies, and applies a local ad hoc signature. A plain `swift build` does not replace app packaging.

Keep the default `gui/output/` location for this workflow. If the environment already sets build overrides such as `BURNLIGHT_BUILD_ROOT`, `BURNLIGHT_ADAPTER_BUILD`, or `BURNLIGHT_VAT_BACKEND`, inspect them before proceeding so you do not accidentally use unrelated outputs.

Build and run GUI validation, saving generated evidence outside the historical records:

```sh
BURNLIGHT_EVIDENCE_DIR="$PWD/build-gui-validation/evidence" ./gui/scripts/test.sh
```

This script rebuilds the app with physical writing disabled and runs GUI validation without recording a disc. Check its exit status and validation results. Keep any failed checks visible in the final report. Historical export checks that require untracked real-disc caches cannot be reproduced from a fresh checkout; do not report them as newly passed.

If the user requested a write-enabled app, rebuild **after** validation:

```sh
BURNLIGHT_ENABLE_WRITES=1 ./gui/scripts/build.sh
```

Otherwise keep the validated read-only app. If another build is needed, explicitly use `BURNLIGHT_ENABLE_WRITES=0`. Running `test.sh` again will replace a write-enabled app with a read-only one.

## 5. Verify and open the result

Verify the final bundle's signature:

```sh
codesign --verify --deep --strict gui/output/BurnLight.app
```

Inspect `gui/output/BurnLight.app/Contents/Resources/LocalConfiguration.json` and confirm that `allowPhysicalWrites` matches the requested mode. Confirm that the bundle includes the VAT backend and its provenance in `Contents/Resources/Provenance.json`; packaging must retain its hash checks and third-party notices.

Open the newly built app:

```sh
open gui/output/BurnLight.app
```

If UI inspection is available, confirm that its main window opens. A drive does not need to be connected. Opening the window is sufficient for this build task; do not start a disc inspection, export, burn, or recovery operation. If you cannot inspect the window, distinguish a successful launch command from a visually confirmed launch.

Leave the app in `gui/output/` unless the user requested installation elsewhere. Do not replace an installed copy or create a release package as an extra step.

## 6. Report what was delivered

Give the user the absolute path to `BurnLight.app`, whether it is read-only or write-enabled, the checks that actually passed, and whether the main window was confirmed. Mention any build-related changes or unresolved blockers. Do not claim new physical-disc or Windows compatibility results from a source build or offline validation.
