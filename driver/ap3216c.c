#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define AP3216C_REG_SYSTEM_MODE      0x00
#define AP3216C_REG_IR_LOW           0x0A
#define AP3216C_REG_IR_HIGH          0x0B
#define AP3216C_REG_ALS_LOW          0x0C
#define AP3216C_REG_ALS_HIGH         0x0D
#define AP3216C_REG_PS_LOW           0x0E
#define AP3216C_REG_PS_HIGH          0x0F

#define AP3216C_SYSTEM_MODE_POWER_DOWN          0
#define AP3216C_SYSTEM_MODE_ALS_ACTIVE          1
#define AP3216C_SYSTEM_MODE_PS_IR_ACTIVE        2
#define AP3216C_SYSTEM_MODE_ALS_PS_IR_ACTIVE    3
#define AP3216C_SYSTEM_MODE_SW_RESET            4
#define AP3216C_SYSTEM_MODE_ALS_ONCE            5
#define AP3216C_SYSTEM_MODE_PS_IR_ONCE          6
#define AP3216C_SYSTEM_MODE_ALS_PS_IR_ONCE      7

struct ap3216c_data {
    struct device      *dev;
    struct i2c_client  *client;
    struct timer_list   update_timer;
    struct work_struct  read_work;

    unsigned char system_mode;
    bool          ir_ps_overflow;
    unsigned int  ir_data;
    unsigned int  als_data;
    bool          object_detect;
    unsigned int  ps_data;
};

/* ---- low-level I2C helper ---- */

static int ap3216c_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
    struct i2c_msg msg[2];
    u8 reg_addr = reg;
    int ret;

    msg[0].addr  = client->addr;
    msg[0].flags = 0;
    msg[0].buf   = &reg_addr;
    msg[0].len   = 1;

    msg[1].addr  = client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].buf   = val;
    msg[1].len   = 1;

    ret = i2c_transfer(client->adapter, msg, 2);
    if (ret != 2) {
        dev_err(&client->dev, "read reg 0x%02x failed: %d\n", reg, ret);
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}

/* ---- IIO channel definitions ---- */

