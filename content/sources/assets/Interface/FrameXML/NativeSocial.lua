-- Native Social: NSOC v1 client transport for the patched 3.3.5a native UI.
--
-- Part of mod-native-social. This file is shipped into the patched FrameXML
-- through mod-content-manager (schema-1 raw files) and replaces nothing on its
-- own: it is the permanent seam that will grow into the real Players tab.
--
-- Transport: the stock 3.3.5a addon-message facility.
--   SendAddonMessage("NSOC", message, "WHISPER", UnitName("player"))
--   event CHAT_MSG_ADDON with arg1 = prefix, arg2 = message.
-- The worldserver intercepts the LANG_ADDON whisper command channel, answers
-- NSOC requests with addon-message whispers, and the client receives them here.
--
-- This is patched native FrameXML, not an addon. See docs/NSOC_PROTOCOL.md for
-- the exact wire format this transport implements.

-- ---------------------------------------------------------------------------
-- NSOC v1 constants (mirror src/NsocHandler.h and docs/NSOC_PROTOCOL.md).
-- ---------------------------------------------------------------------------
local NSOC_PREFIX = "NSOC";
local NSOC_VERSION = "01";
local NSOC_DELIMITER = "\t";
local NSOC_MAX_REQUEST_ID_LENGTH = 16;
local NSOC_MAX_MESSAGE_LENGTH = 254;

local NSOC_CMD_LIST = "LIST";
local NSOC_CMD_LIST_START = "LIST_START";
local NSOC_CMD_LIST_PROFILE = "LIST_PROFILE";
local NSOC_CMD_LIST_END = "LIST_END";
local NSOC_CMD_ERROR = "ERROR";

-- Players panel layout constants.
local NSOC_PLAYERS_BUTTON_HEIGHT = 18;

-- ---------------------------------------------------------------------------
-- Transport frame. Owns CHAT_MSG_ADDON so the transport is alive whenever the
-- client is running, independent of which FriendsFrame tab is showing.
-- ---------------------------------------------------------------------------
local transport = CreateFrame("Frame", "NativeSocialTransport");
transport:RegisterEvent("CHAT_MSG_ADDON");

-- One request in flight at a time. Every LIST request issues a fresh request id;
-- responses that carry an older id are stale and ignored.
local requestCounter = 0;
local pendingRequestId = nil;

-- Current LIST transaction state; rendering reads only this.
local listResult = {
	requestId = nil,
	state = "idle",      -- "idle", "requesting", "listening", "complete", "error"
	errors = nil,        -- formatted error detail when state == "error"
	profiles = { },      -- display names, in server order
	expectedCount = 0,
};

-- local faces of the scroll frame state, reused on every render pass.
local shownProfiles = { };
local playersFrame;

