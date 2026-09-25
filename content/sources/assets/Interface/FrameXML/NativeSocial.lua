-- Native Social account directory and profile management for protected WoW 3.3.5a FrameXML.
-- The server remains authoritative for identity, membership, privacy and admin access.

local NSOC_PREFIX = "NSOC";
local NSOC_VERSION = "01";
local NSOC_DELIMITER = "\t";
local NSOC_MAX_MESSAGE_LENGTH = 254;
local NSOC_PLAYERS_BUTTON_HEIGHT = 58;
local NSOC_ADMIN_BUTTON_HEIGHT = 24;

local transport = CreateFrame("Frame", "NativeSocialTransport");
transport:RegisterEvent("CHAT_MSG_ADDON");

local requestCounter = 0;
local pending = nil;
local currentView = "directory";
local directory = { state = "idle", expected = 0, rows = { }, parts = { }, error = nil };
local profile = { state = "idle", configured = false, displayName = "", appearOffline = false, status = nil };
local admin = { capsKnown = false, authorized = false, state = "idle", expected = 0,
	rows = { }, status = nil, selectedId = nil, afterSave = false };
local shownRows = { };
local adminShownRows = { };
local playersFrameCreated = false;

local raceNames = {
	[1] = RACE_HUMAN or "Human", [2] = RACE_ORC or "Orc",
	[3] = RACE_DWARF or "Dwarf", [4] = RACE_NIGHTELF or "Night Elf",
	[5] = RACE_SCOURGE or "Undead", [6] = RACE_TAUREN or "Tauren",
	[7] = RACE_GNOME or "Gnome", [8] = RACE_TROLL or "Troll",
	[10] = RACE_BLOODELF or "Blood Elf", [11] = RACE_DRAENEI or "Draenei",
};

local classNames = {
	[1] = CLASS_WARRIOR or "Warrior", [2] = CLASS_PALADIN or "Paladin",
	[3] = CLASS_HUNTER or "Hunter", [4] = CLASS_ROGUE or "Rogue",
	[5] = CLASS_PRIEST or "Priest", [6] = CLASS_DEATHKNIGHT or "Death Knight",
	[7] = CLASS_SHAMAN or "Shaman", [8] = CLASS_MAGE or "Mage",
	[9] = CLASS_WARLOCK or "Warlock", [11] = CLASS_DRUID or "Druid",
};

local function NativeSocial_NextRequestId()
	requestCounter = requestCounter + 1;
	local value = requestCounter;
	local id = "";
	while value > 0 do
		local digit = value % 36;
		value = math.floor(value / 36);
		id = string.char(digit < 10 and (48 + digit) or (55 + digit)) .. id;
	end
	while #id < 4 do id = "0" .. id; end
	return id;
end

local function NativeSocial_Escape(value)
	value = string.gsub(value or "", "\\", "\\\\");
	value = string.gsub(value, "\t", "\\t");
	-- string.gsub also returns a replacement count. Return the escaped value
	-- alone so a final NSOC argument cannot silently add an extra field.
	return value;
end

local function NativeSocial_Unescape(value)
	local out = { };
	local i = 1;
	while i <= #value do
		local c = string.sub(value, i, i);
		local n = i < #value and string.sub(value, i + 1, i + 1) or nil;
		if c == "\\" and n == "\\" then
			tinsert(out, "\\"); i = i + 2;
		elseif c == "\\" and n == "t" then
			tinsert(out, "\t"); i = i + 2;
		else
			tinsert(out, c); i = i + 1;
		end
	end
	return table.concat(out);
end

local function NativeSocial_Split(message)
	local fields = { };
	local cursor = 1;
	while cursor <= #message do
		local tab = string.find(message, NSOC_DELIMITER, cursor, true);
		if not tab then tinsert(fields, string.sub(message, cursor)); break; end
		tinsert(fields, string.sub(message, cursor, tab - 1));
		cursor = tab + 1;
	end
	if #message > 0 and string.sub(message, -1) == NSOC_DELIMITER then tinsert(fields, ""); end
	return fields;
