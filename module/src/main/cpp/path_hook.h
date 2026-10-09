#pragma once

#include <jni.h>

#include "zygisk.hpp"

// Hides LineageOS filesystem fingerprints inside the current process:
//  - ROM-named entries are removed from directory listings of system paths
//    (RRO overlay APKs, permission XMLs, the platform resource APK, ...).
//  - readlink()/readSymbolicLink() results are scrubbed so /proc/self/fd
//    enumeration cannot reveal lineage-named open files.
//  - reflection lookups of the LineageOS AssetManager.LINEAGE_APK_PATH field
//    behave as if the field did not exist.
// The original files stay untouched; only this process's view changes.
// Call in preAppSpecialize for target processes only.  Returns false when no
// entry point could be hooked.
bool install_path_hooks(JNIEnv *env, zygisk::Api *api);

// Closes file descriptors inherited from zygote that point at ROM-named files.
// Path hiding (SUSFS, mount tricks) does not cover already-open descriptors,
// and detectors enumerate /proc/self/fd to find them.  Call this in
// postAppSpecialize for target processes only, before application code runs.
void close_leaked_rom_fds();
