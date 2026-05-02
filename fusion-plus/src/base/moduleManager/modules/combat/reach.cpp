#include "reach.h"

// NOTE: Other headers in this project pull in <Windows.h> which defines
// `min` and `max` as preprocessor macros, breaking std::max/std::min. Rather
// than try to undefine them globally, this TU uses the parenthesised
// (std::max)(...) pattern (same convention used by leftAutoClicker.cpp).

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <random>

// Defensive: if Windows leaked min/max as macros, kill them here.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "sdk/sdk.h"
#include "java/java.h"
#include "menu/menu.h"
#include "moduleManager/commonData.h"
#include "configManager/configManager.h"
#include "util/logger.h"
#include "util/math/math.h"
#include "util/minecraft/minecraft.h"

// =============================================================================
// Reach
//   Extended-range melee attack assist.
//
//   Vanilla Minecraft hard-caps melee attacks at 3.0 blocks (server-side too,
//   on most setups). This module raises that for the *client* by manually
//   ray-casting against entity bounding boxes inside the slider's range and
//   triggering a synthetic Minecraft.clickMouse() when an in-range target is
//   found beyond the vanilla 3.0 distance.
//
//   Behaviour summary:
//     - Only fires while LMB is held (configurable via Reach_OnlyOnLMB)
//     - Ignores friends, current player, and dead entities
//     - Rate-limited by user-configured CPS so it doesn't spam-attack
//     - Optional: visibility check, weapon-only gate
//
//   This file is fully additive. The previous implementation was a stub with
//   an empty Update() so the slider did nothing. Nothing else in the codebase
//   needs to change.
// =============================================================================

namespace
{
	// Standard Minecraft entity-collision border. Real EntityRenderer.getMouseOver
	// expands the entity AABB by this much before the ray-cast.
	constexpr float ENTITY_BORDER_SIZE = 0.1f;

	// The vanilla hard limit. We only fire synthetic attacks for targets
	// strictly *further* than this — closer ones use the normal click path
	// so we don't double-attack the same hit.
	constexpr float VANILLA_REACH = 3.0f;

	// Compute the look direction unit vector from yaw/pitch in degrees.
	// Matches Minecraft's coordinate convention used elsewhere in this codebase.
	static Vector3 LookDirection(float yawDeg, float pitchDeg)
	{
		const float DEG2RAD = 0.0174532925f;
		float yaw   = yawDeg   * DEG2RAD;
		float pitch = pitchDeg * DEG2RAD;
		float cosP = std::cos(pitch);
		return Vector3(
			-std::sin(yaw) * cosP,
			-std::sin(pitch),
			 std::cos(yaw) * cosP
		);
	}

