#include "killAura.h"

// Same Windows.h-min/max workaround as reach.cpp.
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cmath>
#include <random>

#include "configManager/configManager.h"
#include "java/java.h"
#include "menu/menu.h"
#include "moduleManager/commonData.h"
#include "sdk/sdk.h"
#include "util/keys.h"
#include "util/logger.h"
#include "util/math/camera.h"
#include "util/math/math.h"
#include "util/math/rotationUtils.h"
#include "util/minecraft/minecraft.h"

// =============================================================================
// Local helpers
// =============================================================================
namespace
{
	constexpr float ENTITY_BORDER_SIZE = 0.1f;
	constexpr float DEG2RAD            = 0.0174532925f;

	static Vector3 LookDirection(float yawDeg, float pitchDeg)
	{
		float yaw = yawDeg   * DEG2RAD;
		float pit = pitchDeg * DEG2RAD;
		float cp  = std::cos(pit);
		return Vector3(-std::sin(yaw) * cp, -std::sin(pit), std::cos(yaw) * cp);
	}

	static bool RayHitsAABB(const Vector3& origin, const Vector3& dir,
		const Vector3& aabbMin, const Vector3& aabbMax,
		float& outT)
	{
		float tmin = 0.0f, tmax = FLT_MAX;
		auto axis = [&](float o, float d, float lo, float hi) -> bool
		{
			if (std::fabs(d) < 1e-8f) return o >= lo && o <= hi;
			float invD = 1.0f / d;
			float t1 = (lo - o) * invD;
			float t2 = (hi - o) * invD;
			if (t1 > t2) std::swap(t1, t2);
			tmin = (tmin > t1) ? tmin : t1;
			tmax = (tmax < t2) ? tmax : t2;
			return tmax >= tmin;
		};
		if (!axis(origin.x, dir.x, aabbMin.x, aabbMax.x)) return false;
		if (!axis(origin.y, dir.y, aabbMin.y, aabbMax.y)) return false;
		if (!axis(origin.z, dir.z, aabbMin.z, aabbMax.z)) return false;
		outT = tmin;
		return tmin >= 0.0f;
	}

	static int RandomClickDelayMs(int minCps, int maxCps)
	{
		if (minCps <= 0) minCps = 1;
		if (maxCps < minCps) maxCps = minCps;
		int hi = (int)std::round(1000.0 / ((minCps < 1) ? 1 : minCps));
		int lo = (int)std::round(1000.0 / ((maxCps < 1) ? 1 : maxCps));
		if (lo > hi) std::swap(lo, hi);
		static std::mt19937 rng{ std::random_device{}() };
		std::uniform_int_distribution<int> d(lo, hi);
		return d(rng);
	}
}

// =============================================================================
// ModuleBase plumbing — defined in cpp so we can refer to settings without
// pulling configManager headers into the public header.
// =============================================================================
int  KillAura::GetKey()              { return settings::KA_Key; }
bool KillAura::IsEnabled()           { return settings::KA_Enabled; }
void KillAura::SetEnabled(bool e)    { settings::KA_Enabled = e; }
void KillAura::Toggle()              { settings::KA_Enabled = !settings::KA_Enabled; }

