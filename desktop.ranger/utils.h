#pragma once

class SecurityDescriptor {
public:
	explicit SecurityDescriptor(std::wstring sddl);

	bool Initialized() const noexcept;

	bool SetDescriptor(HANDLE hObject) const;

	PSECURITY_DESCRIPTOR GetDescriptor() const noexcept;

private:
	// RAII для LocalFree
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