#include <pch.h>

#include <security_descriptor.h>

namespace DesktopRanger::Tests
{
	TEST(SecurityDescriptor, CreatesValidDescriptor)
	{
		SecurityDescriptor descriptor(L"D:P");

		EXPECT_TRUE(descriptor.Initialized());
		EXPECT_NE(descriptor.GetDescriptor(), nullptr);
	}

	TEST(SecurityDescriptor, RejectsInvalidSddl)
	{
		SecurityDescriptor descriptor(L"this is definitely fake SDDL");

		EXPECT_FALSE(descriptor.Initialized());
		EXPECT_EQ(descriptor.GetDescriptor(), nullptr);
	}

} // namespace DesktopRanger::Tests