#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <sddl.h>
#pragma comment(lib, "Advapi32.lib")

#include "security_descriptor.h"

SecurityDescriptor::SecurityDescriptor(std::wstring sddl)
{
	PSECURITY_DESCRIPTOR raw{};

	if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
			sddl.c_str(), SDDL_REVISION_1, &raw, nullptr)) {
		return;
	}

	psd_.reset(raw);
}

bool SecurityDescriptor::Initialized() const noexcept
{
	return static_cast<bool>(psd_);
}

bool SecurityDescriptor::SetDescriptor(HANDLE hObject) const noexcept
{
	if (!psd_) {
		return false;
	}

	if (!::SetKernelObjectSecurity(hObject, DACL_SECURITY_INFORMATION, psd_.get())) {
		return false;
	}

	return true;
}

PSECURITY_DESCRIPTOR SecurityDescriptor::GetDescriptor() const noexcept
{
	return psd_.get();
}