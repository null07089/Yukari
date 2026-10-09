# Yukari

Yukari is a target-scoped Zygisk module that hides custom-ROM ServiceManager
signals. Matching is ASCII case-insensitive for `lineage`, `crdroid`, `aospa`,
`pixelexperience`, `omnirom`, `protonaosp`, plus the exact service name
`profile`. A default-on stealth mode additionally hides the `lineageos.platform`
resource package, the `org.lineageos.*` system features and the LineageOS
protected-broadcast actions inside target processes; each channel can be
disabled in the configuration.

The preferred implementation hooks `android.os.BinderProxy.transactNative` via
Zygisk's JNI hook API. ServiceManager enumeration/debug replies are filtered at
the Parcel layer. Matching direct `getService`/`checkService` lookups (including
the newer `getService2`/`checkService2`) are redirected only for application
callers. The first caller outside Binder/ServiceManager/reflection plumbing
determines the policy: framework and Lineage callers keep the real Binder,
and unknown callers or failed classification are left unchanged;
so libbinder PLT/GOT relocations are not modified. `listServices` and
`getServiceDebugInfo` are filtered without changing UTF-16 string lengths, and
`ServiceManager.sCache` is cleaned during app specialization. Systems without
the stable JNI entry point use the legacy ioctl filter as a fallback; its PLT
replacement points at an anonymous RX trampoline.

Binder transaction numbers are read from the running framework's
`IServiceManager.Stub.TRANSACTION_*` fields, not inferred from SDK_INT.
Newer Lineage builds insert `getService2`/`checkService2` and shift existing
slots even within the same SDK level. Android 8/9 use the legacy 1/2/4 slots;
unavailable modern fields disable the affected filtering rather than guess.
Application lookup requests are copied and rewritten with equal-length UTF-16
placeholders; the caller's original Parcel and the reply layout remain intact.
The legacy ioctl fallback still filters enumeration only, without direct
lookup redirection or Java caller classification.

Beyond ServiceManager, each target process also hides three additional
ROM-signal channels (all enabled by default, all process-local and
length-preserving):

- **Resource package** — AssetManager name/ID lookups treat `lineageos.platform`
  (resource package id `0x3f`) as absent; requires the Android 9+ entry points.
- **System features** — `IPackageManager.hasSystemFeature` request names are
  rewritten and `Parcel.nativeReadString8/16` returns equal-length placeholders
  for the `org.lineageos.*` names, so enumeration cannot observe them.
- **Protected broadcasts** — outbound `IActivityManager` broadcast requests are
  copied and the ten lineage protected actions replaced, so sends succeed
  silently instead of raising `SecurityException`.
- **Filesystem fingerprints** — ROM-named entries are removed from system
  directory listings (RRO overlays, permission XMLs, platform resources) and
  reflection on `AssetManager.LINEAGE_APK_PATH` is hidden.

Module updates require a reboot: Zygisk keeps the module `.so` mapped in
zygote, and replacing the file under a live mapping crashes the process.

Private ELF symbols are hidden with a linker version script and stripped from
release artifacts. The module mapping can still be visible in `/proc/self/maps`
because the JNI callback must remain resident; unloading it safely would
require relocating the complete C++ runtime and is intentionally avoided.

See [README.zh-CN.md](README.zh-CN.md) for the detailed design and verification
commands.

Native-only libbinder lookups, explicitly trusted framework namespaces, and
ROM-specific late cache injection are not fully covered by this JNI policy.
Device-level startup and detector regression checks remain necessary.

## Configuration

```json
{
  "enabled": true,
  "force_denylist_unmount": true,
  "hide_lineage_resources": true,
  "hide_lineage_features": true,
  "hide_lineage_broadcasts": true,
  "hide_lineage_files": true,
  "targets": ["com.example.app"]
}
```

Set `force_denylist_unmount` to `false` on devices where target apps depend
on Magisk-provided mounts; service filtering remains enabled.

Set `hide_lineage_resources` to `true` (default) to also hide the
`lineageos.platform` resource package inside target processes. The hook filters
AssetManager name/ID lookups for resource package id `0x3f` (and `defPackage`
`lineageos.platform`), so resource probes behave as on a non-LineageOS build
without touching PackageManager or the system image. Applications that
legitimately use Lineage SDK resources lose them in that process; package
lists, SDK classes and `/system` files stay visible, so set the flag to `false`
for such targets. The hook needs the Android 9 (API 28) AssetManager entry
points; on older releases the flag is skipped.

