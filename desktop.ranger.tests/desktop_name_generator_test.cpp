#include "pch.h"

#include <algorithm>
#include <random>
#include <string>
#include <unordered_set>

#include "desktop_name_generator.h"

namespace DesktopRanger::DesktopName::Tests
{
	TEST(DesktopNameGenerator, GeneratesDefaultLengthName)
	{
		std::mt19937 generator{ 12345 };

		const std::wstring name = Generate(generator);

		EXPECT_EQ(name.size(), kDesktopNameLength);
	}

	TEST(DesktopNameGenerator, GeneratesRequestedLength)
	{
		std::mt19937 generator{ 12345 };

		const std::wstring name = Generate(generator, 32);

		EXPECT_EQ(name.size(), 32U);
	}

	TEST(DesktopNameGenerator, GeneratesEmptyNameForZeroLength)
	{
		std::mt19937 generator{ 12345 };

		const std::wstring name = Generate(generator, 0);

		EXPECT_TRUE(name.empty());
	}

	TEST(DesktopNameGenerator, UsesOnlyAllowedCharacters)
	{
		std::mt19937 generator{ 12345 };

		const std::wstring name = Generate(generator, 4096);

		const bool allCharactersAreValid =
			std::ranges::all_of(name, IsValidDesktopNameCharacter);

		EXPECT_TRUE(allCharactersAreValid);
	}

	TEST(DesktopNameGenerator, DoesNotContainNullCharacters)
	{
		std::mt19937 generator{ 12345 };

		const std::wstring name = Generate(generator);

		EXPECT_EQ(name.find(L'\0'), std::wstring::npos);
	}

	TEST(DesktopNameGenerator, IsDeterministicForFixedSeed)
	{
		std::mt19937 firstGenerator{ 12345 };
		std::mt19937 secondGenerator{ 12345 };

		const std::wstring firstName = Generate(firstGenerator);

		const std::wstring secondName = Generate(secondGenerator);

		EXPECT_EQ(firstName, secondName);
	}

	TEST(DesktopNameGenerator, ProducesDifferentNamesSequentially)
	{
		std::mt19937 generator{ 12345 };

		const std::wstring firstName = Generate(generator);

		const std::wstring secondName = Generate(generator);

		EXPECT_NE(firstName, secondName);
	}

	TEST(DesktopNameGenerator, ProducesUniqueNamesInSmallSample)
	{
		constexpr std::size_t sampleSize = 1000;

		std::mt19937 generator{ 12345 };
		std::unordered_set<std::wstring> names;
		names.reserve(sampleSize);

		for (std::size_t index = 0; index < sampleSize; ++index) {
			names.insert(Generate(generator));
		}

		EXPECT_EQ(names.size(), sampleSize);
	}

	TEST(DesktopNameGenerator, RejectsDisallowedCharacters)
	{
		EXPECT_FALSE(DesktopRanger::DesktopName::IsValidDesktopNameCharacter(L'/'));
		EXPECT_FALSE(DesktopRanger::DesktopName::IsValidDesktopNameCharacter(L'\\'));
		EXPECT_FALSE(DesktopRanger::DesktopName::IsValidDesktopNameCharacter(L'\0'));
	}

	TEST(DesktopNameGenerator, AcceptsAllowedCharacters)
	{
		EXPECT_TRUE(DesktopRanger::DesktopName::IsValidDesktopNameCharacter(L'a'));
		EXPECT_TRUE(DesktopRanger::DesktopName::IsValidDesktopNameCharacter(L'7'));
		EXPECT_TRUE(DesktopRanger::DesktopName::IsValidDesktopNameCharacter(L'_'));
	}

} // namespace DesktopRanger::DesktopName::Tests