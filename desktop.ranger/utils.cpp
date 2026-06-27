#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "sddl.h"
#pragma comment(lib, "Advapi32.lib")

#include "status_logger.h"

#include "utils.h"

SecurityDescriptor::SecurityDescriptor(std::wstring sddl)
{
	PSECURITY_DESCRIPTOR raw{};
	if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
			sddl.data(), SDDL_REVISION_1, &raw, nullptr)) {
		LOG_ERR(L"ConvertStringSecurityDescriptorToSecurityDescriptorA "
				"failed");
		return;
	}
	psd_.reset(raw); // RAII
}

bool SecurityDescriptor::Initialized() const noexcept
{
	return static_cast<bool>(psd_);
}

bool SecurityDescriptor::SetDescriptor(HANDLE hObject) const
{
	if (!psd_) {
		LOG_ERR(L"Descriptor is null");
		return false;
	}

	if (!::SetKernelObjectSecurity(hObject, DACL_SECURITY_INFORMATION, psd_.get())) {
		LOG_ERR(L"SetKernelObjectSecurity failed");
		return false;
	}

	return true;
}

PSECURITY_DESCRIPTOR SecurityDescriptor::GetDescriptor() const noexcept
{
	return psd_.get();
}