-- ---------------------------------------------------------------------------
-- Request id generation.
-- ---------------------------------------------------------------------------
local function NativeSocial_NextRequestId()
	-- Base-36 (0-9 then A-Z): every character is ASCII alphanumeric, the
	-- documented grammar. The running counter guarantees session uniqueness.
	requestCounter = requestCounter + 1;
	local value = requestCounter;
	local id = "";
	while value > 0 do
		local digit = value % 36;
		value = math.floor(value / 36);
		if ( digit < 10 ) then
			id = string.char(48 + digit) .. id;
		else
			id = string.char(65 + digit - 10) .. id;
		end
	end
	while ( #id < 4 ) do
		id = "0" .. id;
	end
	if ( #id > NSOC_MAX_REQUEST_ID_LENGTH ) then
		id = string.sub(id, #id - NSOC_MAX_REQUEST_ID_LENGTH + 1);
	end
	return id;
end

-- ---------------------------------------------------------------------------
-- Escaping: the client only ever decodes display names the server escaped
-- (backslash first, then tab). The decode below is the exact inverse of the
-- server EscapeDisplayName(); see docs/NSOC_PROTOCOL.md. Byte-oriented, so
-- UTF-8 display names pass through untouched.
-- ---------------------------------------------------------------------------
local function NativeSocial_Unescape(escaped)
	local bytes = string.len(escaped);
	local out = { };
	local i = 1;
	while ( i <= bytes ) do
		local c = string.sub(escaped, i, i);
		if ( c == "\\" and i < bytes ) then
			local next = string.sub(escaped, i + 1, i + 1);
			if ( next == "\\" ) then
				tinsert(out, "\\");
				i = i + 2;
			elseif ( next == "t" ) then
				tinsert(out, "\t");
				i = i + 2;
			else
				tinsert(out, c);
				i = i + 1;
			end
		else
			tinsert(out, c);
			i = i + 1;
		end
	end
	return table.concat(out);
end

-- ---------------------------------------------------------------------------
-- Strict message framing: tab-delimited fields, no assumptions about content.
-- ---------------------------------------------------------------------------
local function NativeSocial_Split(message)
	local fields = { };
	local cursor = 1;
	while cursor <= string.len(message) do
		local start = cursor;
		local tab = string.find(message, NSOC_DELIMITER, cursor, true);
		if ( not tab ) then
			tinsert(fields, string.sub(message, start));
			break;
		end
		tinsert(fields, string.sub(message, start, tab - 1));
		cursor = tab + 1;
	end
	return fields;
end

local function NativeSocial_FailTransaction(errorDetail)
	listResult.state = "error";
	listResult.errors = errorDetail;
	pendingRequestId = nil;
	NativeSocialPlayers_Render();
end

local function NativeSocial_HandleResponse(message)
	-- Reject anything outside the server's own payload discipline. The server
	-- never sends above the documented limit; a longer frame is not NSOC.
	if ( not message or string.len(message) > NSOC_MAX_MESSAGE_LENGTH ) then
		return;
	end

	local fields = NativeSocial_Split(message);
	if ( #fields < 3 ) then
		return;
	end
	if ( fields[1] ~= NSOC_VERSION ) then
		return;
	end

	local command = fields[2];
	local requestId = fields[3];

	-- Only ever process traffic that belongs to the request we actually sent.
	-- Stale frames (an older refresh) and hidden garbage are dropped whole.
	if ( not pendingRequestId or requestId ~= pendingRequestId ) then
		return;
	end

	if ( command == NSOC_CMD_LIST_START ) then
		if ( #fields ~= 4 ) then
			NativeSocial_FailTransaction("LIST_START with wrong field count");
			return;
		end
		local count = tonumber(fields[4]);
		if ( not count or count < 0 or count > 100000 ) then
			NativeSocial_FailTransaction("LIST_START with invalid count");
			return;
		end
		listResult.requestId = requestId;
		listResult.state = "listening";
		listResult.expectedCount = count;
		listResult.profiles = { };
		listResult.errors = nil;
		NativeSocialPlayers_Render();
	elseif ( command == NSOC_CMD_LIST_PROFILE ) then
		if ( #fields ~= 4 or listResult.state ~= "listening" ) then
			return;
		end
		local displayName = NativeSocial_Unescape(fields[4]);
		if ( displayName ~= "" ) then
			tinsert(listResult.profiles, displayName);
		end
	elseif ( command == NSOC_CMD_LIST_END ) then
		if ( #fields ~= 3 or listResult.state ~= "listening" ) then
			return;
		end
		listResult.state = "complete";
		NativeSocialPlayers_Render();
	elseif ( command == NSOC_CMD_ERROR ) then
		if ( #fields ~= 5 ) then
			NativeSocial_FailTransaction("malformed server ERROR");
			return;
		end
		listResult.state = "error";
		listResult.errors = fields[4] .. ": " .. fields[5];
		pendingRequestId = nil;
		NativeSocialPlayers_Render();
	end
end

local function NativeSocial_OnEvent(self, event, ...)
	if ( event ~= "CHAT_MSG_ADDON" ) then
		return;
	end
	-- CHAT_MSG_ADDON: arg1 = addon prefix, arg2 = message. Everything else that
	-- uses the addon-message transport must keep flowing; only NSOC is ours.
	local prefix, message = ...;
	if ( prefix ~= NSOC_PREFIX ) then
		return;
	end
	-- A malformed frame must never take the UI down with it; fail the current
	-- transaction cleanly instead of propagating a Lua error.
	local ok = pcall(NativeSocial_HandleResponse, message);
	if ( not ok ) then
		NativeSocial_FailTransaction("malformed server response");
	end
end

transport:SetScript("OnEvent", NativeSocial_OnEvent);

-- ---------------------------------------------------------------------------
-- Players list rendering (DynamicScrollFrame machinery, native 3.3.5a).
-- ---------------------------------------------------------------------------
local function NativeSocialPlayers_SetButton(button, index, firstButton)
	local name = _G[button:GetName() .. "Name"];
	local profile = shownProfiles[index];
	if ( profile ) then
		name:SetText(profile);
		name:Show();
	else
		name:SetText("");
		name:Hide();
	end
	return NSOC_PLAYERS_BUTTON_HEIGHT;
end

local function NativeSocialPlayers_GetScrollTop(offset)
	local count = #shownProfiles;
	if ( count == 0 ) then
		return;
	end
	local index = math.floor(offset / NSOC_PLAYERS_BUTTON_HEIGHT) + 1;
	if ( index > count ) then
		index = count;
	end
	local heightUsed = (index - 1) * NSOC_PLAYERS_BUTTON_HEIGHT - offset;
	return index, heightUsed, count * NSOC_PLAYERS_BUTTON_HEIGHT;
end

local function NativeSocialPlayers_ClearList()
	shownProfiles = { };
	NativeSocialPlayersScrollFrameScrollBar:SetValue(0);
	DynamicScrollFrame_Update(NativeSocialPlayersScrollFrame);
end

local function NativeSocialPlayers_EnsureCreated()
	if ( playersFrame ) then
		return;
	end
	playersFrame = true;
	DynamicScrollFrame_CreateButtons(NativeSocialPlayersScrollFrame,
		"NativeSocialPlayersButtonTemplate", NSOC_PLAYERS_BUTTON_HEIGHT,
		NativeSocialPlayers_SetButton, NativeSocialPlayers_GetScrollTop);
	-- Hide the transport's own button text baseline.
	DynamicScrollFrame_Update(NativeSocialPlayersScrollFrame);
end

function NativeSocialPlayers_Render()
	if ( not playersFrame ) then
		return;
	end
	local status = NativeSocialPlayersPanelStatus;
	local state = listResult.state;
	if ( state == "requesting" or state == "listening" ) then
		status:SetText("Requesting Native Social players from the server...");
		status:Show();
		NativeSocialPlayers_ClearList();
		return;
	end
	if ( state == "error" ) then
		local detail = listResult.errors;
		if ( detail and detail ~= "" ) then
			status:SetText("Native Social error (" .. detail .. ")");
		else
			status:SetText("Native Social server error");
		end
		status:Show();
		NativeSocialPlayers_ClearList();
		return;
	end
	if ( state == "complete" ) then
		if ( #listResult.profiles == 0 ) then
			status:SetText("No Native Social players are online and advertised right now.");
			status:Show();
			NativeSocialPlayers_ClearList();
			return;
		end
		status:Hide();
		shownProfiles = listResult.profiles;
		NativeSocialPlayersScrollFrameScrollBar:SetValue(0);
		DynamicScrollFrame_Update(NativeSocialPlayersScrollFrame);
	end
end

-- Public entry point: request the Native Social player list and put the panel
-- into the "requesting" state. Wired to the Players tab, the Refresh button and
-- FriendsFrame_Update(); every call issues a fresh request id so responses
-- cannot bleed between refreshes.
function NativeSocialPlayers_Refresh()
	if ( not UnitExists("player") ) then
		return;
	end
	local playerName = UnitName("player");
	if ( not playerName or playerName == "" ) then
		return;
	end

	NativeSocialPlayers_EnsureCreated();

	local requestId = NativeSocial_NextRequestId();
	pendingRequestId = requestId;
	listResult.requestId = requestId;
	listResult.state = "requesting";
	listResult.errors = nil;
	listResult.profiles = { };
	listResult.expectedCount = 0;

	-- Send the NSOC LIST command over the stock addon-message transport. The
	-- exact wire frame is: NSOC\t01\tLIST\t<request_id>.
	SendAddonMessage(NSOC_PREFIX,
		NSOC_VERSION .. NSOC_DELIMITER .. NSOC_CMD_LIST .. NSOC_DELIMITER .. requestId,
		"WHISPER", playerName);

	NativeSocialPlayers_Render();
end