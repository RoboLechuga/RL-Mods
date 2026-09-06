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
#include <unordered_set>
#include <utility>

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
        // One-shot, read-only diagnostic for the multiplayer tuner UI.
        //
        // This intentionally does not use the reference-builder hook. It starts
        // at the same root used by the proven single-player tuner-text reader,
        // then walks nearby heap objects and records printable text plus the
        // pointer path that reached it. Any text that our existing tuning-name
        // parser recognizes is marked prominently in the log.
        constexpr int MAX_DEPTH = 5;
        constexpr size_t MAX_NODES = 768;
        constexpr size_t OBJECT_SCAN_BYTES = 0x300;
        constexpr size_t MAX_TEXT_LENGTH = 128;
        constexpr size_t MAX_TEXT_HITS = 2500;

        struct UiNode
        {
            std::uintptr_t address = 0;
            int depth = 0;
            std::string path;
        };

        SYSTEMTIME now{};
        GetLocalTime(&now);

        std::ostringstream log;

        log << "============================================================\n";
        log << "RL-Mods multiplayer tuner UI text diagnostic\n";
        log << "BUILD: MP_UI_TEXT_V1_READ_ONLY\n";
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

        if (!gameModule)
        {
            log << "Game module: unavailable\n";
        }
        else
        {
            const std::uintptr_t base =
                reinterpret_cast<std::uintptr_t>(
                    gameModule);

            const std::uintptr_t rootOffset =
                GetExecutableVersion() ==
                    ExecutableVersion::LearnAndPlay2024
                ? TUNER_TEXT_2024_ROOT_OFFSET
                : TUNER_TEXT_2022_ROOT;

            const std::uintptr_t rootSlot =
                base + rootOffset;

            std::uintptr_t rootObject = 0;

            log << "Tuner root slot: "
                << AddressText(rootSlot)
                << "\n";

            if (!TryReadValue(
                    rootSlot,
                    rootObject) ||
                !rootObject)
            {
                log << "Tuner root object: unavailable\n";
            }
            else
            {
                log << "Tuner root object: "
                    << AddressText(rootObject)
                    << "\n";

                // Show the existing single-player text chain explicitly first.
                std::uintptr_t knownChild = 0;
                std::uintptr_t knownText = 0;

                if (TryReadValue(
                        rootObject + 0x28,
                        knownChild) &&
                    knownChild)
                {
                    log << "Known SP child *(root+0x28): "
                        << AddressText(knownChild)
                        << "\n";

                    if (TryReadValue(
                            knownChild + 0x44,
                            knownText) &&
                        knownText)
                    {
                        const std::string known =
                            ReadString(
                                knownText,
                                MAX_TEXT_LENGTH);

                        log << "Known SP text *(child+0x44): "
                            << AddressText(knownText)
                            << " -> \""
                            << known
                            << "\"\n";
                    }
                    else
                    {
                        log << "Known SP text *(child+0x44): null/unreadable\n";
                    }
                }
                else
                {
                    log << "Known SP child *(root+0x28): null/unreadable\n";
                }

                auto queryReadable =
                    [](std::uintptr_t address,
                       MEMORY_BASIC_INFORMATION& mbi) -> bool
                    {
                        if (address < 0x10000)
                            return false;

                        if (!VirtualQuery(
                                reinterpret_cast<const void*>(
                                    address),
                                &mbi,
                                sizeof(mbi)))
                        {
                            return false;
                        }

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

                auto readAscii =
                    [&queryReadable](std::uintptr_t address,
                                     size_t maxLength) -> std::string
                    {
                        MEMORY_BASIC_INFORMATION mbi{};

                        if (!queryReadable(address, mbi))
                            return {};

                        const std::uintptr_t regionEnd =
                            reinterpret_cast<std::uintptr_t>(
                                mbi.BaseAddress) +
                            static_cast<std::uintptr_t>(
                                mbi.RegionSize);

                        const size_t available =
                            static_cast<size_t>(
                                regionEnd - address);

                        const size_t limit =
                            available < maxLength
                            ? available
                            : maxLength;

                        const auto* bytes =
                            reinterpret_cast<const unsigned char*>(
                                address);

                        std::string text;
                        text.reserve(limit);

                        for (size_t i = 0;
                             i < limit;
                             ++i)
                        {
                            const unsigned char c = bytes[i];

                            if (c == 0)
                                break;

                            if (c < 32 || c > 126)
                                return {};

                            text.push_back(
                                static_cast<char>(c));
                        }

                        return text;
                    };

                auto readUtf16Ascii =
                    [&queryReadable](std::uintptr_t address,
                                     size_t maxLength) -> std::string
                    {
                        MEMORY_BASIC_INFORMATION mbi{};

                        if (!queryReadable(address, mbi))
                            return {};

                        const std::uintptr_t regionEnd =
                            reinterpret_cast<std::uintptr_t>(
                                mbi.BaseAddress) +
                            static_cast<std::uintptr_t>(
                                mbi.RegionSize);

                        const size_t availableBytes =
                            static_cast<size_t>(
                                regionEnd - address);

                        const size_t availableChars =
                            availableBytes /
                            sizeof(std::uint16_t);

                        const size_t limit =
                            availableChars < maxLength
                            ? availableChars
                            : maxLength;

                        const auto* chars =
                            reinterpret_cast<const std::uint16_t*>(
                                address);

                        std::string text;
                        text.reserve(limit);

                        for (size_t i = 0;
                             i < limit;
                             ++i)
                        {
                            const std::uint16_t c = chars[i];

                            if (c == 0)
                                break;

                            if (c < 32 || c > 126)
                                return {};

                            text.push_back(
                                static_cast<char>(c));
                        }

                        return text;
                    };

                auto isHeapPointer =
                    [](std::uintptr_t address) -> bool
                    {
                        if (address < 0x10000 ||
                            (address & 0x3) != 0)
                        {
                            return false;
                        }

                        MEMORY_BASIC_INFORMATION mbi{};

                        if (!VirtualQuery(
                                reinterpret_cast<const void*>(
                                    address),
                                &mbi,
                                sizeof(mbi)))
                        {
                            return false;
                        }

                        if (mbi.State != MEM_COMMIT ||
                            (mbi.Protect & PAGE_GUARD) ||
                            (mbi.Protect & PAGE_NOACCESS))
                        {
                            return false;
                        }

                        const DWORD executable =
                            PAGE_EXECUTE |
                            PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE |
                            PAGE_EXECUTE_WRITECOPY;

                        if (mbi.Protect & executable)
                            return false;

                        // The tuner UI objects we care about are heap/private
                        // objects. Avoid wandering through the executable image,
                        // vtables and mapped file data.
                        return mbi.Type == MEM_PRIVATE;
                    };

                auto logText =
                    [&log](const char* kind,
                           const std::string& path,
                           std::uintptr_t address,
                           const std::string& text,
                           size_t& textHits)
                    {
                        if (text.size() < 4)
                            return;

                        ++textHits;

                        Tuning parsed{};
                        const bool tuningText =
                            TryLookupTuningText(
                                text,
                                parsed);

                        if (tuningText)
                        {
                            log << "*** TUNING-TEXT ";
                        }

                        log << kind
                            << ' '
                            << path
                            << " @ "
                            << AddressText(address)
                            << " = \""
                            << text
                            << "\"";

                        if (tuningText)
                        {
                            log << " -> "
                                << VectorText(parsed)
                                << " / "
                                << Name(parsed);
                        }

                        log << "\n";
                    };

                std::deque<UiNode> queue;
                std::unordered_set<std::uintptr_t> visited;

                // Seed the root and the known SP child first so the familiar
                // branch is examined before the broader graph consumes nodes.
                queue.push_back(
                    { rootObject, 0, "ROOT" });

                if (knownChild &&
                    knownChild != rootObject)
                {
                    queue.push_back(
                        { knownChild, 1, "ROOT/+0x28" });
                }

                size_t nodesScanned = 0;
                size_t textHits = 0;

                log << "\nUI OBJECT GRAPH TEXT WALK\n";
                log << "  maxDepth=" << MAX_DEPTH
                    << " maxNodes=" << MAX_NODES
                    << " scanBytesPerObject=0x"
                    << std::hex << std::uppercase
                    << OBJECT_SCAN_BYTES
                    << std::dec
                    << "\n";

                while (!queue.empty() &&
                       nodesScanned < MAX_NODES &&
                       textHits < MAX_TEXT_HITS)
                {
                    UiNode node =
                        std::move(queue.front());
                    queue.pop_front();

                    if (!node.address ||
                        visited.find(node.address) !=
                            visited.end())
                    {
                        continue;
                    }

                    visited.insert(node.address);
                    ++nodesScanned;

                    if (!IsReadableRange(
                            reinterpret_cast<const void*>(
                                node.address),
                            OBJECT_SCAN_BYTES))
                    {
                        continue;
                    }

                    std::array<
                        std::uint8_t,
                        OBJECT_SCAN_BYTES> bytes{};

                    std::memcpy(
                        bytes.data(),
                        reinterpret_cast<const void*>(
                            node.address),
                        bytes.size());

                    log << "\nNODE "
                        << nodesScanned
                        << " depth="
                        << node.depth
                        << " path="
                        << node.path
                        << " address="
                        << AddressText(node.address)
                        << "\n";

                    // Inline ASCII strings inside the object itself.
                    for (size_t i = 0;
                         i < bytes.size() &&
                         textHits < MAX_TEXT_HITS;)
                    {
                        if (bytes[i] < 32 ||
                            bytes[i] > 126)
                        {
                            ++i;
                            continue;
                        }

                        const size_t start = i;
                        std::string text;

                        while (i < bytes.size() &&
                               bytes[i] >= 32 &&
                               bytes[i] <= 126 &&
                               text.size() < MAX_TEXT_LENGTH)
                        {
                            text.push_back(
                                static_cast<char>(bytes[i]));
                            ++i;
                        }

                        if (text.size() >= 4)
                        {
                            std::ostringstream pathText;
                            pathText << node.path
                                     << "/inline+0x"
                                     << std::hex
                                     << std::uppercase
                                     << start;

                            logText(
                                "INLINE-ASCII",
                                pathText.str(),
                                node.address + start,
                                text,
                                textHits);
                        }
                    }

                    // Inline UTF-16 strings containing ordinary ASCII glyphs.
                    for (size_t i = 0;
                         i + 7 < bytes.size() &&
                         textHits < MAX_TEXT_HITS;
                         ++i)
                    {
                        if (bytes[i] < 32 ||
                            bytes[i] > 126 ||
                            bytes[i + 1] != 0)
                        {
                            continue;
                        }

                        const size_t start = i;
                        std::string text;
                        size_t cursor = i;

                        while (cursor + 1 < bytes.size() &&
                               bytes[cursor] >= 32 &&
                               bytes[cursor] <= 126 &&
                               bytes[cursor + 1] == 0 &&
                               text.size() < MAX_TEXT_LENGTH)
                        {
                            text.push_back(
                                static_cast<char>(
                                    bytes[cursor]));
                            cursor += 2;
                        }

                        if (text.size() >= 4)
                        {
                            std::ostringstream pathText;
                            pathText << node.path
                                     << "/inline16+0x"
                                     << std::hex
                                     << std::uppercase
                                     << start;

                            logText(
                                "INLINE-UTF16",
                                pathText.str(),
                                node.address + start,
                                text,
                                textHits);

                            i = cursor - 1;
                        }
                    }

                    // Every 32-bit field is treated as a possible pointer. For
                    // each readable target, first test whether it is text. If it
                    // is not text and still looks like heap/object memory, queue
                    // it for another level of the graph walk.
                    for (size_t offset = 0;
                         offset + sizeof(std::uintptr_t) <=
                             bytes.size() &&
                         textHits < MAX_TEXT_HITS;
                         offset += sizeof(std::uintptr_t))
                    {
                        std::uintptr_t pointer = 0;

                        std::memcpy(
                            &pointer,
                            bytes.data() + offset,
                            sizeof(pointer));

                        if (!pointer)
                            continue;

                        const std::string ascii =
                            readAscii(
                                pointer,
                                MAX_TEXT_LENGTH);

                        const std::string utf16 =
                            ascii.empty()
                            ? readUtf16Ascii(
                                pointer,
                                MAX_TEXT_LENGTH)
                            : std::string{};

                        std::ostringstream fieldPath;
                        fieldPath << node.path
                                  << "/+0x"
                                  << std::hex
                                  << std::uppercase
                                  << offset;

                        if (ascii.size() >= 4)
                        {
                            logText(
                                "PTR-ASCII",
                                fieldPath.str(),
                                pointer,
                                ascii,
                                textHits);
                            continue;
                        }

                        if (utf16.size() >= 4)
                        {
                            logText(
                                "PTR-UTF16",
                                fieldPath.str(),
                                pointer,
                                utf16,
                                textHits);
                            continue;
                        }

                        if (node.depth < MAX_DEPTH &&
                            isHeapPointer(pointer) &&
                            visited.find(pointer) ==
                                visited.end())
                        {
                            queue.push_back(
                                {
                                    pointer,
                                    node.depth + 1,
                                    fieldPath.str()
                                });
                        }
                    }
                }

                log << "\nSUMMARY\n";
                log << "  nodes scanned: "
                    << nodesScanned
                    << "\n";
                log << "  printable text hits: "
                    << textHits
                    << "\n";
                log << "  queued nodes remaining at cap: "
                    << queue.size()
                    << "\n";
            }
        }

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
