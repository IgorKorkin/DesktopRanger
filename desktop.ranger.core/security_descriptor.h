#pragma once

#include <memory>
#include <string>

#include <Windows.h>

class SecurityDescriptor {
public:
	explicit SecurityDescriptor(std::wstring sddl);

	[[nodiscard]] bool Initialized() const noexcept;

	[[nodiscard]] bool SetDescriptor(HANDLE hObject) const noexcept;

	[[nodiscard]]
	PSECURITY_DESCRIPTOR GetDescriptor() const noexcept;

private:
	struct LocalFreeDeleter {
		void operator()(void *p) const noexcept
		{
			if (p) {
				::LocalFree(p);
			}
		}
	};

	using LocalMemPtr = std::unique_ptr<void, LocalFreeDeleter>;

	LocalMemPtr psd_;
};