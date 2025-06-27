// smart_petcare_driver.c (gpiod_get + device tree overlay + 함수 정리 버전)
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>

#define DEVICE_NAME "spetcom"
#define CLASS_NAME  "spet"
#define MAX_BUF_LEN 256
#define BIT_DELAY_MS 1

#define IOCTL_SET_MODE _IOW('p', 1, int)
#define MODE_MONITOR 0
#define MODE_PETCAM  1

static int major_number;
static struct class*  spet_class  = NULL;
static struct device* spet_device = NULL;
static struct cdev spet_cdev;
static DEFINE_MUTEX(spet_mutex);

static int current_mode = -1;

static struct gpio_desc *gpiod_m_data, *gpiod_m_clk;
static struct gpio_desc *gpiod_p_data, *gpiod_p_clk;
static DECLARE_WAIT_QUEUE_HEAD(read_wq);
static int read_ready = 0;
static int irq_number = -1;
static char irq_buffer[MAX_BUF_LEN] = {0};
static int irq_buf_pos = 0;
static struct device *driver_dev = NULL;

static int configure_gpios(struct device *dev) {
    gpiod_m_data = gpiod_get(dev, "monitor-data", GPIOD_IN);
    gpiod_m_clk  = gpiod_get(dev, "monitor-clk",  GPIOD_IN);
    gpiod_p_data = gpiod_get(dev, "petcam-data",  GPIOD_IN);
    gpiod_p_clk  = gpiod_get(dev, "petcam-clk",   GPIOD_IN);

    if (!gpiod_m_data || !gpiod_m_clk || !gpiod_p_data || !gpiod_p_clk) {
        printk(KERN_ERR "[spetcom] Failed to acquire GPIOs via gpiod_get()\n");
        return -ENODEV;
    }
    return 0;
}

static irqreturn_t clk_irq_handler(int irq, void *dev_id) {
    struct gpio_desc *data_in = (current_mode == MODE_MONITOR) ? gpiod_p_data : gpiod_m_data;
    int bit = gpiod_get_value(data_in);
    irq_buffer[irq_buf_pos / 8] |= (bit << (7 - (irq_buf_pos % 8)));
    irq_buf_pos++;
    if (irq_buf_pos >= MAX_BUF_LEN * 8) {
        read_ready = 1;
        wake_up_interruptible(&read_wq);
        irq_buf_pos = 0;
    }
    return IRQ_HANDLED;
}

static void send_bit(struct gpio_desc *data, struct gpio_desc *clk, int bit) {
    gpiod_set_value(data, bit);
    msleep(BIT_DELAY_MS);
    gpiod_set_value(clk, 1);
    msleep(BIT_DELAY_MS);
    gpiod_set_value(clk, 0);
    msleep(BIT_DELAY_MS);
}

static ssize_t spet_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    char *kbuf;
    struct gpio_desc *data_out, *clk_out;
    int i, j;

    if (current_mode == -1) return -EINVAL;

    kbuf = kmalloc(len, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;
    if (copy_from_user(kbuf, buffer, len)) { kfree(kbuf); return -EFAULT; }

    mutex_lock(&spet_mutex);
    data_out = (current_mode == MODE_MONITOR) ? gpiod_m_data : gpiod_p_data;
    clk_out  = (current_mode == MODE_MONITOR) ? gpiod_m_clk : gpiod_p_clk;

    gpiod_direction_output(data_out, 0);
    gpiod_direction_output(clk_out, 0);

    for (i = 0; i < len; i++)
        for (j = 7; j >= 0; j--)
            send_bit(data_out, clk_out, (kbuf[i] >> j) & 1);

    gpiod_direction_input(data_out);
    gpiod_direction_input(clk_out);
    mutex_unlock(&spet_mutex);
    kfree(kbuf);
    return len;
}

static ssize_t spet_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    struct gpio_desc *clk_in = (current_mode == MODE_MONITOR) ? gpiod_p_clk : gpiod_m_clk;
    int gpio_num = desc_to_gpio(clk_in);
    irq_number = gpiod_to_irq(gpio_num);
    if (irq_number < 0) return -EINVAL;

    read_ready = 0;
    irq_buf_pos = 0;
    memset(irq_buffer, 0, sizeof(irq_buffer));

    if (request_irq(irq_number, clk_irq_handler, IRQF_TRIGGER_RISING, "spetcom_clk_irq", NULL))
        return -EIO;

    wait_event_interruptible(read_wq, read_ready);
    free_irq(irq_number, NULL);

    int byte_len = (irq_buf_pos + 7) / 8;
    if (copy_to_user(buffer, irq_buffer, min(len, byte_len))) return -EFAULT;
    return min(len, byte_len);

}

static long spet_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
    if (cmd == IOCTL_SET_MODE && (arg == MODE_MONITOR || arg == MODE_PETCAM)) {
        current_mode = arg;
        return 0;
    }
    return -EINVAL;
}

static int spet_open(struct inode *inodep, struct file *filep) { return 0; }
static int spet_release(struct inode *inodep, struct file *filep) { return 0; }

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = spet_open,
    .release = spet_release,
    .read = spet_read,
    .write = spet_write,
    .unlocked_ioctl = spet_ioctl,
};

static int __init spet_init(void) {
    int ret;

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) return major_number;

    spet_class = class_create(CLASS_NAME);
    if (IS_ERR(spet_class)) return PTR_ERR(spet_class);

    spet_device = device_create(spet_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(spet_device)) {
        class_destroy(spet_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(spet_device);
    }

    driver_dev = spet_device;
    ret = configure_gpios(driver_dev);
    if (ret < 0) {
        device_destroy(spet_class, MKDEV(major_number, 0));
        class_destroy(spet_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }

    mutex_init(&spet_mutex);
    return 0;
}

static void __exit spet_exit(void) {
    cdev_del(&spet_cdev);
    device_destroy(spet_class, MKDEV(major_number, 0));
    class_unregister(spet_class);
    class_destroy(spet_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    mutex_destroy(&spet_mutex);

    gpiod_put(gpiod_m_data);
    gpiod_put(gpiod_m_clk);
    gpiod_put(gpiod_p_data);
    gpiod_put(gpiod_p_clk);
}

module_init(spet_init);
module_exit(spet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Smart Petcare Communication System (gpiod_get + device tree)");
MODULE_VERSION("1.0");