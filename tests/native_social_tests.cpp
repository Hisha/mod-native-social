#include "NsocCodec.h"
#include "SocialAccountEligibility.h"
#include "SocialAdmin.h"
#include "SocialDirectory.h"
#include "SocialProfile.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace nativesocial;

namespace
{
void DisplayNameTests()
{
    std::string name = "  Kevin  ";
    assert(ValidateDisplayName(name, 3, 24) == NameResult::Ok);
    assert(name == "Kevin");
    assert(DisplayNameKey("Kevin") == DisplayNameKey("kevin"));
    std::string shortName = "ab";
    assert(ValidateDisplayName(shortName, 3, 24) == NameResult::TooShort);
    std::string unsafe = "bad|name";
    assert(ValidateDisplayName(unsafe, 3, 24) == NameResult::InvalidCharacter);
}

void AccountEligibilityTests()
{
    SocialAccountEligibility eligibility;
    eligibility.Configure("rndbot", " AHBOT, , SECOND_SERVICE,  ");

    assert(!eligibility.IsEligibleUsername("RNDBOT123"));
    assert(!eligibility.IsEligibleUsername("rndbot123"));
    assert(!eligibility.IsEligibleUsername("AHBOT"));
    assert(!eligibility.IsEligibleUsername("ahbot"));
    assert(eligibility.IsEligibleUsername("normal_human_account"));
    assert(eligibility.IsEligibleUsername("AHBOT2"));
    assert(!eligibility.IsEligibleUsername("second_service"));
    assert(eligibility.ExplicitExclusionCount() == 2);
}

void DirectoryTests()
{
    std::vector<SocialProfile> profiles = {
        { 1, "Mike", false }, { 2, "Kevin", false }, { 3, "Bot", false },
        { 4, "Hidden", true }, { 5, "", false }
    };
    DirectoryPresenceMap live;
    live[2] = { true, "God", 80, 1, 2, 'A', "Stormwind City" };
    live[4] = { true, "Secret", 80, 2, 1, 'H', "Orgrimmar" };
    std::unordered_set<std::uint32_t> bots = { 3 };

    auto const directory = BuildDirectory(profiles, bots, live);
    assert(directory.size() == 3);
    assert(directory[0].accountId == 2 && directory[0].Online());
    assert(directory[1].accountId == 4 && !directory[1].Online());
    assert(directory[2].accountId == 1 && !directory[2].Online());
    assert(DirectoryEntryPresenceFields(directory[1]).empty());
    assert(directory[1].presence.characterName.empty());
    assert(directory[1].presence.zone.empty());
    for (auto const& entry : directory)
        assert(entry.displayName != "LOGIN_USERNAME");
}

void CodecTests()
{
    assert(nsocc::IsNsoc("NSOC\t01\tDIR_LIST\tA001"));
    assert(!nsocc::IsNsoc("NSOC\t01"));
    std::string const escaped = nsocc::Escape("a\\b\tc");
    assert(nsocc::Unescape(escaped) == "a\\b\tc");

    auto offline = nsocc::EncodeEntryFrames("DIR_ENTRY", "A001", 0,
        { "0", "7", "Mike" }, { });
    assert(offline.size() == 1);
    assert(offline[0].size() <= nsocc::MaxMessageLength);

    auto chunked = nsocc::EncodeEntryFrames("DIR_ENTRY", "A001", 1,
        { "1", "8", "Kevin" },
        { "CharacterName", "80", "1", "2", "A", "A long localized location" }, 58);
    assert(chunked.size() > 1);
    for (auto const& frame : chunked)
        assert(frame.size() <= 58);

    std::string const profileSave = nsocc::Frame("PROFILE_SAVE",
        { "A002", nsocc::Escape("New Name"), "1" });
    auto const fields = nsocc::Split(profileSave);
    assert(fields.size() == 6);
    assert(fields[2] == "PROFILE_SAVE" && fields[3] == "A002");
    assert(nsocc::Unescape(fields[4]) == "New Name" && fields[5] == "1");
    assert(profileSave.size() <= nsocc::MaxMessageLength);
}

void AdminTests()
{
    int creates = 0;
    int profiles = 0;
    auto preflight = [](std::string const&) { return NameResult::Ok; };
    auto create = [&](std::string const&, std::string const&)
    {
        ++creates;
        return AccountCreateResult{ CoreAccountCreateResult::Ok, 42 };
    };
    auto profile = [&](std::uint32_t, std::string const&)
    {
        ++profiles;
        return NameResult::Ok;
    };

    AdminResult denied = CreateManagedAccount(false, "LOGIN", "secret", "Kevin",
        3, 24, preflight, create, profile);
    assert(denied.code == AdminResultCode::Unauthorized && creates == 0 && profiles == 0);
    assert(denied.message.find("secret") == std::string::npos);

    AdminResult duplicate = CreateManagedAccount(true, "LOGIN", "secret", "Kevin", 3, 24,
        [](std::string const&) { return NameResult::AlreadyTaken; }, create, profile);
    assert(duplicate.code == AdminResultCode::DisplayNameTaken && creates == 0);

    AdminResult failed = CreateManagedAccount(true, "LOGIN", "secret", "Kevin", 3, 24,
        preflight,
        [](std::string const&, std::string const&)
        { return AccountCreateResult{ CoreAccountCreateResult::StorageFailure, 0 }; }, profile);
    assert(failed.code == AdminResultCode::AccountCreateFailed && profiles == 0);

    AdminResult partial = CreateManagedAccount(true, "LOGIN", "secret", "Kevin", 3, 24,
        preflight, create,
        [](std::uint32_t, std::string const&) { return NameResult::StorageFailure; });
    assert(partial.code == AdminResultCode::ProfileSetupFailed && partial.accountId == 42);
    assert(partial.message.find("secret") == std::string::npos);

    AdminResult complete = CreateManagedAccount(true, "LOGIN", "secret", "Kevin",
        3, 24, preflight, create, profile);
    assert(complete.Complete() && complete.accountId == 42);
}
}

int main()
{
    DisplayNameTests();
    AccountEligibilityTests();
    DirectoryTests();
    CodecTests();
    AdminTests();
    std::cout << "native-social standalone tests passed\n";
}
