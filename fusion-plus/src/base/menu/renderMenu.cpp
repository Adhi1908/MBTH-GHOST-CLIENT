#include "menu.h"

#include <imgui/imgui_internal.h>

#include "base/base.h"
#include "moduleManager/moduleManager.h"
#include "sdk/net/minecraft/client/Minecraft.h"
#include "util/logger.h"
#include "util/window/borderless.h"
#include "configManager/configManager.h"
#include "notificationManager/notificationManager.h"

const int CATEGORY_FONT_SIZE_INT = 22;
const FontSize CATEGORY_FONT_SIZE = FontSize::SIZE_22;
const int MODULE_FONT_SIZE_INT = 18;
const FontSize MODULE_FONT_SIZE = FontSize::SIZE_18;
const FontSize BUTTON_FONT_SIZE = FontSize::SIZE_18;

std::unique_ptr<std::once_flag> setConfigName = std::make_unique<std::once_flag>();
void Menu::RenderMenu()
{
	static int selectedCategory = 0;
	static std::vector<std::string> categories = ModuleManager::GetCategories();
	static int selectedModule = 0;
	static std::vector<std::unique_ptr<ModuleBase>>& modules = ModuleManager::GetModules();

	// Hud Boolean
	if (!Menu::openHudEditor && selectedCategory == -2 && selectedModule == 3)
	{
		Menu::openHudEditor = true;
	}
	else if (Menu::openHudEditor && (selectedCategory != -2 || selectedModule != 3))
	{
		Menu::openHudEditor = false;
	}

	ImGui::SetNextWindowSize(ImVec2(1000.f, 650.f));
	ImGui::Begin("MBTH", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		ImVec2 titleSize = Menu::fontBold->CalcTextSizeA(24, FLT_MAX, 0.0f, ("MBTH" + std::string(Base::m_version)).c_str());
		float leftWidth = titleSize.x += 40.f;
		float topHeight = titleSize.y + 20.f;

		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 10.f, ImGui::GetCursorPosY() + 10.f)); // Change padding from 10 to 20
		ImGui::BeginGroup(); // Group for the bottom left part

		ImGui::BeginChild("##LogoVersion", ImVec2(leftWidth, topHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			Menu::GlitchText("MBTH", FontSize::SIZE_24);
			ImGui::SameLine();
			Menu::VerticalSeparator("LogoSep", ImGui::GetWindowSize().y - 20.f);
			ImGui::SameLine();
			Menu::BoldText(Base::m_version, FontSize::SIZE_24);
		}
		ImGui::EndChild();

		// === Search bar ====================================================
		// Lets the user filter modules across all categories. Empty string = no filter.
		static char searchBuffer[64] = "";
		ImGui::SetCursorPosX(ImGui::GetCursorPosX());

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, settings::Menu_ComponentsRounding);
		ImGui::SetNextItemWidth(leftWidth);
		ImGui::InputTextWithHint("##ModuleSearch", "Search modules...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
		ImGui::PopStyleVar();

		// Helper: case-insensitive substring contains.
		auto containsIC = [](const std::string& haystack, const std::string& needle) -> bool {
			if (needle.empty()) return true;
			if (needle.size() > haystack.size()) return false;
			auto tolow = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
			for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
				bool match = true;
				for (size_t j = 0; j < needle.size(); ++j) {
					if (tolow(haystack[i + j]) != tolow(needle[j])) { match = false; break; }
				}
				if (match) return true;
			}
			return false;
		};

		std::string searchQuery = searchBuffer;
		bool searchActive = !searchQuery.empty();
		// ====================================================================

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f)); // Need to manually set padding for this child so the "selected" line is rendered correctly
		// Slightly shorter than before to make room for the search bar above.
		ImGui::BeginChild("##ModulesList", ImVec2(leftWidth, ImGui::GetWindowHeight() - topHeight - 220.f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			if (searchActive)
			{
				// === Search mode: show ALL matching modules across categories ===
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
				int matchCount = 0;
				for (int i = 0; i < (int)modules.size(); i++)
				{
					const std::string& nm  = modules[i]->GetName();
					const std::string& cat = modules[i]->GetCategory();
					const std::string  desc = modules[i]->GetDescription();
					if (!containsIC(nm, searchQuery) &&
						!containsIC(cat, searchQuery) &&
						!containsIC(desc, searchQuery))
						continue;

					matchCount++;
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.f);

					if (i == selectedModule)
					{
						ImVec2 currentPos = ImGui::GetCursorScreenPos();
						currentPos.x += leftWidth - 13.f;
						currentPos.y -= 3.f;
						ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
						ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + 1.f, currentPos.y + 36.f), color);
					}

					// Show "Module Name (Category)" so the user can disambiguate.
					std::string label = nm + " (" + cat + ")";
					if (Menu::TransparentButton(label.c_str(), ImVec2(leftWidth - 20.f, 30.f), MODULE_FONT_SIZE))
					{
						selectedModule = i;
						// Auto-jump to that category so the right pane shows the right module.
						for (int c = 0; c < (int)categories.size(); c++)
						{
							if (categories[c] == cat) { selectedCategory = c; break; }
						}
					}
					if (ImGui::IsItemHovered() && !desc.empty())
					{
						ImGui::SetTooltip("%s", desc.c_str());
					}
				}
				if (matchCount == 0)
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.f);
					ImGui::TextDisabled("No modules match.");
				}
			}
			else if (selectedCategory >= 0)
			{
				if (selectedCategory >= categories.size())
					selectedCategory = 0;

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f); // Manual padding
				for (int i = 0; i < modules.size(); i++)
				{
					// Category Check
					if (modules[i]->GetCategory() != categories[selectedCategory])
						continue;

					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.f); // Manual padding

					// Render "selected" line
					if (i == selectedModule)
					{
						ImVec2 currentPos = ImGui::GetCursorScreenPos();
						currentPos.x += leftWidth - 13.f;
						currentPos.y -= 3.f;

						ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
						ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + 1.f, currentPos.y + 36.f), color);
					}

					if (Menu::TransparentButton(modules[i]->GetName().c_str(), ImVec2(leftWidth - 20.f, 30.f), MODULE_FONT_SIZE))
					{
						selectedModule = i;
					}
					// Hover tooltip with the module's description.
					std::string desc = modules[i]->GetDescription();
					if (!desc.empty() && ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", desc.c_str());
					}
				}
			}
			else if (selectedCategory == -1) // Configs
			{
				static std::vector<std::string> configTabs = { "Local Configs" };

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f); // Manual padding
				for (int i = 0; i < configTabs.size(); i++)
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.f); // Manual padding

					// Render "selected" line
					if (i == selectedModule)
					{
						ImVec2 currentPos = ImGui::GetCursorScreenPos();
						currentPos.x += leftWidth - 13.f;
						currentPos.y -= 3.f;

						ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
						ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + 1.f, currentPos.y + 36.f), color);
					}

					if (Menu::TransparentButton(configTabs[i].c_str(), ImVec2(leftWidth - 20.f, 30.f), MODULE_FONT_SIZE))
					{
						selectedModule = i;
					}
				}
			}
			else if (selectedCategory == -2) // Settings
			{
				static std::vector<std::string> settingTabs = { "General", "Friends", "Style", "HUD" };

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f); // Manual padding
				for (int i = 0; i < settingTabs.size(); i++)
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.f); // Manual padding

					// Render "selected" line
					if (i == selectedModule)
					{
						ImVec2 currentPos = ImGui::GetCursorScreenPos();
						currentPos.x += leftWidth - 13.f;
						currentPos.y -= 3.f;

						ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
						ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + 1.f, currentPos.y + 36.f), color);
					}

					if (Menu::TransparentButton(settingTabs[i].c_str(), ImVec2(leftWidth - 20.f, 30.f), MODULE_FONT_SIZE))
					{
						selectedModule = i;
					}
				}
			}

			else
			{
				selectedCategory = 0;
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();

		ImVec2 currentPos = ImGui::GetCursorScreenPos();
		if (Menu::MenuButton("Configs", ImVec2(leftWidth, 35.f), BUTTON_FONT_SIZE))
		{
			selectedCategory = -1;
			selectedModule = 0;
		}
		if (selectedCategory == -1)
		{
			ImVec2 textSize = Menu::font->CalcTextSizeA(CATEGORY_FONT_SIZE_INT, FLT_MAX, 0.0f, "Configs");

			currentPos.y += 33.f;
			currentPos.x += 20.f;

			ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
			ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + leftWidth - 40.f, currentPos.y + 1), color);
		}

		currentPos = ImGui::GetCursorScreenPos();
		if (Menu::MenuButton("Settings", ImVec2(leftWidth, 35.f), BUTTON_FONT_SIZE))
		{
			selectedCategory = -2;
			selectedModule = 0;
		}
		if (selectedCategory == -2)
		{
			ImVec2 textSize = Menu::font->CalcTextSizeA(CATEGORY_FONT_SIZE_INT, FLT_MAX, 0.0f, "Settings");

			currentPos.y += 33.f;
			currentPos.x += 20.f;

			ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
			ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + leftWidth - 40.f, currentPos.y + 1), color);
		}

		if (Menu::DetachButton("Detach", ImVec2(leftWidth, 35.f), BUTTON_FONT_SIZE))
		{
			Base::m_running = false;
		}

		ImGui::EndGroup();

		ImGui::SameLine();

		ImGui::BeginGroup(); // Group for the right part

		static float occupiedSpace = std::accumulate(categories.begin(), categories.end(), 0.f, [](float acc, const std::string& str) { return acc + Menu::font->CalcTextSizeA(CATEGORY_FONT_SIZE_INT, FLT_MAX, 0.0f, str.c_str()).x + 8; }) + (categories.size() - 1);
		static float availableSpace = ImGui::GetWindowSize().x - leftWidth - 50.f;
		static float spaceBetween = max(10.f, (availableSpace - occupiedSpace) / (categories.size() * 2)) - 9.f;

		ImGui::BeginChild("##CategoriesBar", ImVec2(ImGui::GetWindowSize().x - leftWidth - 50.f, topHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() - 2.f, ImGui::GetCursorPosY() - 6.f));

			for (int i = 0; i < categories.size(); i++)
			{
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spaceBetween);

				// Render "selected" line
				if (i == selectedCategory)
				{
					ImVec2 currentPos = ImGui::GetCursorScreenPos();
					ImVec2 textSize = Menu::font->CalcTextSizeA(CATEGORY_FONT_SIZE_INT, FLT_MAX, 0.0f, categories[i].c_str());

					currentPos.y += 38.f;

					ImColor color = ImColor(settings::Menu_PrimaryColor[0], settings::Menu_PrimaryColor[1], settings::Menu_PrimaryColor[2], settings::Menu_PrimaryColor[3]);
					ImGui::GetWindowDrawList()->AddRectFilled(currentPos, ImVec2(currentPos.x + textSize.x + 8, currentPos.y + 1), color);
				}

				if (Menu::TransparentButton(categories[i].c_str(), ImVec2(0.f, 0.f), CATEGORY_FONT_SIZE))
				{
					selectedCategory = i;
					selectedModule = ModuleManager::GetFirstModuleIndexByCategory(categories[i]);
				}
				ImGui::SameLine();

				if (i != categories.size() - 1)
				{
					ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + spaceBetween, ImGui::GetCursorPosY() + 5));
					Menu::VerticalSeparator(("CatSep" + std::to_string(i)).c_str(), ImGui::GetWindowSize().y - 20.f);
					ImGui::SameLine();
				}
			}
		}
		ImGui::EndChild();

		ImGui::BeginChild("##ModuleSettings", ImVec2(ImGui::GetWindowSize().x - leftWidth - 50.f, ImGui::GetWindowHeight() - topHeight - 50.f), true);
		{
			ImGui::SetCursorPos(ImVec2(20.f, 20.f)); // Manual padding
			ImGui::BeginGroup();
			if (selectedCategory >= 0)
			{
				if (selectedCategory >= categories.size())
					selectedCategory = 0;

				if (selectedModule >= modules.size() || selectedModule < 0)
					selectedModule = ModuleManager::GetFirstModuleIndexByCategory(categories[selectedCategory]);

				modules[selectedModule]->RenderMenu();
			}
			else if (selectedCategory == -1) // Configs
			{
				if (selectedModule == 0) // Local Configs
				{
					static std::vector<std::string> configFiles = configmanager::GetConfigList();
					static int selectedConfig = 0;
					std::string selectedConfigName = configFiles.size() > 0 && configFiles.size() > selectedConfig ? configFiles[selectedConfig] : "null";

					ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
					ImGui::BeginChild("###ConfigLits", ImVec2(0, 436.f));
					{
						bool hasScrollbar = ImGui::GetCurrentWindow()->ScrollMax.y > 0.0f;

						for (int i = 0; i < configFiles.size(); ++i)
						{
							bool deleted = false;
							if (Menu::ConfigItem(configFiles[i].c_str(), &deleted, hasScrollbar))
							{
								selectedConfig = i;
								setConfigName = std::make_unique<std::once_flag>();
							}

							if (deleted)
							{
								if (configmanager::RemoveConfig(i))
								{
									NotificationManager::Send("MBTH :: Config", "Config \"%s\" has been removed.", configFiles[i].c_str());
								}
								else
								{
									NotificationManager::Send("MBTH :: Config", "Config \"%s\" could not be removed.", configFiles[i].c_str());
								}

								configFiles = configmanager::GetConfigList();
								
								if (selectedConfig == i)
								{
									selectedConfig = 0;
									setConfigName = std::make_unique<std::once_flag>();
								}
							}
						}
					}
					ImGui::EndChild();
					ImGui::PopStyleColor();

					ImVec2 openButtonSize = Menu::font18->CalcTextSizeA(18, FLT_MAX, 0.0f, "Open Folder");
					openButtonSize.x += ImGui::GetStyle().FramePadding.x * 8;
					ImVec2 refreshButtonSize = Menu::font18->CalcTextSizeA(18, FLT_MAX, 0.0f, "Refresh");
					refreshButtonSize.x += ImGui::GetStyle().FramePadding.x * 8;

					if (Menu::Button(("Load \"" + selectedConfigName + "\"").c_str(), ImVec2(ImGui::GetWindowSize().x - openButtonSize.x - refreshButtonSize.x - 56.f, 30.f)))
					{
						if (configFiles.size() > 0)
						{
							if (configmanager::LoadConfig(selectedConfig))
							{
								NotificationManager::Send("MBTH :: Config", "Config \"%s\" has been loaded.", selectedConfigName.c_str());
								Menu::ResetSetupFlags();
							}
							else
							{
								NotificationManager::Send("MBTH :: Config", "Config \"%s\" could not be loaded.", selectedConfigName.c_str());
							}
						}
					}

					ImGui::SameLine();

					if (Menu::Button("Open Folder", ImVec2(openButtonSize.x, 30.f)))
					{
						ShellExecuteA(NULL, "open", configmanager::GetConfigPath().c_str(), NULL, NULL, SW_SHOWNORMAL);
					}

					ImGui::SameLine();

					if (Menu::Button("Refresh", ImVec2(refreshButtonSize.x, 30.f)))
					{
						configFiles = configmanager::GetConfigList();
						NotificationManager::Send("MBTH :: Config", "Config list has been refreshed.");
					}

					ImVec2 saveButtonSize = Menu::font18->CalcTextSizeA(18, FLT_MAX, 0.0f, "Save");
					saveButtonSize.x += ImGui::GetStyle().FramePadding.x * 8;
					saveButtonSize.y += ImGui::GetStyle().FramePadding.y * 2;

					static char saveConfigName[128] = "";
					std::call_once(*setConfigName, []() {
						const char* selectedConfigNameC = configFiles.size() > 0 ? configFiles[selectedConfig].c_str() : "";
						strcpy_s(saveConfigName, selectedConfigNameC);
						});

					ImGui::SetNextItemWidth(ImGui::GetWindowSize().x + 190.f - saveButtonSize.x);
					ImGui::InputText("##saveConfigName", saveConfigName, IM_ARRAYSIZE(saveConfigName));

					ImGui::SameLine();

					if (Menu::Button("Save", ImVec2(saveButtonSize.x, 30.f)))
					{
						int result = configmanager::SaveConfig(saveConfigName);
						if (result != -1)
						{
							configFiles = configmanager::GetConfigList();
							selectedConfig = result;
							NotificationManager::Send("MBTH :: Config", "Config \"%s\" has been saved.", saveConfigName);
						}
						else
						{
							NotificationManager::Send("MBTH :: Config", "Config \"%s\" could not be saved.", saveConfigName);
						}
					}
				}

				else
				{
					selectedModule = 0;
				}
			}
			else if (selectedCategory == -2) // Settings
			{
				if (selectedModule == 0) // General
				{
					Menu::KeybindButton("Menu", settings::Menu_Keybind, false);
					Menu::KeybindButton("Detach", settings::Menu_DetachKey, false);

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
					Menu::HorizontalSeparator("Sep1");
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

					Menu::Checkbox("GUI Movement", &settings::Menu_GUIMovement);

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
					Menu::HorizontalSeparator("Sep2");
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
				}
				else if (selectedModule == 1) // Friends
				{
					std::vector<std::string> friends = settings::friends;

					ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
					ImGui::BeginChild("###FriendsLits", ImVec2(0, 476.f));
					{
						bool hasScrollbar = ImGui::GetCurrentWindow()->ScrollMax.y > 0.0f;

						for (int i = 0; i < friends.size(); ++i)
						{
							bool deleted = false;
							Menu::ConfigItem(friends[i].c_str(), &deleted, hasScrollbar);

							if (deleted)
							{
								if (configmanager::RemoveFriend(friends[i].c_str()))
								{
									NotificationManager::Send("MBTH :: Friends", "Friend \"%s\" has been removed.", friends[i].c_str());
								}
								else
								{
									NotificationManager::Send("MBTH :: Friends", "Friend \"%s\" could not be removed.", friends[i].c_str());
								}
							}
						}
					}
					ImGui::EndChild();
					ImGui::PopStyleColor();

					ImVec2 addFriendButtonSize = Menu::font18->CalcTextSizeA(18, FLT_MAX, 0.0f, "Add");
					addFriendButtonSize.x += ImGui::GetStyle().FramePadding.x * 8;
					addFriendButtonSize.y += ImGui::GetStyle().FramePadding.y * 2;

					static char newFriendName[128] = "";
					ImGui::SetNextItemWidth(ImGui::GetWindowSize().x + 190.f - addFriendButtonSize.x);
					ImGui::InputText("##addFriendName", newFriendName, IM_ARRAYSIZE(newFriendName));

					ImGui::SameLine();

					if (Menu::Button("Add", ImVec2(addFriendButtonSize.x, 30.f)))
					{
						if (configmanager::AddFriend(newFriendName))
						{
							NotificationManager::Send("MBTH :: Friends", "Friend \"%s\" has been added.", newFriendName);
						}
						else
						{
							NotificationManager::Send("MBTH :: Friends", "Friend \"%s\" could not be added.", newFriendName);
						}
					}
				}
				else if (selectedModule == 2) // Style
				{
					if (Menu::ColorEdit("Primary Color", settings::Menu_PrimaryColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Secondary Color", settings::Menu_SecondaryColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Background Color", settings::Menu_BackgroundColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Child Background Color", settings::Menu_ChildBackgroundColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Outline Color", settings::Menu_OutlineColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Text Color", settings::Menu_TextColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Seperator Color", settings::Menu_SeperatorColor)) Menu::SetupStyle();
					if (Menu::ColorEdit("Detach Button Color", settings::Menu_DetachButtonColor)) Menu::SetupStyle();

					if (Menu::Slider("Window Rounding", &settings::Menu_WindowRounding, 0.f, 12.f)) Menu::SetupStyle();
					if (Menu::Slider("Item Rounding", &settings::Menu_ComponentsRounding, 0.f, 12.f)) Menu::SetupStyle();
				}
				else if (selectedModule == 3) // HUD
				{
					Menu::Checkbox("Disable all Hud/Overlay rendering", &settings::Hud_DisableAllRendering);

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
					Menu::HorizontalSeparatorText("Notification Sounds", FontSize::SIZE_18);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

					Menu::Checkbox("Enable Sounds (master)",     &settings::Notif_SoundsEnabled);
					Menu::Checkbox("Play on Info notifications", &settings::Notif_SoundOnInfo);
					Menu::Checkbox("Play on Warnings",           &settings::Notif_SoundOnWarning);
					Menu::Checkbox("Play on Errors",             &settings::Notif_SoundOnError);

					if (Menu::Button("Test sounds (Info / Warn / Error)", ImVec2(0, 30.f)))
					{
						// Play whichever ones are currently enabled, briefly spaced
						// so each tone is audible. NotificationManager::Send will
						// route them through the user's per-type toggles.
						NotificationManager::Send("MBTH :: Info",    "Info sound test");
						Sleep(220);
						NotificationManager::Send("MBTH :: Warning", "Warning sound test");
						Sleep(220);
						NotificationManager::Send("MBTH :: Error",   "Error sound test");
					}

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
					Menu::HorizontalSeparatorText("Watermark", FontSize::SIZE_18);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

					Menu::Checkbox("Show Watermark", &settings::Hud_Watermark);
					Menu::Checkbox("Version", &settings::Hud_WatermarkVersion);
					Menu::Checkbox("FPS", &settings::Hud_WatermarkFps);
					Menu::Checkbox("Ping", &settings::Hud_WatermarkPing);
					Menu::Checkbox("Coordinates", &settings::Hud_WatermarkCoords);
					Menu::Checkbox("Direction", &settings::Hud_WatermarkDirection);
					Menu::Checkbox("Time", &settings::Hud_WatermarkTime);

					if (Menu::Slider("X Position##Watermark", &settings::Hud_WatermarkPosition[0], 0.f, 1920.f)) Menu::ResetSetupFlags();
					if (Menu::Slider("Y Position##Watermark", &settings::Hud_WatermarkPosition[1], 0.f, 1080.f)) Menu::ResetSetupFlags();


					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
					Menu::HorizontalSeparatorText("Keybinds", FontSize::SIZE_18);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

					Menu::Checkbox("Show Keybinds", &settings::Hud_ShowKeybinds);
					if (Menu::Slider("X Position##Keybinds", &settings::Hud_KeybindsPosition[0], 0.f, 1920.f)) Menu::ResetSetupFlags();
					if (Menu::Slider("Y Position##Keybinds", &settings::Hud_KeybindsPosition[1], 0.f, 1080.f)) Menu::ResetSetupFlags();

					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
					Menu::HorizontalSeparatorText("Radar", FontSize::SIZE_18);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

					if (Menu::Slider("X Position##Radar", &settings::Radar_Position[0], 0.f, 1920.f)) Menu::ResetSetupFlags();
					if (Menu::Slider("Y Position##Radar", &settings::Radar_Position[1], 0.f, 1080.f)) Menu::ResetSetupFlags();
					if (Menu::Slider("Size", &settings::Radar_Size, 0.f, 500.f)) Menu::ResetSetupFlags();
					Menu::Slider("Rounding", &settings::Radar_SquareRoundness, 0.f, 100.f);
				}

				else
				{
					selectedModule = 0;
				}
			}

			else
			{
				selectedCategory = 0;
			}
			ImGui::EndGroup();
		}
		ImGui::EndChild();

		ImGui::EndGroup();
	}
	ImGui::End();
}