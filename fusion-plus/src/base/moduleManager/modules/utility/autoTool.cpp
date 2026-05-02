#include "autoTool.h"

#include <Windows.h>
#include <cmath>
#include <chrono>
#include <vector>

#include "sdk/sdk.h"
#include "menu/menu.h"
#include "moduleManager/commonData.h"
#include "util/minecraft/minecraft.h"
#include "util/logger.h"

// =============================================================================
// AutoTool
//   Re-implementation of Raven b++'s "AutoTool" module for Fusion+ / JNI SDK.
//   Switches the player's hotbar slot to the most efficient tool for the block
//   currently being mined, then optionally switches back when LMB is released.
//
//   This file is fully additive - no other source file is modified except for
//   the module list registration in moduleManager.cpp and the settings entries
//   in settings.h. All Minecraft access goes through SDK methods that are
//   already used elsewhere in the project.
// =============================================================================

// ----- Static helpers -------------------------------------------------------

AutoTool::ToolKind AutoTool::GetToolKind(int id)
{
	// 1.8.9 numeric item IDs (also valid in 1.7.10).
	switch (id)
	{
	// Pickaxes
	case 270: case 274: case 257: case 278: case 285: return ToolKind::PICKAXE;
	// Axes
	case 271: case 275: case 258: case 279: case 286: return ToolKind::AXE;
	// Shovels
	case 269: case 273: case 256: case 277: case 284: return ToolKind::SHOVEL;
	// Shears
	case 359: return ToolKind::SHEARS;
	// Swords (used for cobwebs / leaves quickly)
	case 268: case 272: case 267: case 276: case 283: return ToolKind::SWORD;
	}
	return ToolKind::NONE;
}

int AutoTool::GetToolSpeedRank(int id)
{
	// Material tier ranking. Higher = faster mining.
	// Wood=1, Stone=2, Iron=3, Diamond=4, Gold=5 (gold is fastest in vanilla).
	switch (id)
	{
	// Wooden
	case 270: case 271: case 269: case 268: return 1;
	// Stone
	case 274: case 275: case 273: case 272: return 2;
	// Iron
	case 257: case 258: case 256: case 267: return 3;
	// Diamond
	case 278: case 279: case 277: case 276: return 4;
	// Gold (very fast, low durability)
	case 285: case 286: case 284: case 283: return 5;
	// Shears - rank above wood for things they're meant for
	case 359: return 3;
	}
	return 0;
}

AutoTool::ToolKind AutoTool::GetPreferredTool(int blockId)
{
	// Vanilla 1.8/1.7 numeric block IDs.
	switch (blockId)
	{
	// --- Pickaxe blocks ---------------------------------------------------
	// (Stone, ores, metal/quartz/glass/rails/anvils/etc.)
	case 1:   case 4:   case 14:  case 15:  case 16:  case 21:  case 22:
	case 24:  case 27:  case 28:  case 33:  case 41:  case 42:  case 43:
	case 44:  case 45:  case 48:  case 49:  case 52:  case 56:  case 57:
	case 61:  case 66:  case 67:  case 70:  case 73:  case 74:  case 79:
	case 80:  case 82:  case 87:  case 88:  case 89:  case 93:  case 94:
	case 97:  case 98:  case 101: case 108: case 109: case 112: case 113:
	case 114: case 116: case 118: case 121: case 122: case 123: case 124:
	case 128: case 129: case 130: case 133: case 137: case 138: case 139:
	case 145: case 152: case 153: case 154: case 155: case 156: case 157:
	case 158: case 159: case 167: case 168: case 169: case 170: case 173:
	case 174: case 179: case 180: case 181: case 182:
		return ToolKind::PICKAXE;

	// --- Axe blocks (logs, planks, wooden things) -------------------------
	// (5 planks, 17 oak log, 25 note block, 46 tnt, 47 bookshelf, 53 oak stairs,
	//  54 chest, 58 crafting table, 84 jukebox, 85 fence, 86 pumpkin, 91 jack-o,
	//  96 trapdoor, 99/100 mushroom blocks, 103 melon, 107 fence gate,
	//  134-136 spruce/birch/jungle stairs, 146 trapped chest, 162 acacia log,
	//  163-164 acacia/dark-oak stairs, 183-192 various fences/gates)
	case 5:   case 17:  case 25:  case 46:  case 47:  case 53:  case 54:
	case 58:  case 84:  case 85:  case 86:  case 91:  case 96:  case 99:
	case 100: case 103: case 107: case 134: case 135: case 136: case 146:
	case 162: case 163: case 164: case 183: case 184: case 185: case 186:
	case 187: case 188: case 189: case 190: case 191: case 192:
		return ToolKind::AXE;

	// --- Shovel blocks ----------------------------------------------------
	case 2:   case 3:   case 12:  case 13:  case 78:  case 81:  case 110:
	case 111:
		return ToolKind::SHOVEL;

	// --- Shears blocks (leaves, wool, cobwebs, vines) ---------------------
	case 18:  case 161: case 35:  case 30:  case 106:
		return ToolKind::SHEARS;

	// --- Sword blocks (cobwebs are fastest with sword too) ----------------
	// (Cobweb 30 also handled above for shears; either works.)
	}
	return ToolKind::NONE;
}