// =============================================================================
// Target picker
//   Filters CommonData::nativePlayerList by FOV + range + raycast LOS, then
//   sorts by Reach_TargetPriority (Distance / Health / Crosshair).
// =============================================================================
bool KillAura::PickTarget(CommonData::PlayerData& outTarget)
{
	auto& list = CommonData::nativePlayerList;  // non-const: we call non-const SDK methods
	if (list.empty()) return false;

	const Vector3 eye   = CommonData::playerEyePos;
	const Vector2 angles(CommonData::playerYaw, CommonData::playerPitch);
	const std::string ourName = SDK::minecraft->thePlayer->GetName();
	const float maxRange = settings::KA_Range;

	float bestScore = FLT_MAX;
	bool  haveBest  = false;
	CommonData::PlayerData best{};

	for (auto& p : list)

	{
		if (p.name == ourName)             continue;
		if (p.health <= 0.0f)              continue;

		if (settings::KA_IgnoreFriends && configmanager::IsFriend(p.name))    continue;
		if (settings::KA_IgnoreInvisible && p.obj.IsInvisibleToPlayer(SDK::minecraft->thePlayer->GetInstance())) continue;

		// Distance check (cheap).
		Vector3 diff = p.pos - CommonData::playerPos;
		float dist = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
		if (dist > maxRange) continue;

		// FOV check — angular distance from current crosshair to entity head.
		Vector3 headPos = p.pos + Vector3(0, p.height - 0.1f, 0);
		Vector2 anglesToHead = Math::GetAngles(eye, headPos);
		Vector2 fovDiff = Math::WrapAngleTo180(angles.Invert() - anglesToHead.Invert());
		float yawDiffAbs = std::fabs(fovDiff.x);
		if (yawDiffAbs > settings::KA_Fov) continue;

		// Visibility check (raycast). Only used as a SOFT gate — we test against
		// the entity's centre line, not its hitbox slab.
		if (settings::KA_RaycastCheck)
		{
			if (!SDK::minecraft->thePlayer->CanEntityBeSeen(p.obj.GetInstance()))
				continue;
		}

		// Score by user-configured priority.
		float score;
		switch (settings::KA_TargetPriority)
		{
		case 1: // Health
			score = p.health;
			break;
		case 2: // Closest to crosshair
			score = yawDiffAbs;
			break;
		default: // Distance
			score = dist;
		}

		if (score < bestScore)
		{
			bestScore = score;
			best      = p;
			haveBest  = true;
		}
	}

	if (haveBest) outTarget = best;
	return haveBest;
}

