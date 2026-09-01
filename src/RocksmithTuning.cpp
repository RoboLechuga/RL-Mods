#include "RocksmithTuning.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace RocksmithTuning
{
    namespace
    {
        // Rocksmith memory-layout facts for the two versions currently tracked by
        // RSMods: Remastered September 2022 and Learn & Play December 2024.
        // The 2024 build uses relocated module-base roots; the 2022 build
        // uses the fixed roots below.
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
        std::uintptr_t Resolve2024Then2022(
            std::uintptr_t root2024Offset,
            std::uintptr_t root2022,
            const std::array<
                std::uintptr_t,
                N>& offsets)
        {
            HMODULE gameModule =
                GetModuleHandleW(nullptr);

            if (gameModule)
            {
                const std::uintptr_t base =
                    reinterpret_cast<std::uintptr_t>(
                        gameModule);

                const std::uintptr_t resolved2024 =
                    ResolvePointerChain(
                        base + root2024Offset,
                        offsets);

                if (resolved2024)
                    return resolved2024;
            }

            return
                ResolvePointerChain(
                    root2022,
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
            HMODULE gameModule =
                GetModuleHandleW(nullptr);

            if (gameModule)
            {
                const std::uintptr_t base =
                    reinterpret_cast<std::uintptr_t>(
                        gameModule);

                const std::uintptr_t resolved2024 =
                    ResolvePointerChain(
                        base +
                            CURRENT_MENU_2024_ROOT_OFFSET,
                        CURRENT_MENU_OFFSETS);

                const std::string text2024 =
                    ReadString(
                        resolved2024);

                if (!text2024.empty())
                    return text2024;
            }

            const std::uintptr_t resolved2022 =
                ResolvePointerChain(
                    CURRENT_MENU_2022_ROOT,
                    CURRENT_MENU_OFFSETS);

            return
                ReadString(
                    resolved2022);
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
                Resolve2024Then2022(
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
                Resolve2024Then2022(
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
            Resolve2024Then2022(
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
