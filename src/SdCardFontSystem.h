#pragma once

#include <HalStorage.h>  // HalFile (kept open for streamed TTFs)
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <VectorFontSupport.h>

#if CROSSPOINT_VECTOR_FONTS
#include <FontPsram.h>  // PsramVector for resident TTF bytes
#endif

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ReaderFontSizeStep.h"

class GfxRenderer;
class TtfEpdFont;

struct DictionaryFontActivation {
  int fontId = 0;
  bool usingDictionaryFont = false;
};

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  using SettingsPersistenceCallback = void (*)(void* context);

  // Out-of-line (defined where TtfEpdFont is a complete type) so the
  // std::unique_ptr<TtfEpdFont> member works with only a forward declaration.
  SdCardFontSystem();
  ~SdCardFontSystem();
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Register the font resolver and load a saved SD font selection. When the
  /// built-in font is selected, discovery stays deferred until font metadata
  /// is explicitly requested.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);

  // An EPUB can own a temporary per-book settings snapshot while this system
  // repairs a missing font selection. Let that reader persist its own state.
  void setSettingsPersistenceCallback(SettingsPersistenceCallback callback, void* context) {
    settingsPersistenceCallback_ = callback;
    settingsPersistenceContext_ = context;
  }

  /// Temporarily unload the active SD font without clearing the saved setting.
  /// Call ensureLoaded() later to restore it before reader rendering.
  void releaseLoadedFont(GfxRenderer& renderer);

  /// Release all SD-font RAM that network/TLS work does not need.
  void releaseForNetwork(GfxRenderer& renderer);

  /// Close any font file this system keeps open (streamed TTF sources) before
  /// the SD card is handed to another owner (USB Drive). No-op without the
  /// vector-font engine; .cpfont fonts never keep a file open.
  void releaseOpenFontFiles(GfxRenderer& renderer);

  /// Ensure the font catalog is available for settings/web enumeration, including
  /// newly uploaded or deleted fonts visible in the web UI.
  void ensureRegistry();

  /// Release catalog names and paths without unloading the active reader font.
  void releaseRegistry();

  /// Resolve an SD card font ID from family name + selected point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Change the reader font size using the active SD family when one is selected.
  bool changeReaderFontSize(bool larger, FontSizeStepMode mode = FontSizeStepMode::Wrap);

  /// Convert a pre-point-size SD font slot into the exact installed size.
  /// Used while reading legacy per-book reader settings.
  uint8_t resolveLegacySizeStep(const char* familyName, uint8_t sizeStep);

  // Temporarily replace the reader SD font with a per-book dictionary font.
  // At most one SD font family remains resident. Missing or failed dictionary
  // fonts leave the reader font active and retain the saved selection.
  // targetPointSize of zero follows the active reader size. The selected file
  // still uses the closest installed size, so a removed font file degrades
  // safely without retaining another family or size cache in RAM.
  DictionaryFontActivation activateDictionaryFont(GfxRenderer& renderer, const char* familyName,
                                                  uint8_t targetPointSize = 0);

  // Restore the saved reader font after a dictionary lookup and return its ID.
  int restoreReaderFont(GfxRenderer& renderer);

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() {
    registryDirty_.store(true, std::memory_order_release);
    SdCardFontRegistry::invalidateIndex();
  }
  void markRegistryDirtyForPath(const char* path);

  /// Ensure the registry is available and re-scan it after SD changes.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() { ensureRegistry(); }

 private:
  void persistSettingsChange() const;

  // Load the active SD family at the built-in UI point sizes and register each
  // as a size-matched CJK fallback for the corresponding UI font, so CJK book
  // titles/list rows render at the same size as the surrounding Latin UI text.
  // No-op when no SD family is loaded. Safe to call repeatedly (sizes already
  // loaded are reused).
  void setupUiFallbacks(GfxRenderer& renderer);
  void setupUiFallbacksDirect(GfxRenderer& renderer, const char* familyName);

#if CROSSPOINT_VECTOR_FONTS
  // --- Vector (.ttf/.otf/.ttc) font path (FreeInkFont via TtfEpdFont) -------
  // Load/refresh the selected vector family at the reader size closest to the
  // saved point size, register it with the renderer, and set up CJK UI
  // fallbacks. registryWasDirty forces a reload even if nothing else changed.
  void loadTtfFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, bool registryWasDirty);
  // Unregister + free the active vector font (and its UI-size fallbacks).
  void unloadTtf(GfxRenderer& renderer);
  // Register the loaded vector family at each built-in UI size as a CJK
  // fallback (mirrors setupUiFallbacks for .cpfont; skipped for fonts without
  // CJK coverage).
  void setupTtfUiFallbacks(GfxRenderer& renderer);
  // Re-point the renderer's UI fallback map at the vector UI fonts after an
  // SD-font manager unload cleared it (dictionary font swap).
  void reapplyTtfUiFallbacks(GfxRenderer& renderer) const;
  // Open one style source file (resident if small, streamed if large) into
  // ttfSources_[style]. Returns false on open/read failure.
  bool openTtfSource(uint8_t style, const std::string& path);
  // Register every present source with `font` (shared bytes / file handles).
  void addTtfSources(TtfEpdFont& font);
  // Close/free all style sources.
  void freeTtfSources();
  // Whether the saved reader family is a vector family (registry lookup).
  bool isVectorFamily(const char* familyName);
  // ReadFn for streamed sources: serves the PSRAM prefix cache first, SD after.
  static unsigned long prefixRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif  // CROSSPOINT_VECTOR_FONTS

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  std::atomic<bool> registryDirty_{false};
  bool registryLoaded_ = false;
  uint8_t loadedFontPointSize_ = 0;
  bool fontReloadPending_ = false;
  uint32_t loadedRegistryRevision_ = 0;
  SettingsPersistenceCallback settingsPersistenceCallback_ = nullptr;
  void* settingsPersistenceContext_ = nullptr;

#if CROSSPOINT_VECTOR_FONTS
  // One style source file. SMALL files are read fully into `bytes` (resident,
  // PSRAM when present); LARGE files stream from `file` (kept open) so a
  // multi-MB file never sits in RAM. All faces (reader + UI sizes) share these.
  struct TtfSource {
    // Resident form: the whole file. Streamed form: a PSRAM prefix cache of the
    // file head (cmap/loca/hmtx); empty when PSRAM couldn't fund it.
    freeink::font::PsramVector<uint8_t> bytes;
    HalFile file;  // open handle (streamed form)
    bool streamed = false;
    unsigned long size = 0;
    bool present = false;
  };
  struct TtfUiFallback {
    int uiFontId;  // built-in UI font the fallback serves
    int fontId;    // renderer id of the vector font at that UI size
  };

  // Active vector font (at most one reader-size vector family at a time).
  std::unique_ptr<TtfEpdFont> ttf_;
  // Up to 4 style sources: 0=regular (required), 1=bold, 2=italic, 3=bold-italic.
  TtfSource ttfSources_[4];
  std::string ttfFamily_;     // loaded vector family name ("" = none)
  int ttfFontId_ = 0;         // renderer font id for ttf_ (0 = none)
  uint8_t ttfPointSize_ = 0;  // size ttf_ was built at
  // UI-size vector fallbacks (share ttfSources_), with their renderer ids.
  std::vector<std::unique_ptr<TtfEpdFont>> ttfUi_;
  std::vector<TtfUiFallback> ttfUiIds_;
#endif  // CROSSPOINT_VECTOR_FONTS
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