// =============================================================================
// Update — main per-tick entry point
// =============================================================================
void KillAura::Update()
{
	if (!settings::KA_Enabled)            return;
	if (!CommonData::SanityCheck())        return;
	if (Menu::open)                        return;
	if (SDK::minecraft->IsInGuiState())    return;

	// Gate: only-on-LMB
	if (settings::KA_OnlyOnLMB)
	{
		if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
		{
			m_currentTargetInstance = nullptr;
			return;
		}
	}

	// Gate: weapon-only
	if (settings::KA_WeaponOnly &&
	    !MinecraftUtils::IsWeapon(SDK::minecraft->thePlayer->GetInventory().GetCurrentItem()))
	{
		m_currentTargetInstance = nullptr;
		return;
	}

	// Gate: disable-when-flying — Fusion+'s SDK doesn't expose `isFlying` on
	// EntityPlayerSP yet, so we currently *only* honour this setting if the
	// caller knows we're not. The setting is preserved in the UI so once a
	// future SDK addition exposes it (Lunar 1.7.10/1.8.9 path:
	// EntityPlayer.capabilities.isFlying), wiring it up is one line. For now
	// it's a no-op gate.


	// Pick target.
	CommonData::PlayerData target{};
	if (!PickTarget(target))
	{
		m_currentTargetInstance = nullptr;
		return;
	}

	// Reset smoother if this is a new target.
	if (target.obj.GetInstance() != m_currentTargetInstance)
	{
		Vector2 currentRot(CommonData::playerYaw, CommonData::playerPitch);
		m_smoother.Reset(currentRot);
		m_currentTargetInstance = target.obj.GetInstance();
		m_currentTargetName     = target.name;
	}

	// Compute target rotation (aim at body slightly below head — matches LB).
	Vector3 targetEye = target.pos + Vector3(0, (target.height - 0.1f) * 0.85f, 0);
	Vector2 desiredRot = Math::GetAngles(CommonData::playerEyePos, targetEye);

	// Build smoother settings from user config.
	RotationUtils::Settings rs;
	rs.yawCap            = settings::KA_HSpeed;
	rs.pitchCap          = settings::KA_VSpeed;
	rs.legitimize        = settings::KA_Legitimize;
	rs.minRotationDiff   = settings::KA_MinRotationDiff;
	rs.simulateShortStop = settings::KA_SimulateShortStop;
	rs.gcdAngleDelta     = 0.15f;

	Vector2 currentRot(CommonData::playerYaw, CommonData::playerPitch);
	Vector2 newRot;
	bool publish = m_smoother.Apply(currentRot, desiredRot, rs, newRot);
	if (!publish) return;

	// v0.1: publish the rotation visibly. v0.2 will instead intercept the
	// outgoing C03 packet via JVMTI and rewrite its yaw/pitch fields.
	SDK::minecraft->thePlayer->SetAngles(newRot);

	// === Click pacing + raycast confirmation =================================
	auto now = std::chrono::steady_clock::now();
	if (m_nextDelayMs == 0)
	{
		m_nextDelayMs = RandomClickDelayMs(settings::KA_MinCps, settings::KA_MaxCps);
	}
	auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAttack).count();
	if (sinceLast < m_nextDelayMs) return;

	// Confirm we're actually aiming at the entity hitbox before swinging.
	// Reach.cpp pattern: ray from eye + look direction vs entity AABB.
	Vector3 origin = CommonData::playerEyePos;
	Vector3 dir    = LookDirection(newRot.x, newRot.y);
	if (std::isnan(origin.x) || std::isnan(dir.x)) return;

	Vector3 aabbMin(
		(float)target.boundingBox.minX - ENTITY_BORDER_SIZE,
		(float)target.boundingBox.minY - ENTITY_BORDER_SIZE,
		(float)target.boundingBox.minZ - ENTITY_BORDER_SIZE);
	Vector3 aabbMax(
		(float)target.boundingBox.maxX + ENTITY_BORDER_SIZE,
		(float)target.boundingBox.maxY + ENTITY_BORDER_SIZE,
		(float)target.boundingBox.maxZ + ENTITY_BORDER_SIZE);

	float t;
	if (!RayHitsAABB(origin, dir, aabbMin, aabbMax, t)) return;
	if (t > settings::KA_Range) return;

	// All gates satisfied — swing.
	try
	{
		SDK::minecraft->ClickMouse();
	}
	catch (...)
	{
		// Silently ignore — JNI exceptions on world swap. Next tick retries.
		return;
	}

	m_lastAttack  = now;
	m_nextDelayMs = RandomClickDelayMs(settings::KA_MinCps, settings::KA_MaxCps);
}

// =============================================================================
// RenderOverlay — optional FOV circle (mirrors AimAssist's behaviour)
// =============================================================================
void KillAura::RenderOverlay()
{
	if (!settings::KA_Enabled || !CommonData::dataUpdated) return;
	if (!settings::KA_FovCircle) return;

	ImVec2 screenSize = ImGui::GetWindowSize();
	float radAimbotFov = (float)(settings::KA_Fov * PI / 180);
	float radViewFov   = (float)(CommonData::fov  * PI / 180);
	float circleRadius = std::tan(radAimbotFov / 2.f) / std::tan(radViewFov / 2.f) * screenSize.x / 1.7325f;

	ImGui::GetWindowDrawList()->AddCircle(
		ImVec2(screenSize.x / 2.f, screenSize.y / 2.f),
		circleRadius,
		ImColor(settings::KA_FovCircleColor[0],
		        settings::KA_FovCircleColor[1],
		        settings::KA_FovCircleColor[2],
		        settings::KA_FovCircleColor[3]),
		(int)(circleRadius / 3.f), 1.f);
}