Set `hide_lineage_features` to `true` to hide the LineageOS system features
`org.lineageos.livedisplay`, `org.lineageos.profiles`, `org.lineageos.hardware`,
`org.lineageos.globalactions`, `org.lineageos.trust`, `org.lineageos.health`,
`org.lineageos.android` and `org.lineageos.settings` inside target processes.
`PackageManager.hasSystemFeature()` returns `false` for them and
`getSystemAvailableFeatures()` reports equal-length underscore placeholders
instead of the real names; both paths keep the Parcel layout unchanged. The
flag defaults to `true`; set it to `false` for apps that gate their own Lineage
integration on these features.

Set `hide_lineage_broadcasts` to `true` (default) to rewrite the LineageOS
protected-broadcast actions in outbound `IActivityManager` broadcast requests.
Sending e.g. `lineageos.intent.action.REFRESH_PREFERENCE` then behaves like on
AOSP (accepted with no receiver) instead of raising `SecurityException`, which
would otherwise fingerprint the ROM. The action is replaced by an equal-length
placeholder in a private request copy, so the app's own Intent is untouched;
set the flag to `false` for targets that rely on sending these actions.

Set `hide_lineage_files` to `true` (default) to hide LineageOS filesystem
fingerprints inside target processes: ROM-named entries are removed from
directory listings of system paths (`/system`, `/product`, `/vendor`, `/odm`,
`/apex`, ...), covering the `framework-res__lineage_*` RRO overlay, the
`org.lineageos.*.xml` permission files and the platform resource APK;
`readlink()`/`Files.readSymbolicLink()` results are scrubbed as well, so
enumerating `/proc/self/fd` cannot reveal lineage-named open files; reflection
on `android.content.res.AssetManager.LINEAGE_APK_PATH` throws
`NoSuchFieldException`. The files on disk are untouched.

Run `module/action.sh` (installed as `/data/adb/modules/Yukari/action.sh`) to
select targets. `a` merges all discovered third-party apps, `s` merges selected
numbers, `r` replaces targets, `k` preserves the current list, and `q` cancels.
Without a terminal, Volume + merges all discovered apps and Volume - starts
per-app selection. A timeout or unavailable input preserves the existing file.
Owner, secondary-user and work-profile package lists are merged.
The script validates the known configuration fields before updating them;
unsupported JSON fields or escapes leave the file unchanged.

## Build

Install Gradle 8.11.1, JDK 17 and the Android SDK/NDK locally. The repository's
`gradlew` delegates to Gradle on `PATH`; CI installs the pinned distribution.

```bash
./gradlew :module:assembleRelease
bash scripts/package.sh
```

`v*` tags are published through the GitHub Actions workflow. The workflow also
updates the repository-root `update.json` on `Dev`, and `module.prop` points
`updateJson` at
`https://raw.githubusercontent.com/null07089/Yukari/Dev/update.json`, so
Magisk offers module updates from the repository's releases.

## Verification

With `hide_lineage_resources` enabled, a target process should log
`AssetManager resource hook installed` and see
`getIdentifier("config_enableLiveDisplay", "bool", "lineageos.platform")` return
`0` (`getBoolean`/`getInteger` then throw `NotFoundException`), while the
package itself stays listed and SDK classes remain loadable. A non-target app
on the same device must still read `true`/`6500`, confirming the hook is
process-local. Regression-test the target's own features for unexpected
resource failures.

With `hide_lineage_features` enabled,
`getPackageManager().hasSystemFeature("org.lineageos.livedisplay")` must return
`false` in a target process and `getSystemAvailableFeatures()` must not expose
the real `org.lineageos.*` names; a non-target app must still see them.

With `hide_lineage_broadcasts` enabled, sending
`lineageos.intent.action.REFRESH_PREFERENCE` from a target process must succeed
silently instead of raising `SecurityException`, and the module log shows
`scrubbed N lineage broadcast action(s)`; a non-target app must still be
rejected.

With `hide_lineage_files` enabled, `new File("/product/overlay").list()` in a
target process must not contain Lineage-named overlays,
`AssetManager.class.getDeclaredField("LINEAGE_APK_PATH")` must throw
`NoSuchFieldException`, and resolving `/proc/self/fd/*` with
`Files.readSymbolicLink()` or `Os.readlink()` must not return a path containing
`lineage`; a non-target app must still see all of them.
