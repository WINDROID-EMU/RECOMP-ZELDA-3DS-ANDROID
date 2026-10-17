#pragma once

#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

namespace {

// CRC-16-IBM (polynomial 0xA001, bitwise reflection of 0x8005)
// Matches the native OoT3D FUN_002faf90 / Crc16_Compute implementation.
inline uint16_t ComputeOot3dSaveCrc16(const uint8_t* data, size_t length, uint16_t seed = 0U) {
  static const auto table = []() {
    std::array<uint16_t, 256> t{};
    constexpr uint16_t poly = 0xA001U;
    for (uint32_t i = 0; i < 256; ++i) {
      uint16_t c = static_cast<uint16_t>(i);
      for (int bit = 0; bit < 8; ++bit) {
        if ((c & 1U) != 0U) {
          c = static_cast<uint16_t>((c >> 1U) ^ poly);
        } else {
          c >>= 1U;
        }
      }
      t[i] = c;
    }
    return t;
  }();

  uint16_t crc = seed;
  for (size_t i = 0; i < length; ++i) {
    crc = static_cast<uint16_t>((crc >> 8U) ^ table[(crc ^ data[i]) & 0xFFU]);
  }
  return crc;
}

} // namespace

class Oot3dAutoSaveRuntime {
public:
  static constexpr uint32_t kSaveContext = 0x00587958U;
  static constexpr uint32_t kPauseRoot = 0x005043D4U;
  static constexpr uint32_t kPlayStateOffset = 0x0CU;
  static constexpr uint32_t kSlotSaveSize = 5340U; // 0x14DC bytes per save slot file
  static constexpr uint32_t kChecksumOffset = 0x14D8U;
  static constexpr uint32_t kFileNumOffset = 0x14DCU;
  static constexpr uint32_t kGameModeOffset = 0x14E4U;
  static constexpr uint32_t kMasterQuestFlagOffset = 0x000EU;

  struct CollectibleSnapshot {
    uint16_t rupees = 0;
    int16_t rupeeAccumulator = 0;
    uint16_t healthCapacity = 0;
    std::array<uint8_t, 24> inventoryItems{};
    std::array<int8_t, 16> ammo{};
    std::array<int8_t, 19> dungeonKeys{};
    std::array<uint8_t, 20> dungeonItems{};
    int16_t goldSkulltulaTokens = 0;
    uint32_t questItems = 0;
    uint32_t upgrades = 0;
    uint16_t equipment = 0;
    uint8_t magicAcquired = 0;
    uint8_t doubleMagicAcquired = 0;
    uint8_t doubleDefenseAcquired = 0;
    uint8_t biggoronSwordFlag = 0;
    bool valid = false;
  };

  void Reset() {
    mPreviousSnapshot = {};
    mPendingSave = false;
    mDebounceFrames = 0;
  }

