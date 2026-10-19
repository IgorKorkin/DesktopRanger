#include "pch.h"

#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <Windows.h>
#include <Sddl.h>

#include "security_descriptor.h"
#include "internal/window_station_policy_internal.h"

namespace DesktopRanger::Tests
{
	namespace Policy = WindowStationPolicy;
	namespace Internal = Policy::detail;

	// Keep the original public function types, including noexcept, intact.
	static_assert(std::is_same_v<decltype(&Policy::SnapshotDacl),
		std::expected<Policy::UniqueSecurityDescriptor, DWORD> (*)(HWINSTA) noexcept>);
	static_assert(std::is_same_v<decltype(&Policy::RestrictDacl),
		std::expected<Policy::UniqueSecurityDescriptor, DWORD> (*)(HWINSTA) noexcept>);
	static_assert(std::is_same_v<decltype(&Policy::RestoreDacl),
		std::expected<void, DWORD> (*)(HWINSTA, PSECURITY_DESCRIPTOR) noexcept>);
	static_assert(std::is_same_v<decltype(&Policy::ApplyDacl),
		std::expected<void, DWORD> (*)(HWINSTA, const ACL *) noexcept>);

	// No real Window Station or Desktop is opened by these tests. Both OS calls
	// are replaced, even for negative-path and RAII tests.
	class WindowStationPolicy : public ::testing::Test {
	protected:
		inline static thread_local WindowStationPolicy *active{};
		Policy::UniqueSecurityDescriptor current;
		DWORD readError{}, writeError{}, restoreError{}, reportedError{};
		int reads{}, writes{}, reports{};
		int failingWrite{};
		SECURITY_INFORMATION lastInformation{};

		static HWINSTA Station() { return reinterpret_cast<HWINSTA>(1); }

		void SetUp() override
		{
			active = this;
			Reset(L"D:P(D;;0x1;;;WD)(A;;GA;;;SY)(A;;0x2;;;BA)");
		}
		void TearDown() override { active = nullptr; }

