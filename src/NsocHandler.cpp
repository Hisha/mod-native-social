#include "NsocHandler.h"

#include "Chat.h"
#include "Log.h"
#include "Player.h"
#include "SocialService.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace nativesocial
{

bool NsocHandler::IsNsocMessage(std::string const& message)
{
    // NSOC messages start with "NSOC\t01\t"
    if (message.size() < 8)
        return false;
    
    return message.compare(0, 4, nsoch::Prefix) == 0 &&
           message[4] == nsoch::Delimiter &&
           message.compare(5, 2, nsoch::Version) == 0 &&
           message[7] == nsoch::Delimiter;
}

std::vector<std::string> NsocHandler::SplitMessage(std::string const& message)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    std::size_t end = message.find(nsoch::Delimiter);
    
    while (end != std::string::npos)
    {
        fields.push_back(message.substr(start, end - start));
        start = end + 1;
        end = message.find(nsoch::Delimiter, start);
    }
    
    // Add the last field
    if (start < message.size())
    {
        fields.push_back(message.substr(start));
    }
    
    return fields;
}

std::string NsocHandler::EscapeDisplayName(std::string const& displayName)
{
    // Escape backslashes first, then tabs (backslash -> "\\", tab -> "\t").
    // A single left-to-right pass over the source cannot double-process either
    // escape, so backslash handling is unambiguous and the result is reversible.
    std::string escaped;
    escaped.reserve(displayName.size() * 2);
    for (char c : displayName)
    {
        if (c == '\\')
            escaped += "\\\\";
        else if (c == '\t')
            escaped += "\\t";
        else
            escaped += c;
    }
    return escaped;
}



bool NsocHandler::ParseRequest(WorldSession* session, std::string const& message)
{
    if (message.size() > nsoch::MaxMessageLength)
    {
        LOG_ERROR("module.native_social", "NSOC: Message too long ({} bytes), rejecting", message.size());
        return false;
    }
    
    std::vector<std::string> fields = SplitMessage(message);
    
    // Minimum fields: prefix, version, command
    if (fields.size() < 3)
    {
        LOG_ERROR("module.native_social", "NSOC: Malformed message (insufficient fields)");
        return false;
    }
    
    // Check prefix and version
    if (fields[0] != nsoch::Prefix || fields[1] != nsoch::Version)
    {
        LOG_ERROR("module.native_social", "NSOC: Invalid prefix or version");
        return false;
    }
    
    std::string const& command = fields[2];
    
    // Extract request ID - needed for error responses
    RequestId requestId;
    if (fields.size() >= 4)
    {
        requestId = fields[3];
        // Request IDs must be ASCII alphanumeric (A-Z, a-z, 0-9), 1-16 characters.
        bool const validRequestId = !requestId.empty()
            && requestId.size() <= nsoch::MaxRequestIdLength
            && std::all_of(requestId.begin(), requestId.end(),
                [](unsigned char c) { return std::isalnum(c) != 0; });
        if (!validRequestId)
        {
            LOG_ERROR("module.native_social", "NSOC: Invalid request ID ({} bytes, must be 1-{} ASCII alphanumeric)",
                requestId.size(), nsoch::MaxRequestIdLength);
            SendResponse(session, BuildErrorResponse("0000", nsoch::ErrorProtocol, "Invalid request ID"));
            return true;
        }
    }
    else
    {
        // Default request ID if not provided
        requestId = "0000";
    }
    
    if (command == nsoc_cmd::List)
    {
        // LIST command must have exactly 4 fields
        if (fields.size() != 4)
        {
            LOG_ERROR("module.native_social", "NSOC: LIST command has {} fields, expected 4", fields.size());
            SendResponse(session, BuildErrorResponse(requestId, nsoch::ErrorFields, "LIST command has wrong field count"));
            return true;
        }
        
        HandleListCommand(session, requestId);
        return true;
    }
    else if (command == nsoc_cmd::Error || 
             command == nsoc_cmd::ListStart || 
             command == nsoc_cmd::ListProfile || 
             command == nsoc_cmd::ListEnd)
    {
        // These are response codes, not request codes
        LOG_ERROR("module.native_social", "NSOC: Received response code as request: {}", command);
        SendResponse(session, BuildErrorResponse(requestId, nsoch::ErrorProtocol, "Response code received as request"));
        return true;
    }
    else
    {
        LOG_ERROR("module.native_social", "NSOC: Unknown command: {}", command);
        SendResponse(session, BuildErrorResponse(requestId, nsoch::ErrorCommand, "Unknown command"));
        return true;
    }
}

