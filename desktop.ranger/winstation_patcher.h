#pragma once

#include <expected>

namespace WinstationPatcher
{
	[[nodiscard]] bool IsRunningAsSystemNtAuthority() noexcept;

	[[nodiscard]] std::expected<bool, DWORD> IsProcessElevated() noexcept;

	void RevokeWinsta();

	void GrantWinsta();

} // namespace WinstationPatcher