end

local function NativeSocial_SetStatus(kind, message)
	if kind == "directory" then
		directory.state, directory.error = "error", message;
	elseif kind == "profile" or kind == "profileSave" then
		profile.state, profile.status = "error", message;
	elseif kind == "admin" or kind == "adminSave" then
		admin.state, admin.status = "error", message;
	elseif kind == "caps" then
		admin.capsKnown, admin.authorized = true, false;
	end
end

local function NativeSocial_ShowWhisperError(message)
	if UIErrorsFrame then
		UIErrorsFrame:AddMessage(message or "Unable to open whisper.", 1.0, 0.1, 0.1, 1.0);
	elseif DEFAULT_CHAT_FRAME then
		DEFAULT_CHAT_FRAME:AddMessage(message or "Unable to open whisper.");
	end
end

local function NativeSocial_SetShown(frame, shown)
	if shown then frame:Show(); else frame:Hide(); end
end

local function NativeSocial_LayoutNavigation()
	local directoryButton = NativeSocialPlayersPanelDirectoryButton;
	local profileButton = NativeSocialPlayersPanelProfileButton;
	local adminButton = NativeSocialPlayersPanelAdminButton;
	local refreshButton = NativeSocialPlayersPanelRefresh;
	local allButtons = { directoryButton, profileButton, adminButton, refreshButton };
	for _, button in ipairs(allButtons) do
		button:Hide();
		button:ClearAllPoints();
	end

	local visible = { };
	if currentView == "directory" then
		tinsert(visible, profileButton);
		if admin.authorized then tinsert(visible, adminButton); end
		tinsert(visible, refreshButton);
	elseif currentView == "profile" then
		tinsert(visible, directoryButton);
		if admin.authorized then tinsert(visible, adminButton); end
	elseif currentView == "admin" then
		tinsert(visible, directoryButton);
		tinsert(visible, profileButton);
	end

	local buttonToRight = nil;
	for index = #visible, 1, -1 do
		local button = visible[index];
		if buttonToRight then
			button:SetPoint("RIGHT", buttonToRight, "LEFT", -4, 0);
		else
			button:SetPoint("TOPRIGHT", NativeSocialPlayersPanel, "TOPRIGHT", -45, -38);
		end
		button:Show();
		buttonToRight = button;
	end
end

local function NativeSocial_ProfileDraftIsDirty()
	if not playersFrameCreated or profile.state == "idle" or profile.state == "requesting" then return false; end
	local draftName = NativeSocialPlayersPanelProfileName:GetText() or "";
	local draftOffline = NativeSocialPlayersPanelProfileAppearOffline:GetChecked() and true or false;
	return draftName ~= profile.displayName or draftOffline ~= profile.appearOffline;
end

local function NativeSocial_PreserveProfileDraftNotice()
	if currentView ~= "profile" or not NativeSocial_ProfileDraftIsDirty() then return; end
	local notice = "Unsaved profile edits are preserved until you return.";
	if not profile.status or profile.status == "" then
		profile.status = notice;
	elseif not string.find(profile.status, notice, 1, true) then
		profile.status = profile.status .. " " .. notice;
	end
end

local function NativeSocial_Send(kind, command, ...)
	if pending then return false; end
	if not UnitExists("player") then return false; end
	local playerName = UnitName("player");
	if not playerName or playerName == "" then return false; end
	local requestId = NativeSocial_NextRequestId();
	local fields = { NSOC_VERSION, command, requestId, ... };
	local payload = table.concat(fields, NSOC_DELIMITER);
	if #payload + #NSOC_PREFIX + 1 > NSOC_MAX_MESSAGE_LENGTH then
		NativeSocial_SetStatus(kind, "Request is too long.");
		NativeSocialPlayers_Render();
		return false;
	end
	pending = { id = requestId, kind = kind };
	SendAddonMessage(NSOC_PREFIX, payload, "WHISPER", playerName);
	return true;
end

