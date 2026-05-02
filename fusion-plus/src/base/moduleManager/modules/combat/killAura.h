#pragma once

// =============================================================================
// killAura.h
//   LiquidBounce-equivalent KillAura module for Fusion+ (v0.1).
//
//   Status: v0.1 publishes the humanised rotation as the *visible* camera
//   rotation (same as Raven b++'s blatant aura). This is the "loud" variant
//   that visibly turns your character to face the target.
//
//   v0.2 PLANNED: a JVMTI bytecode hook on NetworkManager.sendPacket will
//   rewrite the C03PacketPlayer$PositionLook fields with the silent rotation
//   while restoring the visible camera, matching LiquidBounce's silent
//   variant. See KILLAURA_DESIGN.md §6.3.
//
//   Pipeline per tick:
//     1. Gate checks (enabled, in-game, not in GUI, not flying, etc.)
//     2. Pick a target from CommonData::nativePlayerList using FOV + range
//        + raycast LOS, sorted by configured priority.
//     3. Compute desired rotation to target eye position.
//     4. Run RotationUtils::Smoother::Apply() — speed cap + jitter + GCD fix.
//     5. SetAngles() (v0.1) or queue for sendPacket hook (v0.2).
//     6. If raycast confirms hit + cooldown ready + click pacing ready,
//        SDK::minecraft->ClickMouse() (or send C02PacketUseEntity).
// =============================================================================

#include <chrono>
#include <string>

#include "moduleManager/moduleBase.h"
#include "moduleManager/commonData.h"
#include "util/math/geometry.h"
#include "util/math/rotationUtils.h"

class KillAura : public ModuleBase
{
public:
	void Update() override;

	void RenderOverlay() override;
	void RenderHud() override {};
	void RenderMenu() override;

	std::string GetName()        override { return m_name; }
	std::string GetCategory()    override { return m_category; }
	std::string GetDescription() override
	{
		return "Auto-targets the closest enemy and attacks them with humanised rotations and click pacing. "
		       "WARNING: Bannable on most servers. v0.1 visibly rotates the camera; silent mode planned for v0.2.";
	}
	int  GetKey()                override;

	bool IsEnabled()             override;
	void SetEnabled(bool enabled) override;
	void Toggle()                override;

private:
	std::string m_name     = "KillAura";
	std::string m_category = "Combat";

	RotationUtils::Smoother m_smoother;

	// Currently locked target (raw pointer comparison via GetInstance).
	void*                                 m_currentTargetInstance = nullptr;
	std::string                           m_currentTargetName;
	std::chrono::steady_clock::time_point m_lastAttack            = std::chrono::steady_clock::now();
	int                                   m_nextDelayMs           = 0;

	// Picks the best target satisfying gates. Returns whether one was found
	// and writes it to outTarget.
	bool PickTarget(CommonData::PlayerData& outTarget);
};
