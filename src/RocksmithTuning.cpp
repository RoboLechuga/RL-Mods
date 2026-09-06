#include "RocksmithTuning.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RocksmithTuning
{
    namespace
    {
        enum class ExecutableVersion
        {
            Remastered2022,
            LearnAndPlay2024
        };

        constexpr const wchar_t* INI_SECTION_ROCKSMITH =
            L"Rocksmith";

        constexpr const wchar_t* INI_KEY_VERSION =
            L"Version";

        // Rocksmith memory-layout facts for the two versions currently tracked by
        // RSMods: Remastered September 2022 and Learn & Play December 2024.
        // Tuning / tuner-text / true-tuning roots are module-relative RVAs in
        // both builds. Current-menu is the exception: the 2022 value is absolute.
        constexpr std::uintptr_t ARRANGEMENT_2024_ROOT_OFFSET =
            0x00F6062C;
        constexpr std::uintptr_t ARRANGEMENT_2022_ROOT =
            0x00F5F62C;

        constexpr std::uintptr_t TRUE_TUNING_2024_ROOT_OFFSET =
            0x00F6057C;
        constexpr std::uintptr_t TRUE_TUNING_2022_ROOT =
            0x00F5F57C;

        constexpr std::uintptr_t CURRENT_MENU_2024_ROOT_OFFSET =
            0x00F6062C;
        constexpr std::uintptr_t CURRENT_MENU_2022_ROOT =
            0x0135F62C;

        constexpr std::uintptr_t TUNER_TEXT_2024_ROOT_OFFSET =
            0x00F6062C;
        constexpr std::uintptr_t TUNER_TEXT_2022_ROOT =
            0x00F5F62C;

        constexpr std::array<std::uintptr_t, 7>
            ARRANGEMENT_OFFSETS =
        {
            0x68,
            0x10,
            0x28,
            0x38,
            0x78,
            0x110,
            0x0
        };

        constexpr std::array<std::uintptr_t, 3>
            TRUE_TUNING_OFFSETS =
        {
            0x10,
            0x4,
            0x135C
        };

        constexpr std::array<std::uintptr_t, 3>
            CURRENT_MENU_OFFSETS =
        {
            0x28,
            0x8C,
            0x0
        };

        constexpr std::array<std::uintptr_t, 3>
            TUNER_TEXT_OFFSETS =
        {
            0x28,
            0x44,
            0x0
        };

        constexpr std::array<std::uintptr_t, 6>
            STRING_BYTE_OFFSETS =
        {
            0x0,
            0x2,
            0x4,
            0x6,
            0x8,
            0xA
        };

        struct TuningDefinition
        {
            const char* key;
            std::array<int, 6> strings;
        };

        // Rocksmith's stock tuning names/offsets. Keeping the small lookup in
        // the DLL lets the pre-song tuner be read without shipping RSMods'
        // external tuning.database.json file.
        constexpr TuningDefinition TUNING_DEFINITIONS[] =
        {
            { "ESTANDARD",   {  0,  0,  0,  0,  0,  0 } },
            { "DROPD",       { -2,  0,  0,  0,  0,  0 } },
            { "FSTANDARD",   {  1,  1,  1,  1,  1,  1 } },
            { "OPEND",       { -2,  0,  0, -1, -2, -2 } },
            { "OPENA",       {  0,  0,  2,  2,  2,  0 } },
            { "OPENG",       { -2, -2,  0,  0,  0, -2 } },
            { "OPENE",       {  0,  2,  2,  1,  0,  0 } },
            { "EBSTANDARD",  { -1, -1, -1, -1, -1, -1 } },
            { "EFLAT",       { -1, -1, -1, -1, -1, -1 } },
            { "EBDROPDB",    { -3, -1, -1, -1, -1, -1 } },
            { "DSTANDARD",   { -2, -2, -2, -2, -2, -2 } },
            { "DADGAD",      { -2,  0,  0,  0, -2, -2 } },
            { "DDROPC",      { -4, -2, -2, -2, -2, -2 } },
            { "C#STANDARD",  { -3, -3, -3, -3, -3, -3 } },
            { "DBSTANDARD",  { -3, -3, -3, -3, -3, -3 } },
            { "C#DROPB",     { -5, -3, -3, -3, -3, -3 } },
            { "DBDROPB",     { -5, -3, -3, -3, -3, -3 } },
            { "CSTANDARD",   { -4, -4, -4, -4, -4, -4 } },
            { "CDROPBB",     { -6, -4, -4, -4, -4, -4 } },
            { "BSTANDARD",   { -5, -5, -5, -5, -5, -5 } },
            { "BDROPA",      { -7, -5, -5, -5, -5, -5 } },
            { "BBSTANDARD",  { -6, -6, -6, -6, -6, -6 } },
            { "BBDROPAB",    { -8, -6, -6, -6, -6, -6 } },
            { "ASTANDARD",   { -7, -7, -7, -7, -7, -7 } },
            { "ADROPG",      { -9, -7, -7, -7, -7, -7 } },
            { "ALLFOURTH",   {  0,  0,  0,  0,  1,  1 } },
            { "DOUBLEDROPD", { -2,  0,  0,  0,  0, -2 } },
            { "OPENC6",      { -4,  0, -2,  0,  1,  0 } },
            { "OPENC5",      { -4, -2, -2,  0, -4,  0 } },
            { "DADADD",      { -2,  0,  0,  2,  3, -2 } },
            { "OPENDM7",     { -2,  0,  0, -2,  1, -2 } },
            { "OPENBM",      { -2,  2,  0, -1,  0, -2 } },
            { "EADGBD",      {  0,  0,  0,  0,  0, -2 } },
            { "OPENDM",      { -2,  0,  0, -2, -2, -2 } },
            { "DBDGBE",      { -2,  2,  0,  0,  0,  0 } },
            { "EADGAE",      {  0,  0,  0,  0, -2,  0 } },
            { "OPENEM7",     {  0, -2,  0,  0,  0, -2 } },
            { "EGDGBD",      {  0, -2,  0,  0,  0, -2 } },
            { "EABGBD#",     {  0,  0, -3,  0,  0, -1 } },
            { "EADGBD#",     {  0,  0,  0,  0,  0, -1 } },
            { "OPENDB/C#",   { -3, -1, -1, -2, -3, -3 } },
            { "BEADG",       { -5, -5, -5, -5, -4, -5 } },
            { "AEADG",       { -7, -5, -5, -5, -4, -5 } }
        };

        constexpr const char* PRE_SONG_TUNER_MENUS[] =
        {
            "SelectionListDialog",
            "LearnASong_PreSongTuner",
            "LearnASong_PreSongTunerMP",
            "NonStopPlay_PreSongTuner",
            "NonStopPlay_PreSongTunerMP",
            "ScoreAttack_PreSongTuner",
            "SessionMode_PreSMTunerMP",
            "SessionMode_PreSMTuner",
            "Duet_PreSongTuner",
            "H2H_PreSongTuner",
            "PreGame_GETuner"
        };

        std::wstring BuildIniPath()
        {
            wchar_t path[MAX_PATH] = {};

            const DWORD length =
                GetModuleFileNameW(
                    nullptr,
                    path,
                    MAX_PATH);

            if (length == 0 ||
                length >= MAX_PATH)
            {
                return L"RLMods.ini";
            }

            std::wstring fullPath(path);

            const size_t slash =
                fullPath.find_last_of(
                    L"\\/");

            if (slash ==
                std::wstring::npos)
            {
                return L"RLMods.ini";
            }

            return
                fullPath.substr(
                    0,
                    slash + 1) +
                L"RLMods.ini";
        }

        std::wstring BuildGamePath(
            const wchar_t* fileName)
        {
            wchar_t path[MAX_PATH] = {};

            const DWORD length =
                GetModuleFileNameW(
                    nullptr,
                    path,
                    MAX_PATH);

            if (length == 0 ||
                length >= MAX_PATH)
            {
                return fileName;
            }

            std::wstring fullPath(path);

            const size_t slash =
                fullPath.find_last_of(
                    L"\\/");

            if (slash ==
                std::wstring::npos)
            {
                return fileName;
            }

            return
                fullPath.substr(
                    0,
                    slash + 1) +
                fileName;
        }

        ExecutableVersion ReadExecutableVersion()
        {
            wchar_t version[32] = {};

            const std::wstring iniPath =
                BuildIniPath();

            GetPrivateProfileStringW(
                INI_SECTION_ROCKSMITH,
                INI_KEY_VERSION,
                L"2022",
                version,
                ARRAYSIZE(version),
                iniPath.c_str());

            if (lstrcmpW(
                    version,
                    L"2024") == 0)
            {
                return
                    ExecutableVersion::
                        LearnAndPlay2024;
            }

            return
                ExecutableVersion::
                    Remastered2022;
        }

        ExecutableVersion GetExecutableVersion()
        {
            static const ExecutableVersion version =
                ReadExecutableVersion();

            return version;
        }

        bool IsReadableRange(
            const void* address,
            size_t bytes)
        {
            if (!address || bytes == 0)
                return false;

            MEMORY_BASIC_INFORMATION mbi{};

            if (!VirtualQuery(
                    address,
                    &mbi,
                    sizeof(mbi)))
            {
                return false;
            }

            if (mbi.State != MEM_COMMIT)
                return false;

            if (mbi.Protect & PAGE_GUARD)
                return false;

            if (mbi.Protect & PAGE_NOACCESS)
                return false;

            const DWORD readable =
                PAGE_READONLY |
                PAGE_READWRITE |
                PAGE_WRITECOPY |
                PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;

            if ((mbi.Protect & readable) == 0)
                return false;

            const std::uintptr_t start =
                reinterpret_cast<std::uintptr_t>(
                    address);

            const std::uintptr_t regionStart =
                reinterpret_cast<std::uintptr_t>(
                    mbi.BaseAddress);

            const std::uintptr_t regionEnd =
                regionStart +
                static_cast<std::uintptr_t>(
                    mbi.RegionSize);

            if (start < regionStart ||
                start >= regionEnd)
            {
                return false;
            }

            return
                bytes <=
                static_cast<size_t>(
                    regionEnd - start);
        }

        template <typename T>
        bool TryReadValue(
            std::uintptr_t address,
            T& value)
        {
            if (!IsReadableRange(
                    reinterpret_cast<const void*>(
                        address),
                    sizeof(T)))
            {
                return false;
            }

            value =
                *reinterpret_cast<const T*>(
                    address);

            return true;
        }

        template <size_t N>
        std::uintptr_t ResolvePointerChain(
            std::uintptr_t root,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            std::uintptr_t address = root;

            for (const std::uintptr_t offset :
                 offsets)
            {
                std::uintptr_t next = 0;

                if (!TryReadValue(
                        address,
                        next) ||
                    next == 0)
                {
                    return 0;
                }

                if (next >
                    UINTPTR_MAX -
                    offset)
                {
                    return 0;
                }

                address =
                    next +
                    offset;
            }

            return address;
        }

        template <size_t N>
        std::uintptr_t ResolveConfigured(
            std::uintptr_t root2024Offset,
            std::uintptr_t root2022Offset,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            HMODULE gameModule =
                GetModuleHandleW(nullptr);

            if (!gameModule)
                return 0;

            const std::uintptr_t base =
                reinterpret_cast<std::uintptr_t>(
                    gameModule);

            const std::uintptr_t rootOffset =
                GetExecutableVersion() ==
                    ExecutableVersion::
                        LearnAndPlay2024
                ? root2024Offset
                : root2022Offset;

            return
                ResolvePointerChain(
                    base +
                        rootOffset,
                    offsets);
        }

        std::string ReadString(
            std::uintptr_t address,
            size_t maxLength = 128)
        {
            if (!address || maxLength == 0)
                return {};

            std::string result;
            result.reserve(maxLength);

            for (size_t i = 0;
                 i < maxLength;
                 ++i)
            {
                unsigned char value = 0;

                if (!TryReadValue(
                        address + i,
                        value))
                {
                    return {};
                }

                if (value == 0)
                    break;

                // Keep printable ASCII and UTF-8 bytes. Reject ordinary
                // control characters so a bad pointer does not masquerade as
                // a string.
                if (value < 32 &&
                    value != '\t')
                {
                    return {};
                }

                result.push_back(
                    static_cast<char>(
                        value));
            }

            if (result.empty() ||
                result.size() == maxLength)
            {
                return {};
            }

            return result;
        }

        std::string AddressText(
            std::uintptr_t address)
        {
            std::ostringstream text;

            text << "0x"
                 << std::uppercase
                 << std::hex
                 << std::setw(
                        static_cast<int>(
                            sizeof(std::uintptr_t) * 2))
                 << std::setfill('0')
                 << address;

            return text.str();
        }

        template <size_t N>
        std::uintptr_t TracePointerChain(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t root,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            log << label << "\n";
            log << "  root: "
                << AddressText(root)
                << "\n";

            std::uintptr_t address = root;

            for (size_t i = 0;
                 i < offsets.size();
                 ++i)
            {
                log << "  step "
                    << i
                    << ": read "
                    << AddressText(address);

                std::uintptr_t next = 0;

                if (!TryReadValue(
                        address,
                        next))
                {
                    log << " -> UNREADABLE\n";
                    return 0;
                }

                log << " -> "
                    << AddressText(next);

                if (next == 0)
                {
                    log << " -> NULL\n";
                    return 0;
                }

                if (next >
                    UINTPTR_MAX -
                    offsets[i])
                {
                    log << " -> OVERFLOW\n";
                    return 0;
                }

                address =
                    next +
                    offsets[i];

                log << " + "
                    << AddressText(
                        offsets[i])
                    << " = "
                    << AddressText(address)
                    << "\n";
            }

            log << "  final: "
                << AddressText(address)
                << "\n";

            return address;
        }

        void TraceRawBytes(
            std::ostringstream& log,
            std::uintptr_t address,
            size_t bytes)
        {
            log << "  raw: ";

            for (size_t i = 0;
                 i < bytes;
                 ++i)
            {
                std::uint8_t value = 0;

                if (!TryReadValue(
                        address + i,
                        value))
                {
                    log << "<unreadable at +0x"
                        << std::hex
                        << std::uppercase
                        << i
                        << std::dec
                        << ">";
                    break;
                }

                if (i != 0)
                    log << ' ';

                log << std::uppercase
                    << std::hex
                    << std::setw(2)
                    << std::setfill('0')
                    << static_cast<int>(value)
                    << std::dec;
            }

            log << "\n";
        }

        template <size_t N>
        void TraceStringPointer(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t root,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            const std::uintptr_t address =
                TracePointerChain(
                    log,
                    label,
                    root,
                    offsets);

            if (!address)
            {
                log << "  result: FAILED\n\n";
                return;
            }

            TraceRawBytes(
                log,
                address,
                32);

            const std::string text =
                ReadString(
                    address,
                    128);

            log << "  text: "
                << (text.empty()
                    ? "<empty/unreadable>"
                    : text)
                << "\n\n";
        }

        template <size_t N>
        void TraceArrangementPointer(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t root,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            const std::uintptr_t address =
                TracePointerChain(
                    log,
                    label,
                    root,
                    offsets);

            if (!address)
            {
                log << "  result: FAILED\n\n";
                return;
            }

            TraceRawBytes(
                log,
                address,
                16);

            log << "  strings raw/signed: ";

            for (size_t i = 0;
                 i < STRING_BYTE_OFFSETS.size();
                 ++i)
            {
                std::uint8_t raw = 0;

                if (!TryReadValue(
                        address +
                            STRING_BYTE_OFFSETS[i],
                        raw))
                {
                    log << "<unreadable>";
                    break;
                }

                const int value =
                    static_cast<int>(
                        static_cast<std::int8_t>(
                            raw));

                if (i != 0)
                    log << ", ";

                log << static_cast<int>(raw)
                    << '/'
                    << value;
            }

            log << "\n\n";
        }

        template <size_t N>
        void TraceFloatPointer(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t root,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            const std::uintptr_t address =
                TracePointerChain(
                    log,
                    label,
                    root,
                    offsets);

            if (!address)
            {
                log << "  result: FAILED\n\n";
                return;
            }

            float value = 0.0f;

            if (!TryReadValue(
                    address,
                    value))
            {
                log << "  float: UNREADABLE\n\n";
                return;
            }

            TraceRawBytes(
                log,
                address,
                sizeof(float));

            log << "  float: "
                << value
                << "\n\n";
        }

        void ReplaceAll(
            std::string& text,
            const std::string& from,
            const std::string& to)
        {
            if (from.empty())
                return;

            size_t position = 0;

            while ((position =
                        text.find(
                            from,
                            position)) !=
                    std::string::npos)
            {
                text.replace(
                    position,
                    from.size(),
                    to);

                position +=
                    to.size();
            }
        }

        std::string NormalizeTuningKey(
            std::string text)
        {
            ReplaceAll(
                text,
                "\xE2\x99\xAF",
                "#");

            ReplaceAll(
                text,
                "\xE2\x99\xAD",
                "b");

            // Some localized strings can carry an unresolved $[id] prefix.
            if (text.rfind("$[", 0) == 0)
            {
                const size_t close =
                    text.find(']');

                if (close !=
                    std::string::npos)
                {
                    text.erase(
                        0,
                        close + 1);
                }
            }

            std::string normalized;
            normalized.reserve(
                text.size());

            for (unsigned char c : text)
            {
                if (std::isspace(c) ||
                    c == '-' ||
                    c == '_')
                {
                    continue;
                }

                normalized.push_back(
                    static_cast<char>(
                        std::toupper(c)));
            }

            return normalized;
        }

        bool TryNoteOffsetFromE(
            const std::string& note,
            int& offset)
        {
            // Standard/drop naming in Rocksmith is overwhelmingly expressed
            // downward from E. F Standard is the stock +1 exception.
            struct NamedOffset
            {
                const char* note;
                int offset;
            };

            constexpr NamedOffset NOTE_OFFSETS[] =
            {
                { "E",   0 },
                { "EB", -1 },
                { "D#", -1 },
                { "D",  -2 },
                { "DB", -3 },
                { "C#", -3 },
                { "C",  -4 },
                { "B",  -5 },
                { "BB", -6 },
                { "A#", -6 },
                { "A",  -7 },
                { "AB", -8 },
                { "G#", -8 },
                { "G",  -9 },
                { "GB", -10 },
                { "F#", -10 },
                { "F",   1 }
            };

            for (const auto& candidate :
                 NOTE_OFFSETS)
            {
                if (note == candidate.note)
                {
                    offset = candidate.offset;
                    return true;
                }
            }

            return false;
        }

        bool TryParseGenericTuning(
            const std::string& key,
            Tuning& tuning)
        {
            constexpr const char* STANDARD_SUFFIX =
                "STANDARD";

            const size_t standardLength =
                8;

            if (key.size() > standardLength &&
                key.compare(
                    key.size() -
                        standardLength,
                    standardLength,
                    STANDARD_SUFFIX) == 0)
            {
                const std::string note =
                    key.substr(
                        0,
                        key.size() -
                            standardLength);

                int offset = 0;

                if (!TryNoteOffsetFromE(
                        note,
                        offset))
                {
                    return false;
                }

                tuning.strings.fill(offset);
                return true;
            }

            if (key == "DROPD")
            {
                tuning.strings =
                    { -2, 0, 0, 0, 0, 0 };
                return true;
            }

            const size_t drop =
                key.find("DROP");

            if (drop !=
                    std::string::npos &&
                drop > 0)
            {
                const std::string upperNote =
                    key.substr(
                        0,
                        drop);

                int upper = 0;

                if (!TryNoteOffsetFromE(
                        upperNote,
                        upper))
                {
                    return false;
                }

                tuning.strings =
                {
                    upper - 2,
                    upper,
                    upper,
                    upper,
                    upper,
                    upper
                };

                return true;
            }

            return false;
        }

        bool TryLookupTuningText(
            const std::string& text,
            Tuning& tuning)
        {
            const std::string key =
                NormalizeTuningKey(
                    text);

            if (key.empty() ||
                key == "CUSTOMTUNING")
            {
                return false;
            }

            for (const auto& definition :
                 TUNING_DEFINITIONS)
            {
                if (key == definition.key)
                {
                    tuning.strings =
                        definition.strings;
                    return true;
                }
            }

            return
                TryParseGenericTuning(
                    key,
                    tuning);
        }

        std::string CurrentMenu()
        {
            if (GetExecutableVersion() ==
                ExecutableVersion::
                    LearnAndPlay2024)
            {
                HMODULE gameModule =
                    GetModuleHandleW(nullptr);

                if (!gameModule)
                    return {};

                const std::uintptr_t base =
                    reinterpret_cast<std::uintptr_t>(
                        gameModule);

                const std::uintptr_t resolved =
                    ResolvePointerChain(
                        base +
                            CURRENT_MENU_2024_ROOT_OFFSET,
                        CURRENT_MENU_OFFSETS);

                return
                    ReadString(
                        resolved);
            }

            const std::uintptr_t resolved =
                ResolvePointerChain(
                    CURRENT_MENU_2022_ROOT,
                    CURRENT_MENU_OFFSETS);

            return
                ReadString(
                    resolved);
        }

        bool IsPreSongTunerMenu(
            const std::string& menu)
        {
            for (const char* candidate :
                 PRE_SONG_TUNER_MENUS)
            {
                if (menu.find(candidate) !=
                    std::string::npos)
                {
                    return true;
                }
            }

            return false;
        }

        bool TryReadTunerTextTuning(
            Tuning& tuning)
        {
            const std::uintptr_t address =
                ResolveConfigured(
                    TUNER_TEXT_2024_ROOT_OFFSET,
                    TUNER_TEXT_2022_ROOT,
                    TUNER_TEXT_OFFSETS);

            if (!address)
                return false;

            const std::string text =
                ReadString(
                    address,
                    128);

            if (text.empty())
                return false;

            return
                TryLookupTuningText(
                    text,
                    tuning);
        }

        bool TryReadArrangementMemory(
            Tuning& tuning)
        {
            const std::uintptr_t address =
                ResolveConfigured(
                    ARRANGEMENT_2024_ROOT_OFFSET,
                    ARRANGEMENT_2022_ROOT,
                    ARRANGEMENT_OFFSETS);

            if (!address)
                return false;

            Tuning candidate{};

            for (size_t i = 0;
                 i < STRING_BYTE_OFFSETS.size();
                 ++i)
            {
                std::uint8_t raw = 0;

                if (!TryReadValue(
                        address +
                            STRING_BYTE_OFFSETS[i],
                        raw))
                {
                    return false;
                }

                const int value =
                    static_cast<int>(
                        static_cast<std::int8_t>(
                            raw));

                if (value < -24 ||
                    value > 24)
                {
                    return false;
                }

                candidate.strings[i] =
                    value;
            }

            tuning = candidate;
            return true;
        }

        // Multiplayer target acquisition is intentionally absent from this
        // diagnostic build. F10 performs only a one-shot, read-only UI scan.

        const char* NoteNameFromOffset(
            int semitonesFromE)
        {
            static const char* names[12] =
            {
                "C",
                "Db",
                "D",
                "Eb",
                "E",
                "F",
                "Gb",
                "G",
                "Ab",
                "A",
                "Bb",
                "B"
            };

            int pitchClass =
                (4 + semitonesFromE) % 12;

            if (pitchClass < 0)
                pitchClass += 12;

            return names[pitchClass];
        }
    }

    std::string CurrentMenuName()
    {
        return CurrentMenu();
    }

    bool IsPreSongTuner(
        const std::string& menu)
    {
        return IsPreSongTunerMenu(menu);
    }

    bool IsSongGameplayMenu(
        const std::string& menu)
    {
        constexpr const char* GAME_SUFFIX = "_Game";
        constexpr size_t GAME_SUFFIX_LENGTH = 5;

        return
            menu.size() >= GAME_SUFFIX_LENGTH &&
            menu.compare(
                menu.size() - GAME_SUFFIX_LENGTH,
                GAME_SUFFIX_LENGTH,
                GAME_SUFFIX) == 0;
    }

    bool InitializeTunerTargetCapture()
    {
        // Deliberately no hook in this diagnostic build.
        return true;
    }

    void ShutdownTunerTargetCapture()
    {
        // Nothing installed.
    }

    bool TryReadTunerTarget(
        int player,
        Tuning& tuning)
    {
        // Preserve only the original P1-style tuner text reader while we map
        // the multiplayer UI text path. P2 intentionally returns unavailable.
        if (player != 0)
            return false;

        return TryReadTunerTextTuning(tuning);
    }

    bool TryReadTunerTarget(
        Tuning& tuning)
    {
        return TryReadTunerTarget(
            0,
            tuning);
    }

    bool CaptureDebugSnapshot()
    {
        // V5 starts from the exact custom tuning that V4 positively located,
        // then reverse-walks the two structurally identical live P1/P2 owner
        // objects. No builder hook, code patching, text scan, or broad forward
        // object traversal is used here.
        constexpr std::array<std::int32_t, 6> TARGET_OFFSETS =
        {
            -5, -3, -3, -3, -3, -3
        };

        constexpr std::array<std::int32_t, 6> TARGET_MIDI =
        {
            35, 42, 47, 52, 56, 61
        };

        constexpr std::uintptr_t TARGET_FIELD_OFFSET = 0x50;
        constexpr std::uintptr_t MIDI_FIELD_OFFSET = 0x38;
        constexpr size_t SCAN_CHUNK_BYTES = 0x4000;
        constexpr size_t PATTERN_OVERLAP_BYTES =
            sizeof(TARGET_OFFSETS) - 1;
        constexpr size_t MAX_REGIONS = 4096;
        constexpr size_t MAX_PATTERN_HITS = 256;
        constexpr size_t MAX_SEED_REFERENCES = 1024;
        constexpr std::uintptr_t MAX_TWIN_REF_GAP = 0x40;
        constexpr size_t MAX_PAIR_CLUSTERS = 32;
        constexpr size_t SEED_PAIR_WINDOWS = 8;
        constexpr std::uintptr_t WINDOW_ALIGN = 0x100;
        constexpr std::uintptr_t WINDOW_SIZE = 0x100;
        constexpr int MAX_REVERSE_DEPTH = 4;
        constexpr size_t MAX_WINDOWS_PER_LEVEL = 24;
        constexpr size_t MAX_REVERSE_WINDOWS = 160;
        constexpr size_t MAX_REVERSE_HITS_PER_LEVEL = 2048;
        constexpr size_t MAX_AGGREGATES = 512;

        struct MemoryRegion
        {
            std::uintptr_t base = 0;
            size_t size = 0;
            DWORD protect = 0;
            DWORD type = 0;
            std::uintptr_t allocationBase = 0;
        };

        struct PatternHit
        {
            std::uintptr_t array = 0;
            std::uintptr_t owner = 0;
            bool midiMatches = false;
            std::uint32_t signature = 0;
        };

        struct SeedReference
        {
            std::uintptr_t location = 0;
            std::uintptr_t target = 0;
            DWORD type = 0;
            std::uintptr_t allocationBase = 0;
        };

        struct PairCluster
        {
            std::uintptr_t first = 0;
            std::uintptr_t second = 0;
            std::uintptr_t gap = 0;
        };

        struct ReverseWindow
        {
            std::uintptr_t start = 0;
            std::uintptr_t end = 0;
            int depth = 0;
            size_t child = static_cast<size_t>(-1);
            std::uintptr_t viaLocation = 0;
            std::uintptr_t viaTarget = 0;
            size_t hitCount = 0;
            DWORD sourceType = 0;
            std::uintptr_t sourceAllocationBase = 0;
        };

        struct ReverseHit
        {
            std::uintptr_t location = 0;
            std::uintptr_t target = 0;
            size_t childWindow = static_cast<size_t>(-1);
            DWORD sourceType = 0;
            std::uintptr_t sourceAllocationBase = 0;
        };

        struct WindowAggregate
        {
            std::uintptr_t start = 0;
            size_t count = 0;
            ReverseHit sample{};
            bool gameImage = false;
            bool image = false;
            bool rootNear = false;
            bool childNear = false;
        };

        // Fixed diagnostic storage lives in this DLL's image. Pointer scans
        // explicitly exclude the DLL allocation and this worker thread's stack,
        // so the scanner cannot discover its own search arrays/results and
        // manufacture fake reverse references.
        static std::array<MemoryRegion, MAX_REGIONS> privateRegions{};
        static std::array<MemoryRegion, MAX_REGIONS> pointerRegions{};
        static std::array<PatternHit, MAX_PATTERN_HITS> patternHits{};
        static std::array<SeedReference, MAX_SEED_REFERENCES> seedReferences{};
        static std::array<PairCluster, MAX_PAIR_CLUSTERS> pairClusters{};
        static std::array<ReverseWindow, MAX_REVERSE_WINDOWS> reverseWindows{};
        static std::array<ReverseHit, MAX_REVERSE_HITS_PER_LEVEL> reverseHits{};
        static std::array<WindowAggregate, MAX_AGGREGATES> aggregates{};
        static std::array<size_t, MAX_WINDOWS_PER_LEVEL> currentWindowIndices{};
        static std::array<size_t, MAX_WINDOWS_PER_LEVEL> nextWindowIndices{};
        static std::array<std::uint8_t,
            SCAN_CHUNK_BYTES + PATTERN_OVERLAP_BYTES> scanBuffer{};
        static int diagnosticImageMarker = 0;

        int stackMarker = 0;
        MEMORY_BASIC_INFORMATION diagnosticMbi{};
        MEMORY_BASIC_INFORMATION stackMbi{};
        VirtualQuery(
            &diagnosticImageMarker,
            &diagnosticMbi,
            sizeof(diagnosticMbi));
        VirtualQuery(
            &stackMarker,
            &stackMbi,
            sizeof(stackMbi));

        const std::uintptr_t diagnosticAllocationBase =
            reinterpret_cast<std::uintptr_t>(
                diagnosticMbi.AllocationBase);
        const std::uintptr_t workerStackAllocationBase =
            reinterpret_cast<std::uintptr_t>(
                stackMbi.AllocationBase);

        const HANDLE process = GetCurrentProcess();

        auto safeRead =
            [process](
                std::uintptr_t address,
                void* destination,
                size_t bytes) -> bool
            {
                if (!address || !destination || bytes == 0)
                    return false;

                SIZE_T copied = 0;
                return
                    ReadProcessMemory(
                        process,
                        reinterpret_cast<const void*>(address),
                        destination,
                        bytes,
                        &copied) != FALSE &&
                    copied == bytes;
            };

        auto safeReadPointer =
            [&safeRead](
                std::uintptr_t address,
                std::uintptr_t& value) -> bool
            {
                std::uint32_t raw = 0;
                if (!safeRead(address, &raw, sizeof(raw)))
                {
                    value = 0;
                    return false;
                }

                value = static_cast<std::uintptr_t>(raw);
                return true;
            };

        auto regionReadable =
            [](const MEMORY_BASIC_INFORMATION& mbi) -> bool
            {
                if (mbi.State != MEM_COMMIT ||
                    (mbi.Protect & PAGE_GUARD) ||
                    (mbi.Protect & PAGE_NOACCESS))
                {
                    return false;
                }

                const DWORD readable =
                    PAGE_READONLY |
                    PAGE_READWRITE |
                    PAGE_WRITECOPY |
                    PAGE_EXECUTE_READ |
                    PAGE_EXECUTE_READWRITE |
                    PAGE_EXECUTE_WRITECOPY;

                return (mbi.Protect & readable) != 0;
            };

        auto regionExecutable =
            [](DWORD protect) -> bool
            {
                const DWORD executable =
                    PAGE_EXECUTE |
                    PAGE_EXECUTE_READ |
                    PAGE_EXECUTE_READWRITE |
                    PAGE_EXECUTE_WRITECOPY;

                return (protect & executable) != 0;
            };

        auto collectRegions =
            [&regionReadable,
             &regionExecutable,
             diagnosticAllocationBase,
             workerStackAllocationBase](
                bool includeImage,
                MemoryRegion* output,
                size_t capacity) -> size_t
            {
                if (!output || capacity == 0)
                    return 0;

                SYSTEM_INFO systemInfo{};
                GetSystemInfo(&systemInfo);

                std::uintptr_t cursor =
                    reinterpret_cast<std::uintptr_t>(
                        systemInfo.lpMinimumApplicationAddress);
                const std::uintptr_t maximum =
                    reinterpret_cast<std::uintptr_t>(
                        systemInfo.lpMaximumApplicationAddress);
                size_t count = 0;

                while (cursor < maximum && count < capacity)
                {
                    MEMORY_BASIC_INFORMATION mbi{};
                    if (!VirtualQuery(
                            reinterpret_cast<const void*>(cursor),
                            &mbi,
                            sizeof(mbi)))
                    {
                        break;
                    }

                    const std::uintptr_t regionBase =
                        reinterpret_cast<std::uintptr_t>(
                            mbi.BaseAddress);
                    const std::uintptr_t regionSize =
                        static_cast<std::uintptr_t>(mbi.RegionSize);
                    const std::uintptr_t next =
                        regionBase + regionSize;

                    if (next <= cursor)
                        break;

                    const std::uintptr_t allocationBase =
                        reinterpret_cast<std::uintptr_t>(
                            mbi.AllocationBase);

                    const bool allowedType =
                        mbi.Type == MEM_PRIVATE ||
                        (includeImage && mbi.Type == MEM_IMAGE);

                    if (allowedType &&
                        regionReadable(mbi) &&
                        !regionExecutable(mbi.Protect) &&
                        allocationBase != diagnosticAllocationBase &&
                        allocationBase != workerStackAllocationBase)
                    {
                        output[count++] =
                        {
                            regionBase,
                            static_cast<size_t>(regionSize),
                            mbi.Protect,
                            mbi.Type,
                            allocationBase
                        };
                    }

                    cursor = next;
                }

                return count;
            };

        const size_t privateRegionCount =
            collectRegions(
                false,
                privateRegions.data(),
                privateRegions.size());
        const size_t pointerRegionCount =
            collectRegions(
                true,
                pointerRegions.data(),
                pointerRegions.size());

        SYSTEMTIME now{};
        GetLocalTime(&now);

        HMODULE gameModule = GetModuleHandleW(nullptr);
        const std::uintptr_t gameModuleBase =
            reinterpret_cast<std::uintptr_t>(gameModule);
        std::uintptr_t gameAllocationBase = 0;

        if (gameModule)
        {
            MEMORY_BASIC_INFORMATION gameMbi{};
            if (VirtualQuery(gameModule, &gameMbi, sizeof(gameMbi)))
            {
                gameAllocationBase =
                    reinterpret_cast<std::uintptr_t>(
                        gameMbi.AllocationBase);
            }
        }

        std::uintptr_t rootSlot = 0;
        std::uintptr_t rootObject = 0;
        std::uintptr_t knownChild = 0;

        if (gameModule)
        {
            const std::uintptr_t rootOffset =
                GetExecutableVersion() ==
                    ExecutableVersion::LearnAndPlay2024
                ? TUNER_TEXT_2024_ROOT_OFFSET
                : TUNER_TEXT_2022_ROOT;

            rootSlot = gameModuleBase + rootOffset;
            safeReadPointer(rootSlot, rootObject);

            if (rootObject)
            {
                safeReadPointer(
                    rootObject + 0x28,
                    knownChild);
            }
        }

        std::ostringstream log;
        log << "============================================================\n";
        log << "RL-Mods multiplayer tuner reverse owner locator\n";
        log << "BUILD: MP_TARGET_V5_REVERSE_OWNER_WALK\n";
        log << std::setfill('0')
            << std::dec
            << now.wYear << '-'
            << std::setw(2) << now.wMonth << '-'
            << std::setw(2) << now.wDay << ' '
            << std::setw(2) << now.wHour << ':'
            << std::setw(2) << now.wMinute << ':'
            << std::setw(2) << now.wSecond
            << "\n";
        log << "Current menu: " << CurrentMenuName() << "\n";
        log << "Builder hook: DISABLED\n";
        log << "Scanner self-filter: DLL image + worker stack excluded\n";
        log << "Expected P1/P2 offsets: [-5,-3,-3,-3,-3,-3]\n";
        log << "Expected MIDI: [35,42,47,52,56,61]\n\n";
        log << "Tuner root slot: " << AddressText(rootSlot) << "\n";
        log << "Tuner root object: " << AddressText(rootObject) << "\n";
        log << "Known SP child *(root+0x28): "
            << AddressText(knownChild) << "\n";
        log << "Game image allocation: "
            << AddressText(gameAllocationBase) << "\n";
        log << "Diagnostic image allocation excluded: "
            << AddressText(diagnosticAllocationBase) << "\n";
        log << "Worker stack allocation excluded: "
            << AddressText(workerStackAllocationBase) << "\n\n";
        log << "SCAN REGIONS\n";
        log << "  private pattern regions: " << privateRegionCount << "\n";
        log << "  private+image pointer regions: " << pointerRegionCount << "\n\n";

        // Locate the exact six-int target arrays and validate owner+0x38 MIDI.
        size_t patternHitCount = 0;
        const void* targetBytes =
            static_cast<const void*>(TARGET_OFFSETS.data());

        for (size_t r = 0;
             r < privateRegionCount && patternHitCount < MAX_PATTERN_HITS;
             ++r)
        {
            const MemoryRegion& region = privateRegions[r];

            for (size_t regionOffset = 0;
                 regionOffset < region.size &&
                 patternHitCount < MAX_PATTERN_HITS;)
            {
                const size_t primaryBytes =
                    (std::min)(
                        SCAN_CHUNK_BYTES,
                        region.size - regionOffset);
                const size_t extraBytes =
                    (std::min)(
                        PATTERN_OVERLAP_BYTES,
                        region.size - regionOffset - primaryBytes);
                const size_t requested = primaryBytes + extraBytes;
                SIZE_T copied = 0;

                if (!ReadProcessMemory(
                        process,
                        reinterpret_cast<const void*>(
                            region.base + regionOffset),
                        scanBuffer.data(),
                        requested,
                        &copied) ||
                    copied < sizeof(TARGET_OFFSETS))
                {
                    regionOffset += primaryBytes;
                    continue;
                }

                const size_t available = static_cast<size_t>(copied);
                size_t i = 0;
                while (i < primaryBytes &&
                       ((region.base + regionOffset + i) & 3u))
                {
                    ++i;
                }

                for (;
                     i < primaryBytes &&
                     i + sizeof(TARGET_OFFSETS) <= available &&
                     patternHitCount < MAX_PATTERN_HITS;
                     i += sizeof(std::uint32_t))
                {
                    if (std::memcmp(
                            scanBuffer.data() + i,
                            targetBytes,
                            sizeof(TARGET_OFFSETS)) != 0)
                    {
                        continue;
                    }

                    const std::uintptr_t arrayAddress =
                        region.base + regionOffset + i;

                    bool duplicate = false;
                    for (size_t h = 0; h < patternHitCount; ++h)
                    {
                        if (patternHits[h].array == arrayAddress)
                        {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate)
                        continue;

                    PatternHit hit{};
                    hit.array = arrayAddress;

                    if (arrayAddress >= TARGET_FIELD_OFFSET)
                    {
                        hit.owner = arrayAddress - TARGET_FIELD_OFFSET;
                        std::array<std::int32_t, 6> midi{};

                        if (safeRead(
                                hit.owner + MIDI_FIELD_OFFSET,
                                midi.data(),
                                sizeof(midi)) &&
                            midi == TARGET_MIDI)
                        {
                            hit.midiMatches = true;
                            safeRead(
                                hit.owner,
                                &hit.signature,
                                sizeof(hit.signature));
                        }
                    }

                    patternHits[patternHitCount++] = hit;
                }

                regionOffset += primaryBytes;
            }
        }

        log << "EXACT TARGET ARRAYS\n";
        log << "  count: " << patternHitCount << "\n";

        size_t validatedCount = 0;
        for (size_t i = 0; i < patternHitCount; ++i)
        {
            const PatternHit& hit = patternHits[i];
            log << "  HIT " << (i + 1)
                << " array=" << AddressText(hit.array)
                << " owner=" << AddressText(hit.owner)
                << " midi=" << (hit.midiMatches ? "MATCH" : "no");

            if (hit.midiMatches)
            {
                ++validatedCount;
                log << " signature="
                    << AddressText(
                        static_cast<std::uintptr_t>(hit.signature));
            }
            log << "\n";
        }
        log << "  validated owners: " << validatedCount << "\n\n";

        // Select the closest two validated owners sharing the same type/vtable
        // signature. This selected V4's two 0x011AC7EC live MP objects while
        // rejecting the unrelated validated copy with a different header.
        size_t twinA = static_cast<size_t>(-1);
        size_t twinB = static_cast<size_t>(-1);
        std::uintptr_t twinDistance = UINTPTR_MAX;

        for (size_t a = 0; a < patternHitCount; ++a)
        {
            if (!patternHits[a].midiMatches)
                continue;

            for (size_t b = a + 1; b < patternHitCount; ++b)
            {
                if (!patternHits[b].midiMatches ||
                    patternHits[a].signature != patternHits[b].signature)
                {
                    continue;
                }

                const std::uintptr_t ownerA = patternHits[a].owner;
                const std::uintptr_t ownerB = patternHits[b].owner;
                const std::uintptr_t distance =
                    ownerA > ownerB ? ownerA - ownerB : ownerB - ownerA;

                if (distance < twinDistance)
                {
                    twinDistance = distance;
                    twinA = a;
                    twinB = b;
                }
            }
        }

        std::array<std::uintptr_t, 2> twinOwners = { 0, 0 };
        log << "SELECTED TWIN LIVE OWNERS\n";

        if (twinA != static_cast<size_t>(-1) &&
            twinB != static_cast<size_t>(-1))
        {
            twinOwners[0] = patternHits[twinA].owner;
            twinOwners[1] = patternHits[twinB].owner;

            log << "  A owner=" << AddressText(twinOwners[0])
                << " array=" << AddressText(patternHits[twinA].array)
                << "\n";
            log << "  B owner=" << AddressText(twinOwners[1])
                << " array=" << AddressText(patternHits[twinB].array)
                << "\n";
            log << "  shared signature="
                << AddressText(
                    static_cast<std::uintptr_t>(
                        patternHits[twinA].signature))
                << "\n";
            log << "  owner distance=" << AddressText(twinDistance) << "\n";
        }
        else
        {
            log << "  FAILED: no matching twin owner pair\n";
        }

        // Direct reverse references to the two owners. Because all diagnostic
        // raw-address storage is in this DLL's excluded image and this worker's
        // stack is excluded, these references come from Rocksmith/other modules,
        // not from V5's own vectors or scan buffers.
        size_t seedReferenceCount = 0;

        if (twinOwners[0] && twinOwners[1])
        {
            for (size_t r = 0;
                 r < pointerRegionCount &&
                 seedReferenceCount < MAX_SEED_REFERENCES;
                 ++r)
            {
                const MemoryRegion& region = pointerRegions[r];

                for (size_t regionOffset = 0;
                     regionOffset < region.size &&
                     seedReferenceCount < MAX_SEED_REFERENCES;)
                {
                    const size_t primaryBytes =
                        (std::min)(
                            SCAN_CHUNK_BYTES,
                            region.size - regionOffset);
                    SIZE_T copied = 0;

                    if (!ReadProcessMemory(
                            process,
                            reinterpret_cast<const void*>(
                                region.base + regionOffset),
                            scanBuffer.data(),
                            primaryBytes,
                            &copied) ||
                        copied < sizeof(std::uint32_t))
                    {
                        regionOffset += primaryBytes;
                        continue;
                    }

                    const size_t available = static_cast<size_t>(copied);
                    size_t j = 0;
                    while (j < available &&
                           ((region.base + regionOffset + j) & 3u))
                    {
                        ++j;
                    }

                    for (;
                         j + sizeof(std::uint32_t) <= available &&
                         seedReferenceCount < MAX_SEED_REFERENCES;
                         j += sizeof(std::uint32_t))
                    {
                        std::uint32_t raw = 0;
                        std::memcpy(
                            &raw,
                            scanBuffer.data() + j,
                            sizeof(raw));

                        const std::uintptr_t value =
                            static_cast<std::uintptr_t>(raw);

                        if (value != twinOwners[0] &&
                            value != twinOwners[1])
                        {
                            continue;
                        }

                        seedReferences[seedReferenceCount++] =
                        {
                            region.base + regionOffset + j,
                            value,
                            region.type,
                            region.allocationBase
                        };
                    }

                    regionOffset += primaryBytes;
                }
            }
        }

        std::sort(
            seedReferences.begin(),
            seedReferences.begin() + seedReferenceCount,
            [](const SeedReference& a, const SeedReference& b)
            {
                return a.location < b.location;
            });

        log << "\nDIRECT REFERENCES TO TWIN OWNERS\n";
        log << "  count: " << seedReferenceCount << "\n";

        for (size_t i = 0; i < seedReferenceCount; ++i)
        {
            const SeedReference& ref = seedReferences[i];
            log << "  " << AddressText(ref.location)
                << " -> " << AddressText(ref.target)
                << (ref.target == twinOwners[0] ? " owner-A" : " owner-B");

            if (ref.allocationBase == gameAllocationBase &&
                ref.type == MEM_IMAGE)
            {
                log << "  *** GAME IMAGE/STATIC";
            }
            else if (ref.type == MEM_IMAGE)
            {
                log << "  [image]";
            }

            if (rootObject &&
                ref.location >= rootObject &&
                ref.location < rootObject + 0x1000)
            {
                log << "  *** ROOT +"
                    << AddressText(ref.location - rootObject);
            }

            if (knownChild &&
                ref.location >= knownChild &&
                ref.location < knownChild + 0x1000)
            {
                log << "  *** KNOWN CHILD +"
                    << AddressText(ref.location - knownChild);
            }

            log << "\n";
        }

        // Find compact locations that hold one pointer to each owner. V4's
        // strongest live candidate was exactly this shape at +0x08 spacing.
        size_t pairClusterCount = 0;

        for (size_t i = 0;
             i < seedReferenceCount && pairClusterCount < MAX_PAIR_CLUSTERS;
             ++i)
        {
            for (size_t j = i + 1;
                 j < seedReferenceCount && pairClusterCount < MAX_PAIR_CLUSTERS;
                 ++j)
            {
                const std::uintptr_t gap =
                    seedReferences[j].location - seedReferences[i].location;

                if (gap > MAX_TWIN_REF_GAP)
                    break;

                if (seedReferences[i].target == seedReferences[j].target)
                    continue;

                const std::uintptr_t key =
                    seedReferences[i].location &
                    ~static_cast<std::uintptr_t>(0x3F);
                bool duplicate = false;

                for (size_t k = 0; k < pairClusterCount; ++k)
                {
                    const std::uintptr_t existingKey =
                        pairClusters[k].first &
                        ~static_cast<std::uintptr_t>(0x3F);
                    if (existingKey == key)
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (duplicate)
                    continue;

                pairClusters[pairClusterCount++] =
                {
                    seedReferences[i].location,
                    seedReferences[j].location,
                    gap
                };
            }
        }

        std::sort(
            pairClusters.begin(),
            pairClusters.begin() + pairClusterCount,
            [](const PairCluster& a, const PairCluster& b)
            {
                if (a.gap != b.gap)
                    return a.gap < b.gap;
                return a.first < b.first;
            });

        log << "\nCLOSE TWIN-REFERENCE PAIRS\n";
        log << "  count: " << pairClusterCount << "\n";
        for (size_t i = 0; i < pairClusterCount; ++i)
        {
            log << "  PAIR " << (i + 1)
                << " " << AddressText(pairClusters[i].first)
                << " / " << AddressText(pairClusters[i].second)
                << " gap=" << AddressText(pairClusters[i].gap)
                << (i < SEED_PAIR_WINDOWS ? "  <seed>" : "")
                << "\n";
        }

        auto alignedWindowStart =
            [](std::uintptr_t address) -> std::uintptr_t
            {
                return address &
                    ~static_cast<std::uintptr_t>(WINDOW_ALIGN - 1);
            };

        size_t reverseWindowCount = 0;
        size_t currentWindowCount = 0;
        const size_t seedCount =
            (std::min)(pairClusterCount, SEED_PAIR_WINDOWS);

        for (size_t i = 0;
             i < seedCount &&
             reverseWindowCount < reverseWindows.size() &&
             currentWindowCount < currentWindowIndices.size();
             ++i)
        {
            const std::uintptr_t firstBlock =
                alignedWindowStart(pairClusters[i].first);
            const std::uintptr_t secondBlock =
                alignedWindowStart(pairClusters[i].second);
            const std::uintptr_t start =
                (std::min)(firstBlock, secondBlock);
            const std::uintptr_t end =
                (std::max)(firstBlock, secondBlock) + WINDOW_SIZE;

            bool duplicate = false;
            for (size_t k = 0; k < currentWindowCount; ++k)
            {
                const ReverseWindow& existing =
                    reverseWindows[currentWindowIndices[k]];
                if (existing.start == start && existing.end == end)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;

            ReverseWindow window{};
            window.start = start;
            window.end = end;
            window.depth = 0;
            reverseWindows[reverseWindowCount] = window;
            currentWindowIndices[currentWindowCount++] =
                reverseWindowCount++;
        }

        auto sortCurrentWindows =
            [](size_t* indices, size_t count)
            {
                std::sort(
                    indices,
                    indices + count,
                    [](size_t a, size_t b)
                    {
                        return reverseWindows[a].start <
                            reverseWindows[b].start;
                    });
            };

        auto findChildWindow =
            [](
                std::uintptr_t value,
                const size_t* indices,
                size_t count) -> size_t
            {
                if (!indices || count == 0)
                    return static_cast<size_t>(-1);

                size_t lo = 0;
                size_t hi = count;

                while (lo < hi)
                {
                    const size_t mid = lo + (hi - lo) / 2;
                    if (reverseWindows[indices[mid]].start <= value)
                        lo = mid + 1;
                    else
                        hi = mid;
                }

                if (lo == 0)
                    return static_cast<size_t>(-1);

                for (size_t probe = lo;
                     probe > 0 && lo - probe < 4;
                     --probe)
                {
                    const size_t index = indices[probe - 1];
                    const ReverseWindow& window = reverseWindows[index];
                    if (value >= window.start && value < window.end)
                        return index;
                }

                return static_cast<size_t>(-1);
            };

        auto logReversePath =
            [&log,
             reverseWindowCount](size_t index)
            {
                (void)reverseWindowCount;
                log << "      PATH";
                size_t current = index;
                size_t guard = 0;

                while (current != static_cast<size_t>(-1) &&
                       current < reverseWindows.size() &&
                       guard++ < 16)
                {
                    const ReverseWindow& window = reverseWindows[current];
                    log << " ["
                        << AddressText(window.start)
                        << ".."
                        << AddressText(window.end)
                        << ")";

                    if (window.child != static_cast<size_t>(-1))
                    {
                        log << " --"
                            << AddressText(window.viaLocation)
                            << "->"
                            << AddressText(window.viaTarget)
                            << "-->";
                    }

                    current = window.child;
                }
                log << "\n";
            };

        log << "\nREVERSE OWNER/CONTAINER WALK\n";
        log << "  seed windows: " << currentWindowCount << "\n";

        size_t totalReverseHits = 0;
        size_t gameStaticHits = 0;
        size_t rootNeighborhoodHits = 0;

        for (int depth = 1;
             depth <= MAX_REVERSE_DEPTH && currentWindowCount > 0;
             ++depth)
        {
            sortCurrentWindows(
                currentWindowIndices.data(),
                currentWindowCount);

            size_t reverseHitCount = 0;

            for (size_t r = 0;
                 r < pointerRegionCount &&
                 reverseHitCount < MAX_REVERSE_HITS_PER_LEVEL;
                 ++r)
            {
                const MemoryRegion& region = pointerRegions[r];

                for (size_t regionOffset = 0;
                     regionOffset < region.size &&
                     reverseHitCount < MAX_REVERSE_HITS_PER_LEVEL;)
                {
                    const size_t primaryBytes =
                        (std::min)(
                            SCAN_CHUNK_BYTES,
                            region.size - regionOffset);
                    SIZE_T copied = 0;

                    if (!ReadProcessMemory(
                            process,
                            reinterpret_cast<const void*>(
                                region.base + regionOffset),
                            scanBuffer.data(),
                            primaryBytes,
                            &copied) ||
                        copied < sizeof(std::uint32_t))
                    {
                        regionOffset += primaryBytes;
                        continue;
                    }

                    const size_t available = static_cast<size_t>(copied);
                    size_t j = 0;
                    while (j < available &&
                           ((region.base + regionOffset + j) & 3u))
                    {
                        ++j;
                    }

                    for (;
                         j + sizeof(std::uint32_t) <= available &&
                         reverseHitCount < MAX_REVERSE_HITS_PER_LEVEL;
                         j += sizeof(std::uint32_t))
                    {
                        std::uint32_t raw = 0;
                        std::memcpy(
                            &raw,
                            scanBuffer.data() + j,
                            sizeof(raw));

                        const std::uintptr_t value =
                            static_cast<std::uintptr_t>(raw);
                        const size_t child =
                            findChildWindow(
                                value,
                                currentWindowIndices.data(),
                                currentWindowCount);

                        if (child == static_cast<size_t>(-1))
                            continue;

                        reverseHits[reverseHitCount++] =
                        {
                            region.base + regionOffset + j,
                            value,
                            child,
                            region.type,
                            region.allocationBase
                        };
                    }

                    regionOffset += primaryBytes;
                }
            }

            totalReverseHits += reverseHitCount;
            size_t aggregateCount = 0;

            for (size_t i = 0; i < reverseHitCount; ++i)
            {
                const ReverseHit& hit = reverseHits[i];
                const std::uintptr_t start =
                    alignedWindowStart(hit.location);
                size_t aggregateIndex = static_cast<size_t>(-1);

                for (size_t a = 0; a < aggregateCount; ++a)
                {
                    if (aggregates[a].start == start)
                    {
                        aggregateIndex = a;
                        break;
                    }
                }

                if (aggregateIndex == static_cast<size_t>(-1))
                {
                    if (aggregateCount >= MAX_AGGREGATES)
                        continue;

                    aggregateIndex = aggregateCount++;
                    aggregates[aggregateIndex] = WindowAggregate{};
                    aggregates[aggregateIndex].start = start;
                    aggregates[aggregateIndex].sample = hit;
                }

                WindowAggregate& aggregate = aggregates[aggregateIndex];
                ++aggregate.count;
                aggregate.image =
                    aggregate.image || hit.sourceType == MEM_IMAGE;
                aggregate.gameImage =
                    aggregate.gameImage ||
                    (hit.sourceType == MEM_IMAGE &&
                     hit.sourceAllocationBase == gameAllocationBase);
                aggregate.rootNear =
                    aggregate.rootNear ||
                    (rootObject &&
                     hit.location >= rootObject &&
                     hit.location < rootObject + 0x1000);
                aggregate.childNear =
                    aggregate.childNear ||
                    (knownChild &&
                     hit.location >= knownChild &&
                     hit.location < knownChild + 0x1000);
            }

            std::sort(
                aggregates.begin(),
                aggregates.begin() + aggregateCount,
                [](const WindowAggregate& a,
                   const WindowAggregate& b)
                {
                    const int specialA =
                        (a.rootNear ? 8 : 0) +
                        (a.childNear ? 4 : 0) +
                        (a.gameImage ? 2 : 0) +
                        (a.image ? 1 : 0);
                    const int specialB =
                        (b.rootNear ? 8 : 0) +
                        (b.childNear ? 4 : 0) +
                        (b.gameImage ? 2 : 0) +
                        (b.image ? 1 : 0);

                    if (specialA != specialB)
                        return specialA > specialB;
                    if (a.count != b.count)
                        return a.count > b.count;
                    return a.start < b.start;
                });

            const size_t keepCount =
                (std::min)(aggregateCount, MAX_WINDOWS_PER_LEVEL);
            size_t nextWindowCount = 0;

            log << "  depth " << depth
                << ": refs=" << reverseHitCount
                << " source-windows=" << aggregateCount
                << " kept=" << keepCount << "\n";

            for (size_t i = 0;
                 i < keepCount &&
                 reverseWindowCount < reverseWindows.size() &&
                 nextWindowCount < nextWindowIndices.size();
                 ++i)
            {
                const WindowAggregate& aggregate = aggregates[i];
                ReverseWindow window{};
                window.start = aggregate.start;
                window.end = aggregate.start + WINDOW_SIZE;
                window.depth = depth;
                window.child = aggregate.sample.childWindow;
                window.viaLocation = aggregate.sample.location;
                window.viaTarget = aggregate.sample.target;
                window.hitCount = aggregate.count;
                window.sourceType = aggregate.sample.sourceType;
                window.sourceAllocationBase =
                    aggregate.sample.sourceAllocationBase;

                const size_t index = reverseWindowCount;
                reverseWindows[reverseWindowCount++] = window;
                nextWindowIndices[nextWindowCount++] = index;

                log << "    WINDOW " << (i + 1)
                    << " [" << AddressText(window.start)
                    << ".." << AddressText(window.end)
                    << ") refs=" << window.hitCount
                    << " sample=" << AddressText(window.viaLocation)
                    << " -> " << AddressText(window.viaTarget);

                bool important = false;

                if (aggregate.gameImage)
                {
                    ++gameStaticHits;
                    important = true;
                    log << "  *** GAME IMAGE/STATIC";
                }
                else if (aggregate.image)
                {
                    log << "  [image]";
                }

                if (aggregate.rootNear)
                {
                    ++rootNeighborhoodHits;
                    important = true;
                    log << "  *** TUNER ROOT NEIGHBORHOOD";
                }

                if (aggregate.childNear)
                {
                    ++rootNeighborhoodHits;
                    important = true;
                    log << "  *** KNOWN CHILD NEIGHBORHOOD";
                }

                if (rootSlot >= window.start && rootSlot < window.end)
                {
                    ++rootNeighborhoodHits;
                    important = true;
                    log << "  *** CONTAINS ROOT SLOT";
                }

                if (rootObject >= window.start && rootObject < window.end)
                {
                    ++rootNeighborhoodHits;
                    important = true;
                    log << "  *** CONTAINS ROOT OBJECT";
                }

                log << "\n";

                if (important)
                    logReversePath(index);
            }

            currentWindowCount = nextWindowCount;
            for (size_t i = 0; i < nextWindowCount; ++i)
                currentWindowIndices[i] = nextWindowIndices[i];
        }

        log << "\nSUMMARY\n";
        log << "  exact target arrays: " << patternHitCount << "\n";
        log << "  validated owners: " << validatedCount << "\n";
        log << "  direct refs to twin owners: " << seedReferenceCount << "\n";
        log << "  close twin-reference pairs: " << pairClusterCount << "\n";
        log << "  reverse refs across levels: " << totalReverseHits << "\n";
        log << "  game-image/static source windows: " << gameStaticHits << "\n";
        log << "  tuner-root/child source windows: "
            << rootNeighborhoodHits << "\n";
        log << "============================================================\n\n";

        const std::wstring path =
            BuildGamePath(L"RLMods_tuning_debug.txt");

        FILE* file = nullptr;
        if (_wfopen_s(
                &file,
                path.c_str(),
                L"a, ccs=UTF-8") != 0 ||
            !file)
        {
            return false;
        }

        const std::string text = log.str();
        std::fwprintf(file, L"%hs", text.c_str());
        std::fclose(file);
        return true;
    }

    bool TryReadArrangement(
        Tuning& tuning)
    {
        // RSMods' Whammy auto-tune does not use the in-song arrangement
        // pointer while the pre-song tuner is building. It waits for the
        // tuner's displayed tuning text, then uses the arrangement pointer
        // once gameplay has loaded. Do the same here, but non-blocking.
        const std::string menu =
            CurrentMenu();

        if (IsPreSongTunerMenu(menu))
        {
            return
                TryReadTunerTextTuning(
                    tuning);
        }

        if (TryReadArrangementMemory(
                tuning))
        {
            return true;
        }

        // If menu detection itself is unavailable but a tuner string exists,
        // accept it only for a menu name that clearly identifies a tuner.
        if (!menu.empty() &&
            menu.find("Tuner") !=
                std::string::npos)
        {
            return
                TryReadTunerTextTuning(
                    tuning);
        }

        return false;
    }

    bool TryReadReferenceHz(
        int& referenceHz)
    {
        const std::uintptr_t address =
            ResolveConfigured(
                TRUE_TUNING_2024_ROOT_OFFSET,
                TRUE_TUNING_2022_ROOT,
                TRUE_TUNING_OFFSETS);

        if (!address)
            return false;

        float raw = 0.0f;

        if (!TryReadValue(
                address,
                raw))
        {
            return false;
        }

        if (!std::isfinite(raw) ||
            raw <= 0.0f)
        {
            return false;
        }

        // Rocksmith uses A220-family values for some bass arrangements.
        // Normalize those to the corresponding A440-family reference so the
        // guitar-side pitch ratio does not accidentally add an octave.
        if (raw >= 200.0f &&
            raw <= 260.0f)
        {
            raw *= 2.0f;
        }

        const int rounded =
            static_cast<int>(
                std::lround(raw));

        if (rounded < 420 ||
            rounded > 461)
        {
            return false;
        }

        referenceHz = rounded;
        return true;
    }

    bool TryGetUniformShift(
        const Tuning& physical,
        const Tuning& target,
        int& semitones)
    {
        const int delta =
            target.strings[0] -
            physical.strings[0];

        for (size_t i = 1;
             i < physical.strings.size();
             ++i)
        {
            if (target.strings[i] -
                    physical.strings[i] !=
                delta)
            {
                return false;
            }
        }

        semitones = delta;
        return true;
    }

    Tuning Shifted(
        const Tuning& tuning,
        int semitones)
    {
        Tuning result = tuning;

        for (int& value :
             result.strings)
        {
            value += semitones;
        }

        return result;
    }

    std::string Name(
        const Tuning& tuning)
    {
        bool standard = true;

        for (size_t i = 1;
             i < tuning.strings.size();
             ++i)
        {
            if (tuning.strings[i] !=
                tuning.strings[0])
            {
                standard = false;
                break;
            }
        }

        if (standard)
        {
            return
                NoteNameFromOffset(
                    tuning.strings[0]);
        }

        const int upper =
            tuning.strings[1];

        bool drop = true;

        for (size_t i = 2;
             i < tuning.strings.size();
             ++i)
        {
            if (tuning.strings[i] !=
                upper)
            {
                drop = false;
                break;
            }
        }

        if (drop &&
            tuning.strings[0] ==
                upper - 2)
        {
            const std::string low =
                NoteNameFromOffset(
                    tuning.strings[0]);

            if (upper == 0)
                return "Drop " + low;

            return
                std::string(
                    NoteNameFromOffset(
                        upper)) +
                " Drop " +
                low;
        }

        return "Custom";
    }

    std::string VectorText(
        const Tuning& tuning)
    {
        char buffer[96] = {};

        sprintf_s(
            buffer,
            "[%d,%d,%d,%d,%d,%d]",
            tuning.strings[0],
            tuning.strings[1],
            tuning.strings[2],
            tuning.strings[3],
            tuning.strings[4],
            tuning.strings[5]);

        return buffer;
    }
}
