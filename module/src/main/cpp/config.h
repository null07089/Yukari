#pragma once

#include <string>
#include <vector>

struct YukariConfig {
    bool enabled = false;
    // Keep the historical default, but allow devices whose applications rely
    // on Magisk-provided mounts to opt out without disabling service filtering.
    bool force_denylist_unmount = true;
    // Hide the lineageos.platform resource package inside target processes by
    // filtering AssetManager lookups.  Enabled by default; applications that
    // legitimately consume the Lineage SDK resources lose them.
    bool hide_lineage_resources = true;
    // Hide the LineageOS system features (org.lineageos.*) from
    // PackageManager.hasSystemFeature and getSystemAvailableFeatures.
    bool hide_lineage_features = true;
    // Rewrite the lineage protected-broadcast actions in outbound broadcasts so
    // sending them is no longer rejected on LineageOS.
    bool hide_lineage_broadcasts = true;
    // Hide LineageOS filesystem fingerprints: ROM-named entries in system
    // directory listings and the AssetManager.LINEAGE_APK_PATH field.
    bool hide_lineage_files = true;
    std::vector<std::string> targets;
};

bool load_config(YukariConfig &out);
bool is_target(const YukariConfig &config, const std::string &package_name);
