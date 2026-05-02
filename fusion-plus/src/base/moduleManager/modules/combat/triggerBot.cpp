#include "triggerBot.h"

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
#include "menu/menu.h"
#include "moduleManager/commonData.h"
#include "sdk/sdk.h"
#include "util/logger.h"
#include "util/minecraft/minecraft.h"

namespace
{
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

int  TriggerBot::GetKey()              { return settings::TBot_Key; }
bool TriggerBot::IsEnabled()           { return settings::TBot_Enabled; }
void TriggerBot::SetEnabled(bool e)    { settings::TBot_Enabled = e; }
void TriggerBot::Toggle()              { settings::TBot_Enabled = !settings::TBot_Enabled; }

void TriggerBot::Update()
{
	if (!settings::TBot_Enabled)            return;
	if (!CommonData::SanityCheck())         return;
	if (Menu::open)                         return;
	if (SDK::minecraft->IsInGuiState())     return;

	// Gate: weapon-only.
	if (settings::TBot_WeaponOnly &&
	    !MinecraftUtils::IsWeapon(SDK::minecraft->thePlayer->GetInventory().GetCurrentItem()))
	{
		m_lastTargetInst  = nullptr;
		m_targetFirstSeen = std::chrono::steady_clock::time_point{};
		return;
	}

	// Gate: only-on-LMB held. (Same logic as Reach.)
	if (settings::TBot_OnlyOnLMB)
	{
		if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
		{
			m_lastTargetInst  = nullptr;
			m_targetFirstSeen = std::chrono::steady_clock::time_point{};
			return;
		}
	}

	// Vanilla raycast — what's currently under the crosshair?
	CMovingObjectPosition mop = SDK::minecraft->GetMouseOver();
	CEntity hit = mop.GetEntityHit();
	if (hit.GetInstance() == nullptr)
	{
		// Not pointing at any entity at all.
		m_lastTargetInst  = nullptr;
		m_targetFirstSeen = std::chrono::steady_clock::time_point{};
		return;
	}

	// Skip if it's ourselves.
	const std::string ourName = SDK::minecraft->thePlayer->GetName();

	// Look up the player's full record in CommonData::nativePlayerList by
	// JNI ref equality. This is the same trick reach.cpp uses to filter
	// friends and check health.
	auto& list = CommonData::nativePlayerList;
	bool   isPlayer  = false;
	bool   isFriend  = false;
	bool   isInvis   = false;
	float  hpHealth  = 1.0f;

	for (auto& p : list)
	{
		if (p.obj.GetInstance() == hit.GetInstance())
		{
			isPlayer = true;
			if (p.name == ourName) return;        // never attack ourselves
			isFriend = configmanager::IsFriend(p.name);
			isInvis  = p.obj.IsInvisibleToPlayer(SDK::minecraft->thePlayer->GetInstance());
			hpHealth = p.health;
			break;
		}
	}

	// Configurable: only fire on players (not mobs). Default true since most
	// PvP servers only care about player kills and we want to avoid eating
	// our cooldown swinging at chickens.
	if (settings::TBot_PlayersOnly && !isPlayer)
	{
		m_lastTargetInst  = nullptr;
		m_targetFirstSeen = std::chrono::steady_clock::time_point{};
		return;
	}

	// Skip dead targets.
	if (isPlayer && hpHealth <= 0.0f)
	{
		m_lastTargetInst  = nullptr;
		m_targetFirstSeen = std::chrono::steady_clock::time_point{};
		return;
	}

	// Skip friends / invisibles per setting.
	if (isFriend && settings::TBot_IgnoreFriends)   return;
	if (isInvis  && settings::TBot_IgnoreInvisible) return;

	// Reaction-time hold delay.
	auto now = std::chrono::steady_clock::now();
	if (hit.GetInstance() != m_lastTargetInst)
	{
		// New target acquired — reset the hold timer.
		m_lastTargetInst  = hit.GetInstance();
		m_targetFirstSeen = now;
		// Don't fire on the same tick the target appeared — even a fast human
		// has ~80-150 ms of reaction. The user-set HoldDelay enforces this.
		return;
	}

	long held = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_targetFirstSeen).count();
	if (held < settings::TBot_HoldDelay) return;

	// CPS pacing — same idiom as reach.cpp / leftAutoClicker.cpp.
	if (m_nextDelayMs == 0)
	{
		m_nextDelayMs = RandomClickDelayMs(settings::TBot_MinCps, settings::TBot_MaxCps);
	}
	long sinceLast = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_lastAttack).count();
	if (sinceLast < m_nextDelayMs) return;

	// Fire a synthetic mouse-press to the Minecraft window. This goes through
	// vanilla code-paths so the server sees a regular swing + C02PacketUseEntity
	// emitted from a normal user click. No rotation packet of ours involved.
	POINT pos_cursor;
	GetCursorPos(&pos_cursor);
	SendMessage(Menu::handleWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pos_cursor.x, pos_cursor.y));
	SendMessage(Menu::handleWindow, WM_LBUTTONUP,   0,          MAKELPARAM(pos_cursor.x, pos_cursor.y));

	m_lastAttack  = now;
	m_nextDelayMs = RandomClickDelayMs(settings::TBot_MinCps, settings::TBot_MaxCps);
}

void TriggerBot::RenderMenu()
{
	Menu::ToggleWithKeybind(&settings::TBot_Enabled, settings::TBot_Key);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("TBot_Sep1");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Slider("Min CPS", &settings::TBot_MinCps, 1, settings::TBot_MaxCps);
	Menu::Slider("Max CPS", &settings::TBot_MaxCps, settings::TBot_MinCps, 20);
	Menu::Slider("Reaction Hold (ms)", &settings::TBot_HoldDelay, 0, 500,
		ImVec2(0, 0), "%d ms (humans need ~150 ms)");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("TBot_Sep2");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Checkbox("Only While Attacking", &settings::TBot_OnlyOnLMB);
	Menu::Checkbox("Weapon Only",          &settings::TBot_WeaponOnly);
	Menu::Checkbox("Players Only",         &settings::TBot_PlayersOnly);
	Menu::Checkbox("Ignore Friends",       &settings::TBot_IgnoreFriends);
	Menu::Checkbox("Ignore Invisible",     &settings::TBot_IgnoreInvisible);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
	Menu::HorizontalSeparator("TBot_Sep3");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

	Menu::Text("No rotation manipulation - safe alongside AimAssist.", FontSize::SIZE_14);
	Menu::Text("Server sees the same packets as a clean clicker.",     FontSize::SIZE_14);
}
