#include "pch.h"

#include <Windows.h>

#include "internal/process_elevation_internal.h"

namespace DesktopRanger::Tests
{
	namespace Elevation = DesktopRanger::ProcessElevation;

	class ProcessElevationTest : public ::testing::Test {
	protected:
		inline static thread_local ProcessElevationTest *active{};
		HANDLE token{ reinterpret_cast<HANDLE>(1) };
		DWORD openError{}, queryError{};
		bool failOpen{}, failQuery{};
		DWORD tokenFlags{};
		DWORD returnedSize{ sizeof(TOKEN_ELEVATION) };
		int opens{}, queries{}, closes{};

		void SetUp() override
		{
			active = this;
		}
		void TearDown() override
		{
			active = nullptr;
		}

		static BOOL WINAPI Open(HANDLE process, DWORD access, PHANDLE token)
		{
			EXPECT_EQ(process, ::GetCurrentProcess());
			EXPECT_EQ(access, TOKEN_QUERY);
			++active->opens;
			if (active->failOpen) {
				::SetLastError(active->openError);
				return FALSE;
			}
			*token = active->token;
			return TRUE;
		}
		static BOOL WINAPI Query(HANDLE token, TOKEN_INFORMATION_CLASS informationClass,
								 LPVOID information, DWORD length, PDWORD returnedLength)
		{
			EXPECT_EQ(token, active->token);
			EXPECT_EQ(informationClass, TokenElevation);
			EXPECT_EQ(length, sizeof(TOKEN_ELEVATION));
			++active->queries;
			if (active->failQuery) {
				::SetLastError(active->queryError);
				return FALSE;
			}
			auto *elevation = static_cast<TOKEN_ELEVATION *>(information);
			elevation->TokenIsElevated = active->tokenFlags;
			*returnedLength = active->returnedSize;
			return TRUE;
		}
		static BOOL WINAPI Close(HANDLE token)
		{
			EXPECT_EQ(token, active->token);
			++active->closes;
			return TRUE;
		}
		static Elevation::detail::ElevationApi Api()
		{
			return { &Open, &Query, &Close };
		}
	};

	TEST_F(ProcessElevationTest, ReportsElevatedAndClosesToken)
	{
		tokenFlags = 1;
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_TRUE(result);
		EXPECT_TRUE(*result);
		EXPECT_EQ(opens, 1);
		EXPECT_EQ(queries, 1);
		EXPECT_EQ(closes, 1);
	}
	TEST_F(ProcessElevationTest, ReportsNotElevatedAndClosesToken)
	{
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_TRUE(result);
		EXPECT_FALSE(*result);
		EXPECT_EQ(closes, 1);
	}
	TEST_F(ProcessElevationTest, OpenFailurePreservesErrorWithoutClosingUnownedToken)
	{
		failOpen = true;
		openError = ERROR_ACCESS_DENIED;
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), openError);
		EXPECT_EQ(queries, 0);
		EXPECT_EQ(closes, 0);
	}
	TEST_F(ProcessElevationTest, OpenFailureWithZeroLastErrorRemainsFailure)
	{
		failOpen = true;
		openError = ERROR_SUCCESS;
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), 0);
		EXPECT_EQ(closes, 0);
	}
	TEST_F(ProcessElevationTest, QueryFailureClosesTokenAndPreservesError)
	{
		failQuery = true;
		queryError = ERROR_INVALID_HANDLE;
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), queryError);
		EXPECT_EQ(closes, 1);
	}
	TEST_F(ProcessElevationTest, QueryFailureWithZeroLastErrorRemainsFailure)
	{
		failQuery = true;
		queryError = ERROR_SUCCESS;
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), 0);
		EXPECT_EQ(closes, 1);
	}
	TEST_F(ProcessElevationTest, RejectsShortElevationResultAndClosesToken)
	{
		returnedSize = sizeof(TOKEN_ELEVATION) - 1;
		const auto result = Elevation::detail::IsProcessElevated(Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), ERROR_INVALID_DATA);
		EXPECT_EQ(closes, 1);
	}
} // namespace DesktopRanger::Tests
