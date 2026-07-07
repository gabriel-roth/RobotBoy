# Robot Boy Rename Design

## Goal

Rename the combined plugin and repository from Foobar to Robot Boy as a clean identity break. Use `Robot Boy` for user-facing names and `RobotBoy` wherever spaces are not allowed. Keep the four module slugs unchanged.

## Scope

- Change VCV Rack and MetaModule plugin metadata from `Foobar` to `Robot Boy` / `RobotBoy`.
- Rename active build targets, package names, registration symbols, compile-time identifiers, asset prefixes, current documentation, and active comments that encode the old plugin identity.
- Preserve module slugs: `Loooop`, `Lop`, `MF20Filter`, and `Particules`.
- Rename the GitHub repository from `Foobar` to `RobotBoy`, update `origin`, and rename the local checkout from `~/Dev/Foobar` to `~/Dev/RobotBoy`.
- Leave historical `.superpowers` reports and unrelated existing untracked files unchanged.

## Patch Migration

Keep `Foobar-test.vcv` unchanged. Decompress it with Zstandard, parse its JSON payload, change module references whose `plugin` value is exactly `Foobar` to `RobotBoy`, and write a separately compressed `RobotBoy-test.vcv`. Module names and all other patch data remain unchanged.

## Compatibility

This is intentionally a clean break. No compatibility alias for the old `Foobar` plugin slug will be added. Existing patches must be migrated to `RobotBoy`, as demonstrated by the copied test patch.

## Verification

- Validate all edited JSON files.
- Confirm active source and current documentation contain no remaining plugin-identity uses of `Foobar`; adjudicate historical records separately.
- Confirm `RobotBoy-test.vcv` decompresses as valid JSON, contains `RobotBoy` plugin references, contains no `Foobar` plugin references, and otherwise matches the original patch.
- Run the repository test suites.
- Build the VCV Rack plugin and MetaModule package and inspect the resulting package/asset paths.
- After repository and directory renames, confirm the current branch, `origin`, local path, and GitHub repository all use `RobotBoy`.

## Safety

Only files required by the rename and the new migrated patch will be staged. Existing unrelated untracked files will not be modified or committed.
