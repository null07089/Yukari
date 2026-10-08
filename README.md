# Yukari

Yukari is a target-scoped Zygisk module that hides custom-ROM ServiceManager
signals. Matching is ASCII case-insensitive for `lineage`, `crdroid`, `aospa`,
`pixelexperience`, `omnirom`, `protonaosp`, plus the exact service name
`profile`. An opt-in mode additionally hides the `lineageos.platform` resource
package inside target processes.

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
  "hide_lineage_resources": false,
  "targets": ["com.example.app"]
}
```

Set `force_denylist_unmount` to `false` on devices where target apps depend
on Magisk-provided mounts; service filtering remains enabled.

Set `hide_lineage_resources` to `true` to also hide the `lineageos.platform`
resource package inside target processes. The hook filters AssetManager
name/ID lookups for resource package id `0x3f` (and `defPackage`
`lineageos.platform`), so resource probes behave as on a non-LineageOS build
without touching PackageManager or the system image. Applications that
legitimately use Lineage SDK resources lose them in that process, and package
lists, SDK classes and `/system` files stay visible; leave the flag off unless
a target specifically probes these resources. The hook needs the Android 9
(API 28) AssetManager entry points; on older releases the flag is skipped.

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

## Verification

With `hide_lineage_resources` enabled, a target process should log
`AssetManager resource hook installed` and see
`getIdentifier("config_enableLiveDisplay", "bool", "lineageos.platform")` return
`0` (`getBoolean`/`getInteger` then throw `NotFoundException`), while the
package itself stays listed and SDK classes remain loadable. A non-target app
on the same device must still read `true`/`6500`, confirming the hook is
process-local. Regression-test the target's own features for unexpected
resource failures.
