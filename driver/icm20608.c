#include "linux/gfp_types.h"
#include "linux/iio/types.h"
#include "linux/mutex.h"
#include "linux/spi/spi.h"
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/input.h>
#include <linux/blk-mq.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/iio/iio.h>

#define ICM_CMD_WRITE               (0x00)
#define ICM_CMD_READ                (0x80)

#define ICM_REG_SELF_TEST_X_GYRO    (0x00)
#define ICM_REG_SELF_TEST_Y_GYRO    (0x01)
#define ICM_REG_SELF_TEST_Z_GYRO    (0x02)
#define ICM_REG_SELF_TEST_X_ACCEL   (0x0D)
#define ICM_REG_SELF_TEST_Y_ACCEL   (0x0E)
#define ICM_REG_SELF_TEST_Z_ACCEL   (0x0F)
#define ICM_REG_XG_OFFS_USRH        (0x13)
#define ICM_REG_XG_OFFS_USRL        (0x14)
#define ICM_REG_YG_OFFS_USRH        (0x15)
#define ICM_REG_YG_OFFS_USRL        (0x16)
#define ICM_REG_ZG_OFFS_USRH        (0x17)
#define ICM_REG_ZG_OFFS_USRL        (0x18)
#define ICM_REG_SMPLRT_DIV          (0x19)
#define ICM_REG_CONFIG              (0x1A)
#define ICM_REG_GYRO_CONFIG         (0x1B)
#define ICM_REG_ACCEL_CONFIG        (0x1C)
#define ICM_REG_ACCEL_CONFIG2       (0x1D)
#define ICM_REG_LP_MODE_CFG         (0x1E)
#define ICM_REG_ACCEL_WOM_THR       (0x1F)
#define ICM_REG_FIFO_EN             (0x23)
#define ICM_REG_FSYNC_INT           (0x36)
#define ICM_REG_INT_PIN_CFG         (0x37)
#define ICM_REG_INT_ENABLE          (0x38)
#define ICM_REG_INT_STATUS          (0x3A)
#define ICM_REG_ACCEL_XOUT_H        (0x3B)
#define ICM_REG_ACCEL_XOUT_L        (0x3C)
#define ICM_REG_ACCEL_YOUT_H        (0x3D)
#define ICM_REG_ACCEL_YOUT_L        (0x3E)
#define ICM_REG_ACCEL_ZOUT_H        (0x3F)
#define ICM_REG_ACCEL_ZOUT_L        (0x40)
#define ICM_REG_TEMP_OUT_H          (0x41)
#define ICM_REG_TEMP_OUT_L          (0x42)
#define ICM_REG_GYRO_XOUT_H         (0x43)
#define ICM_REG_GYRO_XOUT_L         (0x44)
#define ICM_REG_GYRO_YOUT_H         (0x45)
#define ICM_REG_GYRO_YOUT_L         (0x46)
#define ICM_REG_GYRO_ZOUT_H         (0x47)
#define ICM_REG_GYRO_ZOUT_L         (0x48)
#define ICM_REG_SIGNAL_PATH_RESET   (0x68)
#define ICM_REG_ACCEL_INTEL_CTRL    (0x69)
#define ICM_REG_USER_CTRL           (0x6A)
#define ICM_REG_PWR_MGMT_1          (0x6B)
#define ICM_REG_PWR_MGMT_2          (0x6C)
#define ICM_REG_WHO_AM_I            (0x75)
#define ICM_WHO_AM_I_VAL            (0xAE)
#define ICM_REG_XA_OFFSET_H         (0x77)
#define ICM_REG_XA_OFFSET_L         (0x78)
#define ICM_REG_YA_OFFSET_H         (0x7A)
#define ICM_REG_YA_OFFSET_L         (0x7B)
#define ICM_REG_ZA_OFFSET_H         (0x7D)
#define ICM_REG_ZA_OFFSET_L         (0x7E)

#define ICM20608_POLL_INTERVAL_MS  (500)

#define ICM20608_CALIBRATION_SAMPLES   (100)
#define ICM20608_CALIBRATION_DELAY_MS  (2)
#define ICM20608_ACCEL_1G_RAW          (16384) /* LSB/g at the +-2g full scale used during init */

struct imu_data {
    s16 accel_x;
    s16 accel_y;
    s16 accel_z;
    s16 temp;
    s16 gyro_x;
    s16 gyro_y;
    s16 gyro_z;
};