local function NativeSocialPlayers_SetButton(button, index, firstButton)
	local row = shownRows[index];
	local name = _G[button:GetName() .. "Name"];
	local character = _G[button:GetName() .. "Character"];
	local faction = _G[button:GetName() .. "Faction"];
	local location = _G[button:GetName() .. "Location"];
	local whisper = _G[button:GetName() .. "Whisper"];
	if not row then
		name:SetText(""); character:SetText(""); faction:SetText(""); location:SetText("");
		button.accountId = nil; whisper:Hide();
		button:Hide(); return NSOC_PLAYERS_BUTTON_HEIGHT;
	end
	button.accountId = row.accountId;
	button:Show();
	name:SetText((row.online and "|cff20ff20+|r " or "|cff808080-|r ") .. row.displayName);
	if row.online then
		character:SetText(row.characterName .. " - " .. (LEVEL or "Level") .. " " .. row.level .. " " ..
			(raceNames[row.race] or ("Race " .. row.race)) .. " " ..
			(classNames[row.class] or ("Class " .. row.class)));
		faction:SetText(row.faction == "H" and (HORDE or "Horde") or (ALLIANCE or "Alliance"));
		location:SetText(row.location);
		whisper:Show();
	else
		character:SetText(PLAYER_OFFLINE or "Offline"); faction:SetText(""); location:SetText(""); whisper:Hide();
	end
	return NSOC_PLAYERS_BUTTON_HEIGHT;
end

