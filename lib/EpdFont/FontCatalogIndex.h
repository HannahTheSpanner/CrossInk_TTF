#pragma once

#include <cstddef>
#include <cstdint>

#include "VectorFontSupport.h"

namespace fontcatalog {
// Rebuildable cache: separate from user font files and EPUB caches.
inline constexpr char Path[] = "/.crosspoint/font-catalog.bin";
inline constexpr char TempPath[] = "/.crosspoint/font-catalog.tmp";
inline constexpr uint32_t Magic = 0x46434931;
// Version 2 adds vector (.ttf/.otf/.ttc) families, flagged in Entry::flags.
// Builds without the vector-font engine keep version 1 so their index stays
// byte-identical; the mismatch forces a one-time rebuild when switching
// between the two, so fonts already on the card are picked up either way.
inline constexpr uint32_t Version = CROSSPOINT_VECTOR_FONTS ? 2 : 1;
inline constexpr uint8_t EntryFlagVector = 0x01;
inline constexpr uint32_t MaxBytes = 2 * 1024 * 1024;
inline constexpr uint16_t MaxFiles = 256;
inline constexpr uint16_t MaxPath = 255;
struct Header {
  uint32_t magic = Magic;
  uint32_t version = Version;
  uint64_t inventory = 0;
  uint32_t count = 0;
  uint32_t reserved = 0;
};
struct Entry {
  char name[128] = {};
  uint32_t offset = 0;
  uint32_t bytes = 0;
  uint32_t hash = 0;
  uint16_t count = 0;
  uint8_t first = 0;
  uint8_t last = 0;
  uint8_t flags = 0;  // EntryFlagVector; always 0 in version 1
  uint8_t reserved[3] = {};
  uint32_t checksum = 0;
};
static_assert(sizeof(Header) == 24 && sizeof(Entry) == 152, "Update font index format version");
inline uint32_t hashBytes(uint32_t hash, const void* bytes, size_t size) {
  const auto* p = static_cast<const uint8_t*>(bytes);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * 16777619u;
  return hash;
}
bool inventory(uint64_t& fingerprint);
}  // namespace fontcatalog
