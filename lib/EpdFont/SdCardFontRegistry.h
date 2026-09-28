#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "VectorFontSupport.h"

// Reader point sizes offered for direct TrueType/OpenType (.ttf/.otf/.ttc)
// families. Vector fonts render at any size, so the family "ships" this fixed
// set; every size-aware UI (settings, in-book menu, web settings) reads it via
// SdCardFontFamilyInfo::availableSizes().
inline constexpr uint8_t VECTOR_FONT_POINT_SIZES[] = {10, 11, 12, 13};

struct SdCardFontFileInfo {
  std::string path;   // v4 on-disk naming: "/<root>/<Family>/<Family>_<size>.cpfont"
                      // where <root> is "/.fonts" (preferred, hidden) or "/fonts" (visible).
                      // e.g. "/.fonts/NotoSansCJK/NotoSansCJK_14.cpfont"
  uint8_t pointSize;  // parsed from filename: 14 (0 for size-free vector fonts)
  uint8_t style;      // .cpfont: always 0 (all 4 styles bundled in one file).
                      // Vector family: the style ROLE of this file:
                      // 0=regular, 1=bold, 2=italic, 3=bold-italic. A loose
                      // vector file directly under a fonts root is always role 0.
};

struct SdCardFontFamilyInfo {
  // Names/range summaries do not hydrate paths. Detail consumers call this once.
  bool ensureDetails() const;
  // Drop paths loaded from the persistent index while retaining the family
  // summary. This lets streaming consumers bound RAM to one family at a time.
  void releaseDetails() const;
  uint8_t firstSize = 0, lastSize = 0;
  uint32_t indexOffset = 0, indexBytes = 0, indexHash = 0;
  uint16_t indexCount = 0;
  // Rebuild-only source marker. It is not serialized and is irrelevant after
  // the index has been loaded.
  bool sourceVisibleRoot = false;
  std::string name;  // directory name, e.g. "NotoSansCJK"
  mutable std::vector<SdCardFontFileInfo> files;
  // true for a TrueType/OpenType family (.ttf/.otf/.ttc) rendered at any size
  // via the FreeInkFont engine (see TtfEpdFont / SdCardFontSystem). `files`
  // then holds one entry per style role, pointSize 0. false = a directory of
  // pre-rasterized .cpfont files. Always false when CROSSPOINT_VECTOR_FONTS is 0.
  bool vector = false;

  const SdCardFontFileInfo* findFile(uint8_t size, uint8_t style = 0) const;
  const SdCardFontFileInfo* findClosestFile(uint8_t targetSize, uint8_t style = 0) const;
  std::vector<uint8_t> availableSizes() const;
};

class SdCardFontRegistry {
 public:
  static constexpr int MAX_SD_FAMILIES = 128;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Returns true if any families found.
  // Use lastDiscoveryFailed() to distinguish an empty card from an incomplete
  // scan caused by a recoverable directory-entry allocation failure.
  bool discover();
  // Validate directory names/file sizes, then load names only from the index.
  // A missing/stale index is rebuilt once; font contents are not probed on hits.
  bool loadNames(bool checkInventory = false);
  static void invalidateIndex();
  bool needsRefresh() const;
  uint32_t revision() const { return revision_; }
  bool lastDiscoveryFailed() const { return discoveryFailed_; }
  void clear();

  // Parse a v4 .cpfont filename without allocating. Reused by the dictionary
  // font path, which scans one selected family without retaining a catalog.
  static bool parseFilename(const char* filename, uint8_t& size, uint8_t& style);

#if CROSSPOINT_VECTOR_FONTS
  // Match a vector font filename (.ttf/.otf/.ttc, case-insensitive) and return
  // the length of the base name (extension stripped) in `baseLen`.
  static bool parseVectorFontName(const char* filename, size_t& baseLen);
  // Style role (0=regular, 1=bold, 2=italic, 3=bold-italic) inferred from a
  // vector font's base name (case-insensitive "bold"/"italic"/"oblique" tokens).
  static uint8_t parseVectorStyle(const char* baseName, size_t baseLen);
  // Refine each vector file's style role from its real face metadata
  // (FtFont::inspectStream: OS/2 weight + italic flag), keeping the
  // filename-derived role when the face can't be read. Then keep at most one
  // file per role, chosen deterministically by design weight.
  static void refineVectorStyles(const char* dirPath, std::vector<SdCardFontFileInfo>& files);
  // FtFont::ReadFn over a HalFile* ctx (absolute-offset reads; count 0 is a
  // seek probe). Shared by face inspection here and streamed TTF sources
  // (SdCardFontSystem).
  static unsigned long halFileRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  const SdCardFontFamilyInfo* findSummary(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically
  bool discoveryFailed_ = false;
  uint32_t revision_ = 0;
  uint32_t inventoryGeneration_ = 0;
  uint64_t inventoryFingerprint_ = 0;
  bool inventoryKnown_ = false;
  bool readIndex(uint64_t fingerprint);

  // Rebuild the cache while retaining only family summaries and one directory
  // entry at a time. Full paths are written straight to the cache file.
  bool rebuildIndex(uint64_t fingerprint, uint32_t generation);
};
