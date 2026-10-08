#pragma once

#include <string>
#include <vector>

struct YukariConfig {
    bool enabled = false;
    // Keep the historical default, but allow devices whose applications rely
    // on Magisk-provided mounts to opt out without disabling service filtering.
    bool force_denylist_unmount = true;
    // Hide the lineageos.platform resource package inside target processes by
    // filtering AssetManager lookups.  Off by default because applications that
    // legitimately consume the Lineage SDK resources lose them.
    bool hide_lineage_resources = false;
    // Hide the LineageOS system features (org.lineageos.*) from
    // PackageManager.hasSystemFeature and getSystemAvailableFeatures.
    bool hide_lineage_features = false;
    // Rewrite the lineage protected-broadcast actions in outbound broadcasts so
    // sending them is no longer rejected on LineageOS.
    bool hide_lineage_broadcasts = false;
    std::vector<std::string> targets;
};

bool load_config(YukariConfig &out);
bool is_target(const YukariConfig &config, const std::string &package_name);
