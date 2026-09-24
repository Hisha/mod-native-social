#include "NsocHandler.h"

#include "Chat.h"
#include "Log.h"
#include "NsocCodec.h"
#include "Player.h"
#include "SocialAdmin.h"
#include "SocialDirectory.h"
#include "SocialService.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace nativesocial
{
namespace
{
namespace command
{
char constexpr List[] = "LIST";
char constexpr ListStart[] = "LIST_START";
char constexpr ListProfile[] = "LIST_PROFILE";
char constexpr ListEnd[] = "LIST_END";
char constexpr DirectoryList[] = "DIR_LIST";
char constexpr DirectoryStart[] = "DIR_START";
char constexpr DirectoryEntry[] = "DIR_ENTRY";
char constexpr DirectoryEnd[] = "DIR_END";
char constexpr AdminCaps[] = "ADMIN_CAPS";
char constexpr AdminCapsResult[] = "ADMIN_CAPS_RESULT";
char constexpr AdminList[] = "ADMIN_LIST";
char constexpr AdminStart[] = "ADMIN_START";
char constexpr AdminEntry[] = "ADMIN_ENTRY";
char constexpr AdminEnd[] = "ADMIN_END";
char constexpr AdminSetName[] = "ADMIN_SET_NAME";
char constexpr AdminCreate[] = "ADMIN_CREATE_ACCOUNT";
char constexpr AdminResult[] = "ADMIN_RESULT";
char constexpr Error[] = "ERROR";
}

bool ValidRequestId(std::string const& value)
{
    return !value.empty() && value.size() <= nsoch::MaxRequestIdLength &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}

bool ParseAccountId(std::string const& value, std::uint32_t& out)
{
    if (value.empty() || value.size() > 10)
        return false;
    std::uint64_t parsed = 0;
    for (unsigned char c : value)
    {
        if (!std::isdigit(c))
            return false;
        parsed = parsed * 10 + (c - '0');
        if (parsed > std::numeric_limits<std::uint32_t>::max())
            return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return out != 0;
}

std::string ErrorFrame(std::string const& requestId, std::uint16_t code, std::string const& detail)
{
    char codeBuffer[5];
    std::snprintf(codeBuffer, sizeof(codeBuffer), "%04X", code);
    return nsocc::Frame(command::Error, { requestId, codeBuffer, nsocc::Escape(detail) });
}

void SendError(WorldSession* session, std::string const& requestId,
    std::uint16_t code, std::string const& detail)
{
    NsocHandler::SendResponse(session, ErrorFrame(requestId, code, detail));
}

bool ServiceReady(WorldSession* session, std::string const& requestId)
{
    if (SocialService::Instance().IsInitialized() && SocialService::Instance().Available())
        return true;
    SendError(session, requestId, nsoch::ErrorUnavailable, "Module unavailable");
    return false;
}

void SendLegacyList(WorldSession* session, std::string const& requestId)
{
    auto const profiles = SocialService::Instance().ListAdvertisedOnline();
    NsocHandler::SendResponse(session, nsocc::Frame(command::ListStart,
        { requestId, std::to_string(profiles.size()) }));
    for (SocialProfile const& profile : profiles)
        NsocHandler::SendResponse(session, nsocc::Frame(command::ListProfile,
            { requestId, nsocc::Escape(profile.displayName) }));
    NsocHandler::SendResponse(session, nsocc::Frame(command::ListEnd, { requestId }));
}

void SendDirectory(WorldSession* session, std::string const& requestId)
{
    auto const entries = SocialService::Instance().BuildPublicDirectory(session);
    NsocHandler::SendResponse(session, nsocc::Frame(command::DirectoryStart,
        { requestId, std::to_string(entries.size()) }));
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        auto identity = DirectoryEntryIdentityFields(entries[i]);
        auto presence = DirectoryEntryPresenceFields(entries[i]);
        identity[2] = nsocc::Escape(identity[2]);
        if (!presence.empty())
        {
            presence[0] = nsocc::Escape(presence[0]);
            presence.back() = nsocc::Escape(presence.back());
        }
        auto const frames = nsocc::EncodeEntryFrames(command::DirectoryEntry,
            requestId, static_cast<std::uint32_t>(i), identity, presence);
        if (frames.empty())
        {
            SendError(session, requestId, nsoch::ErrorLength, "Directory entry exceeds transport budget");
            return;
        }
        for (std::string const& frame : frames)
            NsocHandler::SendResponse(session, frame);
    }
    NsocHandler::SendResponse(session, nsocc::Frame(command::DirectoryEnd, { requestId }));
}

void SendAdminProfiles(WorldSession* session, std::string const& requestId)
{
    SocialService& service = SocialService::Instance();
    if (!service.IsAuthorizedAdmin(session))
    {
        SendError(session, requestId, nsoch::ErrorUnauthorized, "Administrator authorization required");
        return;
    }
    auto const states = service.ListAdminProfiles(session);
    NsocHandler::SendResponse(session, nsocc::Frame(command::AdminStart,
        { requestId, std::to_string(states.size()) }));
    for (auto const& state : states)
        NsocHandler::SendResponse(session, nsocc::Frame(command::AdminEntry,
            { requestId, std::to_string(state.accountId), state.configured ? "1" : "0",
              nsocc::Escape(state.displayName) }));
    NsocHandler::SendResponse(session, nsocc::Frame(command::AdminEnd, { requestId }));
}

void SendAdminResult(WorldSession* session, std::string const& requestId, AdminResult const& result)
{
    NsocHandler::SendResponse(session, nsocc::Frame(command::AdminResult,
        { requestId, AdminResultCodeName(result.code), std::to_string(result.accountId),
          nsocc::Escape(result.message) }));
}
}

bool NsocHandler::ParseRequest(WorldSession* session, std::string const& message)
{
    if (message.size() > nsocc::MaxMessageLength)
        return true;
    std::vector<std::string> fields = nsocc::Split(message);
    if (fields.size() < 4 || fields[0] != nsocc::Prefix || fields[1] != nsocc::Version)
        return true;

    std::string const& requestId = fields[3];
    if (!ValidRequestId(requestId))
    {
        SendError(session, "0000", nsoch::ErrorProtocol, "Invalid request ID");
        return true;
    }
    if (!ServiceReady(session, requestId))
        return true;

    std::string const& cmd = fields[2];
    if (cmd == command::List && fields.size() == 4)
        SendLegacyList(session, requestId);
    else if (cmd == command::DirectoryList && fields.size() == 4)
        SendDirectory(session, requestId);
    else if (cmd == command::AdminCaps && fields.size() == 4)
        SendResponse(session, nsocc::Frame(command::AdminCapsResult,
            { requestId, SocialService::Instance().IsAuthorizedAdmin(session) ? "1" : "0" }));
    else if (cmd == command::AdminList && fields.size() == 4)
        SendAdminProfiles(session, requestId);
    else if (cmd == command::AdminSetName && fields.size() == 6)
    {
        if (!SocialService::Instance().IsAuthorizedAdmin(session))
        {
            SendError(session, requestId, nsoch::ErrorUnauthorized, "Administrator authorization required");
            return true;
        }
        std::uint32_t accountId = 0;
        if (!ParseAccountId(fields[4], accountId))
        {
            SendError(session, requestId, nsoch::ErrorValidation, "Invalid account ID");
            return true;
        }
        NameResult const result = SocialService::Instance().AdminSetDisplayName(
            session, accountId, nsocc::Unescape(fields[5]));
        AdminResult response;
        response.code = result == NameResult::Ok ? AdminResultCode::Success :
            (result == NameResult::AlreadyTaken ? AdminResultCode::DisplayNameTaken :
             AdminResultCode::ProfileSetupFailed);
        response.accountId = accountId;
        response.message = result == NameResult::Ok ? "Display name saved" :
            "Display name " + NameResultToString(result);
        SendAdminResult(session, requestId, response);
    }
    else if (cmd == command::AdminCreate && fields.size() == 7)
    {
        std::string const accountName = nsocc::Unescape(fields[4]);
        std::string password = nsocc::Unescape(fields[5]);
        std::string const displayName = nsocc::Unescape(fields[6]);
        fields[5].assign(fields[5].size(), '\0');
        AdminResult const result = SocialService::Instance().AdminCreateAccount(
            session, accountName, std::move(password), displayName);
        SendAdminResult(session, requestId, result);
    }
    else
        SendError(session, requestId,
            (cmd == command::List || cmd == command::DirectoryList || cmd == command::AdminCaps ||
             cmd == command::AdminList || cmd == command::AdminSetName || cmd == command::AdminCreate)
                ? nsoch::ErrorFields : nsoch::ErrorCommand,
            "Unknown command or wrong field count");
    return true;
}

bool NsocHandler::HandleRequest(WorldSession* session, std::string& message)
{
    if (message.size() < 5 || message.compare(0, 4, nsocc::Prefix) != 0 ||
        message[4] != nsocc::Delimiter)
        return false;
    bool const sensitive = message.find("\tADMIN_CREATE_ACCOUNT\t") != std::string::npos;
    auto consume = [&](bool result)
    {
        if (sensitive)
            message.assign(message.size(), '\0');
        return result;
    };
    if (message.size() > nsocc::MaxMessageLength)
    {
        SendError(session, "0000", nsoch::ErrorLength, "Request exceeds transport budget");
        return consume(true);
    }
    if (!nsocc::IsNsoc(message))
    {
        SendError(session, "0000", nsoch::ErrorVersion, "Unsupported NSOC protocol version");
        return consume(true);
    }
    // Raw frames are never logged: ADMIN_CREATE_ACCOUNT may contain a password.
    bool const consumed = ParseRequest(session, message);
    return consume(consumed);
}

void NsocHandler::SendResponse(WorldSession* session, std::string const& response)
{
    if (!session || !session->GetPlayer())
        return;
    if (response.size() > nsocc::MaxMessageLength)
    {
        LOG_ERROR("module.native_social", "NSOC response exceeds {} bytes; discarded", nsocc::MaxMessageLength);
        return;
    }
    Player* const player = session->GetPlayer();
    player->Whisper(response, LANG_ADDON, player);
}
}
