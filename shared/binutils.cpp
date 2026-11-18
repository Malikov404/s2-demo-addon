#include "stdafx.h"

#include "binutils.h"

#include <algorithm>
#include <vector>

#define PtrFromRva( base, rva ) ( ( ( PBYTE ) base ) + rva )

namespace Afx {
namespace BinUtils {

////////////////////////////////////////////////////////////////////////////////

// TODO: optimize this.
MemRange FindBytes(MemRange memRange, char const * pattern, size_t patternSize)
{
	size_t matchDepth = 0;
	size_t oldMemRangeStart = memRange.Start;

	if(!pattern)
		return MemRange(oldMemRangeStart, min(oldMemRangeStart, memRange.End));

	if(1 > patternSize)
		return MemRange(oldMemRangeStart, min(oldMemRangeStart +1, memRange.End));

	for(;memRange.Start < memRange.End;memRange.Start++)
	{
		char cur = *(char const *)memRange.Start;

		if(cur == pattern[matchDepth])
			matchDepth++;
		else
		{
			memRange.Start -= matchDepth;
			matchDepth = 0;
			continue;
		}

		if(matchDepth == patternSize)
			return MemRange(memRange.Start-matchDepth+1, memRange.Start+1);
	}

	return MemRange(oldMemRangeStart, min(oldMemRangeStart, memRange.End));
}

MemRange FindBytesReverse(MemRange memRange, char const * pattern, size_t patternSize)
{
	size_t matchDepth = 0;
	size_t oldMemRangeEnd = memRange.End;

	if(!pattern)
		return MemRange(max(oldMemRangeEnd, memRange.Start), oldMemRangeEnd);

	if(1 > patternSize)
		return MemRange(max(oldMemRangeEnd -1, memRange.Start), oldMemRangeEnd);

	for(;memRange.Start < memRange.End;)
	{
		memRange.End--;
		char cur = *(char const *)memRange.End;

		if(cur == pattern[patternSize - matchDepth - 1])
			matchDepth++;
		else
		{
			memRange.End += matchDepth;
			matchDepth = 0;
			continue;
		}

		if(matchDepth == patternSize)
			return MemRange(memRange.End, memRange.End+matchDepth);
	}

	return MemRange(max(oldMemRangeEnd, memRange.Start), oldMemRangeEnd);
}

MemRange FindCString(MemRange memRange, char const * pattern)
{
	return FindBytes(memRange, pattern, strlen(pattern)+1);
}

MemRange FindWCString(MemRange memRange, wchar_t const * pattern)
{
	return FindBytes(memRange, (char const *)pattern, sizeof(wchar_t)*(wcslen(pattern)+1));
}

MemRange FindPatternString(MemRange memRange, char const * hexBytePattern)
{
	const MemRange empty(memRange.Start, memRange.Start);
	if(!hexBytePattern || memRange.IsEmpty()) return empty;

	// Parse once per search, not once per byte of the module. Keep nibble
	// wildcards and use the longest literal run to reject candidates cheaply.
	std::vector<unsigned char> values, masks;
	size_t anchor = 0, anchorSize = 0, literalSize = 0;
	const auto nibble = [](char c) -> int {
		if('0' <= c && c <= '9') return c - '0';
		if('a' <= c && c <= 'f') return c - 'a' + 10;
		if('A' <= c && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	while(*hexBytePattern) {
		while(*hexBytePattern == ' ') ++hexBytePattern;
		if(!*hexBytePattern) break;
		const char hi = *hexBytePattern++;
		while(*hexBytePattern == ' ') ++hexBytePattern;
		const char lo = *hexBytePattern ? *hexBytePattern++ : '?';
		const int high = nibble(hi), low = nibble(lo);
		if((hi != '?' && high < 0) || (lo != '?' && low < 0)) return empty;
		const auto mask = static_cast<unsigned char>((hi == '?' ? 0 : 0xf0) | (lo == '?' ? 0 : 0x0f));
		values.push_back(static_cast<unsigned char>(((high & 0xf) << 4 | (low & 0xf)) & mask));
		masks.push_back(mask);
		literalSize = mask == 0xff ? literalSize + 1 : 0;
		if(literalSize > anchorSize) { anchorSize = literalSize; anchor = values.size() - literalSize; }
	}
	const size_t size = values.size();
	if(!size || size > memRange.End - memRange.Start) return empty;
	const auto last = reinterpret_cast<const unsigned char*>(memRange.End - size);
	for(auto candidate = reinterpret_cast<const unsigned char*>(memRange.Start); candidate <= last; ++candidate) {
		if(anchorSize) {
			const auto found = static_cast<const unsigned char*>(memchr(candidate + anchor, values[anchor], last - candidate + 1));
			if(!found) break;
			candidate = found - anchor;
			if(memcmp(found, values.data() + anchor, anchorSize)) continue;
		}
		size_t i = 0;
		for(; i < size; ++i) if((candidate[i] & masks[i]) != values[i]) break;
		if(i == size) return MemRange(reinterpret_cast<size_t>(candidate), reinterpret_cast<size_t>(candidate) + size);
	}
	return empty;
}

MemRange FindAddrInt32OffsetRef(MemRange memRange, size_t addr, int32_t extraOffset)
{
	size_t memRangeStart = memRange.Start;
	while(true) {
		if(sizeof(int32_t) >  memRange.End - memRangeStart) break;

		int32_t int32Value = *(const int32_t *)memRangeStart;
		size_t curAddr = memRangeStart + extraOffset + int32Value;

		if(curAddr == addr)
			return MemRange(memRangeStart, memRangeStart+sizeof(int32_t));

		memRangeStart++;
	}

	return MemRange(memRange.Start, min(memRange.Start, memRange.End));
}

MemRange FindAddrInt32OffsetRefInContext(MemRange memRange, size_t addr, int32_t extraOffset, char const * prefixHexBytePattern, char const * suffixHexBytePattern) {
	while(true) {
		MemRange memRange2 = prefixHexBytePattern ? FindPatternString(memRange, prefixHexBytePattern) : memRange;
		if(memRange2.IsEmpty()) break;
		MemRange memRange3 = FindAddrInt32OffsetRef(memRange.And(MemRange(memRange2.End, memRange2.End+sizeof(int32_t))), addr, extraOffset);
		if(memRange3.IsEmpty()) {
			if(prefixHexBytePattern) {
				memRange.Start = memRange2.End;
				continue;
			}
			break;
		}
		if(suffixHexBytePattern) {
			MemRange memRange4 = FindPatternString(MemRange(memRange3.End, memRange.End), suffixHexBytePattern);
			if(memRange4.IsEmpty()) {
				memRange.Start = prefixHexBytePattern ? memRange2.End : memRange3.End;
				continue;
			}
		}
		return memRange3;
	}

	return MemRange(memRange.Start, min(memRange.Start, memRange.End));
}

size_t FindClassVtable(HMODULE hModule, const char * name, DWORD rttiBaseClassArrayOffset, DWORD completeObjectLocatorOffset)
{
	ImageSectionsReader imageSectionReader(hModule);

	if (imageSectionReader.Eof())
		return 0;

	imageSectionReader.Next();

	MemRange data2Range = imageSectionReader.GetMemRange();

	if (imageSectionReader.Eof())
		return 0;

	imageSectionReader.Next();

	MemRange data3Range = imageSectionReader.GetMemRange();

	MemRange rangeName = FindCString(data3Range, name);

	if (rangeName.IsEmpty())
		return 0;

	MemRange rangeRttiTypeDescriptor = data3Range.And(MemRange::FromSize((size_t)(rangeName.Start - 2*sizeof(void*)), (size_t)sizeof(DWORD)));

	if (rangeRttiTypeDescriptor.IsEmpty())
		return 0;

#ifndef _WIN64
	DWORD dwOffsetRttiTypeDescriptor = rangeRttiTypeDescriptor.Start;
#else
	DWORD dwOffsetRttiTypeDescriptor = (DWORD)(rangeRttiTypeDescriptor.Start - (size_t)hModule);
#endif

	MemRange rangeMaybeRttiBaseClassDescriptor = MemRange(data2Range.Start,data2Range.Start);
	while(true)
	{
		rangeMaybeRttiBaseClassDescriptor = FindBytes(MemRange(rangeMaybeRttiBaseClassDescriptor.End, data2Range.End), (const char *)&dwOffsetRttiTypeDescriptor, sizeof(DWORD));

		if (rangeMaybeRttiBaseClassDescriptor.IsEmpty())
			break;

#ifndef _WIN64
		DWORD dwOffsetMaybeRttiBaseClassDescriptor = rangeMaybeRttiBaseClassDescriptor.Start;
#else
		DWORD dwOffsetMaybeRttiBaseClassDescriptor = (DWORD)(rangeMaybeRttiBaseClassDescriptor.Start - (size_t)hModule);
#endif		

		MemRange rangeMaybeRttiBaseClassArrayRef = MemRange(data2Range.Start, data2Range.Start);
		while (true)
		{
			rangeMaybeRttiBaseClassArrayRef = FindBytes(MemRange(rangeMaybeRttiBaseClassArrayRef.End, data2Range.End), (char const *)&dwOffsetMaybeRttiBaseClassDescriptor, sizeof(DWORD));

			if (rangeMaybeRttiBaseClassArrayRef.IsEmpty())
				break;

			MemRange rangeMaybeRttiBaseClassArray = data2Range.And(MemRange::FromSize((size_t)(rangeMaybeRttiBaseClassArrayRef.Start - sizeof(DWORD) * rttiBaseClassArrayOffset), (size_t)sizeof(DWORD)));

			if (rangeMaybeRttiBaseClassArray.IsEmpty())
				continue;

#ifndef _WIN64
			DWORD dwOffsetMaybeRttiBaseClassArray = rangeMaybeRttiBaseClassArray.Start;
#else
			DWORD dwOffsetMaybeRttiBaseClassArray = (DWORD)(rangeMaybeRttiBaseClassArray.Start - (size_t)hModule);
#endif		


			MemRange rangeMaybeRttiClassHirachyDescriptorRef = MemRange(data2Range.Start, data2Range.Start);
			while (true)
			{
				rangeMaybeRttiClassHirachyDescriptorRef = FindBytes(MemRange(rangeMaybeRttiClassHirachyDescriptorRef.End, data2Range.End), (const char *)&dwOffsetMaybeRttiBaseClassArray, sizeof(DWORD));

				if (rangeMaybeRttiClassHirachyDescriptorRef.IsEmpty())
					break;

				MemRange rangeMaybeRttiClassHirachyDescriptor = data2Range.And(MemRange::FromSize((size_t)(rangeMaybeRttiClassHirachyDescriptorRef.Start - 0x0c), (size_t)sizeof(DWORD)));

				if (rangeMaybeRttiClassHirachyDescriptor.IsEmpty())
					continue;

#ifndef _WIN64
				DWORD dwOffsetMaybeRttiClassHirachyDescriptor = rangeMaybeRttiClassHirachyDescriptor.Start;
#else
				DWORD dwOffsetMaybeRttiClassHirachyDescriptor = (DWORD)(rangeMaybeRttiClassHirachyDescriptor.Start - (size_t)hModule);
#endif	

				MemRange rangeMaybeCompleteObjectAllocatorRef = MemRange(data2Range.Start, data2Range.Start);
				while (true)
				{
					rangeMaybeCompleteObjectAllocatorRef = FindBytes(MemRange(rangeMaybeCompleteObjectAllocatorRef.End, data2Range.End), (const char *)&dwOffsetMaybeRttiClassHirachyDescriptor, sizeof(DWORD));

					if (rangeMaybeCompleteObjectAllocatorRef.IsEmpty())
						break;

					MemRange rangeMaybeCompleteObjectAllocator = data2Range.And(MemRange::FromSize((size_t)(rangeMaybeCompleteObjectAllocatorRef.Start - 0x10), (size_t)sizeof(DWORD)));

					if (rangeMaybeCompleteObjectAllocator.IsEmpty())
						continue;

					if (completeObjectLocatorOffset != *(DWORD *)(rangeMaybeCompleteObjectAllocator.Start + 0x4))
						continue;

					MemRange rangeMaybeVTableRef = MemRange(data2Range.Start, data2Range.Start);
					while (true)
					{
						rangeMaybeVTableRef = FindBytes(MemRange(rangeMaybeVTableRef.End, data2Range.End), (const char *)&rangeMaybeCompleteObjectAllocator.Start, sizeof(size_t));

						if (rangeMaybeVTableRef.IsEmpty())
							break;

						MemRange rangeMaybeVTable = data2Range.And(MemRange::FromSize((size_t)(rangeMaybeVTableRef.Start + sizeof(void*)), (size_t)sizeof(DWORD)));

						if (rangeMaybeVTable.IsEmpty())
							continue;

						return rangeMaybeVTable.Start;
					}
				}
			}
		}
	}

	return 0;
}

// ImageSectionsReader /////////////////////////////////////////////////////////

ImageSectionsReader::ImageSectionsReader(HMODULE hModule)
{
	PIMAGE_DOS_HEADER DosHeader = ( PIMAGE_DOS_HEADER ) hModule;
	PIMAGE_NT_HEADERS NtHeader; 

	m_hModule = hModule;
	m_SectionsLeft = 0;

	NtHeader = (PIMAGE_NT_HEADERS) PtrFromRva( DosHeader, DosHeader->e_lfanew );
	if( IMAGE_NT_SIGNATURE != NtHeader->Signature )
	{
		return;		
	}

	m_SectionsLeft = NtHeader->FileHeader.NumberOfSections;
	m_Section = (PIMAGE_SECTION_HEADER) PtrFromRva( NtHeader, sizeof(IMAGE_NT_HEADERS) );
}

bool ImageSectionsReader::Eof(void)
{
	return !(0 < m_SectionsLeft);
}

void ImageSectionsReader::Next(void)
{
	if(Eof()) return;

	m_SectionsLeft--;
	m_Section = (PIMAGE_SECTION_HEADER) PtrFromRva( m_Section, sizeof(IMAGE_SECTION_HEADER) );
}

void ImageSectionsReader::Next(DWORD characteristicsFilter)
{
	for(;!Eof();Next())
	{
		if(characteristicsFilter == (m_Section->Characteristics & characteristicsFilter))
			break;
	}
}

PIMAGE_SECTION_HEADER ImageSectionsReader::Get(void)
{
	return m_Section;
}

MemRange ImageSectionsReader::GetMemRange(void)
{
	size_t startAddress = GetStartAddress();
	return MemRange(startAddress, startAddress+GetSize());
}

size_t ImageSectionsReader::GetStartAddress(void)
{
	return (size_t)PtrFromRva(m_hModule, m_Section->VirtualAddress);
}

size_t ImageSectionsReader::GetSize(void)
{
	return m_Section->Misc.VirtualSize;
}

// MemRange ////////////////////////////////////////////////////////////////////


MemRange MemRange::FromEmpty() {
	return MemRange(0,0);
}

MemRange MemRange::FromSize(size_t address, size_t size)
{
	return MemRange(address, address + size);
}

MemRange::MemRange()
{
	Start = End = 0;
}

MemRange::MemRange(size_t start, size_t end)
{
	Start = start;
	End = end;
}

bool MemRange::IsEmpty(void) const
{
	return End <= Start;
}

MemRange MemRange::And(const MemRange & range) const
{
	if (this->IsEmpty())
		return MemRange(*this);

	if (range.IsEmpty())
		return MemRange(range);

	return MemRange(max(range.Start, this->Start), min(range.End, this->End));
}


} // namespace Afx {
} // namespace BinUtils {
