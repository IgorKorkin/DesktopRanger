#pragma once

#include <cstddef>
#include <random>
#include <string>

namespace DesktopRanger::DesktopName
{
	inline constexpr auto kDesktopNameLength = 255;

	std::wstring Generate();

	std::wstring Generate(std::mt19937 &generator,
						  std::size_t length = kDesktopNameLength);

	bool IsValidDesktopNameCharacter(wchar_t character) noexcept;

} // namespace DesktopRanger::DesktopName