struct icm20608_drv_data {
    struct spi_device *spi;
    struct mutex lock;
    struct imu_data data;
    struct timer_list timer;
    struct work_struct work;
    /*
     * Software accel calibration bias (current_avg - desired_value at
     * probe time), subtracted from every raw accel read. The accel
     * hardware offset registers (XA/YA/ZA_OFFSET_H/L) were tried first,
     * but writing even tiny, correctly-scaled values into them corrupts
     * the sensor output on this chip, so calibration is done here instead.
     */
    s16 accel_offset_x;
    s16 accel_offset_y;
    s16 accel_offset_z;
};

static int icm20608_iio_info_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask);
static int icm20608_iio_info_write_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int val,
			int val2,
			long mask);
static struct iio_info icm20608_iio_info = {
    .read_raw = icm20608_iio_info_read_raw,
    .write_raw = icm20608_iio_info_write_raw,
};

static struct iio_chan_spec icm20608_iio_chan_spec[] = {
    [0] = {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
    [1] = {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Y,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
    [2] = {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Z,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
    [3] = {
        .type = IIO_ANGL_VEL,
        .modified = 1,
        .channel2 = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
    [4] = {
        .type = IIO_ANGL_VEL,
        .modified = 1,
        .channel2 = IIO_MOD_Y,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
    [5] = {
        .type = IIO_ANGL_VEL,
        .modified = 1,
        .channel2 = IIO_MOD_Z,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
    [6] = {
        .type = IIO_TEMP,
        .channel = 1,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    }
};

static int icm20608_probe(struct spi_device *spi);
static void icm20608_remove(struct spi_device *spi);

static const struct of_device_id icm20608_of_match_table[] = {
    { .compatible = "tdk,icm20608" } ,
    {}
};
MODULE_DEVICE_TABLE(of, icm20608_of_match_table);

static const struct spi_device_id icm20608_id[] = {
    { "icm20608", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, icm20608_id);

static struct spi_driver icm20608_driver = {
    .driver = {
        .name = "icm20608_driver",
        .of_match_table = icm20608_of_match_table,
    },
    .probe = icm20608_probe,
    .remove = icm20608_remove,
    .id_table = icm20608_id,
};
module_spi_driver(icm20608_driver);

static int icm20608_write_reg(struct spi_device *spi, u8 reg, u8 value)
{
    u8 msg[2] = {reg | ICM_CMD_WRITE, value};
    return spi_write(spi, msg, sizeof(msg));
}

static int icm20608_write_offset(struct spi_device *spi, u8 reg_h, u8 reg_l, s16 value)
{
    int ret;

    ret = icm20608_write_reg(spi, reg_h, (u8)(value >> 8));
    if (ret)
        return ret;

    return icm20608_write_reg(spi, reg_l, (u8)value);
}

/*
 * The ICM20608 expects one continuous full-duplex clock burst per
 * transaction (address byte, then len dummy bytes while data is
 * clocked back). spi_write_then_read() splits this into two separate
 * transfers, which lets some SPI controllers (e.g. i.MX6ULL ECSPI)
 * deassert CS between them and desyncs the chip, so use a single
 * spi_transfer instead.
 */
static int icm20608_read_regs(struct spi_device *spi, u8 reg, u8 *buf, size_t len)
{
    struct spi_message msg;
    struct spi_transfer xfer = {0};
    u8 tx[1 + 14] = {0};
    u8 rx[1 + 14] = {0};
    int ret;

    if (len > sizeof(tx) - 1)
        return -EINVAL;

    tx[0] = reg | ICM_CMD_READ;
    xfer.tx_buf = tx;
    xfer.rx_buf = rx;
    xfer.len = len + 1;

    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);

    ret = spi_sync(spi, &msg);
    if (ret)
        return ret;

    memcpy(buf, rx + 1, len);
    return 0;
}

static int icm20608_read_reg(struct spi_device *spi, u8 reg, u8 *value)
{
    return icm20608_read_regs(spi, reg, value, 1);
}

/* ACCEL_XOUT_H .. GYRO_ZOUT_L are 14 contiguous registers */
static int icm20608_read_data(struct spi_device *spi, struct imu_data *data)
{
    u8 buf[14];
    int ret;

    ret = icm20608_read_regs(spi, ICM_REG_ACCEL_XOUT_H, buf, sizeof(buf));
    if (ret)
        return ret;

    data->accel_x = (s16)((buf[0] << 8) | buf[1]);
    data->accel_y = (s16)((buf[2] << 8) | buf[3]);
    data->accel_z = (s16)((buf[4] << 8) | buf[5]);
    data->temp    = (s16)((buf[6] << 8) | buf[7]);
    data->gyro_x  = (s16)((buf[8] << 8) | buf[9]);
    data->gyro_y  = (s16)((buf[10] << 8) | buf[11]);
    data->gyro_z  = (s16)((buf[12] << 8) | buf[13]);

    return 0;
}

static void icm20608_apply_accel_offset(struct icm20608_drv_data *prv_data, struct imu_data *data)
{
    data->accel_x -= prv_data->accel_offset_x;
    data->accel_y -= prv_data->accel_offset_y;
    data->accel_z -= prv_data->accel_offset_z;
}

/* runs in process context: safe to block on the SPI transfer */
static void icm20608_work_func(struct work_struct *work)
{
    struct icm20608_drv_data *prv_data = container_of(work, struct icm20608_drv_data, work);
    struct imu_data data;
    int ret;

    ret = icm20608_read_data(prv_data->spi, &data);
    if (ret) {
        dev_err(&prv_data->spi->dev, "failed to read imu data. ret: %d\n", ret);
        return;
    }
    icm20608_apply_accel_offset(prv_data, &data);

    mutex_lock(&prv_data->lock);
    prv_data->data = data;
    mutex_unlock(&prv_data->lock);

    dev_dbg(&prv_data->spi->dev,
             "accel: %d %d %d, gyro: %d %d %d, temp: %d\n",
             data.accel_x, data.accel_y, data.accel_z,
             data.gyro_x, data.gyro_y, data.gyro_z, data.temp);
}

/* runs in softirq context: must not block, just hand off to the workqueue */
static void icm20608_timer_func(struct timer_list *timer)
{
    struct icm20608_drv_data *prv_data = container_of(timer, struct icm20608_drv_data, timer);

    schedule_work(&prv_data->work);
    mod_timer(&prv_data->timer, jiffies + msecs_to_jiffies(ICM20608_POLL_INTERVAL_MS));
}

/*
 * Zero-bias calibration: assumes the board is stationary and lying flat
 * (Z axis up) for the duration of this call. Averages a burst of raw
 * samples, then:
 *  - gyro: writes the negated bias into the per-axis hardware offset
 *    registers (XG/YG/ZG_OFFS_USRH/L) -- confirmed to work correctly on
 *    this chip, added directly to the gyro output at the current FS_SEL.
 *  - accel: writing even a tiny, correctly-scaled bias into the hardware
 *    XA/YA/ZA_OFFSET_H/L registers was found to corrupt the sensor output
 *    on this chip (see conversation history), so the bias is instead
 *    stored in prv_data and subtracted from every raw read in software
 *    (icm20608_apply_accel_offset()).
 * Must run after the accel full scale range is configured, since
 * ICM20608_ACCEL_1G_RAW assumes +-2g.
 */
static int icm20608_calibrate(struct icm20608_drv_data *prv_data)
{
    struct spi_device *spi = prv_data->spi;
    struct imu_data data;
    s32 accel_x_sum = 0, accel_y_sum = 0, accel_z_sum = 0;
    s32 gyro_x_sum = 0, gyro_y_sum = 0, gyro_z_sum = 0;
    int ret, i;

    for (i = 0; i < ICM20608_CALIBRATION_SAMPLES; i++) {
        ret = icm20608_read_data(spi, &data);
        if (ret)
            return ret;

        accel_x_sum += data.accel_x;
        accel_y_sum += data.accel_y;
        accel_z_sum += data.accel_z;
        gyro_x_sum  += data.gyro_x;
        gyro_y_sum  += data.gyro_y;
        gyro_z_sum  += data.gyro_z;

        msleep(ICM20608_CALIBRATION_DELAY_MS);
    }

    prv_data->accel_offset_x = (s16)(accel_x_sum / ICM20608_CALIBRATION_SAMPLES);
    prv_data->accel_offset_y = (s16)(accel_y_sum / ICM20608_CALIBRATION_SAMPLES);
    prv_data->accel_offset_z = (s16)((accel_z_sum / ICM20608_CALIBRATION_SAMPLES) - ICM20608_ACCEL_1G_RAW);

    dev_info(&spi->dev,
             "calibration averages: accel=%d %d %d gyro=%d %d %d, accel_offset=%d %d %d\n",
             accel_x_sum / ICM20608_CALIBRATION_SAMPLES,
             accel_y_sum / ICM20608_CALIBRATION_SAMPLES,
             accel_z_sum / ICM20608_CALIBRATION_SAMPLES,
             gyro_x_sum / ICM20608_CALIBRATION_SAMPLES,
             gyro_y_sum / ICM20608_CALIBRATION_SAMPLES,
             gyro_z_sum / ICM20608_CALIBRATION_SAMPLES,
             prv_data->accel_offset_x, prv_data->accel_offset_y, prv_data->accel_offset_z);

    ret = icm20608_write_offset(spi, ICM_REG_XG_OFFS_USRH, ICM_REG_XG_OFFS_USRL,
                     (s16)(-(gyro_x_sum / ICM20608_CALIBRATION_SAMPLES)));
    if (ret)
        return ret;

    ret = icm20608_write_offset(spi, ICM_REG_YG_OFFS_USRH, ICM_REG_YG_OFFS_USRL,
                     (s16)(-(gyro_y_sum / ICM20608_CALIBRATION_SAMPLES)));
    if (ret)
        return ret;

    return icm20608_write_offset(spi, ICM_REG_ZG_OFFS_USRH, ICM_REG_ZG_OFFS_USRL,
                     (s16)(-(gyro_z_sum / ICM20608_CALIBRATION_SAMPLES)));
}

static int icm20608_init(struct spi_device *spi, struct icm20608_drv_data *prv_data)
{
    int ret = 0;
    u8 who_am_i = 0;

    /* reset the imu; H_RESET takes up to 100ms per the datasheet */
    ret = icm20608_write_reg(spi, ICM_REG_PWR_MGMT_1, 0x80);
    if (ret)
        return ret;
    msleep(100);

    /* wake up and select the gyro PLL as the clock source */
    ret = icm20608_write_reg(spi, ICM_REG_PWR_MGMT_1, 0x01);
    if (ret)
        return ret;
    msleep(50);

    /* verify communication by checking the WHO_AM_I reg */
    ret = icm20608_read_reg(spi, ICM_REG_WHO_AM_I, &who_am_i);
    if (ret)
        return ret;
    if (who_am_i != ICM_WHO_AM_I_VAL) {
        dev_err(&spi->dev, "unexpected WHO_AM_I value: 0x%02x\n", who_am_i);
        return -ENODEV;
    }
    dev_info(&spi->dev, "icm20608 found, WHO_AM_I = 0x%02x\n", who_am_i);

    /* gyro full scale range: +-250dps (FS_SEL = 0) */
    ret = icm20608_write_reg(spi, ICM_REG_GYRO_CONFIG, 0x00);
    if (ret)
        return ret;

    /* accel full scale range: +-2g (AFS_SEL = 0) */
    ret = icm20608_write_reg(spi, ICM_REG_ACCEL_CONFIG, 0x00);
    if (ret)
        return ret;

    /* disable FIFO for all sensors */
    ret = icm20608_write_reg(spi, ICM_REG_FIFO_EN, 0x00);
    if (ret)
        return ret;

    /* let the new config settle, then zero-bias calibrate while idle */
    msleep(10);
    ret = icm20608_calibrate(prv_data);
    if (ret)
        dev_err(&spi->dev, "calibration failed, continuing with existing offsets. ret: %d\n", ret);

    return 0;
}

static int icm20608_probe(struct spi_device *spi)
{
    int ret = 0;
    struct icm20608_drv_data *prv_data = NULL;
    
    prv_data = devm_kzalloc(&spi->dev, sizeof(struct icm20608_drv_data), GFP_KERNEL);
    if (!prv_data) {
        dev_err(&spi->dev, "probe failed: out of memory.\n");
        return -ENOMEM;
    }
    spi_set_drvdata(spi, prv_data);

    prv_data->spi = spi;

    spi->mode = SPI_MODE_3;
    spi->bits_per_word = 8;
    if (!spi->max_speed_hz) 
        spi->max_speed_hz = 1000000;

    ret = spi_setup(spi);
    if (ret) {
        dev_err(&spi->dev, "cannot setup spi. ret: %d\n", ret);
        return ret;
    }

    mutex_init(&prv_data->lock);

    ret = icm20608_init(spi, prv_data);
    if (ret) {
        dev_err(&spi->dev, "cannot init icm20608. ret: %d\n", ret);
        return ret;
    }

    /* initialize iio dev; prv_data is already devm-allocated above, so the
     * iio core's own private-data area is unused and just linked to it */
    struct iio_dev *indio_dev = devm_iio_device_alloc(&spi->dev, 0);
    if (!indio_dev) {
        dev_err(&spi->dev, "probe failed: cannot alloc iio device.\n");
        return -ENOMEM;
    }
    iio_device_set_drvdata(indio_dev, prv_data);
    indio_dev->name = "icm20608";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->num_channels = ARRAY_SIZE(icm20608_iio_chan_spec);
    indio_dev->channels = icm20608_iio_chan_spec;
    indio_dev->info = &icm20608_iio_info;

    ret = devm_iio_device_register(&spi->dev, indio_dev);
    if (ret) {
        dev_err(&spi->dev, "probe failed: cannot register iio device. ret: %d\n", ret);
        return ret;
    }

    INIT_WORK(&prv_data->work, icm20608_work_func);
    timer_setup(&prv_data->timer, icm20608_timer_func, 0);
    mod_timer(&prv_data->timer, jiffies + msecs_to_jiffies(ICM20608_POLL_INTERVAL_MS));

    pr_info("icm20608 probed.\n");
    return 0;
}

static void icm20608_remove(struct spi_device *spi)
{
    struct icm20608_drv_data *prv_data = spi_get_drvdata(spi);

    del_timer_sync(&prv_data->timer);
    cancel_work_sync(&prv_data->work);

    pr_info("icm20608 removed.\n");
}

/* ---- full scale range (量程) tables ----
 *
 * AFS_SEL / FS_SEL live in bits [4:3] of ACCEL_CONFIG / GYRO_CONFIG, so the
 * table index doubles as the register field value.
 */

/* AFS_SEL 0..3 -> +-2g/4g/8g/16g, expressed as micro m/s^2 per LSB
 * (scale = 9.80665 / (32768 / range_g))
 */
static const int icm20608_accel_scale_micro[] = {599, 1197, 2394, 4788};

/* FS_SEL 0..3 -> +-250/500/1000/2000 dps, expressed as micro rad/s per LSB
 * (scale = (range_dps / 32768) * pi / 180)
 */
static const int icm20608_gyro_scale_micro[] = {133, 266, 533, 1065};

static int icm20608_find_closest_idx(const int *table, int len, int target)
{
    int idx = 0, i, diff, min_diff;

    min_diff = table[0] - target;
    if (min_diff < 0)
        min_diff = -min_diff;

    for (i = 1; i < len; i++) {
        diff = table[i] - target;
        if (diff < 0)
            diff = -diff;
        if (diff < min_diff) {
            min_diff = diff;
            idx = i;
        }
    }

    return idx;
}

static int icm20608_get_scale(struct spi_device *spi, struct iio_chan_spec const *chan,
                   int *val, int *val2)
{
    const int *table;
    u8 reg, cfg;
    int ret;

    if (chan->type == IIO_ACCEL) {
        reg = ICM_REG_ACCEL_CONFIG;
        table = icm20608_accel_scale_micro;
    } else if (chan->type == IIO_ANGL_VEL) {
        reg = ICM_REG_GYRO_CONFIG;
        table = icm20608_gyro_scale_micro;
    } else {
        return -EINVAL;
    }

    ret = icm20608_read_reg(spi, reg, &cfg);
    if (ret)
        return ret;

    *val = 0;
    *val2 = table[(cfg >> 3) & 0x3];
    return IIO_VAL_INT_PLUS_MICRO;
}

static int icm20608_set_scale(struct spi_device *spi, struct iio_chan_spec const *chan,
                   int val, int val2)
{
    const int *table;
    u8 reg, cfg;
    int ret, idx, target;

    if (chan->type == IIO_ACCEL) {
        reg = ICM_REG_ACCEL_CONFIG;
        table = icm20608_accel_scale_micro;
    } else if (chan->type == IIO_ANGL_VEL) {
        reg = ICM_REG_GYRO_CONFIG;
        table = icm20608_gyro_scale_micro;
    } else {
        return -EINVAL;
    }

    target = val * 1000000 + val2;
    idx = icm20608_find_closest_idx(table, 4, target);

    ret = icm20608_read_reg(spi, reg, &cfg);
    if (ret)
        return ret;

    cfg = (cfg & ~(u8)(0x3 << 3)) | (u8)(idx << 3);
    return icm20608_write_reg(spi, reg, cfg);
}

/* ---- iio info callback ---- */

static int icm20608_iio_info_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask)
{
    struct icm20608_drv_data *prv_data = iio_device_get_drvdata(indio_dev);
    struct imu_data data;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        // ret = icm20608_read_data(prv_data->spi, &data);
        // if (ret)
        //     return ret;
        // icm20608_apply_accel_offset(prv_data, &data);

        mutex_lock(&prv_data->lock);
        data = prv_data->data;
        mutex_unlock(&prv_data->lock);

        if (chan->type == IIO_ACCEL && chan->channel2 == IIO_MOD_X) {
            *val = data.accel_x;
        } else if (chan->type == IIO_ACCEL && chan->channel2 == IIO_MOD_Y) {
            *val = data.accel_y;
        } else if (chan->type == IIO_ACCEL && chan->channel2 == IIO_MOD_Z) {
            *val = data.accel_z;
        } else if (chan->type == IIO_ANGL_VEL && chan->channel2 == IIO_MOD_X) {
            *val = data.gyro_x;
        } else if (chan->type == IIO_ANGL_VEL && chan->channel2 == IIO_MOD_Y) {
            *val = data.gyro_y;
        } else if (chan->type == IIO_ANGL_VEL && chan->channel2 == IIO_MOD_Z) {
            *val = data.gyro_z;
        } else {
            *val = data.temp;
        }
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        return icm20608_get_scale(prv_data->spi, chan, val, val2);

    default:
        return -EINVAL;
    }
}

static int icm20608_iio_info_write_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int val,
			int val2,
			long mask)
{
    struct icm20608_drv_data *prv_data = iio_device_get_drvdata(indio_dev);

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        /*
         * OUT registers are read-only measurements; "writing raw" sets the
         * per-axis calibration offset instead. Accel offsets are applied in
         * software (icm20608_apply_accel_offset()) -- the hardware
         * XA/YA/ZA_OFFSET_H/L registers corrupt the sensor output on this
         * chip even with correctly-scaled values, so they're left alone.
         * Gyro offsets use the hardware XG/YG/ZG_OFFS_USRH/L registers,
         * confirmed to work correctly.
         */
        if (chan->type == IIO_ACCEL && chan->channel2 == IIO_MOD_X) {
            prv_data->accel_offset_x = (s16)val;
            return 0;
        } else if (chan->type == IIO_ACCEL && chan->channel2 == IIO_MOD_Y) {
            prv_data->accel_offset_y = (s16)val;
            return 0;
        } else if (chan->type == IIO_ACCEL && chan->channel2 == IIO_MOD_Z) {
            prv_data->accel_offset_z = (s16)val;
            return 0;
        } else if (chan->type == IIO_ANGL_VEL && chan->channel2 == IIO_MOD_X)
            return icm20608_write_offset(prv_data->spi, ICM_REG_XG_OFFS_USRH, ICM_REG_XG_OFFS_USRL, (s16)val);
        else if (chan->type == IIO_ANGL_VEL && chan->channel2 == IIO_MOD_Y)
            return icm20608_write_offset(prv_data->spi, ICM_REG_YG_OFFS_USRH, ICM_REG_YG_OFFS_USRL, (s16)val);
        else if (chan->type == IIO_ANGL_VEL && chan->channel2 == IIO_MOD_Z)
            return icm20608_write_offset(prv_data->spi, ICM_REG_ZG_OFFS_USRH, ICM_REG_ZG_OFFS_USRL, (s16)val);
        else
            return -EINVAL; /* temp has no offset register */

    case IIO_CHAN_INFO_SCALE:
        return icm20608_set_scale(prv_data->spi, chan, val, val2);

    default:
        return -EINVAL;
    }
}

// static int __init icm20608_init(void)
// {
//     return 0;
// }
// module_init(icm20608_init);
// 
// static void __exit icm20608_exit(void)
// {
// }
// module_exit(icm20608_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("budali11");
MODULE_DESCRIPTION("Simple icm20608 driver based on device tree for i.MX6ULL");
