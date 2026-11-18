#pragma once
// SPDX-License-Identifier: AGPL-3.0-only
#include "../shared/binutils.h"
#include <algorithm>

namespace MirvPovBuyMenuAdapter {
using Afx::BinUtils::MemRange;
struct Resolver {
    HMODULE module;
    MemRange text;
    explicit Resolver(HMODULE value) : module(value), text(0, 0) {
        if(value) {
            Afx::BinUtils::ImageSectionsReader sections(value);
            if(!sections.Eof()) text = sections.GetMemRange();
        }
    }
    size_t Find(const char* pattern, MemRange range) const {
        const auto match = Afx::BinUtils::FindPatternString(range, pattern);
        return !match.IsEmpty() && Afx::BinUtils::FindPatternString(
            MemRange(match.Start + 1, range.End), pattern).IsEmpty() ? match.Start : 0;
    }
    size_t Find(const char* pattern) const { return Find(pattern, text); }
    MemRange Body(size_t address) const {
        DWORD64 base = 0;
        const auto* entry = address ? RtlLookupFunctionEntry(address, &base, nullptr) : nullptr;
        return entry && base == reinterpret_cast<size_t>(module)
            ? MemRange(base + entry->BeginAddress, base + entry->EndAddress) : MemRange(0, 0);
    }
    template<class T> bool Bind(T& target, const char* pattern) const {
        target = reinterpret_cast<T>(Find(pattern));
        return target != nullptr;
    }
    template<class T> bool Call(T& target, size_t site) const {
        if(site < text.Start || site > text.End || text.End - site < 5 || *reinterpret_cast<const unsigned char*>(site) != 0xE8) return false;
        const auto address = site + 5 + *reinterpret_cast<const int32_t*>(site + 1);
        if(address < text.Start || address >= text.End) return false;
        target = reinterpret_cast<T>(address);
        return true;
    }
    bool Starts(size_t address, const char* pattern) const {
        if(address < text.Start || address > text.End || text.End - address < 48) return false;
        const auto match = Afx::BinUtils::FindPatternString(MemRange(address, address + 48), pattern);
        return !match.IsEmpty() && match.Start == address;
    }
    size_t Reference(const char* pattern, size_t displacement, size_t target) const {
        auto remaining = text;
        size_t result = 0;
        while(!remaining.IsEmpty()) {
            const auto match = Afx::BinUtils::FindPatternString(remaining, pattern);
            if(match.IsEmpty()) break;
            const auto value = match.Start + displacement + 4 + *reinterpret_cast<const int32_t*>(match.Start + displacement);
            if(value == target) { if(result) return 0; result = match.Start; }
            remaining.Start = match.Start + 1;
        }
        return result;
    }
    size_t Root(size_t address) const {
        if(address < text.Start || address >= text.End) return 0;
        DWORD64 base = 0;
        const auto* entry = address ? RtlLookupFunctionEntry(address, &base, nullptr) : nullptr;
        if(!entry || base != reinterpret_cast<size_t>(module)) return 0;
        for(int depth = 0; depth < 8; ++depth) {
            const auto unwind = reinterpret_cast<const unsigned char*>(base + entry->UnwindData);
            if(!(unwind[0] & (UNW_FLAG_CHAININFO << 3))) return base + entry->BeginAddress;
            entry = reinterpret_cast<const RUNTIME_FUNCTION*>(unwind + ((4 + 2 * unwind[2] + 3) & ~3));
        }
        return 0;
    }
    bool Has(size_t function, const char* pattern) const {
        const auto root = Root(function);
        if(!root) return false;
        auto range = MemRange(root, (std::min)(root + 0x4000, text.End));
        while(!range.IsEmpty()) {
            const auto match = Afx::BinUtils::FindPatternString(range, pattern);
            if(match.IsEmpty()) return false;
            if(Root(match.Start) == root) return true;
            range.Start = match.Start + 1;
        }
        return false;
    }
};
// Signatures preserve native arguments and field accesses, wildcarding relative code/data addresses.
inline constexpr char Open[] = "40 53 56 57 41 56 48 83 EC 38 48 8B F1 48 8B 49 08 48 8B 01 FF 90 10 01 00 00 84 C0 0F 84 ?? ?? ?? ??";
inline constexpr char Close[] = "40 56 48 83 EC 60 48 8B F1 48 8B 49 08 48 8B 01 FF 90 10 01 00 00 84 C0 0F 84 ?? ?? ?? ?? 48 89 9C 24 80 00 00 00";
inline constexpr char Refresh[] = "40 53 48 83 EC 30 48 8B D9 48 8B 49 08 48 8B 01 FF 90 10 01 00 00 84 C0 74 ?? 48 8B CB E8 ?? ?? ?? ??";
inline constexpr char FullRefresh[] = "48 89 5C 24 18 57 48 83 EC 20 48 8B D9 33 C9 E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 0F 84 ?? ?? ?? ??";
inline constexpr char Hover[] = "40 53 56 48 83 EC 68 48 8B F1 E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 0F 84 ?? ?? ?? ?? 48 8B 00 48 8B CB";
inline constexpr char Think[] = "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 74 ?? 48 8B 00 48 8B CF";
inline constexpr char Loadout[] = "48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 41 30 41 8B F8 8B EA 48 8B F1 80 B8 90 07 00 00 00 75 ?? 33 C0 48 8B 6C 24 38 48 8B 74 24 40 48 83 C4 20 5F C3 48 89 5C 24 30 E8 ?? ?? ?? ?? 44 8B C7 8B D5 48 8B 88 D0 28 05 00 E8 ?? ?? ?? ??";
inline constexpr char LoadoutHover[] = "48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 41 30 41 8B F8 8B EA 48 8B F1 80 B8 90 07 00 00 00 75 ?? 33 C0 48 8B 6C 24 38 48 8B 74 24 40 48 83 C4 20 5F C3 48 89 5C 24 30 E8 ?? ?? ?? ?? 44 8B C7 8B D5 48 8B 88 D0 28 05 00 48 8B 01";
inline constexpr char Write[] = "48 83 EC 38 4C 8B C1 38 91 ?? ?? ?? ?? 74 ?? 88 91 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 89 5C 24 30";
inline constexpr char Purchase[] = "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 81 EC 40 01 00 00 49 8B 00 8B EA 48 8B F1 48 8D 15 ?? ?? ?? ??";
inline constexpr char Sell[] = "48 89 5C 24 08 48 89 74 24 10 57 48 81 EC 40 01 00 00 8B DA 48 8B F9 E8 ?? ?? ?? ?? 0F 57 DB 48 8D 15 ?? ?? ?? ??";
inline constexpr char Model[] = "48 89 4C 24 08 55 53 57 41 57 48 8D 6C 24 C1 48 81 EC D8 00 00 00 48 8B F9 4C 8B FA 33 C9 E8 ?? ?? ?? ??";
inline constexpr char Select[] = "40 53 55 56 57 41 56 48 81 EC 40 01 00 00 48 8B F9 8B EA 33 C9 E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ??";
inline constexpr char SetModel[] = "40 53 48 83 EC 40 48 89 74 24 58 48 8B DA 48 8B F1 48 89 7C 24 38 4C 89 64 24 30 48 8D 4C 24 60";
inline constexpr char KeyInput[] = "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 48 8D 71 E0 48 8B E9 48 8B 4E 08 48 8B DA";
inline constexpr char UpdatePreview[] = "85 D2 0F 88 ?? ?? ?? ?? 48 89 4C 24 08 55 53 48 8D AC 24 88 FE FF FF 48 81 EC 78 02 00 00 48 63 DA";
inline constexpr char CopyItem[] = "40 53 55 56 57 48 83 EC 28 48 8B FA 48 8B D9 E8 ?? ?? ?? ?? 0F B7 87 BA 01 00 00 66 39 83 BA 01 00 00";
inline constexpr char EqualItems[] = "0F B6 82 E8 01 00 00 4C 8B C1 38 81 E8 01 00 00 75 ?? 44 8B 99 D0 01 00 00 8B 81 D4 01 00 00 49 C1 E3 20";
inline constexpr char ClearItem[] = "48 89 5C 24 08 57 48 83 EC 20 80 B9 E8 01 00 00 00 48 8B D9 74 ?? C6 81 E8 01 00 00 00 33 FF 48 89 B9 C8 01 00 00";
inline constexpr char PawnModel[] = "48 89 5C 24 08 57 48 83 EC 20 48 8B 41 10 48 8B DA 33 FF 48 8B 10 48 85 D2 74 ?? 48 8B 40 08 4C 63 05 ?? ?? ?? ?? 48 8B 88 88 00 00 00 42 0F B7 04 41 B9 FF FF 00 00 66 3B C1 74 ?? 48 8B 0C 10 EB ?? 48 8B CF 48 85 C9 74 ?? 48 8B 49 08 48 8B 01 FF 50 70 48 85 C0 74 ?? 48 89 3B";
inline constexpr char Item[] = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 30 41 0F B6 D9 41 8B F8 0F B7 F2 48 8B E9";
inline constexpr char DefaultLoadout[] = "41 83 F8 39 77 ?? 83 FA 03 77 ?? 48 63 C2 48 6B D0 3A 49 63 C0 48 03 D0 48 69 C2 B0 05 00 00 48 05 50 01 00 00";
}
