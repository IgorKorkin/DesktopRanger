#pragma once

#include <expected>

namespace WinstationPatcher
{
	[[nodiscard]] bool IsRunningAsSystemNtAuthority() noexcept;

	[[nodiscard]] std::expected<bool, DWORD> IsProcessElevated() noexcept;

	void RevokeWinsta();

	// Restores the snapshot from RevokeWinsta; never synthesizes new permissions.
	void GrantWinsta();

} // namespace WinstationPatcher