// ----- Block-id detection ---------------------------------------------------

bool AutoTool::GetTargetBlockId(int& outBlockId)
{
	// Get the MovingObjectPosition the player is looking at.
	CMovingObjectPosition mop = SDK::minecraft->GetMouseOver();
	if (mop.GetInstance() == nullptr) return false;
	if (!mop.IsTypeOfBlock()) return false;

	// CMovingObjectPosition::GetBlockPosition() returns the precise hitVec
	// (a Vec3) - we floor each component to the integer block coordinate.
	CVec3 hit = mop.GetBlockPosition();
	if (hit.GetInstance() == nullptr) return false;

	Vector3 v = hit.GetNativeVector3();

	// To get the actual block (not the air the ray hit just before), step a
	// tiny amount in the direction of the player's look so we land inside the
	// block instead of on its boundary. This is a safe heuristic - if it fails
	// we still try the floor() of the raw hit vec.
	float yaw   = CommonData::playerYaw;
	float pitch = CommonData::playerPitch;
	float yawRad   = yaw   * 0.0174532925f;
	float pitchRad = pitch * 0.0174532925f;
	float dx = -std::sin(yawRad) * std::cos(pitchRad);
	float dy = -std::sin(pitchRad);
	float dz =  std::cos(yawRad) * std::cos(pitchRad);

	auto tryAt = [&](double px, double py, double pz) -> bool
	{
		int bx = static_cast<int>(std::floor(px));
		int by = static_cast<int>(std::floor(py));
		int bz = static_cast<int>(std::floor(pz));
		CBlock block = SDK::minecraft->theWorld->GetBlock(bx, by, bz);
		if (block.GetInstance() == nullptr) return false;
		int id = block.GetBlockId();
		if (id <= 0) return false;
		outBlockId = id;
		return true;
	};

	// First try a small step inside the block face.
	if (tryAt(v.x + dx * 0.05, v.y + dy * 0.05, v.z + dz * 0.05))
		return true;

	// Fallback: the raw hit position floored.
	return tryAt(v.x, v.y, v.z);
}

// ----- Slot search ----------------------------------------------------------

int AutoTool::FindBestSlotForBlock(int blockId)
{
	ToolKind preferred = GetPreferredTool(blockId);

	CInventoryPlayer inv = SDK::minecraft->thePlayer->GetInventory();
	std::vector<CItemStack> hotbar = inv.GetMainInventory();

	int bestSlot = -1;
	int bestRank = -1;
	int bestPrefBonus = -1;

	for (int slot = 0; slot < 9 && slot < (int)hotbar.size(); ++slot)
	{
		CItemStack stack = hotbar[slot];
		if (stack.GetInstance() == nullptr) continue;
		if (stack.GetItem().GetInstance() == nullptr) continue;

		int id = stack.GetItem().GetID();
		ToolKind kind = GetToolKind(id);
		if (kind == ToolKind::NONE) continue;

		int rank = GetToolSpeedRank(id);
		// Big bonus if it matches the preferred tool category for this block.
		int prefBonus = (preferred != ToolKind::NONE && kind == preferred) ? 100 : 0;

		// If we have no preference (unknown block), accept any tool but prefer
		// pickaxe slightly (most universal).
		if (preferred == ToolKind::NONE && kind == ToolKind::PICKAXE)
			prefBonus = 10;

		int score = rank + prefBonus;
		int bestScore = bestRank + bestPrefBonus;

		if (bestSlot == -1 || score > bestScore)
		{
			bestSlot = slot;
			bestRank = rank;
			bestPrefBonus = prefBonus;
		}
	}

	return bestSlot;
}

