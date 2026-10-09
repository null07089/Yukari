#pragma once

#include <cstddef>
#include <string>

bool hide_service(const std::string &service_name);

// True when the value contains one of the fixed ROM keywords
// (lineage, crdroid, aospa, pixelexperience, omnirom, protonaosp).
// Used for service names and for filtering ROM-named filesystem entries.
bool contains_rom_keyword(const std::string &value);

// Replaces every case-insensitive ROM keyword occurrence in the buffer with
// underscores, keeping the byte length unchanged.  Returns the number of
// replaced occurrences.  The buffer does not need to be NUL-terminated.
std::size_t scrub_rom_keywords(char *buffer, std::size_t length);
