#pragma once

// =============================================================================
// triggerBot.h
//   Detects when the user is *already* aiming at a valid enemy entity
//   (vanilla Minecraft `GetMouseOver()` says so) and emits synthetic LMB
//   clicks at the user's configured CPS while LMB is held.
//
//   Why this exists separately from KillAura:
//     KillAura *publishes* rotations the user didn't perform — that's the
//     #1 thing Hypixel Watchdog flags. TriggerBot does NOT touch rotation;
//     it only fires a click when the player is naturally pointing at a
//     hitbox. Server-side this is indistinguishable from a fast clicker
//     that happens to land hits, which is what AimAssist + good clicks
//     would produce naturally. It pairs perfectly with AimAssist.
//
//   Source patterns reused:
//     - reach.cpp's RandomClickDelayMs CPS pacing
//     - leftAutoClicker.cpp's PostMessage click emission
//     - KILLAURA_DESIGN.md §4 option D and §7 recommendation
// =============================================================================

#include <chrono>
#include <string>

#include "moduleManager/moduleBase.h"

class TriggerBot : public ModuleBase
{
public:
	void Update() override;

	void RenderOverlay() override {};
	void RenderHud() override {};
	void RenderMenu() override;

	std::string GetName()        override { return m_name; }
	std::string GetCategory()    override { return m_category; }
	std::string GetDescription() override
	{
		return "Auto-attacks the entity your crosshair is on while LMB is held. "
		       "No rotation manipulation - safe to pair with AimAssist for legit-style PvP.";
	}
	int  GetKey()                 override;

	bool IsEnabled()              override;
	void SetEnabled(bool enabled) override;
	void Toggle()                 override;

private:
	std::string m_name     = "TriggerBot";
	std::string m_category = "Combat";

	std::chrono::steady_clock::time_point m_lastAttack = std::chrono::steady_clock::now();
	int                                   m_nextDelayMs = 0;

	// Time the entity has been continuously in the crosshair (ms).
	// Used by KA_HoldDelay so a target that just walked across our reticle
	// doesn't fire instantly — humans need a few hundred ms reaction time.
	std::chrono::steady_clock::time_point m_targetFirstSeen = std::chrono::steady_clock::time_point{};
	void*                                 m_lastTargetInst  = nullptr;
};
