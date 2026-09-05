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

        struct BuilderCapture
        {
            volatile LONG sequence = 0;
            std::uintptr_t detection = 0;
            LONG cents = 0;
            DWORD threadId = 0;
            ULONGLONG tick = 0;
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
        log << "RL-Mods tuning-reference builder diagnostic\n";
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
            log << "  Enter/re-enter a pre-song tuner, then press F10 there.\n";
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
