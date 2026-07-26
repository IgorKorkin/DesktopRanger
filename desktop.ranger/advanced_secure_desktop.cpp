#include <bit>
#include <memory>
#include <print>
#include <random>
#include <vector>

#include <Aclapi.h>
#include <Sddl.h>
#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

#include "advanced_secure_desktop.h"
#include "desktop_name_generator.h"
#include "status_logger.h"
#include "utils.h"
#include "winstation_patcher.h"

namespace AdvancedSecureDesktop
{

	struct SecureDesktopInformartion {
		HDESK DesktopHandle{};
		std::wstring DesktopName{};
	};
	SecureDesktopInformartion secureDesktopInformartion{};

	HDESK GetDesktopHandle()
	{
		if (secureDesktopInformartion.DesktopHandle) {
			return secureDesktopInformartion.DesktopHandle;
		}
		return ::GetThreadDesktop(::GetCurrentThreadId());
	}

	std::wstring GetDesktopName()
	{
		if (!secureDesktopInformartion.DesktopName.empty()) {
			return secureDesktopInformartion.DesktopName;
		}
		return L"Default";
	}

	std::wstring GetRandomDesktopName()
	{
		return DesktopRanger::DesktopName::Generate();
	}

	bool CreateNewDesktop(IN std::wstring DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle)
	{
		bool result = false;
		SecretDesktopHandle = nullptr;

		DefaultDesktopHandle = ::GetThreadDesktop(::GetCurrentThreadId());

		if (DefaultDesktopHandle == nullptr) {
			LOG_ERR(L"GetThreadDesktop fail");
			return false;
		}

		// D:P  -> защищённая DACL, пустая (никому не даём доступ)
		SecurityDescriptor sdRestrict(L"D:P");
		if (!sdRestrict.Initialized()) {
			return false;
		}

		SECURITY_ATTRIBUTES saWithVeryStrictSD{
			.nLength = sizeof(saWithVeryStrictSD),
			.lpSecurityDescriptor = sdRestrict.GetDescriptor(),
			.bInheritHandle = FALSE,
		};

		SecretDesktopHandle = ::CreateDesktopW(DesktopName.data(), 0, 0, 0, GENERIC_ALL,
											   &saWithVeryStrictSD);

		if (SecretDesktopHandle == nullptr) {
			LOG_ERR(L"CreateDesktopA fail");
		}

		secureDesktopInformartion.DesktopHandle = SecretDesktopHandle;
		secureDesktopInformartion.DesktopName = DesktopName;

		return SecretDesktopHandle != nullptr;
	}

	bool SwitchToDesktop(IN HDESK TargetDesktopHandle)
	{
		if (!::SetThreadDesktop(TargetDesktopHandle)) {
			LOG_ERR(std::format(L"SetThreadDesktop fail {:#x} ",
								reinterpret_cast<uintptr_t>(TargetDesktopHandle)));

			return false;
		}

		const auto threadId = ::GetCurrentThreadId();
		const HDESK currentDesk = ::GetThreadDesktop(threadId);

		if (currentDesk != TargetDesktopHandle) {
			LOG_ERR(
				std::format(L"GetThreadDesktop mismatch: thread {:#x}, expected {:#x}",
							threadId, reinterpret_cast<uintptr_t>(TargetDesktopHandle)));
			return false;
		}

		if (!::SwitchDesktop(TargetDesktopHandle)) {
			LOG_ERR(std::format(L"SwitchDesktop fail {:#x} ",
								reinterpret_cast<uintptr_t>(TargetDesktopHandle)));
			return false;
		}

		return true;
	}

} // namespace AdvancedSecureDesktop