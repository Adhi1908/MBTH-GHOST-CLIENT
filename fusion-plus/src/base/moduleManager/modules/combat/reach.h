#pragma once

#include <chrono>
#include <string>

#include "util/math/geometry.h"
#include "moduleManager/moduleBase.h"

class Reach : public ModuleBase
{
public:
	void Update() override;

	void RenderOverlay() override {};
	void RenderHud() override {};
	void RenderMenu() override;

	std::string GetName() override { return m_name; }
	std::string GetCategory() override { return m_category; }
	std::string GetDescription() override { return "Extends melee attack range past vanilla 3.0 blocks."; }
	int GetKey() override { return settings::Reach_Key; }

	bool IsEnabled() override { return settings::Reach_Enabled; }
	void SetEnabled(bool enabled) override { settings::Reach_Enabled = enabled; }
	void Toggle() override { settings::Reach_Enabled = !settings::Reach_Enabled; }

private:
	std::string m_name = "Reach";
	std::string m_category = "Combat";

	// Click pacing — initialised on first attack so the user-configured CPS is honoured.
	std::chrono::steady_clock::time_point m_lastAttack{};
	int m_nextDelayMs = 0;
};
