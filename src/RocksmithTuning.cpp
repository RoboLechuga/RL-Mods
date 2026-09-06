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
        // One-shot, read-only multiplayer tuner text locator.
        //
        // Do not walk arbitrary UI pointers: the tuner root reaches large
        // portions of Rocksmith's object graph and quickly explodes into
        // unrelated engine data. Instead, locate every live tuning-looking
        // string in private process memory, then work backward from the string
        // addresses to the objects that reference them.
        //
        // The proven single-player UI layout stores its displayed tuning text
        // pointer at tunerObject + 0x44. For every process-wide tuning-string
        // reference, test that same layout and then locate references to the
        // candidate tuner object itself. This gives us a direct way to find a
        // multiplayer sibling without any code hook.
        constexpr size_t MAX_STRING_LENGTH = 64;
        constexpr size_t MAX_TUNING_HITS = 256;
        constexpr size_t MAX_STRING_REFERENCES = 2048;
        constexpr size_t MAX_OBJECT_REFERENCES = 2048;
        constexpr std::uintptr_t UI_TEXT_FIELD_OFFSET = 0x44;
        constexpr size_t UI_OBJECT_INSPECT_BYTES = 0xA0;
        constexpr size_t ROOT_INSPECT_BYTES = 0x300;

        struct MemoryRegion
        {
            std::uintptr_t base = 0;
            size_t size = 0;
            DWORD protect = 0;
            DWORD type = 0;
        };

        struct TuningHit
        {
            std::uintptr_t address = 0;
            bool utf16 = false;
            std::string text;
            Tuning tuning{};
        };

        struct PointerReference
        {
            std::uintptr_t location = 0;
            std::uintptr_t target = 0;
        };

        struct UiCandidate
        {
            std::uintptr_t base = 0;
            std::uintptr_t textAddress = 0;
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

        auto memoryTypeName =
            [](DWORD type) -> const char*
            {
                switch (type)
                {
                case MEM_PRIVATE:
                    return "PRIVATE";
                case MEM_MAPPED:
                    return "MAPPED";
                case MEM_IMAGE:
                    return "IMAGE";
                default:
                    return "OTHER";
                }
            };

        auto collectPrivateRegions =
            [&regionReadable,
             &regionExecutable]()
                -> std::vector<MemoryRegion>
            {
                std::vector<MemoryRegion> regions;

                SYSTEM_INFO systemInfo{};
                GetSystemInfo(&systemInfo);

                std::uintptr_t cursor =
                    reinterpret_cast<std::uintptr_t>(
                        systemInfo.lpMinimumApplicationAddress);

                const std::uintptr_t maximum =
                    reinterpret_cast<std::uintptr_t>(
                        systemInfo.lpMaximumApplicationAddress);

                while (cursor < maximum)
                {
                    MEMORY_BASIC_INFORMATION mbi{};

                    if (!VirtualQuery(
                            reinterpret_cast<const void*>(
                                cursor),
                            &mbi,
                            sizeof(mbi)))
                    {
                        break;
                    }

                    const std::uintptr_t regionBase =
                        reinterpret_cast<std::uintptr_t>(
                            mbi.BaseAddress);

                    const std::uintptr_t regionSize =
                        static_cast<std::uintptr_t>(
                            mbi.RegionSize);

                    std::uintptr_t next = regionBase + regionSize;

                    if (next <= cursor)
                        break;

                    if (mbi.Type == MEM_PRIVATE &&
                        regionReadable(mbi) &&
                        !regionExecutable(mbi.Protect))
                    {
                        regions.push_back(
                            {
                                regionBase,
                                static_cast<size_t>(
                                    regionSize),
                                mbi.Protect,
                                mbi.Type
                            });
                    }

                    cursor = next;
                }

                return regions;
            };

        auto readPointerText =
            [](std::uintptr_t pointer) -> std::string
            {
                if (!pointer)
                    return {};

                return ReadString(
                    pointer,
                    128);
            };

        auto logUiField =
            [&readPointerText](
                std::ostringstream& log,
                std::uintptr_t base,
                std::uintptr_t offset,
                const char* label)
            {
                std::uintptr_t pointer = 0;

                log << "    " << label
                    << " +"
                    << AddressText(offset)
                    << ": ";

                if (!TryReadValue(
                        base + offset,
                        pointer))
                {
                    log << "<unreadable>\n";
                    return;
                }

                log << AddressText(pointer);

                const std::string text =
                    readPointerText(pointer);

                if (!text.empty())
                    log << " -> \"" << text << "\"";

                log << "\n";
            };

        SYSTEMTIME now{};
        GetLocalTime(&now);

        std::ostringstream log;

        log << "============================================================\n";
        log << "RL-Mods multiplayer tuner process text locator\n";
        log << "BUILD: MP_UI_TEXT_V2_PROCESS_LOCATOR\n";
        log << std::setfill('0')
            << std::dec
            << now.wYear << '-'
            << std::setw(2) << now.wMonth << '-'
            << std::setw(2) << now.wDay << ' '
            << std::setw(2) << now.wHour << ':'
            << std::setw(2) << now.wMinute << ':'
            << std::setw(2) << now.wSecond
            << "\n";
        log << "Current menu: "
            << CurrentMenuName()
            << "\n";
        log << "Builder hook: DISABLED\n";

        HMODULE gameModule =
            GetModuleHandleW(nullptr);

        std::uintptr_t rootSlot = 0;
        std::uintptr_t rootObject = 0;
        std::uintptr_t knownChild = 0;
        std::uintptr_t knownText = 0;

        if (gameModule)
        {
            const std::uintptr_t base =
                reinterpret_cast<std::uintptr_t>(
                    gameModule);

            const std::uintptr_t rootOffset =
                GetExecutableVersion() ==
                    ExecutableVersion::LearnAndPlay2024
                ? TUNER_TEXT_2024_ROOT_OFFSET
                : TUNER_TEXT_2022_ROOT;

            rootSlot = base + rootOffset;

            TryReadValue(
                rootSlot,
                rootObject);

            if (rootObject)
            {
                TryReadValue(
                    rootObject + 0x28,
                    knownChild);

                if (knownChild)
                {
                    TryReadValue(
                        knownChild +
                            UI_TEXT_FIELD_OFFSET,
                        knownText);
                }
            }
        }

        log << "Tuner root slot: "
            << AddressText(rootSlot)
            << "\n";
        log << "Tuner root object: "
            << AddressText(rootObject)
            << "\n";
        log << "Known SP child *(root+0x28): "
            << AddressText(knownChild)
            << "\n";
        log << "Known SP text *(child+0x44): "
            << AddressText(knownText);

        const std::string knownTextValue =
            readPointerText(
                knownText);

        if (!knownTextValue.empty())
            log << " -> \"" << knownTextValue << "\"";

        log << "\n\n";

        const std::vector<MemoryRegion> regions =
            collectPrivateRegions();

        size_t totalPrivateBytes = 0;

        for (const auto& region : regions)
            totalPrivateBytes += region.size;

        log << "PRIVATE READABLE NON-EXEC MEMORY\n";
        log << "  regions: " << regions.size() << "\n";
        log << "  bytes: " << totalPrivateBytes << "\n\n";

        std::vector<TuningHit> tuningHits;
        std::unordered_set<std::uintptr_t> hitAddresses;

        auto addHit =
            [&tuningHits,
             &hitAddresses](
                std::uintptr_t address,
                bool utf16,
                const std::string& text)
            {
                if (tuningHits.size() >=
                    MAX_TUNING_HITS)
                {
                    return;
                }

                if (hitAddresses.find(address) !=
                    hitAddresses.end())
                {
                    return;
                }

                Tuning parsed{};

                if (!TryLookupTuningText(
                        text,
                        parsed))
                {
                    return;
                }

                hitAddresses.insert(address);
                tuningHits.push_back(
                    {
                        address,
                        utf16,
                        text,
                        parsed
                    });
            };

        // Pass 1: locate live private-memory strings that our existing tuning
        // parser recognizes. This searches what Rocksmith actually has in
        // memory right now rather than guessing which UI branch owns it.
        for (const auto& region : regions)
        {
            if (tuningHits.size() >=
                MAX_TUNING_HITS)
            {
                break;
            }

            if (region.size == 0)
                continue;

            const auto* bytes =
                reinterpret_cast<const std::uint8_t*>(
                    region.base);

            // ASCII, null-terminated.
            for (size_t i = 0;
                 i < region.size &&
                 tuningHits.size() <
                     MAX_TUNING_HITS;)
            {
                const std::uint8_t first = bytes[i];

                if (first < 32 || first > 126)
                {
                    ++i;
                    continue;
                }

                if (i > 0 &&
                    bytes[i - 1] >= 32 &&
                    bytes[i - 1] <= 126)
                {
                    ++i;
                    continue;
                }

                std::string candidate;
                candidate.reserve(
                    MAX_STRING_LENGTH);

                size_t cursor = i;

                while (cursor < region.size &&
                       candidate.size() <
                           MAX_STRING_LENGTH)
                {
                    const std::uint8_t c =
                        bytes[cursor];

                    if (c == 0)
                        break;

                    if (c < 32 || c > 126)
                        break;

                    candidate.push_back(
                        static_cast<char>(c));
                    ++cursor;
                }

                if (candidate.size() >= 4 &&
                    cursor < region.size &&
                    bytes[cursor] == 0)
                {
                    addHit(
                        region.base + i,
                        false,
                        candidate);
                }

                i = cursor > i
                    ? cursor + 1
                    : i + 1;
            }

            // UTF-16 containing ordinary printable ASCII glyphs.
            for (size_t i = 0;
                 i + 1 < region.size &&
                 tuningHits.size() <
                     MAX_TUNING_HITS;
                 ++i)
            {
                if ((region.base + i) & 1)
                    continue;

                if (bytes[i] < 32 ||
                    bytes[i] > 126 ||
                    bytes[i + 1] != 0)
                {
                    continue;
                }

                if (i >= 2 &&
                    bytes[i - 2] >= 32 &&
                    bytes[i - 2] <= 126 &&
                    bytes[i - 1] == 0)
                {
                    continue;
                }

                std::string candidate;
                candidate.reserve(
                    MAX_STRING_LENGTH);

                size_t cursor = i;

                while (cursor + 1 <
                           region.size &&
                       candidate.size() <
                           MAX_STRING_LENGTH &&
                       bytes[cursor] >= 32 &&
                       bytes[cursor] <= 126 &&
                       bytes[cursor + 1] == 0)
                {
                    candidate.push_back(
                        static_cast<char>(
                            bytes[cursor]));
                    cursor += 2;
                }

                if (candidate.size() >= 4 &&
                    cursor + 1 <
                        region.size &&
                    bytes[cursor] == 0 &&
                    bytes[cursor + 1] == 0)
                {
                    addHit(
                        region.base + i,
                        true,
                        candidate);
                }
            }
        }

        std::sort(
            tuningHits.begin(),
            tuningHits.end(),
            [](const TuningHit& a,
               const TuningHit& b)
            {
                return a.address < b.address;
            });

        log << "PROCESS-WIDE TUNING TEXT HITS\n";
        log << "  count: "
            << tuningHits.size()
            << "\n";

        for (size_t i = 0;
             i < tuningHits.size();
             ++i)
        {
            const auto& hit = tuningHits[i];

            MEMORY_BASIC_INFORMATION mbi{};
            VirtualQuery(
                reinterpret_cast<const void*>(
                    hit.address),
                &mbi,
                sizeof(mbi));

            log << "  HIT " << (i + 1)
                << " "
                << (hit.utf16
                    ? "UTF16"
                    : "ASCII")
                << " @ "
                << AddressText(hit.address)
                << " type="
                << memoryTypeName(mbi.Type)
                << " text=\""
                << hit.text
                << "\" -> "
                << VectorText(hit.tuning)
                << " / "
                << Name(hit.tuning);

            if (hit.address == knownText)
                log << "  <KNOWN SP TEXT>";

            log << "\n";
        }

        if (tuningHits.empty())
        {
            log << "  No tuning-looking strings found in private memory.\n";
        }

        log << "\n";

        std::unordered_map<
            std::uintptr_t,
            size_t> hitIndex;

        for (size_t i = 0;
             i < tuningHits.size();
             ++i)
        {
            hitIndex[
                tuningHits[i].address] = i;
        }

        std::vector<
            PointerReference> stringReferences;

        // Pass 2: locate every private-memory dword that points directly at a
        // tuning-string hit. The single-player path's +0x44 field will appear
        // here naturally, and an MP sibling using the same layout should too.
        if (!hitIndex.empty())
        {
            for (const auto& region : regions)
            {
                if (stringReferences.size() >=
                    MAX_STRING_REFERENCES)
                {
                    break;
                }

                const std::uintptr_t begin =
                    (region.base + 3u) &
                    ~static_cast<std::uintptr_t>(3u);

                const std::uintptr_t end =
                    region.base +
                    static_cast<std::uintptr_t>(
                        region.size);

                for (std::uintptr_t address = begin;
                     address + sizeof(std::uint32_t) <= end &&
                     stringReferences.size() <
                         MAX_STRING_REFERENCES;
                     address += sizeof(std::uint32_t))
                {
                    const std::uint32_t value =
                        *reinterpret_cast<
                            const std::uint32_t*>(
                                address);

                    if (hitIndex.find(
                            static_cast<std::uintptr_t>(
                                value)) ==
                        hitIndex.end())
                    {
                        continue;
                    }

                    stringReferences.push_back(
                        {
                            address,
                            static_cast<std::uintptr_t>(
                                value)
                        });
                }
            }
        }

        log << "POINTER REFERENCES TO TUNING TEXT\n";
        log << "  count: "
            << stringReferences.size()
            << "\n";

        std::vector<UiCandidate> uiCandidates;
        std::unordered_set<std::uintptr_t>
            candidateBases;

        for (const auto& reference :
             stringReferences)
        {
            const auto hitIt =
                hitIndex.find(
                    reference.target);

            if (hitIt == hitIndex.end())
                continue;

            const TuningHit& hit =
                tuningHits[hitIt->second];

            log << "  REF "
                << AddressText(
                    reference.location)
                << " -> "
                << AddressText(
                    reference.target)
                << " \""
                << hit.text
                << "\"";

            if (reference.location >=
                UI_TEXT_FIELD_OFFSET)
            {
                const std::uintptr_t candidateBase =
                    reference.location -
                    UI_TEXT_FIELD_OFFSET;

                if (IsReadableRange(
                        reinterpret_cast<const void*>(
                            candidateBase),
                        UI_OBJECT_INSPECT_BYTES))
                {
                    log << "  candidateBase="
                        << AddressText(
                            candidateBase);

                    if (candidateBase == knownChild)
                        log << " <KNOWN SP CHILD>";

                    if (candidateBases.insert(
                            candidateBase).second)
                    {
                        uiCandidates.push_back(
                            {
                                candidateBase,
                                reference.target
                            });
                    }
                }
            }

            log << "\n";
        }

        if (stringReferences.empty())
            log << "  none\n";

        log << "\nP1-STYLE +0x44 UI OBJECT CANDIDATES\n";
        log << "  count: "
            << uiCandidates.size()
            << "\n";

        for (size_t i = 0;
             i < uiCandidates.size();
             ++i)
        {
            const auto& candidate =
                uiCandidates[i];

            log << "  CANDIDATE "
                << (i + 1)
                << " base="
                << AddressText(
                    candidate.base);

            if (candidate.base == knownChild)
                log << " <KNOWN SP CHILD>";

            log << "\n";

            logUiField(
                log,
                candidate.base,
                0x44,
                "text");
            logUiField(
                log,
                candidate.base,
                0x5C,
                "reference");
            logUiField(
                log,
                candidate.base,
                0x74,
                "previous/menu");
            logUiField(
                log,
                candidate.base,
                0x8C,
                "current menu");
        }

        log << "\n";

        // Pass 3: work backward one more level. Find every pointer to a
        // candidate UI object. If one lives directly in the known tuner root,
        // we immediately get the clean root + siblingOffset + 0x44 path.
        std::unordered_set<std::uintptr_t>
            objectTargets;

        for (const auto& candidate :
             uiCandidates)
        {
            objectTargets.insert(
                candidate.base);
        }

        std::vector<
            PointerReference> objectReferences;

        if (!objectTargets.empty())
        {
            for (const auto& region : regions)
            {
                if (objectReferences.size() >=
                    MAX_OBJECT_REFERENCES)
                {
                    break;
                }

                const std::uintptr_t begin =
                    (region.base + 3u) &
                    ~static_cast<std::uintptr_t>(3u);

                const std::uintptr_t end =
                    region.base +
                    static_cast<std::uintptr_t>(
                        region.size);

                for (std::uintptr_t address = begin;
                     address + sizeof(std::uint32_t) <= end &&
                     objectReferences.size() <
                         MAX_OBJECT_REFERENCES;
                     address += sizeof(std::uint32_t))
                {
                    const std::uintptr_t value =
                        static_cast<std::uintptr_t>(
                            *reinterpret_cast<
                                const std::uint32_t*>(
                                    address));

                    if (objectTargets.find(value) ==
                        objectTargets.end())
                    {
                        continue;
                    }

                    objectReferences.push_back(
                        {
                            address,
                            value
                        });
                }
            }
        }

        log << "POINTER REFERENCES TO P1-STYLE UI CANDIDATES\n";
        log << "  count: "
            << objectReferences.size()
            << "\n";

        for (const auto& reference :
             objectReferences)
        {
            log << "  OBJREF "
                << AddressText(
                    reference.location)
                << " -> "
                << AddressText(
                    reference.target);

            if (rootObject &&
                reference.location >= rootObject &&
                reference.location <
                    rootObject +
                        ROOT_INSPECT_BYTES)
            {
                log << "  *** DIRECT ROOT +"
                    << AddressText(
                        reference.location -
                        rootObject);
            }

            if (knownChild &&
                reference.location >= knownChild &&
                reference.location <
                    knownChild +
                        ROOT_INSPECT_BYTES)
            {
                log << "  inside knownChild +"
                    << AddressText(
                        reference.location -
                        knownChild);
            }

            log << "\n";
        }

        if (objectReferences.empty())
            log << "  none\n";

        log << "\nSUMMARY\n";
        log << "  private regions scanned: "
            << regions.size()
            << "\n";
        log << "  tuning text hits: "
            << tuningHits.size()
            << "\n";
        log << "  pointers to tuning text: "
            << stringReferences.size()
            << "\n";
        log << "  +0x44 UI candidates: "
            << uiCandidates.size()
            << "\n";
        log << "  pointers to UI candidates: "
            << objectReferences.size()
            << "\n";
        log << "============================================================\n\n";

        const std::wstring path =
            BuildGamePath(
                L"RLMods_tuning_debug.txt");

        FILE* file = nullptr;

        if (_wfopen_s(
                &file,
                path.c_str(),
                L"ab") != 0 ||
            !file)
        {
            return false;
        }

        const std::string text =
            log.str();

        const size_t written =
            fwrite(
                text.data(),
                1,
                text.size(),
                file);

        fclose(file);
        return written == text.size();
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
