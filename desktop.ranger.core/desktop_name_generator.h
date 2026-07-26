#pragma once

#include <cstddef>
#include <random>
#include <string>

namespace DR
{

	inline constexpr std::size_t kDesktopNameLength = 255;

	std::wstring GenerateDesktopName();

	std::wstring GenerateDesktopName(std::mt19937 &generator,
									 std::size_t length = kDesktopNameLength);

	bool IsValidDesktopNameCharacter(wchar_t character) noexcept;

} // namespace DR