  void Update(NativeA32Memory& memory,
              const std::filesystem::path& saveDataDir,
              bool& quickSavePending) {
    if (saveDataDir.empty()) {
      return;
    }

    // 0. Periodically check configuration from auto_save.json (once per second)
    ++mConfigPollFrames;
    if (mConfigPollFrames >= 60U || !mConfigLoaded) {
      mConfigPollFrames = 0U;
      mConfigLoaded = true;
      CheckAutoSaveConfig(saveDataDir);
    }

    if (!mAutoSaveEnabled) {
      mPendingSave = false;
      mDebounceFrames = 0;
      return;
    }

    // 1. Verify we are in active gameplay (PlayState != 0, GameMode == 0)
    uint32_t playState = 0U;
    uint32_t gameMode = UINT32_MAX;
    if (!memory.Read32(kPauseRoot + kPlayStateOffset, &playState) ||
        !memory.Read32(kSaveContext + kGameModeOffset, &gameMode)) {
      return;
    }

    if (playState == 0U || gameMode != 0U) {
      // Outside active gameplay (title screen, cutscene demos, file select, etc.)
      mPreviousSnapshot.valid = false;
      mPendingSave = false;
      mDebounceFrames = 0;
      return;
    }

    // 2. Read active save slot number
    uint32_t fileNum = UINT32_MAX;
    if (!memory.Read32(kSaveContext + kFileNumOffset, &fileNum) || fileNum > 2U) {
      return;
    }

    // 3. Read current collectible state
    CollectibleSnapshot current{};
    if (!ReadSnapshot(memory, current)) {
      return;
    }

    // 4. Initialize baseline snapshot if newly loaded/started
    if (!mPreviousSnapshot.valid) {
      mPreviousSnapshot = current;
      mPreviousSnapshot.valid = true;
      mPendingSave = false;
      mDebounceFrames = 0;
      return;
    }

    // 5. Detect collection events
    const bool collected = DetectCollection(mPreviousSnapshot, current);

    if (collected) {
      mPendingSave = true;
      // Debounce window (~20 frames = ~333ms at 60fps) to let pickup animations / textboxes settle
      mDebounceFrames = 20U;
    }

    // 6. Process pending auto-save
    if (mPendingSave) {
      // If rupees are still counting up in HUD accumulator, hold off saving until accumulator hits 0
      if (current.rupeeAccumulator > 0) {
        mDebounceFrames = 15U;
      } else if (mDebounceFrames > 0U) {
        --mDebounceFrames;
      } else {
        // Debounce expired and accumulator settled: Trigger Auto-Save!
        mPendingSave = false;
        mPreviousSnapshot = current;

        // Perform atomic slot save to disk (save00.bin, save01.bin, etc.)
        PerformAutoSave(memory, saveDataDir, fileNum);

        // Also trigger quickstate capture at frame boundary
        quickSavePending = true;
      }
    }
  }

private:
  static bool ReadSnapshot(NativeA32Memory& memory, CollectibleSnapshot& s) {
    if (!memory.Read16(kSaveContext + 0x0048U, &s.rupees) ||
        !memory.Read16(kSaveContext + 0x155CU, reinterpret_cast<uint16_t*>(&s.rupeeAccumulator)) ||
        !memory.Read16(kSaveContext + 0x0042U, &s.healthCapacity) ||
        !memory.Read16(kSaveContext + 0x00E8U, reinterpret_cast<uint16_t*>(&s.goldSkulltulaTokens)) ||
        !memory.Read32(kSaveContext + 0x00BCU, &s.questItems) ||
        !memory.Read32(kSaveContext + 0x00B8U, &s.upgrades) ||
        !memory.Read16(kSaveContext + 0x00B6U, &s.equipment) ||
        !memory.Read8(kSaveContext + 0x004EU, &s.magicAcquired) ||
        !memory.Read8(kSaveContext + 0x0050U, &s.doubleMagicAcquired) ||
        !memory.Read8(kSaveContext + 0x0051U, &s.doubleDefenseAcquired) ||
        !memory.Read8(kSaveContext + 0x0052U, &s.biggoronSwordFlag)) {
      return false;
    }

    // Inventory items (24 bytes at 0x008C)
    if (!memory.ReadBytes(kSaveContext + 0x008CU, std::span<uint8_t>(s.inventoryItems))) {
      return false;
    }

    // Ammo counts (16 bytes at 0x00A6)
    if (!memory.ReadBytes(kSaveContext + 0x00A6U,
            std::span<uint8_t>(reinterpret_cast<uint8_t*>(s.ammo.data()), s.ammo.size()))) {
      return false;
    }

    // Small dungeon keys (19 bytes at 0x00D4)
    if (!memory.ReadBytes(kSaveContext + 0x00D4U,
            std::span<uint8_t>(reinterpret_cast<uint8_t*>(s.dungeonKeys.data()), s.dungeonKeys.size()))) {
      return false;
    }

    // Dungeon items (boss keys, compasses, maps - 20 bytes at 0x00C0)
    if (!memory.ReadBytes(kSaveContext + 0x00C0U, std::span<uint8_t>(s.dungeonItems))) {
      return false;
    }

    s.valid = true;
    return true;
  }

  static bool DetectCollection(const CollectibleSnapshot& prev, const CollectibleSnapshot& curr) {
    // 1. Rupees collected (wallet count increased or accumulator active)
    if (curr.rupees > prev.rupees || curr.rupeeAccumulator > 0) {
      return true;
    }

    // 2. Health capacity increased (Heart Container / 4 Heart Pieces completed)
    if (curr.healthCapacity > prev.healthCapacity) {
      return true;
    }

    // 3. Inventory items: item acquired in an empty slot (was 0xFF) or item upgraded/changed
    for (size_t i = 0; i < curr.inventoryItems.size(); ++i) {
      if (curr.inventoryItems[i] != prev.inventoryItems[i] && curr.inventoryItems[i] != 0xFFU) {
        return true;
      }
    }

    // 4. Ammo acquired (arrows, bombs, seeds, sticks, nuts, bombchus, beans increased)
    for (size_t a = 0; a < curr.ammo.size(); ++a) {
      if (curr.ammo[a] > prev.ammo[a]) {
        return true;
      }
    }

    // 5. Dungeon small keys: key count changed (collected or door unlocked)
    for (size_t d = 0; d < curr.dungeonKeys.size(); ++d) {
      if (curr.dungeonKeys[d] != prev.dungeonKeys[d]) {
        return true;
      }
    }

    // 6. Dungeon items: Boss key (bit 0), Compass (bit 1), Map (bit 2) newly acquired
    for (size_t d = 0; d < curr.dungeonItems.size(); ++d) {
      if ((curr.dungeonItems[d] & ~prev.dungeonItems[d]) != 0U) {
        return true;
      }
    }

    // 7. Gold Skulltula tokens collected
    if (curr.goldSkulltulaTokens > prev.goldSkulltulaTokens) {
      return true;
    }

    // 8. Quest items (medallions, spiritual stones) or Heart Pieces count increased
    if ((curr.questItems & ~prev.questItems) != 0U) {
      return true;
    }
    const uint32_t currHeartPieces = (curr.questItems >> 24U) & 0xFU;
    const uint32_t prevHeartPieces = (prev.questItems >> 24U) & 0xFU;
    if (currHeartPieces > prevHeartPieces) {
      return true;
    }

    // 9. Equipment: swords, shields, tunics, boots
    if ((curr.equipment & ~prev.equipment) != 0U) {
      return true;
    }

    // 10. Upgrades: strength, scales, quiver, bomb bag, wallet, bullet bag
    if ((curr.upgrades & ~prev.upgrades) != 0U || curr.upgrades > prev.upgrades) {
      return true;
    }

    // 11. Magic / Special flags
    if (curr.magicAcquired > prev.magicAcquired ||
        curr.doubleMagicAcquired > prev.doubleMagicAcquired ||
        curr.doubleDefenseAcquired > prev.doubleDefenseAcquired ||
        curr.biggoronSwordFlag > prev.biggoronSwordFlag) {
      return true;
    }

    return false;
  }

