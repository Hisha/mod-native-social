#ifndef NATIVESOCIAL_NSOCCODEC_H
#define NATIVESOCIAL_NSOCCODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure NSOC framing discipline, independent of AzerothCore and the game
// runtime so it can compile and be unit-tested standalone. Mirrors the wire
// format documented in docs/NSOC_PROTOCOL.md and implemented on the client by
// the patched NativeSocial.lua. NsocHandler composes request/response
// sequences on top of these primitives.
//
// Failure policy: the codec never truncates. A field that cannot be framed
// within the budget aborts the whole encoding step and returns an empty
// result; the caller reports a protocol error instead of emitting a corrupt
// frame.

namespace nativesocial
{
namespace nsocc
{
    constexpr char const* Prefix = "NSOC";
    constexpr char const* Version = "01";
    constexpr char Delimiter = '\t';
    constexpr std::size_t MaxMessageLength = 254;

    // True when frame begins with the exact "NSOC\t01\t" prefix.
    bool IsNsoc(std::string const& frame);

    // Splits a frame into fields by the delimiter. An empty trailing field is
    // preserved (an NSOC frame never ends on the delimiter by construction).
    std::vector<std::string> Split(std::string const& frame);

    // Escapes a text field: backslash first, then tab ("\" -> "\\", tab ->
    // "\t"). Byte-oriented, so UTF-8 passes through untouched. Unescape is
    // the exact inverse.
    std::string Escape(std::string const& value);
    std::string Unescape(std::string const& value);

    // Builds "NSOC\t01\t<command>" + "	<data field>..." as a single frame.
    // Data fields are inserted verbatim (the caller escapes text fields
    // first; numeric/structural fields never need escaping). The result is
    // NOT length-checked here; requesting code validates against the budget.
    std::string Frame(std::string const& command, std::vector<std::string> const& fields);

    // Chunked transport for one directory entry (DIR_ENTRY). identityFields
    // always live in part 0 (they are the fields every client must see);
    // presenceFields are then packed greedily into following parts while each
    // resulting frame stays within budget. Every emitted frame is
    // "NSOC\t01\t<command>\t<requestId>\t<index>\t<part>\t<field>...".
    // Returns one frame per part; an entry whose identity fields alone cannot
    // fit the budget (an authoring/length bug) yields an empty vector. A
    // single field that cannot fit alone also aborts the whole entry.
    std::vector<std::string> EncodeEntryFrames(std::string const& command,
        std::string const& requestId, std::uint32_t entryIndex,
        std::vector<std::string> const& identityFields,
        std::vector<std::string> const& presenceFields,
        std::size_t budget = MaxMessageLength);

    // Frame length in bytes, the definition used by every budget check.
    inline std::size_t FrameLength(std::string const& frame) { return frame.size(); }
}
}

#endif
