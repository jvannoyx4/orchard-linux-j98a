// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

static void apple_h9p_test_legacy_msi(struct kunit *test)
{
	struct device_node empty_node = {};
	struct apple_h9p_pcie *pcie;
	DECLARE_BITMAP(assigned, H9P_NUM_MSI) = {};
	unsigned int port, slot;

	pcie = kunit_kzalloc(test, sizeof(*pcie), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pcie);
	pcie->hw = &apple_h9p_hw_s8003;
	pcie->enabled_ports = GENMASK(3, 0);

	for (port = 0; port < 4; port++) {
		KUNIT_ASSERT_EQ(test, apple_h9p_parse_msi_range(pcie, &empty_node,
				port, assigned), 0);
		KUNIT_EXPECT_EQ(test, apple_h9p_port_msictl(pcie, port), 0x31U);
		KUNIT_EXPECT_EQ(test, apple_h9p_port_msivecbase(pcie, port),
				8 * port * 0x10001);
		for (slot = 0; slot < 8; slot++)
			KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, port),
					8 * port + slot);
		KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, port), 32U);
	}
	KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, 4), 32U);
}

static void apple_h9p_test_sparse_msi(struct kunit *test)
{
	/* Populated ports and vector slices in the J98a firmware. */
	static const struct apple_h9p_hw six_ports = { .num_ports = 6 };
	static const unsigned int ports[] = { 0, 2, 4 };
	static const struct apple_h9p_msi_range ranges[] = {
		{ 0, 8 }, { 16, 4 }, { 24, 4 },
	};
	struct apple_h9p_pcie *pcie;
	DECLARE_BITMAP(assigned, H9P_NUM_MSI) = {};
	unsigned int i, slot;

	pcie = kunit_kzalloc(test, sizeof(*pcie), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pcie);
	pcie->hw = &six_ports;
	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		const struct apple_h9p_msi_range *r = &ranges[i];
		unsigned int port = ports[i];

		pcie->enabled_ports |= BIT(port);
		KUNIT_ASSERT_EQ(test, apple_h9p_msi_set_range(&pcie->msi_range[port],
				assigned, r->first, r->count), 0);
		for (slot = 0; slot < r->count; slot++)
			KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, port),
					r->first + slot);
		KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, port), 32U);
		__clear_bit(r->first, pcie->used_msi);
		KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, port), (u32)r->first);
	}
	KUNIT_EXPECT_EQ(test, apple_h9p_port_msictl(pcie, 4), 0x21U);
	KUNIT_EXPECT_EQ(test, apple_h9p_port_msivecbase(pcie, 4), 0x180018U);
	KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, 1), 32U);
	KUNIT_EXPECT_EQ(test, apple_h9p_msi_claim(pcie, 6), 32U);

	KUNIT_EXPECT_EQ(test, apple_h9p_msi_set_range(&pcie->msi_range[1],
			assigned, 16, 4), -EINVAL);
	KUNIT_EXPECT_EQ(test, apple_h9p_msi_set_range(&pcie->msi_range[1],
			assigned, 29, 4), -EINVAL);
	KUNIT_EXPECT_EQ(test, apple_h9p_msi_set_range(&pcie->msi_range[1],
			assigned, 8, 3), -EINVAL);
}

static struct kunit_case apple_h9p_pcie_cases[] = {
	KUNIT_CASE(apple_h9p_test_legacy_msi),
	KUNIT_CASE(apple_h9p_test_sparse_msi),
	{}
};

static struct kunit_suite apple_h9p_pcie_suite = {
	.name = "apple-h9p-pcie-msi",
	.test_cases = apple_h9p_pcie_cases,
};
kunit_test_suite(apple_h9p_pcie_suite);
