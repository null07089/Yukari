#pragma once

#include <string>

bool hide_service(const std::string &service_name);

// True when the value contains one of the fixed ROM keywords
// (lineage, crdroid, aospa, pixelexperience, omnirom, protonaosp).
// Used for service names and for filtering ROM-named filesystem entries.
bool contains_rom_keyword(const std::string &value);