  static void PerformAutoSave(NativeA32Memory& memory,
                              const std::filesystem::path& saveDataDir,
                              uint32_t fileNum) {
    try {
      uint16_t masterQuestFlag = 0U;
      memory.Read16(kSaveContext + kMasterQuestFlagOffset, &masterQuestFlag);
      const bool isMasterQuest = (masterQuestFlag != 0U);
      const uint32_t slotIndex = fileNum + (isMasterQuest ? 3U : 0U);

      char fileName[32];
      std::snprintf(fileName, sizeof(fileName), "save%02u.bin", slotIndex);
      const std::filesystem::path savePath = saveDataDir / fileName;
      const std::filesystem::path tmpPath = saveDataDir / (std::string(fileName) + ".tmp");

      // Read 5340 bytes of save context from guest memory
      std::vector<uint8_t> buffer(kSlotSaveSize);
      if (!memory.ReadBytes(kSaveContext, std::span<uint8_t>(buffer))) {
        std::cerr << "[AutoSave] Error: failed to read SaveContext from memory\n";
        return;
      }

      // Zero out checksum field prior to calculation
      buffer[kChecksumOffset] = 0U;
      buffer[kChecksumOffset + 1U] = 0U;

      // Compute standard OoT3D CRC-16 checksum
      const uint16_t crc = ComputeOot3dSaveCrc16(buffer.data(), buffer.size(), 0U);
      buffer[kChecksumOffset] = static_cast<uint8_t>(crc & 0xFFU);
      buffer[kChecksumOffset + 1U] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);

      // Keep guest memory checksum in sync
      memory.Write16(kSaveContext + kChecksumOffset, crc);

      // Write atomically to temporary file, then rename
      {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          std::cerr << "[AutoSave] Error: cannot open tmp save file " << tmpPath << '\n';
          return;
        }
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        out.flush();
      }

      std::error_code ec;
      std::filesystem::rename(tmpPath, savePath, ec);
      if (ec) {
        std::cerr << "[AutoSave] Error renaming save file: " << ec.message() << '\n';
        return;
      }

      uint16_t rupees = 0U;
      memory.Read16(kSaveContext + 0x0048U, &rupees);
      std::cout << "[AutoSave] Successfully auto-saved slot " << fileNum
                << " (" << fileName << ", CRC: 0x" << std::hex << crc << std::dec
                << ", Rupees: " << rupees << ")\n";
    } catch (const std::exception& e) {
      std::cerr << "[AutoSave] Exception during auto-save: " << e.what() << '\n';
    }
  }

  void CheckAutoSaveConfig(const std::filesystem::path& saveDataDir) {
    try {
      std::filesystem::path configPath = saveDataDir.parent_path() / "auto_save.json";
      if (!std::filesystem::exists(configPath)) {
        configPath = saveDataDir / "auto_save.json";
      }
      if (!std::filesystem::exists(configPath)) {
        mAutoSaveEnabled = true;
        return;
      }
      std::error_code ec;
      auto writeTime = std::filesystem::last_write_time(configPath, ec);
      if (!ec && writeTime == mLastConfigWriteTime) {
        return;
      }
      mLastConfigWriteTime = writeTime;

      std::ifstream f(configPath);
      if (f.is_open()) {
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        if (content.find("\"auto_save_enabled\": false") != std::string::npos ||
            content.find("\"auto_save_enabled\":false") != std::string::npos) {
          if (mAutoSaveEnabled) {
            std::cout << "[AutoSave] Config updated: Auto Save DISABLED\n";
          }
          mAutoSaveEnabled = false;
        } else {
          if (!mAutoSaveEnabled) {
            std::cout << "[AutoSave] Config updated: Auto Save ENABLED\n";
          }
          mAutoSaveEnabled = true;
        }
      }
    } catch (...) {
    }
  }

  CollectibleSnapshot mPreviousSnapshot{};
  bool mPendingSave = false;
  uint32_t mDebounceFrames = 0;
  bool mAutoSaveEnabled = true;
  bool mConfigLoaded = false;
  uint32_t mConfigPollFrames = 0;
  std::filesystem::file_time_type mLastConfigWriteTime{};
};

} // namespace Oot3dNativeGame
