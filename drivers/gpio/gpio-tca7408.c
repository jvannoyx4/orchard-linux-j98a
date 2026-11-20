// SPDX-License-Identifier: GPL-2.0-only
/* Texas Instruments TCA7408 eight-line I2C GPIO expander */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#define TCA7408_DEVICE_ID	0x01
#define TCA7408_VENDOR_MASK	GENMASK(7, 5)
#define TCA7408_VENDOR_TI	BIT(6)
#define TCA7408_DIRECTION	0x03
#define TCA7408_OUTPUT		0x05
#define TCA7408_HIGH_Z		0x07
#define TCA7408_INPUT		0x0f

struct tca7408 {
	struct gpio_chip chip;
	struct regmap *map;
	/* Serialize direction changes with concurrent reads and writes. */
	struct mutex lock;
};

static int tca7408_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct tca7408 *gpio = gpiochip_get_data(chip);
	unsigned int dir;
	int ret;

	guard(mutex)(&gpio->lock);
	ret = regmap_read(gpio->map, TCA7408_DIRECTION, &dir);
	if (ret)
		return ret;

	return dir & BIT(offset) ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int tca7408_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct tca7408 *gpio = gpiochip_get_data(chip);

	guard(mutex)(&gpio->lock);
	return regmap_update_bits(gpio->map, TCA7408_DIRECTION, BIT(offset), 0);
}

static int tca7408_direction_output(struct gpio_chip *chip, unsigned int offset,
				    int value)
{
	struct tca7408 *gpio = gpiochip_get_data(chip);
	unsigned int bit = BIT(offset);
	int ret;

	guard(mutex)(&gpio->lock);
	/* Load the latch before enabling the output driver for this line. */
	ret = regmap_update_bits(gpio->map, TCA7408_OUTPUT, bit, value ? bit : 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(gpio->map, TCA7408_HIGH_Z, bit, 0);
	if (ret)
		return ret;

	return regmap_update_bits(gpio->map, TCA7408_DIRECTION, bit, bit);
}

static int tca7408_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tca7408 *gpio = gpiochip_get_data(chip);
	unsigned int dir, state, reg;
	int ret;

	guard(mutex)(&gpio->lock);
	ret = regmap_read(gpio->map, TCA7408_DIRECTION, &dir);
	if (ret)
		return ret;

	/* The input status register always reads one for an output. */
	reg = dir & BIT(offset) ? TCA7408_OUTPUT : TCA7408_INPUT;
	ret = regmap_read(gpio->map, reg, &state);
	if (ret)
		return ret;

	return !!(state & BIT(offset));
}

static int tca7408_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct tca7408 *gpio = gpiochip_get_data(chip);
	unsigned int bit = BIT(offset);

	guard(mutex)(&gpio->lock);
	return regmap_update_bits(gpio->map, TCA7408_OUTPUT, bit, value ? bit : 0);
}

static const struct regmap_config tca7408_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x13,
};

static int tca7408_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tca7408 *gpio;
	unsigned int id;
	int ret;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->map = devm_regmap_init_i2c(client, &tca7408_regmap_config);
	if (IS_ERR(gpio->map))
		return dev_err_probe(dev, PTR_ERR(gpio->map), "cannot create register map\n");

	ret = regmap_read(gpio->map, TCA7408_DEVICE_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read device ID\n");
	if ((id & TCA7408_VENDOR_MASK) != TCA7408_VENDOR_TI)
		return dev_err_probe(dev, -ENODEV, "unexpected device ID %#x\n", id);

	mutex_init(&gpio->lock);
	gpio->chip.label = dev_name(dev);
	gpio->chip.parent = dev;
	gpio->chip.owner = THIS_MODULE;
	gpio->chip.base = -1;
	gpio->chip.ngpio = 8;
	gpio->chip.can_sleep = true;
	gpio->chip.get_direction = tca7408_get_direction;
	gpio->chip.direction_input = tca7408_direction_input;
	gpio->chip.direction_output = tca7408_direction_output;
	gpio->chip.get = tca7408_get;
	gpio->chip.set = tca7408_set;

	return devm_gpiochip_add_data(dev, &gpio->chip, gpio);
}

static const struct of_device_id tca7408_of_match[] = {
	{ .compatible = "ti,tca7408" },
	{ }
};
MODULE_DEVICE_TABLE(of, tca7408_of_match);

static const struct i2c_device_id tca7408_ids[] = {
	{ "tca7408" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tca7408_ids);

static struct i2c_driver tca7408_driver = {
	.driver = {
		.name = "tca7408",
		.of_match_table = tca7408_of_match,
	},
	.probe = tca7408_probe,
	.id_table = tca7408_ids,
};
module_i2c_driver(tca7408_driver);

MODULE_DESCRIPTION("Texas Instruments TCA7408 GPIO expander");
MODULE_LICENSE("GPL");