// =============================================================================
// RenderMenu — settings panel
// =============================================================================
void KillAura::RenderMenu()
{
	Menu::ToggleWithKeybind(&settings::KA_Enabled, settings::KA_Key);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("KA_SepWarn");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);

	// === BIG RED WARNING — KillAura v0.1 is detectable, has banned users. ===
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
	Menu::BoldText("WARNING: KillAura v0.1 is BANNABLE.", FontSize::SIZE_16);
	ImGui::PopStyleColor();
	Menu::Text("v0.1 publishes visible rotations. Hypixel WILL flag this.", FontSize::SIZE_14);
	Menu::Text("For safer PvP use TriggerBot + AimAssist instead.",         FontSize::SIZE_14);
	Menu::Text("If you must use KillAura, click 'Apply Legit Preset' below.", FontSize::SIZE_14);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);

	// One-click safer envelope. Mirrors Hypixel-Legit.fusion's KillAura tuning
	// PLUS forces LMB-only and tighter speed caps.
	if (ImGui::Button("Apply Legit Preset", ImVec2(220.f, 28.f)))
	{
		settings::KA_Range             = 3.3f;   // barely past vanilla
		settings::KA_Fov               = 70.0f;  // narrow — looks like aim
		settings::KA_HSpeed            = 22.0f;  // slow per-tick yaw cap
		settings::KA_VSpeed            = 14.0f;  // slow per-tick pitch cap
		settings::KA_MinRotationDiff   = 3.5f;   // refuse tiny corrections
		settings::KA_Legitimize        = true;   // ALL humanisation on
		settings::KA_SimulateShortStop = true;
		settings::KA_RaycastCheck      = true;
		settings::KA_OnlyOnLMB         = true;   // only fire while attacking
		settings::KA_WeaponOnly        = true;
		settings::KA_DisableWhenFlying = true;
		settings::KA_IgnoreFriends     = true;
		settings::KA_IgnoreInvisible   = true;
		settings::KA_MinCps            = 5;
		settings::KA_MaxCps            = 9;      // human-typical
		settings::KA_TargetPriority    = 2;      // closest to crosshair
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(tunes for safer use; still detectable)");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("KA_Sep1");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Slider("Range",        &settings::KA_Range, 3.0f, 6.0f, ImVec2(0,0), "%.2f blocks");

	Menu::Slider("FOV",          &settings::KA_Fov,   30.0f, 360.0f);
	Menu::Slider("Yaw Speed",    &settings::KA_HSpeed, 1.0f, 180.0f, ImVec2(0,0), "%.1f deg/tick");
	Menu::Slider("Pitch Speed",  &settings::KA_VSpeed, 1.0f, 180.0f, ImVec2(0,0), "%.1f deg/tick");
	Menu::Slider("Min Rotation", &settings::KA_MinRotationDiff, 0.0f, 4.0f, ImVec2(0,0), "%.2f deg");
	Menu::Dropdown("Target Priority", settings::KA_TargetPriorityList, &settings::KA_TargetPriority, 3);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("KA_Sep2");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Slider("Min CPS", &settings::KA_MinCps, 1, settings::KA_MaxCps);
	Menu::Slider("Max CPS", &settings::KA_MaxCps, settings::KA_MinCps, 25);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("KA_Sep3");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Checkbox("Humanise (legitimize)", &settings::KA_Legitimize);
	Menu::Checkbox("Simulate Short-Stops",  &settings::KA_SimulateShortStop);
	Menu::Checkbox("Raycast LOS Check",     &settings::KA_RaycastCheck);
	Menu::Checkbox("Only While Attacking",  &settings::KA_OnlyOnLMB);
	Menu::Checkbox("Weapon Only",           &settings::KA_WeaponOnly);
	Menu::Checkbox("Disable When Flying",   &settings::KA_DisableWhenFlying);
	Menu::Checkbox("Ignore Friends",        &settings::KA_IgnoreFriends);
	Menu::Checkbox("Ignore Invisible",      &settings::KA_IgnoreInvisible);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("KA_Sep4");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Checkbox("FOV Circle", &settings::KA_FovCircle);
	if (settings::KA_FovCircle)
	{
		Menu::ColorEdit("FOV Circle Color", settings::KA_FovCircleColor);
	}

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("KA_Sep5");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Text("v0.1 - visible rotation. Silent rotation (JVMTI sendPacket hook) planned for v0.2.", FontSize::SIZE_14);
	Menu::Text("WARNING: This module is detectable on most servers. See KILLAURA_DESIGN.md.", FontSize::SIZE_14);
}
