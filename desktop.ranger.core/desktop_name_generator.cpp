#include "desktop_name_generator.h"

#include <array>
#include <random>
#include <string_view>

namespace DR
{
	namespace
	{
		constexpr std::wstring_view kDesktopNameAlphabet = L"abcdefghijklmnopqrstuvwxyz"
														   L"0123456789"
														   L"_-.,;:@#$%&'()[]{}+=~`!^";

	} // namespace

	bool IsValidDesktopNameCharacter(const wchar_t character) noexcept
	{
		return kDesktopNameAlphabet.find(character) != std::wstring_view::npos;
	}

	std::wstring GenerateDesktopName(std::mt19937 &generator, std::size_t length)
	{
		std::uniform_int_distribution<std::size_t> Distribution(
			0, kDesktopNameAlphabet.size() - 1);

		std::wstring Result;
		Result.reserve(length);

		for (std::size_t index = 0; index < length; ++index) {
			Result.push_back(kDesktopNameAlphabet[Distribution(generator)]);
		}

		return Result;
	}

	std::wstring GenerateDesktopName()
	{
		thread_local std::mt19937 Generator{ std::random_device{}() };
		return GenerateDesktopName(Generator);
	}

} // namespace DR