	// Ray vs axis-aligned bounding box (slab method).
	// Returns true and writes the t-value of the *entry* hit into outT if the
	// ray (origin + t*dir, t >= 0) intersects [aabbMin, aabbMax].
	static bool RayHitsAABB(const Vector3& origin, const Vector3& dir,
		const Vector3& aabbMin, const Vector3& aabbMax,
		float& outT)
	{
		float tmin = 0.0f;
		float tmax = FLT_MAX;

		auto axis = [&](float o, float d, float lo, float hi) -> bool
		{
			if (std::fabs(d) < 1e-8f)
			{
				// Parallel to slab — only ok if origin is inside.
				return o >= lo && o <= hi;
			}
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

	// Pick a random delay in [1000/maxCps, 1000/minCps] ms.
	static int RandomClickDelayMs(int minCps, int maxCps)
	{
		if (minCps <= 0) minCps = 1;
		if (maxCps < minCps) maxCps = minCps;

		int hi = (int)std::round(1000.0 / ((minCps < 1) ? 1 : minCps)); // slowest
		int lo = (int)std::round(1000.0 / ((maxCps < 1) ? 1 : maxCps)); // fastest
		if (lo > hi) std::swap(lo, hi);

		static std::mt19937 rng{ std::random_device{}() };
		std::uniform_int_distribution<int> d(lo, hi);
		return d(rng);
	}
}

void Reach::Update()
{
	if (!settings::Reach_Enabled)               return;
	if (!CommonData::SanityCheck())              return;
	if (Menu::open)                              return;
	if (SDK::minecraft->IsInGuiState())          return;

	// Slider value safe clamp.
	float clamped = settings::Reach_ReachDistance;
	if (clamped > 6.0f)         clamped = 6.0f;
	if (clamped < VANILLA_REACH) clamped = VANILLA_REACH;
	float maxReach = clamped;

	// If the slider is at vanilla distance there's nothing to do.
	if (maxReach <= VANILLA_REACH + 0.001f) return;

	// Optional gate: only fire while the user is actually attacking.
	if (settings::Reach_OnlyOnLMB)
	{
		bool lmbHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (!lmbHeld) return;
	}

	// Optional gate: held item must be a configured weapon.
	if (settings::Reach_WeaponOnly)
	{
		CItemStack held = SDK::minecraft->thePlayer->GetInventory().GetCurrentItem();
		if (!MinecraftUtils::IsWeapon(held)) return;
	}

	// CPS pacing — emit at most one synthetic click per (1000/cps) ms window.
	auto now = std::chrono::steady_clock::now();
	if (m_nextDelayMs == 0)
	{
		m_nextDelayMs = RandomClickDelayMs(settings::Reach_MinCps, settings::Reach_MaxCps);
	}
	auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_lastAttack).count();
	if (sinceLast < m_nextDelayMs) return;

	// Build the ray.
	Vector3 origin = CommonData::playerEyePos;
	Vector3 dir    = LookDirection(CommonData::playerYaw, CommonData::playerPitch);
	if (std::isnan(origin.x) || std::isnan(dir.x)) return;

	// Walk every visible player and find the closest in-range hit.
	float  bestT      = maxReach + 1.0f;
	bool   haveTarget = false;
	CEntityPlayer bestPlayer = CEntityPlayer();

	const std::string ourName = SDK::minecraft->thePlayer->GetName();

	for (const auto& info : CommonData::nativePlayerList)
	{
		// Skip self.
		if (info.name == ourName) continue;
		// Skip dead/invalid.
		if (info.health <= 0.0f) continue;

		// Skip friends if requested.
		if (settings::Reach_IgnoreFriends && configmanager::IsFriend(info.name)) continue;

		// Quick distance reject (cheap, avoids full ray test for far entities).
		float distSq = (info.pos - CommonData::playerPos).Length();
		if (distSq > maxReach + 4.0f) continue; // generous slack for boxes

		// Build entity AABB and inflate by Minecraft's collision border (0.1f).
		Vector3 aabbMin(
			(float)info.boundingBox.minX - ENTITY_BORDER_SIZE,
			(float)info.boundingBox.minY - ENTITY_BORDER_SIZE,
			(float)info.boundingBox.minZ - ENTITY_BORDER_SIZE);
		Vector3 aabbMax(
			(float)info.boundingBox.maxX + ENTITY_BORDER_SIZE,
			(float)info.boundingBox.maxY + ENTITY_BORDER_SIZE,
			(float)info.boundingBox.maxZ + ENTITY_BORDER_SIZE);

		float t;
		if (!RayHitsAABB(origin, dir, aabbMin, aabbMax, t)) continue;
		if (t > maxReach) continue;
		if (t >= bestT)   continue;

		// Wall check between us and the entity hit point.
		// CWorld::RayTraceBlocks signature is:
		//   (Vector3 from, Vector3 to, Vector3& result,
		//    bool stopOnLiquid, bool ignoreBlockWithoutBoundingBox,
		//    bool returnLastUncollidableBlock)
		// and returns TRUE if a block was hit on the way (LOS blocked).
		if (settings::Reach_VisibilityCheck)
		{
			Vector3 hitPoint = origin + dir * t;
			Vector3 unused;
			bool blocked = SDK::minecraft->theWorld->RayTraceBlocks(
				origin, hitPoint, unused,
				/*stopOnLiquid*/ false,
				/*ignoreBlockWithoutBoundingBox*/ true,
				/*returnLastUncollidableBlock*/ false);
			if (blocked) continue;
		}

		bestT      = t;
		haveTarget = true;
		bestPlayer = info.obj;
	}

	if (!haveTarget) return;

	// Vanilla already handles attacks <= 3.0; only fire synthetics outside that.
	if (bestT <= VANILLA_REACH) return;

	// Trigger the synthetic attack.
	try
	{
		SDK::minecraft->ClickMouse();
	}
	catch (...)
	{
		// JNI exceptions can occasionally bubble up if the player swapped worlds
		// between this tick's data snapshot and now — silently ignore, the next
		// tick will retry.
		return;
	}

	m_lastAttack   = now;
	m_nextDelayMs  = RandomClickDelayMs(settings::Reach_MinCps, settings::Reach_MaxCps);
}

void Reach::RenderMenu()
{
	Menu::ToggleWithKeybind(&settings::Reach_Enabled, settings::Reach_Key);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("Reach_Sep1");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Slider("Reach Distance", &settings::Reach_ReachDistance, 3.0f, 6.0f, ImVec2(0, 0), "%.2f blocks");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("Reach_Sep2");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Checkbox("Only When Attacking",  &settings::Reach_OnlyOnLMB);
	Menu::Checkbox("Weapon Only",          &settings::Reach_WeaponOnly);
	Menu::Checkbox("Visibility Check",     &settings::Reach_VisibilityCheck);
	Menu::Checkbox("Ignore Friends",       &settings::Reach_IgnoreFriends);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("Reach_Sep3");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Slider("Min CPS", &settings::Reach_MinCps, 1, settings::Reach_MaxCps);
	Menu::Slider("Max CPS", &settings::Reach_MaxCps, settings::Reach_MinCps, 25);
}
