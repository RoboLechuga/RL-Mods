#include "RocksmithTuning.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

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

        // Multiplayer chart-target capture.
        //
        // The tuning-reference builder receives the player detection object in
        // ESI. On the arrangement-bearing builder call, stack + 0x20 points to
        // an arrangement object whose +0x50 field is the authored int32[6]
        // string tuning. This path was established from a D-standard P1 / E-
        // standard P2 multiplayer capture, where the two arrays resolved to
        // [-2,-2,-2,-2,-2,-2] and [0,0,0,0,0,0] respectively.
        //
        // The hook only copies a validated six-int target and the detection
        // identity. No heap scanning or pointer-graph walking occurs in normal
        // operation.
        constexpr std::uintptr_t REFERENCE_BUILDER_2022 =
            0x004DCCB0;
        constexpr std::uintptr_t REFERENCE_BUILDER_2024_OFFSET =
            0x002AD930;
        constexpr std::uintptr_t BUILDER_CHART_OBJECT_STACK_OFFSET =
            0x20;
        constexpr std::uintptr_t BUILDER_CHART_TUNING_OFFSET =
            0x50;
        constexpr LONG TARGET_CAPTURE_SLOTS = 16;
        constexpr DWORD TRAP_FLAG = 0x100;
        constexpr ULONGLONG TARGET_PAIR_MAX_SEPARATION_MS = 250;

        struct TunerTargetCapture
        {
            volatile LONG sequence = 0;
            std::uintptr_t detection = 0;
            std::array<int, 6> strings{};
            LONG playerTag = -1;
            DWORD threadId = 0;
            ULONGLONG tick = 0;
        };

        TunerTargetCapture g_targetCaptures[TARGET_CAPTURE_SLOTS]{};
        volatile LONG g_targetCaptureCount = 0;

        std::uintptr_t g_referenceBuilder = 0;
        BYTE g_referenceBuilderOriginalByte = 0;
        PVOID g_targetVehHandle = nullptr;
        volatile LONG g_targetHookInstalled = 0;
        __declspec(thread) LONG g_targetSingleStepActive = 0;

        std::uintptr_t ResolveReferenceBuilder()
        {
            if (GetExecutableVersion() ==
                ExecutableVersion::Remastered2022)
            {
                return REFERENCE_BUILDER_2022;
            }

            HMODULE gameModule =
                GetModuleHandleW(nullptr);

            if (!gameModule)
                return 0;

            return
                reinterpret_cast<std::uintptr_t>(
                    gameModule) +
                REFERENCE_BUILDER_2024_OFFSET;
        }

        std::uintptr_t ResolvePlayerOneDetectionObject()
        {
            constexpr std::array<std::uintptr_t, 3>
                DETECTION_OBJECT_OFFSETS =
            {
                0x10,
                0x4,
                0x0
            };

            return
                ResolveConfigured(
                    TRUE_TUNING_2024_ROOT_OFFSET,
                    TRUE_TUNING_2022_ROOT,
                    DETECTION_OBJECT_OFFSETS);
        }

        bool WriteReferenceBuilderByte(
            BYTE value)
        {
            if (!g_referenceBuilder)
                return false;

            DWORD oldProtect = 0;

            if (!VirtualProtect(
                    reinterpret_cast<void*>(
                        g_referenceBuilder),
                    1,
                    PAGE_EXECUTE_READWRITE,
                    &oldProtect))
            {
                return false;
            }

            *reinterpret_cast<volatile BYTE*>(
                g_referenceBuilder) = value;

            FlushInstructionCache(
                GetCurrentProcess(),
                reinterpret_cast<const void*>(
                    g_referenceBuilder),
                1);

            DWORD unusedProtect = 0;
            VirtualProtect(
                reinterpret_cast<void*>(
                    g_referenceBuilder),
                1,
                oldProtect,
                &unusedProtect);

            return true;
        }

        bool TryReadBuilderChartTarget(
            const CONTEXT* context,
            std::array<int, 6>& strings,
            LONG& playerTagOut)
        {
#if defined(_M_IX86)
            if (!context)
                return false;

            // The exhaustive multiplayer capture identified one specific
            // builder-call shape that carries the chart tuning. Require that
            // shape instead of accepting any nearby memory that merely looks
            // like six sane integers.
            constexpr std::uintptr_t PLAYER_TAG_STACK_OFFSET = 0x24;
            constexpr std::uintptr_t STRING_COUNT_STACK_OFFSET = 0x30;
            constexpr std::uintptr_t PLAYER_TAG_CONFIRM_STACK_OFFSET = 0x48;
            constexpr std::uintptr_t DIRECT_TUNING_STACK_OFFSET = 0x88;

            std::uintptr_t chartObject = 0;
            std::uintptr_t directTuning = 0;
            LONG playerTag = -1;
            LONG confirmedPlayerTag = -1;
            LONG stringCount = 0;
            LONG rawStrings[6] = {};

            __try
            {
                chartObject =
                    *reinterpret_cast<const std::uintptr_t*>(
                        context->Esp +
                        BUILDER_CHART_OBJECT_STACK_OFFSET);

                playerTag =
                    *reinterpret_cast<const LONG*>(
                        context->Esp +
                        PLAYER_TAG_STACK_OFFSET);

                stringCount =
                    *reinterpret_cast<const LONG*>(
                        context->Esp +
                        STRING_COUNT_STACK_OFFSET);

                confirmedPlayerTag =
                    *reinterpret_cast<const LONG*>(
                        context->Esp +
                        PLAYER_TAG_CONFIRM_STACK_OFFSET);

                directTuning =
                    *reinterpret_cast<const std::uintptr_t*>(
                        context->Esp +
                        DIRECT_TUNING_STACK_OFFSET);

                if (chartObject == 0 ||
                    stringCount != 6 ||
                    playerTag != confirmedPlayerTag ||
                    (playerTag != 0 && playerTag != 2) ||
                    directTuning !=
                        chartObject +
                        BUILDER_CHART_TUNING_OFFSET)
                {
                    return false;
                }

                for (int i = 0; i < 6; ++i)
                {
                    rawStrings[i] =
                        *reinterpret_cast<const LONG*>(
                            directTuning +
                            (i * sizeof(LONG)));
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }

            for (int i = 0; i < 6; ++i)
            {
                if (rawStrings[i] < -24 ||
                    rawStrings[i] > 24)
                {
                    return false;
                }

                strings[i] =
                    static_cast<int>(
                        rawStrings[i]);
            }

            playerTagOut = playerTag;
            return true;
#else
            (void)context;
            (void)strings;
            (void)playerTagOut;
            return false;
#endif
        }

        void PublishTargetCapture(
            const CONTEXT* context)
        {
#if defined(_M_IX86)
            if (!context || context->Esi == 0)
                return;

            std::array<int, 6> strings{};
            LONG playerTag = -1;

            if (!TryReadBuilderChartTarget(
                    context,
                    strings,
                    playerTag))
            {
                return;
            }

            const LONG sequence =
                InterlockedIncrement(
                    &g_targetCaptureCount);

            TunerTargetCapture& capture =
                g_targetCaptures[
                    (sequence - 1) &
                    (TARGET_CAPTURE_SLOTS - 1)];

            capture.detection =
                static_cast<std::uintptr_t>(
                    context->Esi);
            capture.strings = strings;
            capture.playerTag = playerTag;
            capture.threadId =
                GetCurrentThreadId();
            capture.tick =
                GetTickCount64();

            MemoryBarrier();
            InterlockedExchange(
                &capture.sequence,
                sequence);
#else
            (void)context;
#endif
        }

        LONG CALLBACK TunerTargetBuilderVeh(
            PEXCEPTION_POINTERS exceptionInfo)
        {
#if defined(_M_IX86)
            if (!exceptionInfo ||
                !exceptionInfo->ExceptionRecord ||
                !exceptionInfo->ContextRecord)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            CONTEXT* context =
                exceptionInfo->ContextRecord;

            const DWORD code =
                exceptionInfo->ExceptionRecord->ExceptionCode;

            if (code == EXCEPTION_BREAKPOINT)
            {
                const std::uintptr_t exceptionAddress =
                    reinterpret_cast<std::uintptr_t>(
                        exceptionInfo->ExceptionRecord->ExceptionAddress);

                if (exceptionAddress !=
                    g_referenceBuilder)
                {
                    return EXCEPTION_CONTINUE_SEARCH;
                }

                PublishTargetCapture(context);

                if (!WriteReferenceBuilderByte(
                        g_referenceBuilderOriginalByte))
                {
                    return EXCEPTION_CONTINUE_SEARCH;
                }

                context->Eip =
                    static_cast<DWORD>(
                        g_referenceBuilder);
                context->EFlags |= TRAP_FLAG;
                g_targetSingleStepActive = 1;

                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code == EXCEPTION_SINGLE_STEP &&
                g_targetSingleStepActive != 0)
            {
                WriteReferenceBuilderByte(0xCC);

                context->EFlags &= ~TRAP_FLAG;
                g_targetSingleStepActive = 0;

                return EXCEPTION_CONTINUE_EXECUTION;
            }
#else
            (void)exceptionInfo;
#endif

            return EXCEPTION_CONTINUE_SEARCH;
        }

        bool InstallTunerTargetCapture()
        {
#if !defined(_M_IX86)
            return false;
#else
            if (InterlockedCompareExchange(
                    &g_targetHookInstalled,
                    0,
                    0) != 0)
            {
                return true;
            }

            const std::uintptr_t builder =
                ResolveReferenceBuilder();

            if (!builder ||
                !IsReadableRange(
                    reinterpret_cast<const void*>(
                        builder),
                    1))
            {
                return false;
            }

            BYTE original = 0;

            if (!TryReadValue(
                    builder,
                    original) ||
                original == 0xCC)
            {
                return false;
            }

            PVOID handler =
                AddVectoredExceptionHandler(
                    1,
                    TunerTargetBuilderVeh);

            if (!handler)
                return false;

            g_referenceBuilder = builder;
            g_referenceBuilderOriginalByte = original;
            g_targetVehHandle = handler;

            if (!WriteReferenceBuilderByte(0xCC))
            {
                RemoveVectoredExceptionHandler(
                    handler);
                g_targetVehHandle = nullptr;
                g_referenceBuilder = 0;
                return false;
            }

            InterlockedExchange(
                &g_targetHookInstalled,
                1);

            return true;
#endif
        }

        bool CopyTargetCapture(
            LONG sequence,
            TunerTargetCapture& copy)
        {
            if (sequence <= 0)
                return false;

            const TunerTargetCapture& source =
                g_targetCaptures[
                    (sequence - 1) &
                    (TARGET_CAPTURE_SLOTS - 1)];

            const LONG publishedBefore =
                InterlockedCompareExchange(
                    const_cast<volatile LONG*>(
                        &source.sequence),
                    0,
                    0);

            if (publishedBefore != sequence)
                return false;

            copy.detection = source.detection;
            copy.strings = source.strings;
            copy.playerTag = source.playerTag;
            copy.threadId = source.threadId;
            copy.tick = source.tick;

            MemoryBarrier();

            const LONG publishedAfter =
                InterlockedCompareExchange(
                    const_cast<volatile LONG*>(
                        &source.sequence),
                    0,
                    0);

            return publishedAfter == sequence;
        }

        bool TryReadCapturedTargetPair(
            Tuning& playerOne,
            Tuning& playerTwo)
        {
            const LONG lastSequence =
                InterlockedCompareExchange(
                    &g_targetCaptureCount,
                    0,
                    0);

            if (lastSequence <= 0)
                return false;

            TunerTargetCapture p1{};
            TunerTargetCapture p2{};
            bool haveP1 = false;
            bool haveP2 = false;

            const LONG firstSequence =
                lastSequence >= TARGET_CAPTURE_SLOTS
                ? lastSequence - TARGET_CAPTURE_SLOTS + 1
                : 1;

            for (LONG sequence = lastSequence;
                 sequence >= firstSequence;
                 --sequence)
            {
                TunerTargetCapture candidate{};

                if (!CopyTargetCapture(
                        sequence,
                        candidate))
                {
                    continue;
                }

                // The arrangement-bearing builder call itself identifies the
                // player: the validated call shape carries tag 0 for Player 1
                // and tag 2 for Player 2. Do not add a second dependency on
                // the separately resolved true-tuning pointer; that pointer can
                // legitimately move while the captured target remains valid.
                if (candidate.playerTag == 0)
                {
                    if (!haveP1)
                    {
                        p1 = candidate;
                        haveP1 = true;
                    }
                }
                else if (candidate.playerTag == 2 &&
                    !haveP2)
                {
                    p2 = candidate;
                    haveP2 = true;
                }

                if (haveP1 && haveP2)
                    break;
            }

            if (!haveP1 || !haveP2)
                return false;

            const ULONGLONG separation =
                p1.tick > p2.tick
                ? p1.tick - p2.tick
                : p2.tick - p1.tick;

            if (separation >
                    TARGET_PAIR_MAX_SEPARATION_MS ||
                p1.threadId != p2.threadId)
            {
                return false;
            }

            playerOne.strings = p1.strings;
            playerTwo.strings = p2.strings;
            return true;
        }

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
        return InstallTunerTargetCapture();
    }

    void ShutdownTunerTargetCapture()
    {
#if defined(_M_IX86)
        if (InterlockedCompareExchange(
                &g_targetHookInstalled,
                0,
                0) == 0)
        {
            return;
        }

        WriteReferenceBuilderByte(
            g_referenceBuilderOriginalByte);

        if (g_targetVehHandle)
        {
            RemoveVectoredExceptionHandler(
                g_targetVehHandle);
        }

        g_targetVehHandle = nullptr;
        g_referenceBuilder = 0;
        g_targetSingleStepActive = 0;

        InterlockedExchange(
            &g_targetHookInstalled,
            0);
#endif
    }

    bool TryReadTunerTarget(
        int player,
        Tuning& tuning)
    {
        if (player < 0 || player > 1)
            return false;

        // Preserve the proven single-player P1 source exactly. Multiplayer
        // blanks/repoints that text, so only then do we need the builder pair.
        if (player == 0 &&
            TryReadTunerTextTuning(tuning))
        {
            return true;
        }

        Tuning playerOne{};
        Tuning playerTwo{};

        if (!TryReadCapturedTargetPair(
                playerOne,
                playerTwo))
        {
            return false;
        }

        tuning =
            player == 0
            ? playerOne
            : playerTwo;

        return true;
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
        SYSTEMTIME now{};
        GetLocalTime(&now);

        std::ostringstream log;

        log << "============================================================\n";
        log << "RL-Mods multiplayer Auto target capture test\n";
        log << std::setfill('0')
            << std::dec
            << now.wYear << '-'
            << std::setw(2) << now.wMonth << '-'
            << std::setw(2) << now.wDay << ' '
            << std::setw(2) << now.wHour << ':'
            << std::setw(2) << now.wMinute << ':'
            << std::setw(2) << now.wSecond
            << "\n";
        log << "Build: P2_AUTO_TARGET_TEST\n";
        log << "Current menu: "
            << CurrentMenuName()
            << "\n";
        log << "Target hook: "
            << (InterlockedCompareExchange(
                    &g_targetHookInstalled,
                    0,
                    0) != 0
                ? "installed"
                : "NOT INSTALLED")
            << "\n";
        log << "Captured valid target calls: "
            << InterlockedCompareExchange(
                    &g_targetCaptureCount,
                    0,
                    0)
            << "\n";

        const LONG lastSequence =
            InterlockedCompareExchange(
                &g_targetCaptureCount,
                0,
                0);

        const LONG firstSequence =
            lastSequence >= TARGET_CAPTURE_SLOTS
            ? lastSequence - TARGET_CAPTURE_SLOTS + 1
            : 1;

        for (LONG sequence = firstSequence;
             sequence <= lastSequence;
             ++sequence)
        {
            TunerTargetCapture capture{};

            if (!CopyTargetCapture(sequence, capture))
                continue;

            Tuning captured{};
            captured.strings = capture.strings;

            log << "  capture " << sequence
                << ": tag=" << capture.playerTag
                << " detection=0x"
                << std::hex << std::uppercase
                << capture.detection
                << std::dec
                << " thread=" << capture.threadId
                << " tick=" << capture.tick
                << " target=" << VectorText(captured)
                << " / " << Name(captured)
                << "\n";
        }

        Tuning p1{};
        Tuning p2{};

        if (TryReadCapturedTargetPair(p1, p2))
        {
            log << "P1 captured target: "
                << VectorText(p1)
                << " / "
                << Name(p1)
                << "\n";
            log << "P2 captured target: "
                << VectorText(p2)
                << " / "
                << Name(p2)
                << "\n";
        }
        else
        {
            log << "Captured P1/P2 target pair: unavailable\n";
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
