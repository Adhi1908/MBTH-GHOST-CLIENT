#pragma once

#include <chrono>
#include <string>

#include "moduleManager/moduleBase.h"

class AutoTool : public ModuleBase
{
public:
	void Update() override;

	void RenderOverlay() override {};
	void RenderHud() override {};
	void RenderMenu() override;

	std::string GetName() override { return m_name; }
	std::string GetCategory() override { return m_category; }
	std::string GetDescription() override { return "Auto-switches to the best tool for the block you're mining."; }
	int GetKey() override { return settings::AT_Key; }

	bool IsEnabled() override { return settings::AT_Enabled; }
	void SetEnabled(bool enabled) override { settings::AT_Enabled = enabled; }
	void Toggle() override { settings::AT_Enabled = !settings::AT_Enabled; }

private:
	std::string m_name = "Auto Tool";
	std::string m_category = "Utility";

	enum class ToolKind { NONE, PICKAXE, AXE, SHOVEL, SHEARS, SWORD };

	// State
	bool m_mining = false;
	bool m_isWaiting = false;
	int  m_previousSlot = -1;
	int  m_previousBlockId = -1;
	std::chrono::time_point<std::chrono::steady_clock> m_delayStart;

	// Helpers
	void  FinishMining();
	void  HotkeyToFastest(int blockId);
	int   FindBestSlotForBlock(int blockId);
	bool  GetTargetBlockId(int& outBlockId);

	static ToolKind GetToolKind(int itemId);
	static int      GetToolSpeedRank(int itemId); // higher = faster
	static ToolKind GetPreferredTool(int blockId);
};
