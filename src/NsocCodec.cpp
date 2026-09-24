#include "NsocCodec.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nativesocial
{
namespace nsocc
{
namespace
{

std::size_t constexpr HeaderLength = 8; // "NSOC\t01\t"

} // namespace

bool IsNsoc(std::string const& frame)
{
    if (frame.size() < HeaderLength)
        return false;
    return frame.compare(0, 4, Prefix) == 0 &&
           frame[4] == Delimiter &&
           frame.compare(5, 2, Version) == 0 &&
           frame[7] == Delimiter;
}

std::vector<std::string> Split(std::string const& frame)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= frame.size())
    {
        std::size_t const tab = frame.find(Delimiter, start);
        if (tab == std::string::npos)
        {
            fields.push_back(frame.substr(start));
            break;
        }
        fields.push_back(frame.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

std::string Escape(std::string const& value)
{
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (char const c : value)
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

std::string Unescape(std::string const& value)
{
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size();)
    {
        char const c = value[i];
        if (c == '\\' && i + 1 < value.size())
        {
            char const next = value[i + 1];
            if (next == '\\')
            {
                out += '\\';
                i += 2;
                continue;
            }
            if (next == 't')
            {
                out += '\t';
                i += 2;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

std::string Frame(std::string const& command, std::vector<std::string> const& fields)
{
    std::string frame = Prefix;
    frame += Delimiter;
    frame += Version;
    frame += Delimiter;
    frame += command;
    for (auto const& field : fields)
    {
        frame += Delimiter;
        frame += field;
    }
    return frame;
}

std::vector<std::string> EncodeEntryFrames(std::string const& command,
    std::string const& requestId, std::uint32_t entryIndex,
    std::vector<std::string> const& identityFields,
    std::vector<std::string> const& presenceFields, std::size_t budget)
{
    std::vector<std::string> frames;
    if (identityFields.empty())
        return frames; // identity fields are required

    struct FrameBuilder
    {
        std::string command;
        std::string requestId;
        std::uint32_t entryIndex;
        std::string FrameFor(std::vector<std::string> const& fields, std::size_t partIndex) const
        {
            std::vector<std::string> withMeta;
            withMeta.reserve(fields.size() + 3);
            withMeta.push_back(requestId);
            withMeta.push_back(std::to_string(entryIndex));
            withMeta.push_back(std::to_string(partIndex));
            for (auto const& field : fields)
                withMeta.push_back(field);
            return Frame(command, withMeta);
        }
    } builder{ command, requestId, entryIndex };

    // Part 0: identity fields, then greedily as many presence fields as fit.
    std::vector<std::string> current = identityFields;
    std::size_t index = 0;
    for (; index < presenceFields.size(); ++index)
    {
        std::vector<std::string> probe = current;
        probe.push_back(presenceFields[index]);
        if (builder.FrameFor(probe, 0).size() <= budget)
        {
            current.swap(probe);
            continue;
        }
        // This presence field does not fit part 0; leave it for later parts.
        break;
    }

    std::string identityFrame = builder.FrameFor(current, 0);
    if (identityFrame.size() > budget)
        return {}; // identity fields alone cannot be transported at all

    frames.push_back(std::move(identityFrame));

    std::size_t partIndex = 1;
    std::vector<std::string> partFields;
    for (; index < presenceFields.size(); ++index)
    {
        std::string const& field = presenceFields[index];
        std::vector<std::string> candidate = partFields;
        candidate.push_back(field);
        if (builder.FrameFor(candidate, partIndex).size() <= budget)
        {
            partFields.swap(candidate);
            continue;
        }
        // Push the current part and start the next one with this field.
        if (!partFields.empty())
        {
            frames.push_back(builder.FrameFor(partFields, partIndex));
            ++partIndex;
            partFields.clear();
        }
        // A single field alone has to fit some part, or the entry cannot be
        // transported without corrupting the protocol. Abort the whole step.
        if (builder.FrameFor({ field }, partIndex).size() > budget)
            return {};
        partFields.push_back(field);
    }
    if (!partFields.empty())
        frames.push_back(builder.FrameFor(partFields, partIndex));

    return frames;
}

} // namespace nsocc
} // namespace nativesocial
