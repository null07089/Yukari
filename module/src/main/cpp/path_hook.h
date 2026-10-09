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