		void Reset(const wchar_t *sddl)
		{
			PSECURITY_DESCRIPTOR descriptor{};
			ASSERT_TRUE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
				sddl, SDDL_REVISION_1, &descriptor, nullptr));
			current.reset(descriptor);
		}

		static DWORD WINAPI Read(HANDLE station, SE_OBJECT_TYPE type,
			SECURITY_INFORMATION information, PSID *, PSID *, PACL *, PACL *,
			PSECURITY_DESCRIPTOR *descriptor)
		{
			EXPECT_EQ(station, Station());
			EXPECT_EQ(type, SE_WINDOW_OBJECT);
			EXPECT_EQ(information, DACL_SECURITY_INFORMATION);
			++active->reads;
			if (active->readError) { return active->readError; }
			const auto length = ::GetSecurityDescriptorLength(active->current.get());
			*descriptor = ::LocalAlloc(LPTR, length);
			if (!*descriptor) { return ERROR_NOT_ENOUGH_MEMORY; }
			std::memcpy(*descriptor, active->current.get(), length);
			return ERROR_SUCCESS;
		}

		static DWORD WINAPI Write(HANDLE station, SE_OBJECT_TYPE type,
			SECURITY_INFORMATION information, PSID, PSID, PACL dacl, PACL)
		{
			EXPECT_EQ(station, Station());
			EXPECT_EQ(type, SE_WINDOW_OBJECT);
			++active->writes;
			active->lastInformation = information;
			if (active->writeError && (!active->failingWrite || active->writes == active->failingWrite)) {
				return active->writeError;
			}
			if (active->restoreError && active->writes > 1) { return active->restoreError; }

			SECURITY_DESCRIPTOR descriptor{};
			if (!::InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
				!::SetSecurityDescriptorDacl(&descriptor, TRUE, dacl, FALSE)) {
				return ::GetLastError();
			}
			SECURITY_DESCRIPTOR_CONTROL control{};
			DWORD revision{};
			if (!::GetSecurityDescriptorControl(active->current.get(), &control, &revision)) {
				return ::GetLastError();
			}
			if (information & PROTECTED_DACL_SECURITY_INFORMATION) { control |= SE_DACL_PROTECTED; }
			if (information & UNPROTECTED_DACL_SECURITY_INFORMATION) { control &= ~SE_DACL_PROTECTED; }
			if (!::SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, control & SE_DACL_PROTECTED)) {
				return ::GetLastError();
			}
			DWORD length{};
			::MakeSelfRelativeSD(&descriptor, nullptr, &length);
			Policy::UniqueSecurityDescriptor copy{ ::LocalAlloc(LPTR, length) };
			if (!copy) { return ERROR_NOT_ENOUGH_MEMORY; }
			if (!::MakeSelfRelativeSD(&descriptor, copy.get(), &length)) { return ::GetLastError(); }
			active->current = std::move(copy);
			return ERROR_SUCCESS;
		}

		static void Report(DWORD error) noexcept
		{
			active->reportedError = error;
			++active->reports;
		}

		static Internal::DaclApi Api() { return { &Read, &Write }; }
		ACL *Dacl() { return Policy::GetDacl(current.get()).value(); }

		std::vector<BYTE> DaclBytes()
		{
			auto dacl = Dacl();
			if (!dacl) { return {}; }
			const auto begin = reinterpret_cast<const BYTE *>(dacl);
			return { begin, begin + dacl->AclSize };
		}

		static ACCESS_MASK Mask(const ACL *dacl, DWORD index)
		{
			auto ace = Policy::GetAceAt(dacl, index).value();
			return reinterpret_cast<const ACCESS_ALLOWED_ACE *>(ace)->Mask;
		}
	};

	TEST_F(WindowStationPolicy, RemovesEnumerationAndPreservesOtherExplicitRights)
	{
		Reset(L"D:(A;;0x00020003;;;WD)(A;;0x00000002;;;SY)");
		auto restricted = Policy::BuildRestrictedDacl(Dacl());
		ASSERT_TRUE(restricted);
		EXPECT_EQ(Mask(restricted->get(), 0), READ_CONTROL | WINSTA_READATTRIBUTES);
		EXPECT_EQ(Mask(restricted->get(), 1), WINSTA_READATTRIBUTES);
	}

	TEST_F(WindowStationPolicy, ExpandsGenericRightsBeforeRemovingEnumeration)
	{
		Reset(L"D:(A;;GA;;;WD)(A;;GR;;;SY)(A;;GW;;;BA)(A;;GX;;;BU)");
		auto restricted = Policy::BuildRestrictedDacl(Dacl());
		ASSERT_TRUE(restricted);
		const ACCESS_MASK read = READ_CONTROL | WINSTA_ENUMERATE | WINSTA_READATTRIBUTES | WINSTA_READSCREEN;
		const ACCESS_MASK write = READ_CONTROL | WINSTA_ACCESSCLIPBOARD | WINSTA_CREATEDESKTOP | WINSTA_WRITEATTRIBUTES;
		const ACCESS_MASK execute = READ_CONTROL | WINSTA_ACCESSGLOBALATOMS | WINSTA_EXITWINDOWS;
		EXPECT_EQ(Mask(restricted->get(), 0), (STANDARD_RIGHTS_REQUIRED | WINSTA_ALL_ACCESS) & ~WINSTA_ENUMDESKTOPS);
		EXPECT_EQ(Mask(restricted->get(), 1), read);
		EXPECT_EQ(Mask(restricted->get(), 2), write);
		EXPECT_EQ(Mask(restricted->get(), 3), execute);
	}

	TEST_F(WindowStationPolicy, LeavesSourceDaclUnchanged)
	{
		const auto original = DaclBytes();
		ASSERT_TRUE(Policy::BuildRestrictedDacl(Dacl()));
		EXPECT_EQ(DaclBytes(), original);
	}

	TEST_F(WindowStationPolicy, PreservesDeniedAceSidFlagsAndOrder)
	{
		Reset(L"D:(D;;GA;;;WD)(A;CI;GA;;;SY)(A;;0x2;;;BA)");
		auto restricted = Policy::BuildRestrictedDacl(Dacl());
		ASSERT_TRUE(restricted);
		EXPECT_EQ(restricted->get()->AceCount, Dacl()->AceCount);
		for (DWORD i = 0; i < Dacl()->AceCount; ++i) {
			const auto before = Policy::GetAceAt(Dacl(), i).value();
			const auto after = Policy::GetAceAt(restricted->get(), i).value();
			EXPECT_EQ(std::memcmp(before, after, sizeof(ACE_HEADER)), 0);
			const auto offset = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
			EXPECT_EQ(std::memcmp(reinterpret_cast<const BYTE *>(before) + offset,
				reinterpret_cast<const BYTE *>(after) + offset, before->AceSize - offset), 0);
		}
		const auto denied = Policy::GetAceAt(Dacl(), 0).value();
		EXPECT_EQ(std::memcmp(denied, Policy::GetAceAt(restricted->get(), 0).value(), denied->AceSize), 0);
	}

	TEST_F(WindowStationPolicy, RestrictsObjectAndCallbackAllowedAces)
	{
		// Construct ACEs in memory; no conditional-expression parser is required.
		for (const BYTE type : { ACCESS_ALLOWED_OBJECT_ACE_TYPE, ACCESS_ALLOWED_CALLBACK_ACE_TYPE,
			ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE }) {
			auto source = Policy::CreateAcl(256, ACL_REVISION_DS);
			ASSERT_TRUE(source);
			SecurityDescriptor sidSource(L"D:(A;;GA;;;WD)");
			auto sidAce = reinterpret_cast<ACCESS_ALLOWED_ACE *>(
				const_cast<ACE_HEADER *>(Policy::GetAceAt(Policy::GetDacl(sidSource.GetDescriptor()).value(), 0).value()));
			const bool object = type != ACCESS_ALLOWED_CALLBACK_ACE_TYPE;
			const DWORD sidOffset = object ? FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, ObjectType)
				: FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
			std::vector<BYTE> bytes(sidOffset + ::GetLengthSid(&sidAce->SidStart));
			auto header = reinterpret_cast<ACE_HEADER *>(bytes.data());
			header->AceType = type;
			header->AceSize = static_cast<WORD>(bytes.size());
			*reinterpret_cast<ACCESS_MASK *>(bytes.data() + sizeof(ACE_HEADER)) = GENERIC_ALL;
			ASSERT_TRUE(::CopySid(static_cast<DWORD>(bytes.size()) - sidOffset,
				bytes.data() + sidOffset, &sidAce->SidStart));
			ASSERT_TRUE(Policy::AppendAce(source->get(), header));
			auto restricted = Policy::BuildRestrictedDacl(source->get());
			ASSERT_TRUE(restricted) << static_cast<int>(type);
			EXPECT_EQ(Mask(restricted->get(), 0), (STANDARD_RIGHTS_REQUIRED | WINSTA_ALL_ACCESS) & ~WINSTA_ENUMDESKTOPS);
			const auto after = Policy::GetAceAt(restricted->get(), 0).value();
			EXPECT_EQ(std::memcmp(bytes.data() + sidOffset,
				reinterpret_cast<const BYTE *>(after) + sidOffset, bytes.size() - sidOffset), 0);
		}
	}

	TEST_F(WindowStationPolicy, AcceptsEmptyDacl)
	{
		Reset(L"D:P");
		auto restricted = Policy::BuildRestrictedDacl(Dacl());
		ASSERT_TRUE(restricted);
		EXPECT_EQ(restricted->get()->AceCount, 0);
	}

	TEST_F(WindowStationPolicy, RejectsNullAclAndInvalidHandlesWithoutWriting)
	{
		EXPECT_FALSE(Policy::BuildRestrictedDacl(nullptr));
		EXPECT_FALSE(Policy::GetDacl(nullptr));
		EXPECT_FALSE(Internal::RestrictDacl(nullptr, Api()));
		EXPECT_FALSE(Internal::RestoreDacl(nullptr, current.get(), Api()));
		EXPECT_FALSE(Internal::ApplyDacl(Station(), nullptr, Api()));
		EXPECT_EQ(reads, 0);
		EXPECT_EQ(writes, 0);
	}

	TEST_F(WindowStationPolicy, RejectsNullDaclWithoutChangingItToDenyAll)
	{
		Reset(L"D:NO_ACCESS_CONTROL");
		auto result = Internal::RestrictDacl(Station(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), ERROR_NOT_SUPPORTED);
		EXPECT_EQ(writes, 0);
		EXPECT_EQ(Dacl(), nullptr);
	}

	TEST_F(WindowStationPolicy, SnapshotAndRestoreRecoverExactAclWithoutAddingRights)
	{
		const auto original = DaclBytes();
		auto snapshot = Internal::RestrictDacl(Station(), Api());
		ASSERT_TRUE(snapshot);
		EXPECT_EQ(Mask(Dacl(), 2), WINSTA_READATTRIBUTES);
		EXPECT_NE(DaclBytes(), original);
		ASSERT_TRUE(Internal::RestoreDacl(Station(), snapshot->get(), Api()));
		EXPECT_EQ(DaclBytes(), original);
		EXPECT_EQ(Mask(Dacl(), 2), WINSTA_READATTRIBUTES);
		EXPECT_TRUE(lastInformation & PROTECTED_DACL_SECURITY_INFORMATION);
	}

	TEST_F(WindowStationPolicy, RestorePreservesUnprotectedState)
	{
		Reset(L"D:(A;;GA;;;WD)");
		auto snapshot = Internal::RestrictDacl(Station(), Api());
		ASSERT_TRUE(snapshot);
		ASSERT_TRUE(Internal::RestoreDacl(Station(), snapshot->get(), Api()));
		EXPECT_TRUE(lastInformation & UNPROTECTED_DACL_SECURITY_INFORMATION);
	}

	TEST_F(WindowStationPolicy, SnapshotFailureDoesNotWrite)
	{
		readError = ERROR_ACCESS_DENIED;
		auto result = Internal::RestrictDacl(Station(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), readError);
		EXPECT_EQ(writes, 0);
	}

	TEST_F(WindowStationPolicy, ApplyFailureIsPropagated)
	{
		const auto original = DaclBytes();
		writeError = ERROR_ACCESS_DENIED;
		auto result = Internal::RestrictDacl(Station(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), writeError);
		EXPECT_EQ(DaclBytes(), original);
	}

	TEST_F(WindowStationPolicy, ExplicitRestoreRunsOnce)
	{
		const auto original = DaclBytes();
		{
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restrict());
			ASSERT_TRUE(session.Restore());
			ASSERT_TRUE(session.Restore());
		}
		EXPECT_EQ(writes, 2);
		EXPECT_EQ(DaclBytes(), original);
	}

	TEST_F(WindowStationPolicy, ScopeExitRestoresOnEarlyReturn)
	{
		const auto original = DaclBytes();
		auto operation = [&] {
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restrict());
			return; // e.g. allowing the Desktop or starting the application failed.
		};
		operation();
		EXPECT_EQ(writes, 2);
		EXPECT_EQ(DaclBytes(), original);
	}

	TEST_F(WindowStationPolicy, ExceptionUnwindingRestores)
	{
		const auto original = DaclBytes();
		EXPECT_THROW({
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restrict());
			throw std::runtime_error("launch failed");
		}, std::runtime_error);
		EXPECT_EQ(DaclBytes(), original);
		EXPECT_EQ(writes, 2);
	}

	TEST_F(WindowStationPolicy, DoubleRestrictionDoesNotOverwriteSnapshot)
	{
		const auto original = DaclBytes();
		{
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restrict());
			auto second = session.Restrict();
			ASSERT_FALSE(second);
			EXPECT_EQ(second.error(), ERROR_ALREADY_EXISTS);
			EXPECT_EQ(reads, 1);
		}
		EXPECT_EQ(DaclBytes(), original);
	}

	TEST_F(WindowStationPolicy, FailedExplicitRestoreRetainsSnapshotForDestructorRetry)
	{
		const auto original = DaclBytes();
		{
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restrict());
			writeError = ERROR_ACCESS_DENIED;
			failingWrite = 2;
			auto result = session.Restore();
			ASSERT_FALSE(result);
			EXPECT_EQ(result.error(), ERROR_ACCESS_DENIED);
		}
		EXPECT_EQ(writes, 3);
		EXPECT_EQ(reports, 0);
		EXPECT_EQ(DaclBytes(), original);
	}

	TEST_F(WindowStationPolicy, PermanentRestoreFailureIsReported)
	{
		{
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restrict());
			restoreError = ERROR_ACCESS_DENIED;
			EXPECT_FALSE(session.Restore());
		}
		EXPECT_EQ(writes, 3);
		EXPECT_EQ(reports, 1);
		EXPECT_EQ(reportedError, ERROR_ACCESS_DENIED);
	}

	TEST_F(WindowStationPolicy, FailedRestrictionDoesNotArmCleanup)
	{
		{
			Policy::RestrictedSession session(Station(), &Report, Api());
			writeError = ERROR_ACCESS_DENIED;
			EXPECT_FALSE(session.Restrict());
		}
		EXPECT_EQ(writes, 1);
		EXPECT_EQ(reports, 0);
	}

	TEST_F(WindowStationPolicy, RejectsCompoundAllowedAceWithoutApplying)
	{
		// Windows AddAce itself rejects this obsolete type. Seed the unsupported
		// header directly to verify the whole operation fails before any OS write.
		auto ace = const_cast<ACE_HEADER *>(Policy::GetAceAt(Dacl(), 1).value());
		ace->AceType = ACCESS_ALLOWED_COMPOUND_ACE_TYPE;
		auto result = Internal::RestrictDacl(Station(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(writes, 0);
	}

	TEST_F(WindowStationPolicy, StandaloneRestoreRetainsNullDaclSemantics)
	{
		Reset(L"D:NO_ACCESS_CONTROL");
		auto snapshot = Internal::SnapshotDacl(Station(), Api());
		ASSERT_TRUE(snapshot);
		Reset(L"D:P");
		ASSERT_TRUE(Internal::RestoreDacl(Station(), snapshot->get(), Api()));
		EXPECT_EQ(Dacl(), nullptr);
	}

	TEST_F(WindowStationPolicy, SessionWithoutRestrictionDoesNotWrite)
	{
		{
			Policy::RestrictedSession session(Station(), &Report, Api());
			ASSERT_TRUE(session.Restore());
		}
		EXPECT_EQ(reads, 0);
		EXPECT_EQ(writes, 0);
		EXPECT_EQ(reports, 0);
	}
}