static const struct iio_chan_spec ap3216c_iio_chan_spec[] = {
    {
        .type             = IIO_LIGHT,
        .modified         = 1,
        .channel2         = IIO_MOD_LIGHT_CLEAR,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
    {
        .type             = IIO_PROXIMITY,
        .channel          = 1,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
    {
        .type             = IIO_LIGHT,
        .modified         = 1,
        .channel2         = IIO_MOD_LIGHT_IR,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
};

/* ---- IIO read_raw callback ---- */

static int ap3216c_read_raw(struct iio_dev *indio_dev,
                            const struct iio_chan_spec *chan,
                            int *val, int *val2, long mask)
{
    struct ap3216c_data *prv_data = iio_priv(indio_dev);

    if (mask != IIO_CHAN_INFO_RAW)
        return -EINVAL;

    /*
     * Return the values cached by the background poll (read_work_func) rather
     * than issuing I2C transfers here, so a sysfs read never blocks on the bus.
     */
    switch (chan->type) {
    case IIO_LIGHT:
        if (chan->channel2 == IIO_MOD_LIGHT_CLEAR)
            *val = prv_data->als_data;
        else if (chan->channel2 == IIO_MOD_LIGHT_IR)
            *val = prv_data->ir_data;
        else
            return -EINVAL;
        break;

    case IIO_PROXIMITY:
        *val = prv_data->ps_data;
        break;

    default:
        return -EINVAL;
    }

    return IIO_VAL_INT;
}

static const struct iio_info ap3216c_iio_info = {
    .read_raw = ap3216c_read_raw,
};

/* ---- background polling (timer + workqueue) ---- */

static void read_work_func(struct work_struct *work)
{
    struct ap3216c_data *prv_data =
        container_of(work, struct ap3216c_data, read_work);
    struct i2c_client *client = prv_data->client;
    u8 val[2] = {};

    ap3216c_read_reg(client, AP3216C_REG_SYSTEM_MODE, &val[0]);
    prv_data->system_mode = val[0];

    ap3216c_read_reg(client, AP3216C_REG_IR_LOW,  &val[0]);
    ap3216c_read_reg(client, AP3216C_REG_IR_HIGH, &val[1]);
    prv_data->ir_data = (val[1] << 2) | (val[0] & 0x03);
    prv_data->ir_ps_overflow = !!(val[0] & 0x80);

    ap3216c_read_reg(client, AP3216C_REG_PS_LOW,  &val[0]);
    ap3216c_read_reg(client, AP3216C_REG_PS_HIGH, &val[1]);
    prv_data->ps_data = (val[1] << 4) | (val[0] & 0x0f);
    prv_data->object_detect = !!(val[0] & 0x80);

    ap3216c_read_reg(client, AP3216C_REG_ALS_LOW,  &val[0]);
    ap3216c_read_reg(client, AP3216C_REG_ALS_HIGH, &val[1]);
    prv_data->als_data = (val[1] << 8) | val[0];

    dev_dbg(prv_data->dev, "ir=%u ps=%u als=%u obj=%d overflow=%d\n",
            prv_data->ir_data, prv_data->ps_data, prv_data->als_data,
            prv_data->object_detect, prv_data->ir_ps_overflow);
}

static void ap3216c_update_timer_callback(struct timer_list *t)
{
    struct ap3216c_data *prv_data =
        container_of(t, struct ap3216c_data, update_timer);
    schedule_work(&prv_data->read_work);
    mod_timer(t, jiffies + msecs_to_jiffies(500));
}

/* ---- chip initialisation ---- */

static int ap3216c_chip_init(struct ap3216c_data *prv_data)
{
    unsigned char reg[3] = {};
    int ret;

    reg[0] = AP3216C_REG_SYSTEM_MODE;
    reg[1] = AP3216C_SYSTEM_MODE_SW_RESET;
    ret = i2c_master_send(prv_data->client, reg, 2);
    if (ret <= 0)
        return ret < 0 ? ret : -EIO;
    msleep(50);

    reg[1] = AP3216C_SYSTEM_MODE_ALS_PS_IR_ACTIVE;
    ret = i2c_master_send(prv_data->client, reg, 2);
    if (ret <= 0)
        return ret < 0 ? ret : -EIO;

    /* verify the mode register was written */
    ret = i2c_master_recv(prv_data->client, &reg[2], 1);
    if (ret <= 0)
        return ret < 0 ? ret : -EIO;
    if (reg[2] != AP3216C_SYSTEM_MODE_ALS_PS_IR_ACTIVE) {
        dev_err(prv_data->dev, "failed to set system mode (got 0x%02x)\n", reg[2]);
        return -EIO;
    }

    return 0;
}

/* ---- probe / remove ---- */

static int ap3216c_probe(struct i2c_client *client)
{
    struct device       *dev = &client->dev;
    struct ap3216c_data *prv_data;
    struct iio_dev      *indio;
    int ret;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(dev, "adapter does not support plain I2C\n");
        return -ENODEV;
    }

    /* allocate iio_dev; private data lives immediately after it */
    indio = devm_iio_device_alloc(dev, sizeof(*prv_data));
    if (!indio)
        return -ENOMEM;

    prv_data = iio_priv(indio);
    prv_data->dev    = dev;
    prv_data->client = client;
    i2c_set_clientdata(client, prv_data);

    ret = ap3216c_chip_init(prv_data);
    if (ret) {
        dev_err(dev, "chip init failed: %d\n", ret);
        return ret;
    }

    /* configure the IIO device */
    indio->name         = "ap3216c";
    indio->channels     = ap3216c_iio_chan_spec;
    indio->num_channels = ARRAY_SIZE(ap3216c_iio_chan_spec);
    indio->modes        = INDIO_DIRECT_MODE;
    indio->info         = &ap3216c_iio_info;

    /* background polling via timer + workqueue */
    INIT_WORK(&prv_data->read_work, read_work_func);
    timer_setup(&prv_data->update_timer, ap3216c_update_timer_callback, 0);
    mod_timer(&prv_data->update_timer, jiffies + msecs_to_jiffies(500));

    ret = devm_iio_device_register(dev, indio);
    if (ret) {
        timer_delete_sync(&prv_data->update_timer);
        cancel_work_sync(&prv_data->read_work);
        return ret;
    }

    dev_info(dev, "ap3216c probed, IIO device registered\n");
    return 0;
}

static void ap3216c_remove(struct i2c_client *client)
{
    struct ap3216c_data *prv_data = i2c_get_clientdata(client);

    timer_delete_sync(&prv_data->update_timer);
    cancel_work_sync(&prv_data->read_work);
    dev_info(prv_data->dev, "ap3216c removed\n");
}

/* ---- driver registration ---- */

static const struct of_device_id ap3216c_match_table[] = {
    { .compatible = "dunnan,ap3216c" },
    { }
};
MODULE_DEVICE_TABLE(of, ap3216c_match_table);

static struct i2c_driver ap3216c_driver = {
    .driver = {
        .name           = "ap3216c",
        .of_match_table = ap3216c_match_table,
    },
    .probe  = ap3216c_probe,
    .remove = ap3216c_remove,
};
module_i2c_driver(ap3216c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("budali11");
MODULE_DESCRIPTION("AP3216C ALS/PS/IR sensor IIO driver for i.MX6ULL");