void AutoTool::HotkeyToFastest(int blockId)
{
	int slot = FindBestSlotForBlock(blockId);
	if (slot < 0) return;
	if (slot == SDK::minecraft->thePlayer->GetInventory().GetCurrentItemIndex()) return;

	SDK::minecraft->thePlayer->GetInventory().SetCurrentItemIndex(slot);
}

void AutoTool::FinishMining()
{
	if (settings::AT_HotkeyBack && m_previousSlot >= 0 && m_previousSlot < 9)
	{
		int currentIdx = SDK::minecraft->thePlayer->GetInventory().GetCurrentItemIndex();
		if (currentIdx != m_previousSlot)
			SDK::minecraft->thePlayer->GetInventory().SetCurrentItemIndex(m_previousSlot);
	}
	m_mining = false;
	m_isWaiting = false;
	m_previousBlockId = -1;
	m_previousSlot = -1;
}

// ----- Module main loop -----------------------------------------------------

void AutoTool::Update()
{
	if (!settings::AT_Enabled)
	{
		if (m_mining) FinishMining();
		return;
	}
	if (!CommonData::SanityCheck()) { if (m_mining) FinishMining(); return; }
	if (Menu::open) return;
	if (SDK::minecraft->IsInGuiState()) { if (m_mining) FinishMining(); return; }

	// Are we trying to break a block?
	bool lmbHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (!lmbHeld)
	{
		if (m_mining) FinishMining();
		if (m_isWaiting) m_isWaiting = false;
		return;
	}

	// Gate: only act while looking at a real block (not entity / air).
	int blockId = -1;
	if (!GetTargetBlockId(blockId))
	{
		// Don't reset previousSlot - the user might still be in a swing arc.
		return;
	}

	// Optional gate: only act if currently holding a tool/sword.
	if (settings::AT_OnlyWhenHoldingTool)
	{
		CItemStack held = SDK::minecraft->thePlayer->GetInventory().GetCurrentItem();
		if (held.GetInstance() == nullptr ||
			held.GetItem().GetInstance() == nullptr ||
			GetToolKind(held.GetItem().GetID()) == ToolKind::NONE)
		{
			return;
		}
	}

	// Apply the configured switch delay.
	if (settings::AT_MaxDelay > 0)
	{
		if (m_previousBlockId != blockId)
		{
			m_previousBlockId = blockId;
			m_isWaiting = true;
			m_delayStart = std::chrono::steady_clock::now();
			return;
		}

		if (m_isWaiting)
		{
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - m_delayStart).count();
			if (elapsed < settings::AT_MaxDelay) return;

			m_isWaiting = false;
			if (!m_mining)
			{
				m_previousSlot = SDK::minecraft->thePlayer->GetInventory().GetCurrentItemIndex();
				m_mining = true;
			}
			HotkeyToFastest(blockId);
		}
		return;
	}

	// No delay: switch immediately.
	if (!m_mining)
	{
		m_previousSlot = SDK::minecraft->thePlayer->GetInventory().GetCurrentItemIndex();
		m_mining = true;
	}
	m_previousBlockId = blockId;
	HotkeyToFastest(blockId);
}

// ----- ImGui menu -----------------------------------------------------------

void AutoTool::RenderMenu()
{
	Menu::ToggleWithKeybind(&settings::AT_Enabled, settings::AT_Key);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("AT_Sep1");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Checkbox("Hotkey Back",          &settings::AT_HotkeyBack);
	Menu::Checkbox("Only While Holding Tool", &settings::AT_OnlyWhenHoldingTool);
	Menu::Slider  ("Switch Delay",         &settings::AT_MaxDelay, 0, 2000, ImVec2(0, 0), "%d ms");
}