void NsocHandler::HandleListCommand(WorldSession* session, RequestId const& requestId)
{
    if (!SocialService::Instance().IsInitialized() || !SocialService::Instance().Available())
    {
        LOG_ERROR("module.native_social", "NSOC: LIST command received but module is not available");
        SendResponse(session, BuildErrorResponse(requestId, nsoch::ErrorUnavailable, "Module unavailable"));
        return;
    }
    
    // Get the list of advertised online profiles
    std::vector<SocialProfile> profiles = SocialService::Instance().ListAdvertisedOnline();
    
    // Send LIST_START
    SendResponse(session, BuildListStartResponse(requestId, static_cast<std::uint32_t>(profiles.size())));
    
    // Send each profile
    for (auto const& profile : profiles)
    {
        SendResponse(session, BuildListProfileResponse(requestId, profile.displayName));
    }
    
    // Send LIST_END
    SendResponse(session, BuildListEndResponse(requestId));
}

std::string NsocHandler::BuildErrorResponse(RequestId const& requestId, std::uint16_t errorCode, std::string const& errorMessage)
{
    std::string response = nsoch::Prefix;
    response += nsoch::Delimiter;
    response += nsoch::Version;
    response += nsoch::Delimiter;
    response += nsoc_cmd::Error;
    response += nsoch::Delimiter;
    response += requestId;
    response += nsoch::Delimiter;
    
    // Format error code as 4-digit hex
    char codeBuffer[5];
    snprintf(codeBuffer, sizeof(codeBuffer), "%04X", errorCode);
    response += codeBuffer;
    response += nsoch::Delimiter;
    
    response += errorMessage;
    
    return response;
}

std::string NsocHandler::BuildListStartResponse(RequestId const& requestId, std::uint32_t count)
{
    std::string response = nsoch::Prefix;
    response += nsoch::Delimiter;
    response += nsoch::Version;
    response += nsoch::Delimiter;
    response += nsoc_cmd::ListStart;
    response += nsoch::Delimiter;
    response += requestId;
    response += nsoch::Delimiter;
    
    char countBuffer[11]; // 10 digits + null terminator
    snprintf(countBuffer, sizeof(countBuffer), "%u", count);
    response += countBuffer;
    
    return response;
}

std::string NsocHandler::BuildListProfileResponse(RequestId const& requestId, std::string const& displayName)
{
    std::string response = nsoch::Prefix;
    response += nsoch::Delimiter;
    response += nsoch::Version;
    response += nsoch::Delimiter;
    response += nsoc_cmd::ListProfile;
    response += nsoch::Delimiter;
    response += requestId;
    response += nsoch::Delimiter;
    
    // Escape the display name
    std::string escapedName = EscapeDisplayName(displayName);
    response += escapedName;
    
    return response;
}

std::string NsocHandler::BuildListEndResponse(RequestId const& requestId)
{
    std::string response = nsoch::Prefix;
    response += nsoch::Delimiter;
    response += nsoch::Version;
    response += nsoch::Delimiter;
    response += nsoc_cmd::ListEnd;
    response += nsoch::Delimiter;
    response += requestId;
    
    return response;
}

bool NsocHandler::HandleRequest(WorldSession* session, std::string const& message)
{
    if (!IsNsocMessage(message))
    {
        return false;
    }
    
    LOG_DEBUG("module.native_social", "NSOC: Received message from {}: {}", 
              session ? session->GetPlayerInfo() : "unknown", message);
    
    return ParseRequest(session, message);
}

void NsocHandler::SendResponse(WorldSession* session, std::string const& response)
{
    if (!session || !session->GetPlayer())
    {
        LOG_ERROR("module.native_social", "NSOC: Cannot send response - invalid session");
        return;
    }
    
    // Check message length
    if (response.size() > nsoch::MaxMessageLength)
    {
        LOG_ERROR("module.native_social", "NSOC: Response too long ({} bytes), discarding", response.size());
        // Do not send truncated messages - they would break protocol validity
        return;
    }
    
    LOG_DEBUG("module.native_social", "NSOC: Sending response to {}: {}", 
              session->GetPlayerInfo(), response);
    
    // Send as a LANG_ADDON whisper to the requesting player's own client,
    // which surfaces it to the patched client as an addon message.
    Player* const player = session->GetPlayer();
    player->Whisper(response, LANG_ADDON, player);
}

} // namespace nativesocial
