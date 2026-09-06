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
        // V4 is deliberately narrow. The current multiplayer song has the
        // same custom tuning for P1 and P2: B F# B E G# C#.
        // Relative to E standard that is [-5,-3,-3,-3,-3,-3].
        // Earlier builder diagnostics proved that the live target object keeps
        // six int32 semitone offsets at +0x50 and the matching MIDI notes at
        // +0x38. Find that exact structure without hooks or code patching.
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
        constexpr size_t MAX_PATTERN_HITS = 256;
        constexpr size_t MAX_POINTER_REFERENCES = 2048;
        constexpr size_t OWNER_DUMP_BYTES = 0xA0;
        constexpr size_t ROOT_WALK_BYTES = 0x200;
        constexpr size_t ROOT_WALK_MAX_NODES = 12000;
        constexpr int ROOT_WALK_MAX_DEPTH = 5;

        struct MemoryRegion
        {
            std::uintptr_t base = 0;
            size_t size = 0;
            DWORD protect = 0;
        };

        struct PatternHit
        {
            std::uintptr_t array = 0;
            std::uintptr_t owner = 0;
            bool midiMatches = false;
        };

        struct PointerReference
        {
            std::uintptr_t location = 0;
            std::uintptr_t target = 0;
        };

        struct GraphNode
        {
            std::uintptr_t address = 0;
            size_t parent = static_cast<size_t>(-1);
            std::uintptr_t viaOffset = 0;
            int depth = 0;
        };

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

                if (!safeRead(
                        address,
                        &raw,
                        sizeof(raw)))
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
                        static_cast<std::uintptr_t>(
                            mbi.RegionSize);
                    const std::uintptr_t next =
                        regionBase + regionSize;

                    if (next <= cursor)
                        break;

                    if (mbi.Type == MEM_PRIVATE &&
                        regionReadable(mbi) &&
                        !regionExecutable(mbi.Protect))
                    {
                        regions.push_back(
                            {
                                regionBase,
                                static_cast<size_t>(regionSize),
                                mbi.Protect
                            });
                    }

                    cursor = next;
                }

                return regions;
            };

        auto isPrivateReadablePointer =
            [&regionReadable,
             &regionExecutable](
                std::uintptr_t address) -> bool
            {
                if (!address)
                    return false;

                MEMORY_BASIC_INFORMATION mbi{};

                if (!VirtualQuery(
                        reinterpret_cast<const void*>(address),
                        &mbi,
                        sizeof(mbi)))
                {
                    return false;
                }

                return
                    mbi.Type == MEM_PRIVATE &&
                    regionReadable(mbi) &&
                    !regionExecutable(mbi.Protect);
            };

        auto readObjectBytes =
            [process,
             &regionReadable,
             &regionExecutable](
                std::uintptr_t address,
                size_t wanted,
                std::vector<std::uint8_t>& buffer)
                -> size_t
            {
                buffer.clear();

                if (!address || wanted == 0)
                    return 0;

                MEMORY_BASIC_INFORMATION mbi{};

                if (!VirtualQuery(
                        reinterpret_cast<const void*>(address),
                        &mbi,
                        sizeof(mbi)) ||
                    mbi.Type != MEM_PRIVATE ||
                    !regionReadable(mbi) ||
                    regionExecutable(mbi.Protect))
                {
                    return 0;
                }

                const std::uintptr_t regionStart =
                    reinterpret_cast<std::uintptr_t>(
                        mbi.BaseAddress);
                const std::uintptr_t regionEnd =
                    regionStart +
                    static_cast<std::uintptr_t>(
                        mbi.RegionSize);

                if (address < regionStart ||
                    address >= regionEnd)
                {
                    return 0;
                }

                const size_t available =
                    static_cast<size_t>(regionEnd - address);
                const size_t bytes =
                    (std::min)(wanted, available);

                if (bytes == 0)
                    return 0;

                buffer.resize(bytes);
                SIZE_T copied = 0;

                if (!ReadProcessMemory(
                        process,
                        reinterpret_cast<const void*>(address),
                        buffer.data(),
                        bytes,
                        &copied) ||
                    copied == 0)
                {
                    buffer.clear();
                    return 0;
                }

                buffer.resize(
                    static_cast<size_t>(copied));
                return buffer.size();
            };

        SYSTEMTIME now{};
        GetLocalTime(&now);

        std::ostringstream log;

        log << "============================================================\n";
        log << "RL-Mods multiplayer tuner exact target locator\n";
        log << "BUILD: MP_TARGET_V4_CSHARP_DROP_B_EXACT\n";
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
        log << "Bulk memory reads: ReadProcessMemory snapshots\n";
        log << "Expected P1/P2 offsets: [-5,-3,-3,-3,-3,-3]\n";
        log << "Expected MIDI: [35,42,47,52,56,61]\n\n";

        HMODULE gameModule =
            GetModuleHandleW(nullptr);

        std::uintptr_t rootSlot = 0;
        std::uintptr_t rootObject = 0;
        std::uintptr_t knownChild = 0;

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
            safeReadPointer(rootSlot, rootObject);

            if (rootObject)
            {
                safeReadPointer(
                    rootObject + 0x28,
                    knownChild);
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
            << "\n\n";

        const std::vector<MemoryRegion> regions =
            collectPrivateRegions();

        size_t totalPrivateBytes = 0;
        for (const auto& region : regions)
            totalPrivateBytes += region.size;

        log << "PRIVATE READABLE NON-EXEC MEMORY\n";
        log << "  regions: " << regions.size() << "\n";
        log << "  bytes: " << totalPrivateBytes << "\n\n";

        std::vector<PatternHit> hits;
        std::unordered_set<std::uintptr_t> hitAddresses;
        std::vector<std::uint8_t> scanBuffer;

        const void* targetBytes =
            static_cast<const void*>(
                TARGET_OFFSETS.data());

        for (const auto& region : regions)
        {
            for (size_t regionOffset = 0;
                 regionOffset < region.size &&
                 hits.size() < MAX_PATTERN_HITS;)
            {
                const size_t primaryBytes =
                    (std::min)(
                        SCAN_CHUNK_BYTES,
                        region.size - regionOffset);

                const size_t extraBytes =
                    (std::min)(
                        PATTERN_OVERLAP_BYTES,
                        region.size -
                            regionOffset -
                            primaryBytes);

                const size_t requested =
                    primaryBytes + extraBytes;

                scanBuffer.resize(requested);
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

                const size_t available =
                    static_cast<size_t>(copied);

                size_t i = 0;
                while (i < primaryBytes &&
                       ((region.base + regionOffset + i) & 3u))
                {
                    ++i;
                }

                for (;
                     i < primaryBytes &&
                     i + sizeof(TARGET_OFFSETS) <= available &&
                     hits.size() < MAX_PATTERN_HITS;
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

                    if (!hitAddresses.insert(
                            arrayAddress).second)
                    {
                        continue;
                    }

                    PatternHit hit{};
                    hit.array = arrayAddress;

                    if (arrayAddress >= TARGET_FIELD_OFFSET)
                    {
                        hit.owner =
                            arrayAddress - TARGET_FIELD_OFFSET;

                        std::array<std::int32_t, 6> midi{};

                        if (safeRead(
                                hit.owner + MIDI_FIELD_OFFSET,
                                midi.data(),
                                sizeof(midi)) &&
                            midi == TARGET_MIDI)
                        {
                            hit.midiMatches = true;
                        }
                    }

                    hits.push_back(hit);
                }

                regionOffset += primaryBytes;
            }

            if (hits.size() >= MAX_PATTERN_HITS)
                break;
        }

        log << "EXACT OFFSET ARRAY HITS\n";
        log << "  count: " << hits.size() << "\n";

        size_t validatedCount = 0;
        std::unordered_map<std::uintptr_t, std::string> targetLabels;

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const PatternHit& hit = hits[i];

            log << "  HIT " << (i + 1)
                << " array=" << AddressText(hit.array)
                << " owner(-0x50)=" << AddressText(hit.owner)
                << " midi@owner+0x38="
                << (hit.midiMatches ? "MATCH" : "no")
                << "\n";

            targetLabels[hit.array] =
                std::string("array#") +
                std::to_string(i + 1);

            if (hit.midiMatches)
            {
                ++validatedCount;
                targetLabels[hit.owner] =
                    std::string("VALIDATED-owner#") +
                    std::to_string(i + 1);
            }
        }

        // Once the +0x38 MIDI signature validates an owner, ignore unrelated
        // copies of the same six semitone values for the expensive reference
        // and root-walk passes. If validation somehow finds none, fall back to
        // the raw exact-array hits so the dump is still diagnostic.
        std::unordered_set<std::uintptr_t> interestingTargets;

        if (validatedCount > 0)
        {
            for (const auto& hit : hits)
            {
                if (!hit.midiMatches)
                    continue;

                interestingTargets.insert(hit.array);
                interestingTargets.insert(hit.owner);
            }
        }
        else
        {
            for (const auto& hit : hits)
                interestingTargets.insert(hit.array);
        }

        if (hits.empty())
            log << "  none\n";

        log << "  validated owner count: "
            << validatedCount
            << "\n\n";

        log << "VALIDATED OWNER DWORD DUMPS\n";

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const PatternHit& hit = hits[i];
            if (!hit.midiMatches)
                continue;

            std::vector<std::uint8_t> ownerBytes;
            const size_t bytes =
                readObjectBytes(
                    hit.owner,
                    OWNER_DUMP_BYTES,
                    ownerBytes);

            log << "  OWNER " << (i + 1)
                << " " << AddressText(hit.owner)
                << " target=" << AddressText(hit.array)
                << "\n";

            if (bytes < sizeof(std::uint32_t))
            {
                log << "    <unreadable>\n";
                continue;
            }

            for (size_t offset = 0;
                 offset + sizeof(std::uint32_t) <= bytes;
                 offset += sizeof(std::uint32_t))
            {
                std::uint32_t raw = 0;
                std::memcpy(
                    &raw,
                    ownerBytes.data() + offset,
                    sizeof(raw));

                log << "    +"
                    << AddressText(offset)
                    << " = "
                    << AddressText(
                        static_cast<std::uintptr_t>(raw));

                if (offset >= MIDI_FIELD_OFFSET &&
                    offset < MIDI_FIELD_OFFSET +
                        sizeof(TARGET_MIDI))
                {
                    log << "  <MIDI>";
                }

                if (offset >= TARGET_FIELD_OFFSET &&
                    offset < TARGET_FIELD_OFFSET +
                        sizeof(TARGET_OFFSETS))
                {
                    log << "  <OFFSET>";
                }

                if (static_cast<std::uintptr_t>(raw) ==
                    hit.array)
                {
                    log << "  *** SELF TARGET POINTER";
                }

                log << "\n";
            }
        }

        if (validatedCount == 0)
            log << "  none\n";

        log << "\n";

        std::vector<PointerReference> references;

        if (!interestingTargets.empty())
        {
            for (const auto& region : regions)
            {
                for (size_t regionOffset = 0;
                     regionOffset < region.size &&
                     references.size() <
                         MAX_POINTER_REFERENCES;)
                {
                    const size_t primaryBytes =
                        (std::min)(
                            SCAN_CHUNK_BYTES,
                            region.size - regionOffset);

                    scanBuffer.resize(primaryBytes);
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

                    const size_t available =
                        static_cast<size_t>(copied);

                    size_t j = 0;
                    while (j < available &&
                           ((region.base + regionOffset + j) & 3u))
                    {
                        ++j;
                    }

                    for (;
                         j + sizeof(std::uint32_t) <= available &&
                         references.size() <
                             MAX_POINTER_REFERENCES;
                         j += sizeof(std::uint32_t))
                    {
                        std::uint32_t raw = 0;
                        std::memcpy(
                            &raw,
                            scanBuffer.data() + j,
                            sizeof(raw));

                        const std::uintptr_t value =
                            static_cast<std::uintptr_t>(raw);

                        if (interestingTargets.find(value) ==
                            interestingTargets.end())
                        {
                            continue;
                        }

                        references.push_back(
                            {
                                region.base + regionOffset + j,
                                value
                            });
                    }

                    regionOffset += primaryBytes;
                }

                if (references.size() >=
                    MAX_POINTER_REFERENCES)
                {
                    break;
                }
            }
        }

        log << "POINTER REFERENCES TO EXACT ARRAYS / VALIDATED OWNERS\n";
        log << "  count: " << references.size() << "\n";

        for (const auto& reference : references)
        {
            log << "  REF "
                << AddressText(reference.location)
                << " -> "
                << AddressText(reference.target);

            const auto labelIt =
                targetLabels.find(reference.target);
            if (labelIt != targetLabels.end())
                log << " " << labelIt->second;

            if (rootObject &&
                reference.location >= rootObject &&
                reference.location < rootObject + 0x1000)
            {
                log << "  *** ROOT +"
                    << AddressText(
                        reference.location - rootObject);
            }

            if (knownChild &&
                reference.location >= knownChild &&
                reference.location < knownChild + 0x1000)
            {
                log << "  *** KNOWN CHILD +"
                    << AddressText(
                        reference.location - knownChild);
            }

            for (size_t i = 0; i < hits.size(); ++i)
            {
                if (!hits[i].midiMatches)
                    continue;

                const std::uintptr_t owner = hits[i].owner;
                if (reference.location >= owner &&
                    reference.location < owner + 0x200)
                {
                    log << "  inside owner#"
                        << (i + 1)
                        << " +"
                        << AddressText(
                            reference.location - owner);
                }
            }

            log << "\n";
        }

        if (references.empty())
            log << "  none\n";

        log << "\nTARGETED FORWARD WALK FROM TUNER ROOT\n";

        std::vector<GraphNode> graph;
        std::deque<size_t> queue;
        std::unordered_set<std::uintptr_t> visited;
        size_t graphMatches = 0;

        if (rootObject &&
            isPrivateReadablePointer(rootObject))
        {
            graph.push_back(
                {
                    rootObject,
                    static_cast<size_t>(-1),
                    0,
                    0
                });
            queue.push_back(0);
            visited.insert(rootObject);
        }

        auto logGraphPath =
            [&log,
             &graph,
             &targetLabels](
                size_t nodeIndex,
                std::uintptr_t fieldOffset,
                std::uintptr_t target)
            {
                std::vector<std::uintptr_t> offsets;
                size_t current = nodeIndex;

                while (current !=
                       static_cast<size_t>(-1))
                {
                    const GraphNode& node =
                        graph[current];

                    if (node.parent !=
                        static_cast<size_t>(-1))
                    {
                        offsets.push_back(
                            node.viaOffset);
                    }

                    current = node.parent;
                }

                std::reverse(
                    offsets.begin(),
                    offsets.end());

                log << "  *** FOUND ROOT";

                for (const auto offset : offsets)
                    log << " -> +" << AddressText(offset);

                log << " -> +"
                    << AddressText(fieldOffset)
                    << " -> "
                    << AddressText(target);

                const auto labelIt =
                    targetLabels.find(target);
                if (labelIt != targetLabels.end())
                    log << " " << labelIt->second;

                log << "\n";
            };

        while (!queue.empty() &&
               graph.size() < ROOT_WALK_MAX_NODES)
        {
            const size_t nodeIndex = queue.front();
            queue.pop_front();

            const GraphNode node = graph[nodeIndex];
            std::vector<std::uint8_t> bytes;
            const size_t copied =
                readObjectBytes(
                    node.address,
                    ROOT_WALK_BYTES,
                    bytes);

            if (copied < sizeof(std::uint32_t))
                continue;

            for (size_t offset = 0;
                 offset + sizeof(std::uint32_t) <= copied;
                 offset += sizeof(std::uint32_t))
            {
                std::uint32_t raw = 0;
                std::memcpy(
                    &raw,
                    bytes.data() + offset,
                    sizeof(raw));

                const std::uintptr_t value =
                    static_cast<std::uintptr_t>(raw);

                if (interestingTargets.find(value) !=
                    interestingTargets.end())
                {
                    ++graphMatches;
                    logGraphPath(
                        nodeIndex,
                        offset,
                        value);
                }

                if (node.depth >= ROOT_WALK_MAX_DEPTH ||
                    !isPrivateReadablePointer(value) ||
                    !visited.insert(value).second)
                {
                    continue;
                }

                graph.push_back(
                    {
                        value,
                        nodeIndex,
                        static_cast<std::uintptr_t>(offset),
                        node.depth + 1
                    });

                queue.push_back(
                    graph.size() - 1);

                if (graph.size() >=
                    ROOT_WALK_MAX_NODES)
                {
                    break;
                }
            }
        }

        log << "  nodes visited: "
            << graph.size()
            << "\n";
        log << "  exact target matches from root: "
            << graphMatches
            << "\n";

        if (graph.size() >= ROOT_WALK_MAX_NODES)
            log << "  walk stopped at node limit\n";

        if (graphMatches == 0)
            log << "  no path found within depth/node limits\n";

        log << "\nSUMMARY\n";
        log << "  exact offset arrays: "
            << hits.size()
            << "\n";
        log << "  validated +0x50/+0x38 owners: "
            << validatedCount
            << "\n";
        log << "  pointer references: "
            << references.size()
            << "\n";
        log << "  root-walk exact matches: "
            << graphMatches
            << "\n";
        log << "============================================================\n\n";

        const std::wstring path =
            BuildGamePath(
                L"RLMods_tuning_debug.txt");

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

        std::fwprintf(
            file,
            L"%hs",
            text.c_str());

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
