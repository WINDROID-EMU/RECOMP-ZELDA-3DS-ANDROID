#pragma once

#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

class Oot3dGameplayCheatsRuntime {
public:
  static constexpr uint32_t kSaveContext = 0x00587958U;
  static constexpr uint32_t kPauseRoot = 0x005043D4U;
  static constexpr uint32_t kPlayStateOffset = 0x0CU;
  static constexpr uint32_t kGameModeOffset = 0x14E4U;

  void Update(NativeA32Memory& memory, const std::filesystem::path& saveDataDir) {
    if (saveDataDir.empty()) {
      return;
    }

    // 1. Periodically check cheats config file (~3 times per second = every 20 frames)
    ++mPollFrames;
    if (mPollFrames >= 20U || !mConfigLoaded) {
      mPollFrames = 0U;
      mConfigLoaded = true;
      PollCheatsFile(saveDataDir, memory);
    }

    // 2. Check if in active playable state
    uint32_t playState = 0U;
    uint32_t gameMode = UINT32_MAX;
    if (!memory.Read32(kPauseRoot + kPlayStateOffset, &playState) ||
        !memory.Read32(kSaveContext + kGameModeOffset, &gameMode)) {
      return;
    }
    if (playState == 0U || gameMode != 0U) {
      return;
    }

    // 3. Apply continuous cheat toggles
    if (mInfiniteHealth) {
      uint16_t capacity = 0U;
      if (memory.Read16(kSaveContext + 0x0042U, &capacity) && capacity > 0U) {
        memory.Write16(kSaveContext + 0x0044U, capacity);
      }
    }

    if (mInfiniteMagic) {
      memory.Write8(kSaveContext + 0x004EU, 1U);       // magic acquired
      memory.Write8(kSaveContext + 0x0050U, 1U);       // double magic acquired
      memory.Write8(kSaveContext + 0x0046U, 2U);       // magic capacity level
      memory.Write8(kSaveContext + 0x0047U, 0x60U);    // current magic amount
    }

    if (mInfiniteRupees) {
      memory.Write16(kSaveContext + 0x0048U, 500U);    // 500 rupees
    }

    if (mInfiniteAmmo) {
      memory.Write8(kSaveContext + 0x00A6U + 0U, 30U); // Deku sticks
      memory.Write8(kSaveContext + 0x00A6U + 1U, 40U); // Deku nuts
      memory.Write8(kSaveContext + 0x00A6U + 2U, 40U); // Bombs
      memory.Write8(kSaveContext + 0x00A6U + 3U, 50U); // Arrows
      memory.Write8(kSaveContext + 0x00A6U + 4U, 50U); // Slingshot
      memory.Write8(kSaveContext + 0x00A6U + 8U, 50U); // Bombchus
      memory.Write8(kSaveContext + 0x00A6U + 9U, 10U); // Magic beans
    }
  }

private:
  void PollCheatsFile(const std::filesystem::path& saveDataDir, NativeA32Memory& memory) {
    try {
      std::filesystem::path configPath = saveDataDir.parent_path() / "gameplay_cheats.json";
      if (!std::filesystem::exists(configPath)) {
        configPath = saveDataDir / "gameplay_cheats.json";
      }
      if (!std::filesystem::exists(configPath)) {
        return;
      }

      std::error_code ec;
      const auto writeTime = std::filesystem::last_write_time(configPath, ec);
      if (!ec && writeTime == mLastConfigWriteTime) {
        return;
      }
      mLastConfigWriteTime = writeTime;

      std::ifstream f(configPath);
      if (!f.is_open()) {
        return;
      }
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      f.close();

      // Parse continuous cheat toggles
      mInfiniteHealth = (content.find("\"infinite_health\": true") != std::string::npos ||
                         content.find("\"infinite_health\":true") != std::string::npos);
      mInfiniteMagic  = (content.find("\"infinite_magic\": true") != std::string::npos ||
                         content.find("\"infinite_magic\":true") != std::string::npos);
      mInfiniteRupees = (content.find("\"infinite_rupees\": true") != std::string::npos ||
                         content.find("\"infinite_rupees\":true") != std::string::npos);
      mInfiniteAmmo   = (content.find("\"infinite_ammo\": true") != std::string::npos ||
                         content.find("\"infinite_ammo\":true") != std::string::npos);

      // Check for pending one-shot actions
      const std::string actionKey = "\"pending_action\": \"";
      size_t pos = content.find(actionKey);
      if (pos == std::string::npos) {
        pos = content.find("\"pending_action\":\"");
        if (pos != std::string::npos) pos += 18;
      } else {
        pos += actionKey.length();
      }

      if (pos != std::string::npos) {
        size_t endPos = content.find("\"", pos);
        if (endPos != std::string::npos) {
          std::string action = content.substr(pos, endPos - pos);
          if (!action.empty()) {
            std::cout << "[GameplayCheats] Executing action: " << action << '\n';
            ExecuteAction(action, memory);

            // Clear pending action in file to prevent re-execution
            ClearPendingActionInFile(configPath, content, action);
          }
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "[GameplayCheats] Exception reading config: " << e.what() << '\n';
    }
  }

  void ClearPendingActionInFile(const std::filesystem::path& path,
                                std::string& content,
                                const std::string& action) {
    try {
      const std::string needle1 = "\"pending_action\": \"" + action + "\"";
      const std::string needle2 = "\"pending_action\":\"" + action + "\"";
      const std::string replacement = "\"pending_action\": \"\"";
      size_t pos = content.find(needle1);
      if (pos != std::string::npos) {
        content.replace(pos, needle1.length(), replacement);
      } else {
        pos = content.find(needle2);
        if (pos != std::string::npos) {
          content.replace(pos, needle2.length(), replacement);
        }
      }

      std::ofstream out(path, std::ios::trunc);
      if (out.is_open()) {
        out << content;
        out.flush();
      }
      std::error_code ec;
      mLastConfigWriteTime = std::filesystem::last_write_time(path, ec);
    } catch (...) {}
  }

  void ExecuteAction(const std::string& action, NativeA32Memory& memory) {
    // 1. Max Hearts (20 Heart Containers + Double Defense)
    if (action == "max_hearts") {
      memory.Write16(kSaveContext + 0x0042U, 0x0140U); // 20 hearts (20 * 16 = 320 = 0x140)
      memory.Write16(kSaveContext + 0x0044U, 0x0140U); // health full
      memory.Write8(kSaveContext + 0x0051U, 1U);       // double defense acquired
      memory.Write8(kSaveContext + 0x00E7U, 20U);      // 20 defense hearts
      std::cout << "[GameplayCheats] Applied 20 Hearts and Double Defense\n";
    }
    // 2. Heal link to full
    else if (action == "heal_full") {
      uint16_t cap = 0x0140U;
      memory.Read16(kSaveContext + 0x0042U, &cap);
      if (cap == 0U) cap = 0x0030U;
      memory.Write16(kSaveContext + 0x0044U, cap);
    }
    // 3. Max magic
    else if (action == "max_magic") {
      memory.Write8(kSaveContext + 0x004EU, 1U);
      memory.Write8(kSaveContext + 0x0050U, 1U);
      memory.Write8(kSaveContext + 0x0046U, 2U);
      memory.Write8(kSaveContext + 0x0047U, 0x60U);
    }
    // 4. Max Rupees (500)
    else if (action == "max_rupees") {
      uint32_t upgrades = 0U;
      memory.Read32(kSaveContext + 0x00B8U, &upgrades);
      upgrades |= (3U << 12U); // Tycoon wallet
      memory.Write32(kSaveContext + 0x00B8U, upgrades);
      memory.Write16(kSaveContext + 0x0048U, 500U);
    }
    // 5. All Equipment (Swords, Shields, Tunics, Boots)
    else if (action == "all_equipment") {
      memory.Write16(kSaveContext + 0x00B6U, 0x7777U); // Kokiri/Master/Biggoron, Deku/Hylian/Mirror, all Tunics, all Boots
      memory.Write8(kSaveContext + 0x0052U, 1U);       // Biggoron sword flag
      std::cout << "[GameplayCheats] Unlocked all Swords, Shields, Tunics, and Boots\n";
    }
    // 6. All Quest Items & Medallions
    else if (action == "all_medallions") {
      uint32_t quest = 0U;
      memory.Read32(kSaveContext + 0x00BCU, &quest);
      quest |= 0x007C003FU; // All 6 Medallions + Kokiri Emerald, Goron Ruby, Zora Sapphire, Stone of Agony, Gerudo Card
      memory.Write32(kSaveContext + 0x00BCU, quest);
      std::cout << "[GameplayCheats] Unlocked all Medallions and Spiritual Stones\n";
    }
    // 7. 100 Skulltula Tokens
    else if (action == "all_skulltulas") {
      memory.Write16(kSaveContext + 0x00E8U, 100U);
    }
    // 8. All Dungeon Items & Keys for current dungeon
    else if (action == "dungeon_keys") {
      uint16_t mapIndex = 0U;
      if (memory.Read16(kSaveContext + 0x1592U, &mapIndex) && mapIndex < 19U) {
        memory.Write8(kSaveContext + 0x00D4U + mapIndex, 9U); // 9 small keys
        memory.Write8(kSaveContext + 0x00C0U + mapIndex, 7U); // Boss key (1) | Compass (2) | Map (4)
        std::cout << "[GameplayCheats] Granted 9 Keys, Boss Key, Compass, and Map for dungeon " << mapIndex << '\n';
      }
    }
    // 9. All 24 Inventory Items & Bottles
    else if (action == "all_items") {
      // Inventory slots 0..23
      const std::array<uint8_t, 24> fullInventory{
        0x00U, // Deku Stick
        0x01U, // Deku Nut
        0x02U, // Bomb
        0x03U, // Bow
        0x04U, // Fire Arrow
        0x05U, // Din's Fire
        0x06U, // Slingshot
        0x08U, // Ocarina of Time
        0x09U, // Bombchu
        0x0BU, // Longshot
        0x0CU, // Ice Arrow
        0x0DU, // Farore's Wind
        0x0EU, // Boomerang
        0x0FU, // Lens of Truth
        0x10U, // Magic Beans
        0x11U, // Megaton Hammer
        0x12U, // Light Arrow
        0x13U, // Nayru's Love
        0x18U, // Bottle 1 (Fairy)
        0x15U, // Bottle 2 (Red Potion)
        0x1CU, // Bottle 3 (Blue Fire)
        0x16U  // Bottle 4 (Green Potion)
      };
      memory.WriteBytes(kSaveContext + 0x008CU, std::span<const uint8_t>(fullInventory.data(), 22U));

      // Upgrades: Golden Gauntlets, Golden Scale, Quiver 50, Bomb Bag 40, Bullet Bag 50, Wallet 500
      uint32_t upgrades = (3U) | (3U << 3U) | (3U << 6U) | (2U << 9U) | (3U << 12U) | (3U << 14U);
      memory.Write32(kSaveContext + 0x00B8U, upgrades);

      // Max Ammo
      memory.Write8(kSaveContext + 0x00A6U + 0U, 30U); // Sticks
      memory.Write8(kSaveContext + 0x00A6U + 1U, 40U); // Nuts
      memory.Write8(kSaveContext + 0x00A6U + 2U, 40U); // Bombs
      memory.Write8(kSaveContext + 0x00A6U + 3U, 50U); // Arrows
      memory.Write8(kSaveContext + 0x00A6U + 4U, 50U); // Slingshot
      memory.Write8(kSaveContext + 0x00A6U + 8U, 50U); // Bombchus
      memory.Write8(kSaveContext + 0x00A6U + 9U, 10U); // Beans

      // Enable Magic
      memory.Write8(kSaveContext + 0x004EU, 1U);
      memory.Write8(kSaveContext + 0x0050U, 1U);
      memory.Write8(kSaveContext + 0x0046U, 2U);
      memory.Write8(kSaveContext + 0x0047U, 0x60U);

      std::cout << "[GameplayCheats] Granted all 24 inventory items, bottles, and upgrades\n";
    }
    // 10. Individual item addition
    else if (action.starts_with("item_")) {
      GiveIndividualItem(action.substr(5), memory);
    }
  }

  void GiveIndividualItem(const std::string& item, NativeA32Memory& memory) {
    if (item == "bow") {
      memory.Write8(kSaveContext + 0x008CU + 3U, 0x03U); // Slot 3 = Bow
      memory.Write8(kSaveContext + 0x00A6U + 3U, 50U);   // Arrows ammo
      SetUpgradeBits(memory, 0U, 3U, 3U);                // Quiver 50
    } else if (item == "fire_arrow") {
      memory.Write8(kSaveContext + 0x008CU + 4U, 0x04U); // Slot 4
      EnsureMagicAcquired(memory);
    } else if (item == "ice_arrow") {
      memory.Write8(kSaveContext + 0x008CU + 10U, 0x0CU); // Slot 10
      EnsureMagicAcquired(memory);
    } else if (item == "light_arrow") {
      memory.Write8(kSaveContext + 0x008CU + 16U, 0x12U); // Slot 16
      EnsureMagicAcquired(memory);
    } else if (item == "dins_fire") {
      memory.Write8(kSaveContext + 0x008CU + 5U, 0x05U); // Slot 5
      EnsureMagicAcquired(memory);
    } else if (item == "farores_wind") {
      memory.Write8(kSaveContext + 0x008CU + 11U, 0x0DU); // Slot 11
      EnsureMagicAcquired(memory);
    } else if (item == "nayrus_love") {
      memory.Write8(kSaveContext + 0x008CU + 17U, 0x13U); // Slot 17
      EnsureMagicAcquired(memory);
    } else if (item == "bombs") {
      memory.Write8(kSaveContext + 0x008CU + 2U, 0x02U); // Slot 2 = Bomb
      memory.Write8(kSaveContext + 0x00A6U + 2U, 40U);   // Bomb ammo
      SetUpgradeBits(memory, 3U, 3U, 3U);                // Bomb bag 40
    } else if (item == "bombchus") {
      memory.Write8(kSaveContext + 0x008CU + 8U, 0x09U); // Slot 8 = Bombchu
      memory.Write8(kSaveContext + 0x00A6U + 8U, 50U);   // Bombchu ammo
    } else if (item == "slingshot") {
      memory.Write8(kSaveContext + 0x008CU + 6U, 0x06U); // Slot 6 = Slingshot
      memory.Write8(kSaveContext + 0x00A6U + 4U, 50U);   // Seeds ammo
      SetUpgradeBits(memory, 14U, 3U, 3U);               // Bullet bag 50
    } else if (item == "boomerang") {
      memory.Write8(kSaveContext + 0x008CU + 12U, 0x0EU); // Slot 12
    } else if (item == "hookshot") {
      memory.Write8(kSaveContext + 0x008CU + 9U, 0x0AU); // Slot 9 = Hookshot
    } else if (item == "longshot") {
      memory.Write8(kSaveContext + 0x008CU + 9U, 0x0BU); // Slot 9 = Longshot
    } else if (item == "hammer") {
      memory.Write8(kSaveContext + 0x008CU + 15U, 0x11U); // Slot 15
    } else if (item == "lens") {
      memory.Write8(kSaveContext + 0x008CU + 13U, 0x0FU); // Slot 13
      EnsureMagicAcquired(memory);
    } else if (item == "ocarina_time") {
      memory.Write8(kSaveContext + 0x008CU + 7U, 0x08U); // Slot 7 = Ocarina of Time
    } else if (item == "magic_beans") {
      memory.Write8(kSaveContext + 0x008CU + 14U, 0x10U); // Slot 14 = Beans
      memory.Write8(kSaveContext + 0x00A6U + 9U, 10U);    // 10 beans
    } else if (item == "bottle_empty") {
      PlaceInFirstAvailableBottleSlot(memory, 0x14U);
    } else if (item == "bottle_fairy") {
      PlaceInFirstAvailableBottleSlot(memory, 0x18U);
    } else if (item == "bottle_red") {
      PlaceInFirstAvailableBottleSlot(memory, 0x15U);
    } else if (item == "bottle_blue") {
      PlaceInFirstAvailableBottleSlot(memory, 0x17U);
    } else if (item == "bottle_blue_fire") {
      PlaceInFirstAvailableBottleSlot(memory, 0x1CU);
    } else if (item == "master_sword") {
      SetEquipmentBit(memory, 1U); // bit 1 = Master Sword
    } else if (item == "biggoron_sword") {
      SetEquipmentBit(memory, 2U); // bit 2 = Biggoron Sword
      memory.Write8(kSaveContext + 0x0052U, 1U); // sword flag
    } else if (item == "mirror_shield") {
      SetEquipmentBit(memory, 6U); // bit 6 = Mirror Shield
    } else if (item == "hylian_shield") {
      SetEquipmentBit(memory, 5U); // bit 5 = Hylian Shield
    } else if (item == "goron_tunic") {
      SetEquipmentBit(memory, 9U); // bit 9 = Goron Tunic
    } else if (item == "zora_tunic") {
      SetEquipmentBit(memory, 10U); // bit 10 = Zora Tunic
    } else if (item == "iron_boots") {
      SetEquipmentBit(memory, 13U); // bit 13 = Iron Boots
    } else if (item == "hover_boots") {
      SetEquipmentBit(memory, 14U); // bit 14 = Hover Boots
    } else if (item == "golden_gauntlets") {
      SetUpgradeBits(memory, 6U, 3U, 3U); // strength = 3
    } else if (item == "golden_scale") {
      SetUpgradeBits(memory, 9U, 3U, 2U); // scale = 2
    }
  }

  void PlaceInFirstAvailableBottleSlot(NativeA32Memory& memory, uint8_t bottleItem) {
    for (uint32_t slot = 18U; slot <= 21U; ++slot) {
      uint8_t current = 0xFFU;
      memory.Read8(kSaveContext + 0x008CU + slot, &current);
      if (current == 0xFFU) {
        memory.Write8(kSaveContext + 0x008CU + slot, bottleItem);
        return;
      }
    }
    // If all full, overwrite slot 18
    memory.Write8(kSaveContext + 0x008CU + 18U, bottleItem);
  }

  void SetEquipmentBit(NativeA32Memory& memory, uint32_t bit) {
    uint16_t eq = 0U;
    memory.Read16(kSaveContext + 0x00B6U, &eq);
    eq |= static_cast<uint16_t>(1U << bit);
    memory.Write16(kSaveContext + 0x00B6U, eq);
  }

  void SetUpgradeBits(NativeA32Memory& memory, uint32_t shift, uint32_t maskBits, uint32_t value) {
    uint32_t upgrades = 0U;
    memory.Read32(kSaveContext + 0x00B8U, &upgrades);
    const uint32_t mask = ((1U << maskBits) - 1U) << shift;
    upgrades = (upgrades & ~mask) | ((value & ((1U << maskBits) - 1U)) << shift);
    memory.Write32(kSaveContext + 0x00B8U, upgrades);
  }

  void EnsureMagicAcquired(NativeA32Memory& memory) {
    memory.Write8(kSaveContext + 0x004EU, 1U); // magic acquired
    uint8_t level = 0U;
    memory.Read8(kSaveContext + 0x0046U, &level);
    if (level == 0U) {
      memory.Write8(kSaveContext + 0x0046U, 1U);
      memory.Write8(kSaveContext + 0x0047U, 0x30U);
    }
  }

  bool mInfiniteHealth = false;
  bool mInfiniteMagic = false;
  bool mInfiniteRupees = false;
  bool mInfiniteAmmo = false;
  bool mConfigLoaded = false;
  uint32_t mPollFrames = 0;
  std::filesystem::file_time_type mLastConfigWriteTime{};
};

} // namespace Oot3dNativeGame
