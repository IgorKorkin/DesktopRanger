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
#include "desktop_lifecycle.h"
#include "desktop_name_generator.h"
#include "security_descriptor.h"
#include "status_logger.h"
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
		const auto current = DesktopRanger::DesktopLifecycle::GetCurrentDesktop();
		return current ? current.value() : nullptr;
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

	void ClearDesktopInformation() noexcept
	{
		secureDesktopInformartion.DesktopHandle = nullptr;
		secureDesktopInformartion.DesktopName.clear();
	}

	bool CreateNewDesktop(IN std::wstring DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle)
	{
		DefaultDesktopHandle = nullptr;
		SecretDesktopHandle = nullptr;
		namespace Lifecycle = DesktopRanger::DesktopLifecycle;
		const auto original = Lifecycle::GetCurrentDesktop();
		if (!original) {
			LOG_ERR(std::format(L"GetCurrentDesktop failed: {}", original.error()));
			return false;
		}
		auto desktop = Lifecycle::CreateSecureDesktop(DesktopName);
		if (!desktop) {
			LOG_ERR(std::format(L"CreateSecureDesktop failed: {}", desktop.error()));
			return false;
		}
		secureDesktopInformartion.DesktopName = std::move(DesktopName);
		secureDesktopInformartion.DesktopHandle = desktop->get();
		DefaultDesktopHandle = original.value();  // Borrowed, never closed here.
		SecretDesktopHandle = desktop->release(); // Existing caller-ownership contract.
		return true;
	}

	bool SwitchToDesktop(IN HDESK TargetDesktopHandle)
	{
		const auto result =
			DesktopRanger::DesktopLifecycle::SwitchToDesktop(TargetDesktopHandle);
		if (!result) {
			const auto failure = result.error();
			LOG_ERR(std::format(
				L"SwitchToDesktop step {} failed: {}; rollback failed: {}; error: {}",
				static_cast<int>(failure.step), failure.error,
				failure.rollbackError.has_value(), failure.rollbackError.value_or(0)));
			return false;
		}
		return true;
	}

} // namespace AdvancedSecureDesktop
