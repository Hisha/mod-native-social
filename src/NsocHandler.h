#ifndef NATIVESOCIAL_NSOC_H
#define NATIVESOCIAL_NSOC_H

#include "SocialService.h"

#include <cstdint>
#include <string>

class Player;
class WorldSession;

namespace nativesocial
{

// NSOC protocol constants
namespace nsoch
{
    // Protocol prefix
    constexpr char const* Prefix = "NSOC";
    
    // Protocol version (2 bytes)
    constexpr char const* Version = "01";
    
    // Delimiter
    constexpr char Delimiter = '\t';
    
    // Maximum message length (254 bytes for safety, 255 is the AzerothCore limit)
    constexpr std::size_t MaxMessageLength = 254;
    
    // Request ID grammar: ASCII alphanumeric (A-Z, a-z, 0-9), 1-16 characters
    constexpr std::size_t MaxRequestIdLength = 16;
    
    // Error codes
    constexpr std::uint16_t ErrorProtocol = 0x0000;
    constexpr std::uint16_t ErrorVersion = 0x0001;
    constexpr std::uint16_t ErrorCommand = 0x0002;
    constexpr std::uint16_t ErrorLength = 0x0003;
    constexpr std::uint16_t ErrorFields = 0x0004;
    constexpr std::uint16_t ErrorUnavailable = 0x0005;
}

// NSOC command codes
namespace nsoc_cmd
{
    constexpr char const* List = "LIST";
    constexpr char const* ListStart = "LIST_START";
    constexpr char const* ListProfile = "LIST_PROFILE";
    constexpr char const* ListEnd = "LIST_END";
    constexpr char const* Error = "ERROR";
}

// NSOC request ID type
using RequestId = std::string;

// NSOC protocol handler
class NsocHandler
{
public:
    NsocHandler() = default;
    
    // Handle incoming NSOC request from a player
    // Returns true if the message was consumed (was NSOC traffic)
    static bool HandleRequest(WorldSession* session, std::string const& message);
    
    // Send an NSOC response to a player
    static void SendResponse(WorldSession* session, std::string const& response);
    
private:
    // Parse an incoming NSOC message
    // Returns true if parsing succeeded
    static bool ParseRequest(WorldSession* session, std::string const& message);
    
    // Handle LIST command
    static void HandleListCommand(WorldSession* session, RequestId const& requestId);
    
    // Build an error response
    static std::string BuildErrorResponse(RequestId const& requestId, std::uint16_t errorCode, std::string const& errorMessage);
    
    // Build LIST_START response
    static std::string BuildListStartResponse(RequestId const& requestId, std::uint32_t count);
    
    // Build LIST_PROFILE response
    static std::string BuildListProfileResponse(RequestId const& requestId, std::string const& displayName);
    
    // Build LIST_END response
    static std::string BuildListEndResponse(RequestId const& requestId);
    
    // Check if message is NSOC protocol
    static bool IsNsocMessage(std::string const& message);
    
    // Split message by delimiter
    static std::vector<std::string> SplitMessage(std::string const& message);
    
    // Escape display name for safe inclusion in protocol
    static std::string EscapeDisplayName(std::string const& displayName);
};

} // namespace nativesocial

#endif // NATIVESOCIAL_NSOC_H
