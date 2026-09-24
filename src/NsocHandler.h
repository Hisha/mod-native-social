#ifndef NATIVESOCIAL_NSOCHANDLER_H
#define NATIVESOCIAL_NSOCHANDLER_H

#include <cstddef>
#include <cstdint>
#include <string>

class WorldSession;

namespace nativesocial
{
namespace nsoch
{
constexpr std::size_t MaxRequestIdLength = 16;
constexpr std::uint16_t ErrorProtocol = 0x0000;
constexpr std::uint16_t ErrorVersion = 0x0001;
constexpr std::uint16_t ErrorCommand = 0x0002;
constexpr std::uint16_t ErrorLength = 0x0003;
constexpr std::uint16_t ErrorFields = 0x0004;
constexpr std::uint16_t ErrorUnavailable = 0x0005;
constexpr std::uint16_t ErrorUnauthorized = 0x0006;
constexpr std::uint16_t ErrorValidation = 0x0007;
}

class NsocHandler
{
public:
    static bool HandleRequest(WorldSession* session, std::string& message);
    static void SendResponse(WorldSession* session, std::string const& response);

private:
    static bool ParseRequest(WorldSession* session, std::string const& message);
};
}

#endif
