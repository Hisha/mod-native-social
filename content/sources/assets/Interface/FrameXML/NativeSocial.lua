-- Native Social account directory for protected WoW 3.3.5a FrameXML.
-- The server remains authoritative for membership, privacy and admin access.

local NSOC_PREFIX = "NSOC";
local NSOC_VERSION = "01";
local NSOC_DELIMITER = "\t";
local NSOC_MAX_MESSAGE_LENGTH = 254;
local NSOC_PLAYERS_BUTTON_HEIGHT = 58;

local transport = CreateFrame("Frame", "NativeSocialTransport");
transport:RegisterEvent("CHAT_MSG_ADDON");

local requestCounter = 0;
local pendingRequestId = nil;
local directory = { state = "idle", expected = 0, rows = { }, parts = { }, error = nil };
local shownRows = { };
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
	if #message > 0 and string.sub(message, -1) == NSOC_DELIMITER then
		tinsert(fields, "");
	end
	return fields;
end

local function NativeSocial_Fail(message)
	directory.state = "error";
	directory.error = message;
	pendingRequestId = nil;
	NativeSocialPlayers_Render();
end

local function NativeSocial_Finalize()
	local rows = { };
	for index = 0, directory.expected - 1 do
		local row = directory.parts[index];
		if not row then NativeSocial_Fail("Directory response is incomplete"); return; end
		if row.online and #row.presence ~= 6 then NativeSocial_Fail("Online presence is incomplete"); return; end
		if not row.online and #row.presence ~= 0 then NativeSocial_Fail("Offline entry contains presence"); return; end
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
	directory.rows = rows;
	directory.state = "complete";
	pendingRequestId = nil;
	NativeSocialPlayers_Render();
end

