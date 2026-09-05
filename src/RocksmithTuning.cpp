#include "RocksmithTuning.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
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

        bool LooksLikeDiagnosticText(
            const std::string& text)
        {
            if (text.size() < 3 ||
                text.size() > 127)
            {
                return false;
            }

            size_t alphaNumeric = 0;

            for (unsigned char c : text)
            {
                if (c < 32 || c == 127)
                    return false;

                if (std::isalnum(c))
                    ++alphaNumeric;
            }

            return alphaNumeric >= 2;
        }

        void TraceObjectFieldMap(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t object,
            std::uintptr_t maxOffset,
            bool followOneLevel)
        {
            constexpr std::uintptr_t FIELD_STEP = 0x4;
            constexpr std::uintptr_t NESTED_MAX_OFFSET = 0x100;

            log << label << "\n";
            log << "  object: "
                << AddressText(object)
                << "\n";

            if (!object)
            {
                log << "  result: NULL\n\n";
                return;
            }

            std::vector<std::uintptr_t> seenStrings;

            for (std::uintptr_t offset = 0;
                 offset <= maxOffset;
                 offset += FIELD_STEP)
            {
                if (object > UINTPTR_MAX - offset)
                    break;

                const std::uintptr_t fieldAddress =
                    object + offset;

                std::uintptr_t value = 0;

                if (!TryReadValue(
                        fieldAddress,
                        value))
                {
                    log << "  +"
                        << AddressText(offset)
                        << " = <unreadable>\n";
                    continue;
                }

                log << "  +"
                    << AddressText(offset)
                    << " = "
                    << AddressText(value);

                if (value == 0)
                {
                    log << "\n";
                    continue;
                }

                const std::string directText =
                    ReadString(
                        value,
                        128);

                if (LooksLikeDiagnosticText(
                        directText))
                {
                    log << "  -> \""
                        << directText
                        << "\"";

                    seenStrings.push_back(value);
                }

                log << "\n";

                if (!followOneLevel)
                    continue;

                for (std::uintptr_t nestedOffset = 0;
                     nestedOffset <= NESTED_MAX_OFFSET;
                     nestedOffset += FIELD_STEP)
                {
                    if (value >
                        UINTPTR_MAX -
                        nestedOffset)
                    {
                        break;
                    }

                    std::uintptr_t nestedValue = 0;

                    if (!TryReadValue(
                            value + nestedOffset,
                            nestedValue) ||
                        nestedValue == 0)
                    {
                        continue;
                    }

                    bool alreadySeen = false;

                    for (const std::uintptr_t seen :
                         seenStrings)
                    {
                        if (seen == nestedValue)
                        {
                            alreadySeen = true;
                            break;
                        }
                    }

                    if (alreadySeen)
                        continue;

                    const std::string nestedText =
                        ReadString(
                            nestedValue,
                            128);

                    if (!LooksLikeDiagnosticText(
                            nestedText))
                    {
                        continue;
                    }

                    seenStrings.push_back(
                        nestedValue);

                    log << "      -> +"
                        << AddressText(nestedOffset)
                        << " = "
                        << AddressText(nestedValue)
                        << "  \""
                        << nestedText
                        << "\"\n";
                }
            }

            log << "\n";
        }

        void TraceTunerObjectStructure(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t root)
        {
            constexpr std::uintptr_t KNOWN_CHILD_OFFSET = 0x28;
            constexpr std::uintptr_t ROOT_MAX_OFFSET = 0x100;
            constexpr std::uintptr_t CHILD_MAX_OFFSET = 0x180;

            log << label << "\n";
            log << "  root: "
                << AddressText(root)
                << "\n";

            std::uintptr_t rootObject = 0;

            if (!TryReadValue(
                    root,
                    rootObject) ||
                rootObject == 0)
            {
                log << "  root object: FAILED\n\n";
                return;
            }

            log << "  root object: "
                << AddressText(rootObject)
                << "\n";

            std::uintptr_t knownChild = 0;

            if (rootObject <=
                UINTPTR_MAX -
                KNOWN_CHILD_OFFSET)
            {
                TryReadValue(
                    rootObject +
                        KNOWN_CHILD_OFFSET,
                    knownChild);
            }

            log << "  known child (root + 0x28): "
                << AddressText(knownChild)
                << "\n\n";

            TraceObjectFieldMap(
                log,
                "ROOT OBJECT FIELD MAP",
                rootObject,
                ROOT_MAX_OFFSET,
                true);

            TraceObjectFieldMap(
                log,
                "KNOWN TUNER CHILD FIELD MAP",
                knownChild,
                CHILD_MAX_OFFSET,
                true);
        }

        std::uintptr_t ResolveConfiguredDetectionObject()
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
                    ExecutableVersion::LearnAndPlay2024
                ? TRUE_TUNING_2024_ROOT_OFFSET
                : TRUE_TUNING_2022_ROOT;

            std::uintptr_t address =
                base + rootOffset;

            std::uintptr_t next = 0;

            if (!TryReadValue(address, next) ||
                next == 0)
            {
                return 0;
            }

            address = next + 0x10;

            if (!TryReadValue(address, next) ||
                next == 0)
            {
                return 0;
            }

            address = next + 0x4;

            if (!TryReadValue(address, next) ||
                next == 0)
            {
                return 0;
            }

            return next;
        }

        const char* FindKnownTuningKey(
            const std::array<int, 6>& values)
        {
            for (const auto& definition :
                 TUNING_DEFINITIONS)
            {
                if (values == definition.strings)
                    return definition.key;
            }

            return nullptr;
        }

        bool IsAllZeroTuning(
            const std::array<int, 6>& values)
        {
            for (const int value : values)
            {
                if (value != 0)
                    return false;
            }

            return true;
        }

        void TraceDetectionTuningSignatures(
            std::ostringstream& log,
            std::uintptr_t object,
            int referenceHz)
        {
            constexpr std::uintptr_t SCAN_BYTES = 0x1800;
            constexpr int BASE_MIDI[6] =
            {
                40, 45, 50, 55, 59, 64
            };
            constexpr size_t MAX_HITS = 32;

            size_t hits = 0;

            auto report =
                [&](const char* kind,
                    std::uintptr_t offset,
                    const std::array<int, 6>& tuning,
                    const char* key)
                {
                    if (hits >= MAX_HITS)
                        return;

                    log << "    "
                        << kind
                        << " at +"
                        << AddressText(offset)
                        << ": [";

                    for (size_t i = 0;
                         i < tuning.size();
                         ++i)
                    {
                        if (i != 0)
                            log << ',';

                        log << tuning[i];
                    }

                    log << "]  "
                        << key
                        << "\n";

                    ++hits;
                };

            for (std::uintptr_t offset = 0;
                 offset + 12 <= SCAN_BYTES &&
                 hits < MAX_HITS;
                 ++offset)
            {
                std::array<int, 6> values{};
                bool valid = true;

                for (size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    std::uint8_t raw = 0;

                    if (!TryReadValue(
                            object + offset + i,
                            raw))
                    {
                        valid = false;
                        break;
                    }

                    values[i] =
                        static_cast<int>(
                            static_cast<std::int8_t>(
                                raw));

                    if (values[i] < -24 ||
                        values[i] > 24)
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid &&
                    !IsAllZeroTuning(values))
                {
                    const char* key =
                        FindKnownTuningKey(values);

                    if (key)
                    {
                        report(
                            "signed-byte tuning",
                            offset,
                            values,
                            key);
                    }
                }

                valid = true;

                for (size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    std::uint8_t raw = 0;

                    if (!TryReadValue(
                            object + offset +
                                (i * 2),
                            raw))
                    {
                        valid = false;
                        break;
                    }

                    values[i] =
                        static_cast<int>(
                            static_cast<std::int8_t>(
                                raw));

                    if (values[i] < -24 ||
                        values[i] > 24)
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid &&
                    !IsAllZeroTuning(values))
                {
                    const char* key =
                        FindKnownTuningKey(values);

                    if (key)
                    {
                        report(
                            "stride-2 tuning",
                            offset,
                            values,
                            key);
                    }
                }

                valid = true;

                for (size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    std::uint8_t raw = 0;

                    if (!TryReadValue(
                            object + offset + i,
                            raw))
                    {
                        valid = false;
                        break;
                    }

                    values[i] =
                        static_cast<int>(raw) -
                        BASE_MIDI[i];

                    if (values[i] < -24 ||
                        values[i] > 24)
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid)
                {
                    const char* key =
                        FindKnownTuningKey(values);

                    if (key)
                    {
                        report(
                            "MIDI-byte tuning",
                            offset,
                            values,
                            key);
                    }
                }
            }

            for (std::uintptr_t offset = 0;
                 offset + 24 <= SCAN_BYTES &&
                 hits < MAX_HITS;
                 offset += 4)
            {
                std::array<int, 6> values{};
                bool valid = true;

                for (size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    std::int32_t raw = 0;

                    if (!TryReadValue(
                            object + offset +
                                (i * sizeof(raw)),
                            raw))
                    {
                        valid = false;
                        break;
                    }

                    if (raw < -24 ||
                        raw > 24)
                    {
                        valid = false;
                        break;
                    }

                    values[i] =
                        static_cast<int>(raw);
                }

                if (valid &&
                    !IsAllZeroTuning(values))
                {
                    const char* key =
                        FindKnownTuningKey(values);

                    if (key)
                    {
                        report(
                            "int32 tuning",
                            offset,
                            values,
                            key);
                    }
                }

                valid = true;

                for (size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    std::int32_t raw = 0;

                    if (!TryReadValue(
                            object + offset +
                                (i * sizeof(raw)),
                            raw))
                    {
                        valid = false;
                        break;
                    }

                    values[i] =
                        static_cast<int>(raw) -
                        BASE_MIDI[i];

                    if (values[i] < -24 ||
                        values[i] > 24)
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid)
                {
                    const char* key =
                        FindKnownTuningKey(values);

                    if (key)
                    {
                        report(
                            "MIDI-int32 tuning",
                            offset,
                            values,
                            key);
                    }
                }

                if (referenceHz <= 0)
                    continue;

                valid = true;

                for (size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    float frequency = 0.0f;

                    if (!TryReadValue(
                            object + offset +
                                (i * sizeof(float)),
                            frequency) ||
                        !std::isfinite(frequency) ||
                        frequency < 20.0f ||
                        frequency > 2000.0f)
                    {
                        valid = false;
                        break;
                    }

                    const double midiExact =
                        69.0 +
                        12.0 *
                            std::log2(
                                static_cast<double>(frequency) /
                                static_cast<double>(referenceHz));

                    const int midi =
                        static_cast<int>(
                            std::round(midiExact));

                    if (std::fabs(
                            midiExact -
                            static_cast<double>(midi)) >
                        0.03)
                    {
                        valid = false;
                        break;
                    }

                    values[i] =
                        midi -
                        BASE_MIDI[i];

                    if (values[i] < -24 ||
                        values[i] > 24)
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid)
                {
                    const char* key =
                        FindKnownTuningKey(values);

                    if (key)
                    {
                        report(
                            "float-frequency tuning",
                            offset,
                            values,
                            key);
                    }
                }
            }

            if (hits == 0)
            {
                log << "    no known tuning signatures found\n";
            }
            else if (hits >= MAX_HITS)
            {
                log << "    hit limit reached\n";
            }
        }

        bool TryReadSignedByteTuningAt(
            std::uintptr_t object,
            std::uintptr_t offset,
            std::uintptr_t stride,
            std::array<int, 6>& values)
        {
            for (size_t i = 0;
                 i < values.size();
                 ++i)
            {
                std::uint8_t raw = 0;

                if (!TryReadValue(
                        object + offset +
                            (i * stride),
                        raw))
                {
                    return false;
                }

                const int value =
                    static_cast<int>(
                        static_cast<std::int8_t>(raw));

                if (value < -24 ||
                    value > 24)
                {
                    return false;
                }

                values[i] = value;
            }

            return true;
        }

        bool TryReadMidiByteTuningAt(
            std::uintptr_t object,
            std::uintptr_t offset,
            std::array<int, 6>& values)
        {
            constexpr int BASE_MIDI[6] =
            {
                40, 45, 50, 55, 59, 64
            };

            for (size_t i = 0;
                 i < values.size();
                 ++i)
            {
                std::uint8_t raw = 0;

                if (!TryReadValue(
                        object + offset + i,
                        raw))
                {
                    return false;
                }

                const int value =
                    static_cast<int>(raw) -
                    BASE_MIDI[i];

                if (value < -24 ||
                    value > 24)
                {
                    return false;
                }

                values[i] = value;
            }

            return true;
        }

        bool TryReadInt32TuningAt(
            std::uintptr_t object,
            std::uintptr_t offset,
            bool midiValues,
            std::array<int, 6>& values)
        {
            constexpr int BASE_MIDI[6] =
            {
                40, 45, 50, 55, 59, 64
            };

            for (size_t i = 0;
                 i < values.size();
                 ++i)
            {
                std::int32_t raw = 0;

                if (!TryReadValue(
                        object + offset +
                            (i * sizeof(raw)),
                        raw))
                {
                    return false;
                }

                const int value =
                    midiValues
                    ? static_cast<int>(raw) -
                        BASE_MIDI[i]
                    : static_cast<int>(raw);

                if (value < -24 ||
                    value > 24)
                {
                    return false;
                }

                values[i] = value;
            }

            return true;
        }

        bool TryReadInt32CentTuningAt(
            std::uintptr_t object,
            std::uintptr_t offset,
            std::array<int, 6>& values)
        {
            for (size_t i = 0;
                 i < values.size();
                 ++i)
            {
                std::int32_t cents = 0;

                if (!TryReadValue(
                        object + offset +
                            (i * sizeof(cents)),
                        cents) ||
                    cents < -2400 ||
                    cents > 2400 ||
                    (cents % 100) != 0)
                {
                    return false;
                }

                values[i] =
                    static_cast<int>(cents / 100);
            }

            return true;
        }

        bool TryReadInt16CentTuningAt(
            std::uintptr_t object,
            std::uintptr_t offset,
            std::array<int, 6>& values)
        {
            for (size_t i = 0;
                 i < values.size();
                 ++i)
            {
                std::int16_t cents = 0;

                if (!TryReadValue(
                        object + offset +
                            (i * sizeof(cents)),
                        cents) ||
                    cents < -2400 ||
                    cents > 2400 ||
                    (cents % 100) != 0)
                {
                    return false;
                }

                values[i] =
                    static_cast<int>(cents / 100);
            }

            return true;
        }

        bool TryReadFloatFrequencyTuningAt(
            std::uintptr_t object,
            std::uintptr_t offset,
            int referenceHz,
            std::array<int, 6>& values)
        {
            constexpr int BASE_MIDI[6] =
            {
                40, 45, 50, 55, 59, 64
            };

            if (referenceHz < 100 ||
                referenceHz > 1000)
            {
                referenceHz = 440;
            }

            for (size_t i = 0;
                 i < values.size();
                 ++i)
            {
                float frequency = 0.0f;

                if (!TryReadValue(
                        object + offset +
                            (i * sizeof(float)),
                        frequency) ||
                    !std::isfinite(frequency) ||
                    frequency < 20.0f ||
                    frequency > 2000.0f)
                {
                    return false;
                }

                const double midiExact =
                    69.0 +
                    12.0 *
                        std::log2(
                            static_cast<double>(frequency) /
                            static_cast<double>(referenceHz));

                const int midi =
                    static_cast<int>(
                        std::round(midiExact));

                if (std::fabs(
                        midiExact -
                        static_cast<double>(midi)) >
                    0.03)
                {
                    return false;
                }

                const int value =
                    midi - BASE_MIDI[i];

                if (value < -24 ||
                    value > 24)
                {
                    return false;
                }

                values[i] = value;
            }

            return true;
        }

        void TraceDetectionPairTuningDifferences(
            std::ostringstream& log,
            std::uintptr_t player1,
            std::uintptr_t player2)
        {
            constexpr std::uintptr_t SCAN_BYTES = 0x1800;
            constexpr size_t MAX_HITS = 64;

            log << "\nP1/P2 SAME-OFFSET TUNING DIFFERENCES\n";
            log << "  P1: "
                << AddressText(player1)
                << "\n";
            log << "  P2: "
                << AddressText(player2)
                << "\n";

            if (!player1 || !player2 ||
                player1 == player2)
            {
                log << "  result: invalid pair\n";
                return;
            }

            float p1Reference = 440.0f;
            float p2Reference = 440.0f;

            TryReadValue(
                player1 + 0x135C,
                p1Reference);
            TryReadValue(
                player2 + 0x135C,
                p2Reference);

            const int p1ReferenceHz =
                static_cast<int>(
                    std::round(p1Reference));
            const int p2ReferenceHz =
                static_cast<int>(
                    std::round(p2Reference));

            size_t hits = 0;

            auto report =
                [&](const char* kind,
                    std::uintptr_t offset,
                    const std::array<int, 6>& p1Values,
                    const char* p1Key,
                    const std::array<int, 6>& p2Values,
                    const char* p2Key)
                {
                    if (hits >= MAX_HITS)
                        return;

                    log << "  "
                        << kind
                        << " at +"
                        << AddressText(offset)
                        << ": P1 "
                        << p1Key
                        << " [";

                    for (size_t i = 0;
                         i < p1Values.size();
                         ++i)
                    {
                        if (i != 0)
                            log << ',';
                        log << p1Values[i];
                    }

                    log << "]  |  P2 "
                        << p2Key
                        << " [";

                    for (size_t i = 0;
                         i < p2Values.size();
                         ++i)
                    {
                        if (i != 0)
                            log << ',';
                        log << p2Values[i];
                    }

                    log << "]\n";
                    ++hits;
                };

            auto maybeReport =
                [&](const char* kind,
                    std::uintptr_t offset,
                    const std::array<int, 6>& p1Values,
                    bool p1Valid,
                    const std::array<int, 6>& p2Values,
                    bool p2Valid)
                {
                    if (!p1Valid ||
                        !p2Valid ||
                        p1Values == p2Values)
                    {
                        return;
                    }

                    const char* p1Key =
                        FindKnownTuningKey(p1Values);
                    const char* p2Key =
                        FindKnownTuningKey(p2Values);

                    if (!p1Key || !p2Key)
                        return;

                    report(
                        kind,
                        offset,
                        p1Values,
                        p1Key,
                        p2Values,
                        p2Key);
                };

            for (std::uintptr_t offset = 0;
                 offset + 12 <= SCAN_BYTES &&
                 hits < MAX_HITS;
                 ++offset)
            {
                std::array<int, 6> p1Values{};
                std::array<int, 6> p2Values{};

                maybeReport(
                    "signed-byte",
                    offset,
                    p1Values,
                    TryReadSignedByteTuningAt(
                        player1,
                        offset,
                        1,
                        p1Values),
                    p2Values,
                    TryReadSignedByteTuningAt(
                        player2,
                        offset,
                        1,
                        p2Values));

                maybeReport(
                    "stride-2 byte",
                    offset,
                    p1Values,
                    TryReadSignedByteTuningAt(
                        player1,
                        offset,
                        2,
                        p1Values),
                    p2Values,
                    TryReadSignedByteTuningAt(
                        player2,
                        offset,
                        2,
                        p2Values));

                maybeReport(
                    "MIDI byte",
                    offset,
                    p1Values,
                    TryReadMidiByteTuningAt(
                        player1,
                        offset,
                        p1Values),
                    p2Values,
                    TryReadMidiByteTuningAt(
                        player2,
                        offset,
                        p2Values));

                maybeReport(
                    "int16 cents",
                    offset,
                    p1Values,
                    TryReadInt16CentTuningAt(
                        player1,
                        offset,
                        p1Values),
                    p2Values,
                    TryReadInt16CentTuningAt(
                        player2,
                        offset,
                        p2Values));
            }

            for (std::uintptr_t offset = 0;
                 offset + 24 <= SCAN_BYTES &&
                 hits < MAX_HITS;
                 offset += 4)
            {
                std::array<int, 6> p1Values{};
                std::array<int, 6> p2Values{};

                maybeReport(
                    "int32",
                    offset,
                    p1Values,
                    TryReadInt32TuningAt(
                        player1,
                        offset,
                        false,
                        p1Values),
                    p2Values,
                    TryReadInt32TuningAt(
                        player2,
                        offset,
                        false,
                        p2Values));

                maybeReport(
                    "MIDI int32",
                    offset,
                    p1Values,
                    TryReadInt32TuningAt(
                        player1,
                        offset,
                        true,
                        p1Values),
                    p2Values,
                    TryReadInt32TuningAt(
                        player2,
                        offset,
                        true,
                        p2Values));

                maybeReport(
                    "int32 cents",
                    offset,
                    p1Values,
                    TryReadInt32CentTuningAt(
                        player1,
                        offset,
                        p1Values),
                    p2Values,
                    TryReadInt32CentTuningAt(
                        player2,
                        offset,
                        p2Values));

                maybeReport(
                    "float frequency",
                    offset,
                    p1Values,
                    TryReadFloatFrequencyTuningAt(
                        player1,
                        offset,
                        p1ReferenceHz,
                        p1Values),
                    p2Values,
                    TryReadFloatFrequencyTuningAt(
                        player2,
                        offset,
                        p2ReferenceHz,
                        p2Values));
            }

            if (hits == 0)
            {
                log << "  no differing known tuning structures found\n";
            }
            else if (hits >= MAX_HITS)
            {
                log << "  hit limit reached\n";
            }
        }

        void TraceDetectionObjects(
            std::ostringstream& log)
        {
            constexpr std::uintptr_t TRUE_TUNING_FIELD_OFFSET =
                0x135C;
            constexpr std::uintptr_t SEARCH_RADIUS =
                0x04000000;

            log << "PLAYER DETECTION OBJECT SEARCH\n";

            const std::uintptr_t player1 =
                ResolveConfiguredDetectionObject();

            log << "  known P1 object: "
                << AddressText(player1)
                << "\n";

            if (!player1)
            {
                log << "  result: P1 detection object unresolved\n\n";
                return;
            }

            std::uintptr_t vtable = 0;
            float player1Reference = 0.0f;

            if (!TryReadValue(player1, vtable) ||
                !TryReadValue(
                    player1 + TRUE_TUNING_FIELD_OFFSET,
                    player1Reference))
            {
                log << "  result: P1 object unreadable\n\n";
                return;
            }

            log << "  P1 first dword: "
                << AddressText(vtable)
                << "\n";
            log << "  P1 +0x135C: "
                << player1Reference
                << "\n";

            const std::uintptr_t minimumAddress =
                player1 > SEARCH_RADIUS
                ? player1 - SEARCH_RADIUS
                : 0x10000;

            const std::uintptr_t maximumAddress =
                player1 < UINTPTR_MAX - SEARCH_RADIUS
                ? player1 + SEARCH_RADIUS
                : UINTPTR_MAX;

            std::vector<std::uintptr_t> candidates;
            std::uintptr_t cursor = minimumAddress;

            while (cursor < maximumAddress)
            {
                MEMORY_BASIC_INFORMATION mbi{};

                if (!VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &mbi,
                        sizeof(mbi)))
                {
                    break;
                }

                const std::uintptr_t regionStart =
                    reinterpret_cast<std::uintptr_t>(
                        mbi.BaseAddress);
                const std::uintptr_t regionEnd =
                    regionStart +
                    static_cast<std::uintptr_t>(
                        mbi.RegionSize);

                const DWORD readable =
                    PAGE_READONLY |
                    PAGE_READWRITE |
                    PAGE_WRITECOPY |
                    PAGE_EXECUTE_READ |
                    PAGE_EXECUTE_READWRITE |
                    PAGE_EXECUTE_WRITECOPY;

                if (mbi.State == MEM_COMMIT &&
                    mbi.Type == MEM_PRIVATE &&
                    (mbi.Protect & PAGE_GUARD) == 0 &&
                    (mbi.Protect & PAGE_NOACCESS) == 0 &&
                    (mbi.Protect & readable) != 0)
                {
                    std::uintptr_t start =
                        regionStart < minimumAddress
                        ? minimumAddress
                        : regionStart;
                    std::uintptr_t end =
                        regionEnd > maximumAddress
                        ? maximumAddress
                        : regionEnd;

                    start =
                        (start + 3) &
                        ~static_cast<std::uintptr_t>(3);

                    for (std::uintptr_t address = start;
                         address + sizeof(float) <= end;
                         address += 4)
                    {
                        float possibleReference = 0.0f;

                        if (!TryReadValue(
                                address,
                                possibleReference) ||
                            !std::isfinite(possibleReference) ||
                            possibleReference < 200.0f ||
                            possibleReference > 500.0f ||
                            address < TRUE_TUNING_FIELD_OFFSET)
                        {
                            continue;
                        }

                        const std::uintptr_t candidate =
                            address -
                            TRUE_TUNING_FIELD_OFFSET;

                        std::uintptr_t candidateVtable = 0;

                        if (!TryReadValue(
                                candidate,
                                candidateVtable) ||
                            candidateVtable != vtable)
                        {
                            continue;
                        }

                        bool duplicate = false;

                        for (const std::uintptr_t existing :
                             candidates)
                        {
                            if (existing == candidate)
                            {
                                duplicate = true;
                                break;
                            }
                        }

                        if (!duplicate)
                        {
                            candidates.push_back(candidate);
                        }
                    }
                }

                if (regionEnd <= cursor)
                    break;

                cursor = regionEnd;
            }

            if (candidates.empty())
            {
                log << "  no same-class candidates found within +/-64 MB\n\n";
                return;
            }

            int referenceHz =
                static_cast<int>(
                    std::round(player1Reference));

            for (size_t i = 0;
                 i < candidates.size();
                 ++i)
            {
                const std::uintptr_t candidate =
                    candidates[i];

                float candidateReference = 0.0f;
                TryReadValue(
                    candidate + TRUE_TUNING_FIELD_OFFSET,
                    candidateReference);

                log << "  candidate "
                    << (i + 1)
                    << ": "
                    << AddressText(candidate);

                if (candidate == player1)
                    log << "  <known P1>";

                log << "  +0x135C="
                    << candidateReference
                    << "\n";

                TraceDetectionTuningSignatures(
                    log,
                    candidate,
                    referenceHz);
            }

            log << "\n";
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

        // Temporary multiplayer diagnostic: capture every call to Rocksmith's
        // tuning-reference builder. The builder receives the detection object in
        // ESI and the authored reference cents as its first stack argument. This
        // identifies the player-specific detection objects without scanning the heap.
        constexpr std::uintptr_t REFERENCE_BUILDER_2022 =
            0x004DCCB0;
        constexpr std::uintptr_t REFERENCE_BUILDER_2024_OFFSET =
            0x002AD930;
        constexpr LONG BUILDER_CAPTURE_SLOTS = 64;
        constexpr DWORD TRAP_FLAG = 0x100;

        constexpr size_t BUILDER_STACK_DWORDS = 64;

        struct BuilderCapture
        {
            volatile LONG sequence = 0;
            std::uintptr_t detection = 0;
            LONG cents = 0;
            DWORD threadId = 0;
            ULONGLONG tick = 0;

            std::uintptr_t eax = 0;
            std::uintptr_t ebx = 0;
            std::uintptr_t ecx = 0;
            std::uintptr_t edx = 0;
            std::uintptr_t edi = 0;
            std::uintptr_t ebp = 0;
            std::uintptr_t esp = 0;
            std::uintptr_t eip = 0;
            DWORD eflags = 0;

            std::array<std::uintptr_t, BUILDER_STACK_DWORDS> stack{};
        };

        BuilderCapture g_builderCaptures[BUILDER_CAPTURE_SLOTS]{};
        volatile LONG g_builderCaptureCount = 0;
        LONG g_builderLastDumpSequence = 0;

        std::uintptr_t g_referenceBuilder = 0;
        BYTE g_referenceBuilderOriginalByte = 0;
        PVOID g_builderVehHandle = nullptr;
        volatile LONG g_builderHookInstalled = 0;
        __declspec(thread) LONG g_builderSingleStepActive = 0;

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

        LONG CALLBACK ReferenceBuilderVeh(
            PEXCEPTION_POINTERS exceptionInfo)
        {
#if defined(_M_IX86)
            if (!exceptionInfo ||
                !exceptionInfo->ExceptionRecord ||
                !exceptionInfo->ContextRecord)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const DWORD code =
                exceptionInfo->ExceptionRecord->ExceptionCode;

            CONTEXT* context =
                exceptionInfo->ContextRecord;

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

                LONG cents = 0;

                __try
                {
                    cents =
                        *reinterpret_cast<const LONG*>(
                            context->Esp + sizeof(std::uint32_t));
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    cents = 0x7FFFFFFF;
                }

                const LONG sequence =
                    InterlockedIncrement(
                        &g_builderCaptureCount);

                BuilderCapture& capture =
                    g_builderCaptures[
                        (sequence - 1) &
                        (BUILDER_CAPTURE_SLOTS - 1)];

                capture.detection =
                    static_cast<std::uintptr_t>(
                        context->Esi);
                capture.cents = cents;
                capture.threadId =
                    GetCurrentThreadId();
                capture.tick =
                    GetTickCount64();

                capture.eax = context->Eax;
                capture.ebx = context->Ebx;
                capture.ecx = context->Ecx;
                capture.edx = context->Edx;
                capture.edi = context->Edi;
                capture.ebp = context->Ebp;
                capture.esp = context->Esp;
                capture.eip = context->Eip;
                capture.eflags = context->EFlags;

                for (size_t i = 0;
                     i < BUILDER_STACK_DWORDS;
                     ++i)
                {
                    std::uintptr_t value = 0;

                    __try
                    {
                        value =
                            *reinterpret_cast<const std::uintptr_t*>(
                                context->Esp +
                                (i * sizeof(std::uintptr_t)));
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        value = 0;
                    }

                    capture.stack[i] = value;
                }

                MemoryBarrier();
                InterlockedExchange(
                    &capture.sequence,
                    sequence);

                *reinterpret_cast<volatile BYTE*>(
                    g_referenceBuilder) =
                    g_referenceBuilderOriginalByte;

                FlushInstructionCache(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(
                        g_referenceBuilder),
                    1);

                context->Eip =
                    static_cast<DWORD>(
                        g_referenceBuilder);
                context->EFlags |= TRAP_FLAG;
                g_builderSingleStepActive = 1;

                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code == EXCEPTION_SINGLE_STEP &&
                g_builderSingleStepActive != 0)
            {
                *reinterpret_cast<volatile BYTE*>(
                    g_referenceBuilder) = 0xCC;

                FlushInstructionCache(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(
                        g_referenceBuilder),
                    1);

                context->EFlags &= ~TRAP_FLAG;
                g_builderSingleStepActive = 0;

                return EXCEPTION_CONTINUE_EXECUTION;
            }