local function NativeSocialPlayers_GetScrollTop(offset)
	if #shownRows == 0 then return; end
	local index = math.min(math.floor(offset / NSOC_PLAYERS_BUTTON_HEIGHT) + 1, #shownRows);
	return index, (index - 1) * NSOC_PLAYERS_BUTTON_HEIGHT - offset,
		#shownRows * NSOC_PLAYERS_BUTTON_HEIGHT;
end

local function NativeSocialAdmin_SetButton(button, index, firstButton)
	local row = adminShownRows[index];
	local account = _G[button:GetName() .. "Account"];
	local name = _G[button:GetName() .. "Name"];
	if not row then
		button.accountId = nil; account:SetText(""); name:SetText("");
		button:Hide(); return NSOC_ADMIN_BUTTON_HEIGHT;
	end
	button.accountId = row.accountId;
	button:Show();
	account:SetText("Account " .. row.accountId);
	name:SetText(row.configured and row.displayName or "Not configured");
	if row.accountId == admin.selectedId then button:LockHighlight(); else button:UnlockHighlight(); end
	return NSOC_ADMIN_BUTTON_HEIGHT;
end

local function NativeSocialAdmin_GetScrollTop(offset)
	if #adminShownRows == 0 then return; end
	local index = math.min(math.floor(offset / NSOC_ADMIN_BUTTON_HEIGHT) + 1, #adminShownRows);
	return index, (index - 1) * NSOC_ADMIN_BUTTON_HEIGHT - offset,
		#adminShownRows * NSOC_ADMIN_BUTTON_HEIGHT;
end

local function NativeSocialPlayers_EnsureCreated()
	if playersFrameCreated then return; end
	NativeSocialPlayersScrollFrame.scrollBar = NativeSocialPlayersScrollFrameScrollBar;
	DynamicScrollFrame_CreateButtons(NativeSocialPlayersScrollFrame,
		"NativeSocialPlayersButtonTemplate", NSOC_PLAYERS_BUTTON_HEIGHT,
		NativeSocialPlayers_SetButton, NativeSocialPlayers_GetScrollTop);
	NativeSocialPlayersPanelAdminScrollFrame.scrollBar = NativeSocialPlayersPanelAdminScrollFrameScrollBar;
	DynamicScrollFrame_CreateButtons(NativeSocialPlayersPanelAdminScrollFrame,
		"NativeSocialAdminButtonTemplate", NSOC_ADMIN_BUTTON_HEIGHT,
		NativeSocialAdmin_SetButton, NativeSocialAdmin_GetScrollTop);
	_G[NativeSocialPlayersPanelProfileAppearOffline:GetName() .. "Text"]:SetText("Appear Offline");
	playersFrameCreated = true;
end

function NativeSocialPlayers_Render()
	if not playersFrameCreated then return; end
	NativeSocial_LayoutNavigation();
	local directoryVisible = currentView == "directory";
	NativeSocial_SetShown(NativeSocialPlayersScrollFrame, directoryVisible);
	NativeSocial_SetShown(NativeSocialPlayersPanelStatus, directoryVisible);
	NativeSocial_SetShown(NativeSocialPlayersPanelProfile, currentView == "profile");
	NativeSocial_SetShown(NativeSocialPlayersPanelAdmin, currentView == "admin");

	if directoryVisible then
		local status = NativeSocialPlayersPanelStatus;
		if directory.state == "requesting" or directory.state == "listening" then
			status:SetText("Loading players..."); status:Show(); shownRows = { };
		elseif directory.state == "error" then
			status:SetText("Native Social error: " .. (directory.error or "unknown")); status:Show(); shownRows = { };
		elseif directory.state == "complete" then
			shownRows = directory.rows;
			if #shownRows == 0 then status:SetText("No configured player profiles."); status:Show(); else status:Hide(); end
		else
			status:SetText("Select Refresh to load players."); status:Show(); shownRows = { };
		end
		NativeSocialPlayersScrollFrameScrollBar:SetValue(0);
		DynamicScrollFrame_Update(NativeSocialPlayersScrollFrame);
	end

	if currentView == "profile" then
		local current = profile.configured and profile.displayName or "Not configured";
		NativeSocialPlayersPanelProfileCurrent:SetText("Current display name: " .. current);
		NativeSocialPlayersPanelProfileStatus:SetText(profile.status or "");
	end

	if currentView == "admin" then
		adminShownRows = admin.rows;
		NativeSocialPlayersPanelAdminStatus:SetText(admin.status or "");
		NativeSocialPlayersPanelAdminScrollFrameScrollBar:SetValue(0);
		DynamicScrollFrame_Update(NativeSocialPlayersPanelAdminScrollFrame);
	end
end

local function NativeSocial_RequestDirectory()
	if pending then return false; end
	directory = { state = "requesting", expected = 0, rows = { }, parts = { }, error = nil };
	if not NativeSocial_Send("directory", "DIR_LIST") then return false; end
	NativeSocialPlayers_Render();
	return true;
end

local function NativeSocial_RequestCaps()
	if admin.capsKnown then return; end
	NativeSocial_Send("caps", "ADMIN_CAPS");
end

local function NativeSocial_RequestProfile()
	if pending then return; end
	profile.state, profile.status = "requesting", "Loading profile...";
	if NativeSocial_Send("profile", "PROFILE_GET") then NativeSocialPlayers_Render(); end
end

local function NativeSocial_RequestAdminList(preserveStatus)
	if pending then return; end
	admin.state, admin.expected, admin.rows = "requesting", 0, { };
	if not preserveStatus then admin.status = "Loading eligible accounts..."; end
	if NativeSocial_Send("admin", "ADMIN_LIST") then NativeSocialPlayers_Render(); end
end

local function NativeSocial_FinalizeDirectory()
	local rows = { };
	for index = 0, directory.expected - 1 do
		local row = directory.parts[index];
		if not row then NativeSocial_SetStatus("directory", "Directory response is incomplete"); return false; end
		if row.online and #row.presence ~= 6 then NativeSocial_SetStatus("directory", "Online presence is incomplete"); return false; end
		if not row.online and #row.presence ~= 0 then NativeSocial_SetStatus("directory", "Offline entry contains presence"); return false; end
		if row.online then
			row.characterName = NativeSocial_Unescape(row.presence[1]);
			row.level = tonumber(row.presence[2]) or 0;
			row.race = tonumber(row.presence[3]) or 0;
			row.class = tonumber(row.presence[4]) or 0;
			row.faction = row.presence[5];
			row.location = NativeSocial_Unescape(row.presence[6]);
		end
		tinsert(rows, row);
	end
	directory.rows, directory.state = rows, "complete";
	return true;
end

local function NativeSocial_HandleDirectory(command, fields)
	if command == "DIR_START" then
		if #fields ~= 4 then return false, "Malformed directory start"; end
		local count = tonumber(fields[4]);
		if not count or count < 0 or count > 100000 then return false, "Invalid directory size"; end
		directory.state, directory.expected, directory.parts = "listening", count, { };
	elseif command == "DIR_ENTRY" then
		if #fields < 6 or directory.state ~= "listening" then return false, "Unexpected directory entry"; end
		local index, part = tonumber(fields[4]), tonumber(fields[5]);
		if not index or not part or index < 0 or index >= directory.expected then return false, "Invalid directory entry"; end
		local row = directory.parts[index];
		if part == 0 then
			if row or #fields < 8 then return false, "Malformed directory identity"; end
			row = { online = fields[6] == "1", accountId = tonumber(fields[7]),
				displayName = NativeSocial_Unescape(fields[8]), presence = { }, nextPart = 1 };
			if not row.accountId or row.displayName == "" then return false, "Invalid directory identity"; end
			for i = 9, #fields do tinsert(row.presence, fields[i]); end
			directory.parts[index] = row;
		else
			if not row or part ~= row.nextPart then return false, "Out-of-order directory part"; end
			for i = 6, #fields do tinsert(row.presence, fields[i]); end
			row.nextPart = row.nextPart + 1;
		end
	elseif command == "DIR_END" then
		if #fields ~= 3 or directory.state ~= "listening" then return false, "Malformed directory end"; end
		if not NativeSocial_FinalizeDirectory() then return false, directory.error; end
		pending = nil;
		NativeSocialPlayers_Render();
		NativeSocial_RequestCaps();
		return true;
	else
		return false, "Unexpected directory response";
	end
	NativeSocialPlayers_Render();
	return true;
end

local function NativeSocial_HandleProfile(command, fields)
	if command == "PROFILE_RESULT" then
		if #fields ~= 6 or (fields[4] ~= "0" and fields[4] ~= "1") or
			(fields[6] ~= "0" and fields[6] ~= "1") then return false, "Malformed profile response"; end
		profile.configured = fields[4] == "1";
		profile.displayName = NativeSocial_Unescape(fields[5]);
		profile.appearOffline = fields[6] == "1";
		profile.state, profile.status = "complete", nil;
	elseif command == "PROFILE_SAVE_RESULT" then
		if #fields ~= 8 or (fields[6] ~= "0" and fields[6] ~= "1") or
			(fields[8] ~= "0" and fields[8] ~= "1") then return false, "Malformed profile save response"; end
		profile.configured = fields[6] == "1";
		profile.displayName = NativeSocial_Unescape(fields[7]);
		profile.appearOffline = fields[8] == "1";
		local saved = fields[4] == "accepted";
		profile.state = saved and "complete" or "error";
		profile.status = NativeSocial_Unescape(fields[5]);
	else
		return false, "Unexpected profile response";
	end
	pending = nil;
	if command == "PROFILE_SAVE_RESULT" and fields[4] ~= "accepted" then
		NativeSocialPlayersPanelProfileName:SetText(profile.draftName or profile.displayName);
		NativeSocialPlayersPanelProfileAppearOffline:SetChecked(profile.draftOffline and true or false);
	else
		NativeSocialPlayersPanelProfileName:SetText(profile.displayName);
		NativeSocialPlayersPanelProfileAppearOffline:SetChecked(profile.appearOffline);
	end
	profile.draftName, profile.draftOffline = nil, nil;
	NativeSocialPlayers_Render();
	if command == "PROFILE_SAVE_RESULT" and fields[4] == "accepted" then NativeSocial_RequestDirectory(); end
	return true;
end

local function NativeSocial_HandleWhisper(command, fields)
	if command ~= "WHISPER_TARGET" or #fields ~= 5 or
		(fields[4] ~= "0" and fields[4] ~= "1") then
		return false, "Malformed whisper target response";
	end
	local available = fields[4] == "1";
	local value = NativeSocial_Unescape(fields[5]);
	pending = nil;
	if available then
		if value == "" or not ChatFrame_SendTell then
			NativeSocial_ShowWhisperError("Unable to open whisper.");
		else
			ChatFrame_SendTell(value);
		end
	else
		NativeSocial_ShowWhisperError(value ~= "" and value or "That player is no longer available to whisper.");
	end
	return true;
end

local function NativeSocial_HandleAdmin(command, fields)
	if command == "ADMIN_CAPS_RESULT" then
		if #fields ~= 4 or (fields[4] ~= "0" and fields[4] ~= "1") then return false, "Malformed capability response"; end
		admin.capsKnown, admin.authorized = true, fields[4] == "1";
		pending = nil;
		NativeSocialPlayers_Render();
		return true;
	elseif command == "ADMIN_START" then
		if #fields ~= 4 then return false, "Malformed administrator list start"; end
		local count = tonumber(fields[4]);
		if not count or count < 0 or count > 100000 then return false, "Invalid administrator list size"; end
		admin.state, admin.expected, admin.rows = "listening", count, { };
	elseif command == "ADMIN_ENTRY" then
		if #fields ~= 6 or admin.state ~= "listening" then return false, "Malformed administrator entry"; end
		local accountId = tonumber(fields[4]);
		if not accountId or accountId <= 0 or (fields[5] ~= "0" and fields[5] ~= "1") then return false, "Invalid administrator entry"; end
		tinsert(admin.rows, { accountId = accountId, configured = fields[5] == "1",
			displayName = NativeSocial_Unescape(fields[6]) });
	elseif command == "ADMIN_END" then
		if #fields ~= 3 or admin.state ~= "listening" or #admin.rows ~= admin.expected then return false, "Incomplete administrator list"; end
		admin.state = "complete";
		if not admin.status or admin.status == "Loading eligible accounts..." then admin.status = #admin.rows .. " eligible account(s)."; end
		local refreshDirectory = admin.afterSave;
		admin.afterSave = false;
		pending = nil;
		NativeSocialPlayers_Render();
		if refreshDirectory then NativeSocial_RequestDirectory(); end
		return true;
	elseif command == "ADMIN_RESULT" then
		if #fields ~= 6 then return false, "Malformed administrator result"; end
		admin.status = NativeSocial_Unescape(fields[6]);
		pending = nil;
		if fields[4] == "SUCCESS" then
			-- Clear only after the server confirms persistence. The refreshed list
			-- keeps admin.selectedId highlighted and supplies the updated row.
			NativeSocialPlayersPanelAdminName:SetText("");
			admin.afterSave = true;
			NativeSocial_RequestAdminList(true);
		else
			admin.state = "error";
			NativeSocialPlayers_Render();
		end
		return true;
	else
		return false, "Unexpected administrator response";
	end
	NativeSocialPlayers_Render();
	return true;
end

local function NativeSocial_HandleResponse(message)
	if not message or #message > NSOC_MAX_MESSAGE_LENGTH then return; end
	local fields = NativeSocial_Split(message);
	if #fields < 3 or fields[1] ~= NSOC_VERSION then return; end
	local command, requestId = fields[2], fields[3];
	if not pending or requestId ~= pending.id then return; end
	if command == "ERROR" then
		local kind = pending.kind;
		pending = nil;
		local message = #fields >= 5 and NativeSocial_Unescape(fields[5]) or "Server error";
		if kind == "whisper" then NativeSocial_ShowWhisperError(message); else NativeSocial_SetStatus(kind, message); end
		NativeSocialPlayers_Render();
		return;
	end
	local ok, detail;
	if pending.kind == "directory" then
		ok, detail = NativeSocial_HandleDirectory(command, fields);
	elseif pending.kind == "whisper" then
		ok, detail = NativeSocial_HandleWhisper(command, fields);
	elseif pending.kind == "profile" or pending.kind == "profileSave" then
		ok, detail = NativeSocial_HandleProfile(command, fields);
	elseif pending.kind == "caps" or pending.kind == "admin" or pending.kind == "adminSave" then
		ok, detail = NativeSocial_HandleAdmin(command, fields);
	end
	if not ok then
		local kind = pending and pending.kind or "directory";
		pending = nil;
		local message = detail or "Malformed server response";
		if kind == "whisper" then NativeSocial_ShowWhisperError(message); else NativeSocial_SetStatus(kind, message); end
		NativeSocialPlayers_Render();
	end
end

local function NativeSocial_OnEvent(self, event, ...)
	local prefix, message = ...;
	if event ~= "CHAT_MSG_ADDON" or prefix ~= NSOC_PREFIX then return; end
	local ok = pcall(NativeSocial_HandleResponse, message);
	if not ok then
		local kind = pending and pending.kind or "directory";
		pending = nil;
		if kind == "whisper" then
			NativeSocial_ShowWhisperError("Malformed server response");
		else
			NativeSocial_SetStatus(kind, "Malformed server response");
		end
		NativeSocialPlayers_Render();
	end
end
transport:SetScript("OnEvent", NativeSocial_OnEvent);

function NativeSocialPlayers_ShowDirectory()
	NativeSocialPlayers_EnsureCreated();
	NativeSocial_PreserveProfileDraftNotice();
	currentView = "directory";
	NativeSocialPlayers_Render();
	if directory.state == "idle" then NativeSocial_RequestDirectory(); end
end

function NativeSocialPlayers_ShowProfile()
	NativeSocialPlayers_EnsureCreated();
	currentView = "profile";
	NativeSocialPlayers_Render();
	if profile.state == "idle" then NativeSocial_RequestProfile(); end
end

function NativeSocialPlayers_ShowAdmin()
	NativeSocialPlayers_EnsureCreated();
	if not admin.authorized then return; end
	NativeSocial_PreserveProfileDraftNotice();
	currentView = "admin";
	NativeSocialPlayers_Render();
	NativeSocial_RequestAdminList(false);
end

function NativeSocialPlayers_Refresh()
	NativeSocialPlayers_EnsureCreated();
	if directory.state == "idle" then currentView = "directory"; end
	NativeSocial_RequestDirectory();
end

function NativeSocialPlayers_Whisper(accountId)
	if pending or not accountId then return; end
	local online = false;
	for _, row in ipairs(directory.rows) do
		if row.accountId == accountId and row.online then online = true; break; end
	end
	if not online then
		NativeSocial_ShowWhisperError("That player is no longer available to whisper.");
		return;
	end
	NativeSocial_Send("whisper", "WHISPER_RESOLVE", tostring(accountId));
end

function NativeSocialProfile_Save()
	if pending then return; end
	local displayName = NativeSocialPlayersPanelProfileName:GetText() or "";
	local appearOffline = NativeSocialPlayersPanelProfileAppearOffline:GetChecked() and "1" or "0";
	profile.draftName, profile.draftOffline = displayName, appearOffline == "1";
	profile.state, profile.status = "requesting", "Saving profile...";
	if NativeSocial_Send("profileSave", "PROFILE_SAVE", NativeSocial_Escape(displayName), appearOffline) then NativeSocialPlayers_Render(); end
end

function NativeSocialAdmin_Select(accountId)
	if not accountId then return; end
	admin.selectedId = accountId;
	for _, row in ipairs(admin.rows) do
		if row.accountId == accountId then
			NativeSocialPlayersPanelAdminSelected:SetText("Selected account: " .. accountId);
			NativeSocialPlayersPanelAdminName:SetText(row.displayName or "");
			break;
		end
	end
	NativeSocialPlayers_Render();
end

function NativeSocialAdmin_SaveName()
	if pending or not admin.authorized or not admin.selectedId then
		admin.status = "Select an eligible account first.";
		NativeSocialPlayers_Render();
		return;
	end
	local displayName = NativeSocialPlayersPanelAdminName:GetText() or "";
	admin.status = "Saving display name...";
	if NativeSocial_Send("adminSave", "ADMIN_SET_NAME", tostring(admin.selectedId), NativeSocial_Escape(displayName)) then
		NativeSocialPlayers_Render();
	end
end