local function NativeSocial_HandleResponse(message)
	if not message or #message > NSOC_MAX_MESSAGE_LENGTH then return; end
	local fields = NativeSocial_Split(message);
	if #fields < 3 or fields[1] ~= NSOC_VERSION then return; end
	local command, requestId = fields[2], fields[3];
	if not pendingRequestId or requestId ~= pendingRequestId then return; end

	if command == "DIR_START" then
		if #fields ~= 4 then NativeSocial_Fail("Malformed directory start"); return; end
		local count = tonumber(fields[4]);
		if not count or count < 0 or count > 100000 then NativeSocial_Fail("Invalid directory size"); return; end
		directory.state, directory.expected, directory.parts = "listening", count, { };
		NativeSocialPlayers_Render();
	elseif command == "DIR_ENTRY" then
		if #fields < 6 or directory.state ~= "listening" then return; end
		local index, part = tonumber(fields[4]), tonumber(fields[5]);
		if not index or not part or index < 0 or index >= directory.expected then NativeSocial_Fail("Invalid directory entry"); return; end
		local row = directory.parts[index];
		if part == 0 then
			if row or #fields < 8 then NativeSocial_Fail("Malformed directory identity"); return; end
			row = { online = fields[6] == "1", accountId = tonumber(fields[7]),
				displayName = NativeSocial_Unescape(fields[8]), presence = { }, nextPart = 1 };
			if not row.accountId or row.displayName == "" then NativeSocial_Fail("Invalid directory identity"); return; end
			for i = 9, #fields do tinsert(row.presence, fields[i]); end
			directory.parts[index] = row;
		else
			if not row or part ~= row.nextPart then NativeSocial_Fail("Out-of-order directory part"); return; end
			for i = 6, #fields do tinsert(row.presence, fields[i]); end
			row.nextPart = row.nextPart + 1;
		end
	elseif command == "DIR_END" then
		if #fields ~= 3 or directory.state ~= "listening" then NativeSocial_Fail("Malformed directory end"); return; end
		NativeSocial_Finalize();
	elseif command == "ERROR" then
		NativeSocial_Fail(#fields >= 5 and NativeSocial_Unescape(fields[5]) or "Server error");
	end
end

local function NativeSocial_OnEvent(self, event, ...)
	local prefix, message = ...;
	if event ~= "CHAT_MSG_ADDON" or prefix ~= NSOC_PREFIX then return; end
	local ok = pcall(NativeSocial_HandleResponse, message);
	if not ok then NativeSocial_Fail("Malformed server response"); end
end
transport:SetScript("OnEvent", NativeSocial_OnEvent);

local function NativeSocialPlayers_SetButton(button, index, firstButton)
	local row = shownRows[index];
	local name = _G[button:GetName() .. "Name"];
	local character = _G[button:GetName() .. "Character"];
	local faction = _G[button:GetName() .. "Faction"];
	local location = _G[button:GetName() .. "Location"];
	if not row then
		name:SetText(""); character:SetText(""); faction:SetText(""); location:SetText("");
		button:Hide(); return NSOC_PLAYERS_BUTTON_HEIGHT;
	end
	button:Show();
	name:SetText((row.online and "|cff20ff20+|r " or "|cff808080-|r ") .. row.displayName);
	if row.online then
		character:SetText(row.characterName .. " - " .. (LEVEL or "Level") .. " " .. row.level .. " " ..
			(raceNames[row.race] or ("Race " .. row.race)) .. " " ..
			(classNames[row.class] or ("Class " .. row.class)));
		faction:SetText(row.faction == "H" and (HORDE or "Horde") or (ALLIANCE or "Alliance"));
		location:SetText(row.location);
	else
		character:SetText(PLAYER_OFFLINE or "Offline"); faction:SetText(""); location:SetText("");
	end
	return NSOC_PLAYERS_BUTTON_HEIGHT;
end

local function NativeSocialPlayers_GetScrollTop(offset)
	if #shownRows == 0 then return; end
	local index = math.min(math.floor(offset / NSOC_PLAYERS_BUTTON_HEIGHT) + 1, #shownRows);
	return index, (index - 1) * NSOC_PLAYERS_BUTTON_HEIGHT - offset,
		#shownRows * NSOC_PLAYERS_BUTTON_HEIGHT;
end

local function NativeSocialPlayers_EnsureCreated()
	if playersFrameCreated then return; end

	NativeSocialPlayersScrollFrame.scrollBar = NativeSocialPlayersScrollFrameScrollBar;

	DynamicScrollFrame_CreateButtons(NativeSocialPlayersScrollFrame,
		"NativeSocialPlayersButtonTemplate", NSOC_PLAYERS_BUTTON_HEIGHT,
		NativeSocialPlayers_SetButton, NativeSocialPlayers_GetScrollTop);

	playersFrameCreated = true;
end

function NativeSocialPlayers_Render()
	if not playersFrameCreated then return; end
	local status = NativeSocialPlayersPanelStatus;
	if directory.state == "requesting" or directory.state == "listening" then
		status:SetText("Loading players..."); status:Show(); shownRows = { };
	elseif directory.state == "error" then
		status:SetText("Native Social error: " .. (directory.error or "unknown")); status:Show(); shownRows = { };
	elseif directory.state == "complete" then
		shownRows = directory.rows;
		if #shownRows == 0 then status:SetText("No configured player profiles."); status:Show(); else status:Hide(); end
	end
	NativeSocialPlayersScrollFrameScrollBar:SetValue(0);
	DynamicScrollFrame_Update(NativeSocialPlayersScrollFrame);
end

function NativeSocialPlayers_Refresh()
	if pendingRequestId then return; end
	if not UnitExists("player") then return; end
	local playerName = UnitName("player");
	if not playerName or playerName == "" then return; end
	NativeSocialPlayers_EnsureCreated();
	local requestId = NativeSocial_NextRequestId();
	pendingRequestId = requestId;
	directory = { state = "requesting", expected = 0, rows = { }, parts = { }, error = nil };
	SendAddonMessage(NSOC_PREFIX,
		NSOC_VERSION .. NSOC_DELIMITER .. "DIR_LIST" .. NSOC_DELIMITER .. requestId,
		"WHISPER", playerName);
	NativeSocialPlayers_Render();
end