#else
            (void)exceptionInfo;
#endif

            return EXCEPTION_CONTINUE_SEARCH;
        }

        bool InstallReferenceBuilderDiagnostic()
        {
#if !defined(_M_IX86)
            return false;
#else
            if (InterlockedCompareExchange(
                    &g_builderHookInstalled,
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
                    ReferenceBuilderVeh);

            if (!handler)
                return false;

            DWORD oldProtect = 0;

            if (!VirtualProtect(
                    reinterpret_cast<void*>(builder),
                    1,
                    PAGE_EXECUTE_READWRITE,
                    &oldProtect))
            {
                RemoveVectoredExceptionHandler(
                    handler);
                return false;
            }

            g_referenceBuilder = builder;
            g_referenceBuilderOriginalByte =
                original;
            g_builderVehHandle = handler;

            *reinterpret_cast<volatile BYTE*>(
                builder) = 0xCC;

            FlushInstructionCache(
                GetCurrentProcess(),
                reinterpret_cast<const void*>(builder),
                1);

            InterlockedExchange(
                &g_builderHookInstalled,
                1);

            g_builderLastDumpSequence =
                InterlockedCompareExchange(
                    &g_builderCaptureCount,
                    0,
                    0);

            return true;
#endif
        }

        bool LooksLikePointerValue(std::uintptr_t value)
        {
            if (value < 0x10000)
                return false;

            return IsReadableRange(
                reinterpret_cast<const void*>(value),
                sizeof(std::uintptr_t));
        }

        void AppendContextPointerPreview(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t value)
        {
            log << "    " << label << '=' << AddressText(value);

            if (!LooksLikePointerValue(value))
            {
                log << "\n";
                return;
            }

            std::uintptr_t first = 0;
            TryReadValue(value, first);

            log << "  readable first="
                << AddressText(first);

            const std::string directText =
                ReadString(value, 96);

            if (LooksLikeDiagnosticText(directText))
            {
                log << "  text=\""
                    << directText
                    << "\"";
            }

            log << "\n";
        }

        void AppendBuilderCallContext(
            std::ostringstream& log,
            LONG sequence,
            const BuilderCapture& capture,
            const char* ownerLabel)
        {
            log << "  context event "
                << sequence
                << "  "
                << ownerLabel
                << "  detection="
                << AddressText(capture.detection)
                << " cents=";

            if (capture.cents == 0x7FFFFFFF)
                log << "<unreadable>";
            else
                log << capture.cents;

            log << " thread="
                << capture.threadId
                << " tick="
                << capture.tick;

            if (!capture.stack.empty())
            {
                log << " return="
                    << AddressText(capture.stack[0]);
            }

            log << " EIP="
                << AddressText(capture.eip)
                << " EFLAGS=0x"
                << std::uppercase
                << std::hex
                << capture.eflags
                << std::dec
                << "\n";

            AppendContextPointerPreview(log, "EAX", capture.eax);
            AppendContextPointerPreview(log, "EBX", capture.ebx);
            AppendContextPointerPreview(log, "ECX", capture.ecx);
            AppendContextPointerPreview(log, "EDX", capture.edx);
            AppendContextPointerPreview(log, "ESI", capture.detection);
            AppendContextPointerPreview(log, "EDI", capture.edi);
            AppendContextPointerPreview(log, "EBP", capture.ebp);
            AppendContextPointerPreview(log, "ESP", capture.esp);

            log << "    stack dwords (ESP + offset)\n";

            for (size_t i = 0;
                 i < capture.stack.size();
                 ++i)
            {
                const std::uintptr_t value =
                    capture.stack[i];

                char label[32] = {};
                sprintf_s(
                    label,
                    "[+0x%02X]",
                    static_cast<unsigned int>(
                        i * sizeof(std::uintptr_t)));

                AppendContextPointerPreview(
                    log,
                    label,
                    value);
            }

            log << "\n";
        }

        void TraceContextCandidatePair(
            std::ostringstream& log,
            const char* label,
            std::uintptr_t player1,
            std::uintptr_t player2)
        {
            if (!LooksLikePointerValue(player1) ||
                !LooksLikePointerValue(player2) ||
                player1 == player2)
            {
                return;
            }

            log << "  candidate pair "
                << label
                << ": P1="
                << AddressText(player1)
                << " P2="
                << AddressText(player2)
                << "\n";

            TraceDetectionPairTuningDifferences(
                log,
                player1,
                player2);
        }

        void AppendBuilderContextComparison(
            std::ostringstream& log,
            LONG firstSequence,
            LONG total,
            std::uintptr_t currentPlayer1)
        {
            const BuilderCapture* player1Capture = nullptr;
            const BuilderCapture* player2Capture = nullptr;
            LONG player1Sequence = 0;
            LONG player2Sequence = 0;

            for (LONG sequence = firstSequence;
                 sequence <= total;
                 ++sequence)
            {
                BuilderCapture& slot =
                    g_builderCaptures[
                        (sequence - 1) &
                        (BUILDER_CAPTURE_SLOTS - 1)];

                if (InterlockedCompareExchange(
                        &slot.sequence,
                        0,
                        0) != sequence)
                {
                    continue;
                }

                if (slot.detection == currentPlayer1)
                {
                    if (!player1Capture ||
                        (player1Capture->cents != 0 &&
                         slot.cents == 0))
                    {
                        player1Capture = &slot;
                        player1Sequence = sequence;
                    }

                    continue;
                }

                if (slot.detection != 0 &&
                    currentPlayer1 != 0 &&
                    slot.detection != currentPlayer1)
                {
                    if (!player2Capture ||
                        (player2Capture->cents != 0 &&
                         slot.cents == 0))
                    {
                        player2Capture = &slot;
                        player2Sequence = sequence;
                    }
                }
            }

            log << "\nBUILDER CALL CONTEXT\n";

            if (!player1Capture)
            {
                log << "  no P1 builder context captured\n";
            }
            else
            {
                AppendBuilderCallContext(
                    log,
                    player1Sequence,
                    *player1Capture,
                    "<P1>");
            }

            if (!player2Capture)
            {
                log << "  no P2 builder context captured\n\n";
                return;
            }

            AppendBuilderCallContext(
                log,
                player2Sequence,
                *player2Capture,
                "<P2 candidate>");

            if (!player1Capture)
                return;

            log << "BUILDER CONTEXT P1/P2 POINTER PAIRS\n";

            TraceContextCandidatePair(log, "EAX", player1Capture->eax, player2Capture->eax);
            TraceContextCandidatePair(log, "EBX", player1Capture->ebx, player2Capture->ebx);
            TraceContextCandidatePair(log, "ECX", player1Capture->ecx, player2Capture->ecx);
            TraceContextCandidatePair(log, "EDX", player1Capture->edx, player2Capture->edx);
            TraceContextCandidatePair(log, "EDI", player1Capture->edi, player2Capture->edi);
            TraceContextCandidatePair(log, "EBP", player1Capture->ebp, player2Capture->ebp);

            for (size_t i = 0;
                 i < BUILDER_STACK_DWORDS;
                 ++i)
            {
                char label[32] = {};
                sprintf_s(
                    label,
                    "stack+0x%02X",
                    static_cast<unsigned int>(
                        i * sizeof(std::uintptr_t)));

                TraceContextCandidatePair(
                    log,
                    label,
                    player1Capture->stack[i],
                    player2Capture->stack[i]);
            }

            log << "\n";
        }


        struct BuilderContextRootPair
        {
            std::uintptr_t player1 = 0;
            std::uintptr_t player2 = 0;
            std::string label;
        };

        bool SamePointerPair(
            const BuilderContextRootPair& pair,
            std::uintptr_t player1,
            std::uintptr_t player2)
        {
            return pair.player1 == player1 &&
                pair.player2 == player2;
        }

        void AddBuilderContextRootPair(
            std::vector<BuilderContextRootPair>& roots,
            const std::string& label,
            std::uintptr_t player1,
            std::uintptr_t player2)
        {
            constexpr size_t MAX_ROOT_PAIRS = 96;

            if (roots.size() >= MAX_ROOT_PAIRS ||
                player1 == player2 ||
                !LooksLikePointerValue(player1) ||
                !LooksLikePointerValue(player2))
            {
                return;
            }

            for (const auto& existing : roots)
            {
                if (SamePointerPair(
                        existing,
                        player1,
                        player2))
                {
                    return;
                }
            }

            BuilderContextRootPair root{};
            root.player1 = player1;
            root.player2 = player2;
            root.label = label;
            roots.push_back(root);
        }

        void AppendAllBuilderContexts(
            std::ostringstream& log,
            LONG firstSequence,
            LONG total,
            std::uintptr_t currentPlayer1)
        {
            log << "\nALL BUILDER EVENT CONTEXTS (64 stack dwords each)\n";

            for (LONG sequence = firstSequence;
                 sequence <= total;
                 ++sequence)
            {
                BuilderCapture& slot =
                    g_builderCaptures[
                        (sequence - 1) &
                        (BUILDER_CAPTURE_SLOTS - 1)];

                if (InterlockedCompareExchange(
                        &slot.sequence,
                        0,
                        0) != sequence)
                {
                    continue;
                }

                const char* owner =
                    slot.detection == currentPlayer1
                    ? "<P1>"
                    : (slot.detection != 0 && currentPlayer1 != 0
                        ? "<P2/other>"
                        : "<unknown>");

                AppendBuilderCallContext(
                    log,
                    sequence,
                    slot,
                    owner);
            }
        }

        void AppendUniqueBuilderCallers(
            std::ostringstream& log,
            LONG firstSequence,
            LONG total)
        {
            std::vector<std::uintptr_t> callers;

            for (LONG sequence = firstSequence;
                 sequence <= total;
                 ++sequence)
            {
                BuilderCapture& slot =
                    g_builderCaptures[
                        (sequence - 1) &
                        (BUILDER_CAPTURE_SLOTS - 1)];

                if (InterlockedCompareExchange(
                        &slot.sequence,
                        0,
                        0) != sequence ||
                    slot.stack.empty())
                {
                    continue;
                }

                const std::uintptr_t caller =
                    slot.stack[0];

                if (!caller)
                    continue;

                bool seen = false;

                for (const auto existing : callers)
                {
                    if (existing == caller)
                    {
                        seen = true;
                        break;
                    }
                }

                if (!seen)
                    callers.push_back(caller);
            }

            log << "\nUNIQUE BUILDER CALLERS\n";

            if (callers.empty())
            {
                log << "  none\n";
                return;
            }

            for (size_t i = 0;
                 i < callers.size();
                 ++i)
            {
                const std::uintptr_t caller = callers[i];
                log << "  caller " << (i + 1)
                    << ": " << AddressText(caller)
                    << "\n";

                const std::uintptr_t start =
                    caller >= 16
                    ? caller - 16
                    : caller;

                TraceRawBytes(
                    log,
                    start,
                    80);
            }

            log << "  reference builder bytes\n";
            TraceRawBytes(
                log,
                g_referenceBuilder,
                96);
        }

        void ReportPairTuningHit(
            std::ostringstream& log,
            const std::string& path,
            const char* kind,
            std::uintptr_t offset,
            const std::array<int, 6>& p1Values,
            const std::array<int, 6>& p2Values)
        {
            const char* p1Key =
                FindKnownTuningKey(p1Values);
            const char* p2Key =
                FindKnownTuningKey(p2Values);

            if (!p1Key || !p2Key ||
                p1Values == p2Values)
            {
                return;
            }

            log << "    *** TUNING-DIFF "
                << path
                << " +"
                << AddressText(offset)
                << " "
                << kind
                << ": P1="
                << p1Key
                << " [";

            for (size_t i = 0;
                 i < p1Values.size();
                 ++i)
            {
                if (i != 0) log << ',';
                log << p1Values[i];
            }

            log << "] P2="
                << p2Key
                << " [";

            for (size_t i = 0;
                 i < p2Values.size();
                 ++i)
            {
                if (i != 0) log << ',';
                log << p2Values[i];
            }

            log << "]\n";
        }

        void ScanPairNodeForTuningDifferences(
            std::ostringstream& log,
            const std::string& path,
            std::uintptr_t player1,
            std::uintptr_t player2)
        {
            constexpr size_t SCAN_BYTES = 0x600;
            constexpr int BASE_MIDI[6] =
            {
                40, 45, 50, 55, 59, 64
            };

            if (!IsReadableRange(
                    reinterpret_cast<const void*>(player1),
                    SCAN_BYTES) ||
                !IsReadableRange(
                    reinterpret_cast<const void*>(player2),
                    SCAN_BYTES))
            {
                return;
            }

            std::array<std::uint8_t, SCAN_BYTES> p1Bytes{};
            std::array<std::uint8_t, SCAN_BYTES> p2Bytes{};

            std::memcpy(
                p1Bytes.data(),
                reinterpret_cast<const void*>(player1),
                SCAN_BYTES);
            std::memcpy(
                p2Bytes.data(),
                reinterpret_cast<const void*>(player2),
                SCAN_BYTES);

            auto signedByteTuning =
                [](const auto& bytes,
                   size_t offset,
                   size_t stride,
                   std::array<int, 6>& values)
                {
                    if (offset + (5 * stride) >= bytes.size())
                        return false;

                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        const int value =
                            static_cast<int>(
                                static_cast<std::int8_t>(
                                    bytes[offset + (i * stride)]));

                        if (value < -24 || value > 24)
                            return false;

                        values[i] = value;
                    }

                    return true;
                };

            auto midiByteTuning =
                [&](const auto& bytes,
                    size_t offset,
                    std::array<int, 6>& values)
                {
                    if (offset + 6 > bytes.size())
                        return false;

                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        const int value =
                            static_cast<int>(bytes[offset + i]) -
                            BASE_MIDI[i];

                        if (value < -24 || value > 24)
                            return false;

                        values[i] = value;
                    }

                    return true;
                };

            auto int16CentsTuning =
                [](const auto& bytes,
                   size_t offset,
                   std::array<int, 6>& values)
                {
                    if (offset + (6 * sizeof(std::int16_t)) > bytes.size())
                        return false;

                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        std::int16_t cents = 0;
                        std::memcpy(
                            &cents,
                            bytes.data() + offset +
                                (i * sizeof(cents)),
                            sizeof(cents));

                        if (cents < -2400 ||
                            cents > 2400 ||
                            (cents % 100) != 0)
                        {
                            return false;
                        }

                        values[i] = cents / 100;
                    }

                    return true;
                };

            auto int32Tuning =
                [&](const auto& bytes,
                    size_t offset,
                    bool midi,
                    std::array<int, 6>& values)
                {
                    if (offset + (6 * sizeof(std::int32_t)) > bytes.size())
                        return false;

                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        std::int32_t raw = 0;
                        std::memcpy(
                            &raw,
                            bytes.data() + offset +
                                (i * sizeof(raw)),
                            sizeof(raw));

                        const int value =
                            midi
                            ? static_cast<int>(raw) - BASE_MIDI[i]
                            : static_cast<int>(raw);

                        if (value < -24 || value > 24)
                            return false;

                        values[i] = value;
                    }

                    return true;
                };

            auto int32CentsTuning =
                [](const auto& bytes,
                   size_t offset,
                   std::array<int, 6>& values)
                {
                    if (offset + (6 * sizeof(std::int32_t)) > bytes.size())
                        return false;

                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        std::int32_t cents = 0;
                        std::memcpy(
                            &cents,
                            bytes.data() + offset +
                                (i * sizeof(cents)),
                            sizeof(cents));

                        if (cents < -2400 ||
                            cents > 2400 ||
                            (cents % 100) != 0)
                        {
                            return false;
                        }

                        values[i] = cents / 100;
                    }

                    return true;
                };

            auto floatFrequencyTuning =
                [&](const auto& bytes,
                    size_t offset,
                    std::array<int, 6>& values)
                {
                    if (offset + (6 * sizeof(float)) > bytes.size())
                        return false;

                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        float frequency = 0.0f;
                        std::memcpy(
                            &frequency,
                            bytes.data() + offset +
                                (i * sizeof(frequency)),
                            sizeof(frequency));

                        if (!std::isfinite(frequency) ||
                            frequency < 20.0f ||
                            frequency > 2000.0f)
                        {
                            return false;
                        }

                        const double midiExact =
                            69.0 +
                            12.0 * std::log2(
                                static_cast<double>(frequency) /
                                440.0);

                        const int midi =
                            static_cast<int>(std::round(midiExact));

                        if (std::fabs(
                                midiExact -
                                static_cast<double>(midi)) > 0.03)
                        {
                            return false;
                        }

                        const int value =
                            midi - BASE_MIDI[i];

                        if (value < -24 || value > 24)
                            return false;

                        values[i] = value;
                    }

                    return true;
                };

            for (size_t offset = 0;
                 offset + 12 <= SCAN_BYTES;
                 ++offset)
            {
                std::array<int, 6> p1Values{};
                std::array<int, 6> p2Values{};

                if (signedByteTuning(p1Bytes, offset, 1, p1Values) &&
                    signedByteTuning(p2Bytes, offset, 1, p2Values))
                {
                    ReportPairTuningHit(log, path, "signed-byte", offset, p1Values, p2Values);
                }

                if (signedByteTuning(p1Bytes, offset, 2, p1Values) &&
                    signedByteTuning(p2Bytes, offset, 2, p2Values))
                {
                    ReportPairTuningHit(log, path, "stride-2-byte", offset, p1Values, p2Values);
                }

                if (midiByteTuning(p1Bytes, offset, p1Values) &&
                    midiByteTuning(p2Bytes, offset, p2Values))
                {
                    ReportPairTuningHit(log, path, "MIDI-byte", offset, p1Values, p2Values);
                }

                if (int16CentsTuning(p1Bytes, offset, p1Values) &&
                    int16CentsTuning(p2Bytes, offset, p2Values))
                {
                    ReportPairTuningHit(log, path, "int16-cents", offset, p1Values, p2Values);
                }
            }

            for (size_t offset = 0;
                 offset + 24 <= SCAN_BYTES;
                 offset += 4)
            {
                std::array<int, 6> p1Values{};
                std::array<int, 6> p2Values{};

                if (int32Tuning(p1Bytes, offset, false, p1Values) &&
                    int32Tuning(p2Bytes, offset, false, p2Values))
                {
                    ReportPairTuningHit(log, path, "int32", offset, p1Values, p2Values);
                }

                if (int32Tuning(p1Bytes, offset, true, p1Values) &&
                    int32Tuning(p2Bytes, offset, true, p2Values))
                {
                    ReportPairTuningHit(log, path, "MIDI-int32", offset, p1Values, p2Values);
                }

                if (int32CentsTuning(p1Bytes, offset, p1Values) &&
                    int32CentsTuning(p2Bytes, offset, p2Values))
                {
                    ReportPairTuningHit(log, path, "int32-cents", offset, p1Values, p2Values);
                }

                if (floatFrequencyTuning(p1Bytes, offset, p1Values) &&
                    floatFrequencyTuning(p2Bytes, offset, p2Values))
                {
                    ReportPairTuningHit(log, path, "float-frequency", offset, p1Values, p2Values);
                }
            }
        }

        void ScanPairNodeForStringsAndRawDiffs(
            std::ostringstream& log,
            const std::string& path,
            std::uintptr_t player1,
            std::uintptr_t player2)
        {
            constexpr std::uintptr_t FIELD_BYTES = 0x200;
            constexpr size_t MAX_STRING_DIFFS = 24;
            constexpr size_t MAX_RAW_DIFFS = 32;

            size_t stringDiffs = 0;
            size_t rawDiffs = 0;

            for (std::uintptr_t offset = 0;
                 offset + sizeof(std::uintptr_t) <= FIELD_BYTES;
                 offset += sizeof(std::uintptr_t))
            {
                std::uintptr_t p1Value = 0;
                std::uintptr_t p2Value = 0;

                if (!TryReadValue(player1 + offset, p1Value) ||
                    !TryReadValue(player2 + offset, p2Value) ||
                    p1Value == p2Value)
                {
                    continue;
                }

                if (rawDiffs < MAX_RAW_DIFFS)
                {
                    log << "    RAW-DIFF "
                        << path
                        << " +"
                        << AddressText(offset)
                        << ": P1="
                        << AddressText(p1Value)
                        << " P2="
                        << AddressText(p2Value);

                    const std::int32_t p1Signed =
                        static_cast<std::int32_t>(p1Value);
                    const std::int32_t p2Signed =
                        static_cast<std::int32_t>(p2Value);

                    if ((p1Signed >= -2400 && p1Signed <= 2400) ||
                        (p2Signed >= -2400 && p2Signed <= 2400))
                    {
                        log << " signed=("
                            << p1Signed
                            << ','
                            << p2Signed
                            << ')';
                    }

                    float p1Float = 0.0f;
                    float p2Float = 0.0f;
                    TryReadValue(player1 + offset, p1Float);
                    TryReadValue(player2 + offset, p2Float);

                    if ((std::isfinite(p1Float) && std::fabs(p1Float) < 100000.0f) ||
                        (std::isfinite(p2Float) && std::fabs(p2Float) < 100000.0f))
                    {
                        log << " float=("
                            << p1Float
                            << ','
                            << p2Float
                            << ')';
                    }

                    log << "\n";
                    ++rawDiffs;
                }

                const bool p1Pointer =
                    LooksLikePointerValue(p1Value);
                const bool p2Pointer =
                    LooksLikePointerValue(p2Value);

                if (stringDiffs < MAX_STRING_DIFFS &&
                    p1Pointer != p2Pointer)
                {
                    const std::uintptr_t pointerValue =
                        p1Pointer ? p1Value : p2Value;
                    const std::string pointerText =
                        ReadString(pointerValue, 128);

                    log << "    ASYMMETRIC-POINTER "
                        << path
                        << " +"
                        << AddressText(offset)
                        << ": "
                        << (p1Pointer ? "P1=" : "P2=")
                        << AddressText(pointerValue);

                    if (LooksLikeDiagnosticText(pointerText))
                    {
                        log << " text=\""
                            << pointerText
                            << "\"";
                    }

                    log << "\n";
                    ++stringDiffs;
                }

                if (stringDiffs >= MAX_STRING_DIFFS ||
                    !p1Pointer ||
                    !p2Pointer)
                {
                    continue;
                }

                const std::string p1Text =
                    ReadString(p1Value, 128);
                const std::string p2Text =
                    ReadString(p2Value, 128);

                if (!LooksLikeDiagnosticText(p1Text) ||
                    !LooksLikeDiagnosticText(p2Text) ||
                    p1Text == p2Text)
                {
                    continue;
                }

                Tuning p1Tuning{};
                Tuning p2Tuning{};
                const bool p1IsTuning =
                    TryLookupTuningText(p1Text, p1Tuning);
                const bool p2IsTuning =
                    TryLookupTuningText(p2Text, p2Tuning);

                log << "    "
                    << (p1IsTuning || p2IsTuning
                        ? "*** TUNING-TEXT-DIFF "
                        : "STRING-DIFF ")
                    << path
                    << " +"
                    << AddressText(offset)
                    << ": P1=\""
                    << p1Text
                    << "\" P2=\""
                    << p2Text
                    << "\"\n";

                ++stringDiffs;
            }
        }

        void AppendExhaustiveBuilderPointerGraph(
            std::ostringstream& log,
            LONG firstSequence,
            LONG total,
            std::uintptr_t currentPlayer1)
        {
            constexpr size_t MAX_GRAPH_NODES = 384;
            constexpr int MAX_GRAPH_DEPTH = 3;
            constexpr std::uintptr_t CHILD_FIELD_BYTES = 0x200;

            std::vector<const BuilderCapture*> p1Captures;
            std::vector<const BuilderCapture*> p2Captures;

            for (LONG sequence = firstSequence;
                 sequence <= total;
                 ++sequence)
            {
                BuilderCapture& slot =
                    g_builderCaptures[
                        (sequence - 1) &
                        (BUILDER_CAPTURE_SLOTS - 1)];

                if (InterlockedCompareExchange(
                        &slot.sequence,
                        0,
                        0) != sequence ||
                    slot.detection == 0)
                {
                    continue;
                }

                if (slot.detection == currentPlayer1)
                    p1Captures.push_back(&slot);
                else if (currentPlayer1 != 0)
                    p2Captures.push_back(&slot);
            }

            log << "\nEXHAUSTIVE P1/P2 CONTEXT POINTER GRAPH\n";
            log << "  depth=" << MAX_GRAPH_DEPTH
                << " maxNodes=" << MAX_GRAPH_NODES
                << " scanBytesPerNode=0x600"
                << " childFields=0x200\n";

            if (p1Captures.empty() || p2Captures.empty())
            {
                log << "  insufficient P1/P2 captures for graph search\n";
                return;
            }

            std::vector<BuilderContextRootPair> roots;

            for (size_t p1Index = 0;
                 p1Index < p1Captures.size();
                 ++p1Index)
            {
                for (size_t p2Index = 0;
                     p2Index < p2Captures.size();
                     ++p2Index)
                {
                    const BuilderCapture& p1 = *p1Captures[p1Index];
                    const BuilderCapture& p2 = *p2Captures[p2Index];

                    char prefix[64] = {};
                    sprintf_s(
                        prefix,
                        "eventPair[%u,%u]",
                        static_cast<unsigned int>(p1Index),
                        static_cast<unsigned int>(p2Index));

                    AddBuilderContextRootPair(roots, std::string(prefix) + "/DETECTION", p1.detection, p2.detection);
                    AddBuilderContextRootPair(roots, std::string(prefix) + "/EAX", p1.eax, p2.eax);
                    AddBuilderContextRootPair(roots, std::string(prefix) + "/EBX", p1.ebx, p2.ebx);
                    AddBuilderContextRootPair(roots, std::string(prefix) + "/ECX", p1.ecx, p2.ecx);
                    AddBuilderContextRootPair(roots, std::string(prefix) + "/EDX", p1.edx, p2.edx);
                    AddBuilderContextRootPair(roots, std::string(prefix) + "/EDI", p1.edi, p2.edi);
                    AddBuilderContextRootPair(roots, std::string(prefix) + "/EBP", p1.ebp, p2.ebp);

                    for (size_t i = 0;
                         i < BUILDER_STACK_DWORDS;
                         ++i)
                    {
                        char label[96] = {};
                        sprintf_s(
                            label,
                            "%s/STACK+0x%02X",
                            prefix,
                            static_cast<unsigned int>(
                                i * sizeof(std::uintptr_t)));

                        AddBuilderContextRootPair(
                            roots,
                            label,
                            p1.stack[i],
                            p2.stack[i]);
                    }
                }
            }

            log << "  unique root pairs="
                << roots.size()
                << "\n";

            struct GraphNode
            {
                std::uintptr_t player1 = 0;
                std::uintptr_t player2 = 0;
                int depth = 0;
                std::string path;
            };

            std::vector<GraphNode> nodes;

            auto enqueue =
                [&](std::uintptr_t player1,
                    std::uintptr_t player2,
                    int depth,
                    const std::string& path)
                {
                    if (nodes.size() >= MAX_GRAPH_NODES ||
                        player1 == player2 ||
                        !LooksLikePointerValue(player1) ||
                        !LooksLikePointerValue(player2))
                    {
                        return;
                    }

                    for (const auto& existing : nodes)
                    {
                        if (existing.player1 == player1 &&
                            existing.player2 == player2)
                        {
                            return;
                        }
                    }

                    GraphNode node{};
                    node.player1 = player1;
                    node.player2 = player2;
                    node.depth = depth;
                    node.path = path;
                    nodes.push_back(node);
                };

            for (const auto& root : roots)
            {
                enqueue(
                    root.player1,
                    root.player2,
                    0,
                    root.label);
            }

            for (size_t cursor = 0;
                 cursor < nodes.size() &&
                 cursor < MAX_GRAPH_NODES;
                 ++cursor)
            {
                const GraphNode node = nodes[cursor];

                log << "\n  GRAPH-NODE "
                    << (cursor + 1)
                    << " depth="
                    << node.depth
                    << " path="
                    << node.path
                    << "\n"
                    << "    P1="
                    << AddressText(node.player1)
                    << " P2="
                    << AddressText(node.player2)
                    << "\n";

                ScanPairNodeForTuningDifferences(
                    log,
                    node.path,
                    node.player1,
                    node.player2);

                ScanPairNodeForStringsAndRawDiffs(
                    log,
                    node.path,
                    node.player1,
                    node.player2);

                if (node.depth >= MAX_GRAPH_DEPTH)
                    continue;

                for (std::uintptr_t offset = 0;
                     offset + sizeof(std::uintptr_t) <= CHILD_FIELD_BYTES;
                     offset += sizeof(std::uintptr_t))
                {
                    std::uintptr_t p1Child = 0;
                    std::uintptr_t p2Child = 0;

                    if (!TryReadValue(node.player1 + offset, p1Child) ||
                        !TryReadValue(node.player2 + offset, p2Child) ||
                        p1Child == p2Child ||
                        !LooksLikePointerValue(p1Child) ||
                        !LooksLikePointerValue(p2Child))
                    {
                        continue;
                    }

                    char suffix[32] = {};
                    sprintf_s(
                        suffix,
                        "->+0x%03X",
                        static_cast<unsigned int>(offset));

                    enqueue(
                        p1Child,
                        p2Child,
                        node.depth + 1,
                        node.path + suffix);
                }
            }

            log << "\n  graph nodes visited="
                << (nodes.size() < MAX_GRAPH_NODES
                    ? nodes.size()
                    : MAX_GRAPH_NODES)
                << "\n";

            if (nodes.size() >= MAX_GRAPH_NODES)
            {
                log << "  NOTE: graph node cap reached; root/context data above remains complete\n";
            }
        }

        void AppendBuilderCaptureSummary(
            std::ostringstream& log)
        {
            const LONG total =
                InterlockedCompareExchange(
                    &g_builderCaptureCount,
                    0,
                    0);

            LONG firstSequence =
                g_builderLastDumpSequence + 1;

            const LONG oldestAvailable =
                total >= BUILDER_CAPTURE_SLOTS
                ? total - BUILDER_CAPTURE_SLOTS + 1
                : 1;

            if (firstSequence < oldestAvailable)
            {
                log << "  NOTE: older captures were overwritten; showing newest "
                    << BUILDER_CAPTURE_SLOTS
                    << " events\n";
                firstSequence = oldestAvailable;
            }

            const std::uintptr_t currentPlayer1 =
                ResolveConfiguredDetectionObject();

            log << "  current P1 detection: "
                << AddressText(currentPlayer1)
                << "\n";
            log << "  captured builder calls since previous F10: "
                << (total >= firstSequence
                    ? total - firstSequence + 1
                    : 0)
                << "\n";

            struct DetectionSummary
            {
                std::uintptr_t detection = 0;
                LONG lastCents = 0;
                LONG callCount = 0;
            };

            std::vector<DetectionSummary> detections;

            for (LONG sequence = firstSequence;
                 sequence <= total;
                 ++sequence)
            {
                BuilderCapture& slot =
                    g_builderCaptures[
                        (sequence - 1) &
                        (BUILDER_CAPTURE_SLOTS - 1)];

                const LONG published =
                    InterlockedCompareExchange(
                        &slot.sequence,
                        0,
                        0);

                if (published != sequence)
                    continue;

                const std::uintptr_t detection =
                    slot.detection;

                log << "  event "
                    << sequence
                    << ": detection="
                    << AddressText(detection)
                    << " cents=";

                if (slot.cents == 0x7FFFFFFF)
                    log << "<unreadable>";
                else
                    log << slot.cents;

                log << " thread="
                    << slot.threadId;

                if (detection == currentPlayer1)
                    log << "  <current P1>";

                log << "\n";

                bool found = false;

                for (auto& summary : detections)
                {
                    if (summary.detection == detection)
                    {
                        summary.lastCents = slot.cents;
                        ++summary.callCount;
                        found = true;
                        break;
                    }
                }

                if (!found && detection != 0)
                {
                    DetectionSummary summary{};
                    summary.detection = detection;
                    summary.lastCents = slot.cents;
                    summary.callCount = 1;
                    detections.push_back(summary);
                }
            }

            log << "\nUNIQUE DETECTION OBJECTS\n";

            if (detections.empty())
            {
                log << "  none captured\n";
            }

            for (size_t i = 0;
                 i < detections.size();
                 ++i)
            {
                const auto& summary =
                    detections[i];

                float reference = 0.0f;
                TryReadValue(
                    summary.detection + 0x135C,
                    reference);

                log << "  object "
                    << (i + 1)
                    << ": "
                    << AddressText(summary.detection)
                    << " calls="
                    << summary.callCount
                    << " lastCents=";

                if (summary.lastCents == 0x7FFFFFFF)
                    log << "<unreadable>";
                else
                    log << summary.lastCents;

                log << " +0x135C="
                    << reference;

                if (summary.detection == currentPlayer1)
                    log << "  <current P1>";
                else if (currentPlayer1 != 0)
                    log << "  <P2/other candidate>";

                log << "\n";

                int referenceHz =
                    static_cast<int>(
                        std::round(reference));

                if (referenceHz < 100 ||
                    referenceHz > 1000)
                {
                    referenceHz = 440;
                }

                TraceDetectionTuningSignatures(
                    log,
                    summary.detection,
                    referenceHz);
            }

            if (currentPlayer1 != 0)
            {
                for (const auto& summary : detections)
                {
                    if (summary.detection == 0 ||
                        summary.detection == currentPlayer1)
                    {
                        continue;
                    }

                    TraceDetectionPairTuningDifferences(
                        log,
                        currentPlayer1,
                        summary.detection);
                }
            }

            AppendBuilderContextComparison(
                log,
                firstSequence,
                total,
                currentPlayer1);

            AppendAllBuilderContexts(
                log,
                firstSequence,
                total,
                currentPlayer1);

            AppendUniqueBuilderCallers(
                log,
                firstSequence,
                total);

            AppendExhaustiveBuilderPointerGraph(
                log,
                firstSequence,
                total,
                currentPlayer1);

            g_builderLastDumpSequence = total;
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

    bool TryReadTunerTarget(
        Tuning& tuning)
    {
        return
            TryReadTunerTextTuning(
                tuning);
    }

    bool CaptureDebugSnapshot()
    {
        const bool alreadyInstalled =
            InterlockedCompareExchange(
                &g_builderHookInstalled,
                0,
                0) != 0;

        if (!alreadyInstalled &&
            !InstallReferenceBuilderDiagnostic())
        {
            return false;
        }

        SYSTEMTIME now{};
        GetLocalTime(&now);

        std::ostringstream log;

        log << "============================================================\n";
        log << "RL-Mods tuning-reference builder context diagnostic\n";
        log << "BUILD: BUILDER_CONTEXT_V2_EXHAUSTIVE\n";
        log << std::setfill('0')
            << std::dec
            << now.wYear << '-'
            << std::setw(2) << now.wMonth << '-'
            << std::setw(2) << now.wDay << ' '
            << std::setw(2) << now.wHour << ':'
            << std::setw(2) << now.wMinute << ':'
            << std::setw(2) << now.wSecond
            << "\n";
        log << "Configured version: "
            << (GetExecutableVersion() ==
                    ExecutableVersion::LearnAndPlay2024
                ? "2024"
                : "2022")
            << "\n";
        log << "Reference builder: "
            << AddressText(g_referenceBuilder)
            << "\n";
        log << "Current menu: ";

        const std::string menu =
            CurrentMenu();

        log << (menu.empty()
                ? "<empty/unresolved>"
                : menu)
            << "\n\n";

        if (!alreadyInstalled)
        {
            log << "BUILDER HOOK ARMED\n";
            log << "  This first F10 only arms the capture hook.\n";
            log << "  Enter the MULTIPLAYER pre-song tuner with different P1/P2 arrangements, then press F10 there.\n";
        }
        else
        {
            AppendBuilderCaptureSummary(
                log);

            log << "\nNORMAL RL-MODS READERS\n";

            Tuning target{};

            if (TryReadTunerTarget(target))
            {
                log << "  TryReadTunerTarget(): "
                    << VectorText(target)
                    << " / "
                    << Name(target)
                    << "\n";
            }
            else
            {
                log << "  TryReadTunerTarget(): FAILED\n";
            }

            Tuning arrangement{};

            if (TryReadArrangement(arrangement))
            {
                log << "  TryReadArrangement(): "
                    << VectorText(arrangement)
                    << " / "
                    << Name(arrangement)
                    << "\n";
            }
            else
            {
                log << "  TryReadArrangement(): FAILED\n";
            }

            int referenceHz = 0;

            if (TryReadReferenceHz(referenceHz))
            {
                log << "  TryReadReferenceHz(): A"
                    << referenceHz
                    << "\n";
            }
            else
            {
                log << "  TryReadReferenceHz(): FAILED\n";
            }
        }

        log << "============================================================\n\n";

        const std::wstring debugPath =
            BuildGamePath(
                L"RLMods_tuning_debug.txt");

        FILE* file = nullptr;

        if (_wfopen_s(
                &file,
                debugPath.c_str(),
                L"ab") != 0 ||
            !file)
        {
            return false;
        }

        const std::string output =
            log.str();

        const size_t written =
            fwrite(
                output.data(),
                1,
                output.size(),
                file);

        fclose(file);

        return written ==
            output.size();
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
