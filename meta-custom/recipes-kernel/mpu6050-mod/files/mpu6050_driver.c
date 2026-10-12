#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>              /* file_operations 구조체 용 */
#include <linux/cdev.h>            /* cdev (Character Device) 구조체 용 */
#include <linux/device.h>          /* class, device 관련 /dev/mpu6050 같은 장치 파일 생성 */
#include <linux/uaccess.h>         /* 커널 메모리와 유저 메모리 사이를 안전하게 복사하는 copy_to_user, copy_from_user 용 */
#include <linux/mod_devicetable.h> /* struct of_device_id 정의 */
#include <linux/of.h>              /* Device Tree (Open Firmware) 관련 API */
#include <linux/atomic.h>          /* atomic_t 및 관련 매크로/함수 용 */
#include <linux/i2c.h>             /* struct i2c_client 정의 */
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee");
MODULE_DESCRIPTION("MPU-6050 Character Device Driver");

#define DEVICE_NAME "mpu6050"
#define CLASS_NAME "sensor_class"
#define MAX_DEVICES 32

/* 레지스터 주소 */
#define MPU6050_REG_SIGNAL_PATH_RESET 0x68
#define MPU6050_REG_PWR_MGMT_1        0x6B /* 장치 초기화 */
#define MPU6050_REG_WHO_AM_I          0x75 /* 장치 식별 */
#define MPU6050_REG_SMPLRT_DIV      0x19  /* 측정 주기 조절 */
#define MPU6050_REG_ACCEL_CONFIG    0x1C  /* 가속도계 초기화 및 테스트 */
// #define MPU6050_REG_ACCEL_CONFIG2   0x1D  /* 가속도계 설정 */
#define MPU6050_REG_CONFIG 0x1A

/* 필요한 레지스터 설정 값 */
#define MPU6050_WHO_AM_I_EXPECTED     0x68 /* WHO_AM_I 기본값 */
#define MPU6050_DEVICE_RESET          0x80 /* PWR_MGMT_1: H_RESET */
#define MPU6050_SIG_PATH_RESET_ALL    0x07 /* SIGNAL_PATH_RESET: GYRO | ACCEL | TEMP */
/* 가속도계 설정값 */
#define MPU6050_ACCEL_FS_4G      0x08 /* 가속도 측정 범위 */
#define MPU6050_CONFIG_DLPF_94HZ  0x02 /* 노이즈 필터 설정 */
#define MPU6050_SMPLRT_DIV_125HZ 0x07 /* 1000 / (1 + 7) = 125Hz */

/* 가속도계 X축 상위 바이트 레지스터 주소 (십진수 59) */
#define MPU6050_REG_ACCEL_XOUT_H 0x3B

#define MPU6050_IOC_MAGIC 'M'

/* 가속도 측정 범위 (ACCEL_CONFIG) */
#define MPU6050_IOC_SET_ACCEL_RANGE _IOW(MPU6050_IOC_MAGIC, 1, int)

/* 디지털 로우 패스 필터 강도 (ACCEL_CONFIG_2) */
#define MPU6050_IOC_SET_DLPF        _IOW(MPU6050_IOC_MAGIC, 2, int)

/* 샘플링 속도 디바이더 (SMPLRT_DIV) */
#define MPU6050_IOC_SET_SMPLRT_DIV  _IOW(MPU6050_IOC_MAGIC, 3, int)

static dev_t dev_num;       // 메이저 및 마이너 디바이스 id를 담는 변수
static struct class *mpu6050_class = NULL;
static atomic_t mpu6050_minor_cnt = ATOMIC_INIT(0); /* 발견된 장치 개수 카운터 */

/* 개별 센서마다 하나씩 생성될 구조체 (전역 변수 대체) */
struct mpu6050_data {
    struct i2c_client *client;
    struct cdev cdev;
    struct device *dev;
    int minor; /* 장치 고유 번호 */
};

static int mpu6050_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
    int ret;
    
    /* I2C SMBus API를 사용하여 1바이트 데이터 읽기 */
    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0)
        return ret;
        
    *val = (u8)ret;
    return 0;
}

static int mpu6050_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    /* I2C SMBus API를 사용하여 1바이트 데이터 쓰기 */
    return i2c_smbus_write_byte_data(client, reg, val);
}


/* =========================================================
 * File Operations: 유저 스페이스(앱)와의 통신 인터페이스
 * ========================================================= */

/* 앱에서 open("/dev/mpu6050", O_RDONLY) 호출 시 실행 */
/* inode는 파일의 정적 메타데이터, char device의 경우 메이저, 마이너 번호 포함 */
/* file은 이 장치를 open한 유저 프로세스의 동적인 상태를 관리하기 위함 */
static int mpu6050_open(struct inode *inodep, struct file *filep)
{
    struct mpu6050_data *data;

    /* VFS(가상 파일 시스템)의 inode에 연결된 cdev를 역추적하여 mpu6050_data 구조체의 시작 주소를 찾음 */
    data = container_of(inodep->i_cdev, struct mpu6050_data, cdev);

    /* 찾은 구조체를 filep->private_data에 보관하여 read/write에서 꺼내 씀 */
    filep->private_data = data;

    printk(KERN_INFO "MPU-6050 Sensor [%d]: Device opened\n", data->minor);
    return 0;
}

/* 앱에서 read() 호출 시 실행 */
/* 유저 프로그램이 요구한 데이터의 최대 크기 */
/* 장치 파일을 어디까지 읽었는지 표시하는 커서의 현재 위치 */
static ssize_t mpu6050_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    /* open에서 저장해둔 해당 센서의 고유 데이터를 꺼내옴 */
    struct mpu6050_data *data = filep->private_data;

    char msg[64];
    int msg_len;
    size_t to_copy;
    int ret;

    /* I2C 통신에서는 레지스터 주소의 최상위 비트(MSB) 조작이 불필요함 */
    u8 reg = MPU6050_REG_ACCEL_XOUT_H;

    /* 가속도계 X, Y, Z 축 상/하위 바이트를 담을 총 6바이트의 수신 버퍼 */
    u8 rx_buf[6] = {0};

    /* 2의 보수 포맷을 정상적으로 처리하기 위한 16비트 부호 있는 정수형 변수 */
    s16 accel_x, accel_y, accel_z;

    /* I2C 표준 통신(Bulk Read)을 위한 메시지 구조체 배열 */
    struct i2c_msg msgs[2];

    /* 센서와 통신: 레지스터 주소 1바이트 쓰기 후, 데이터 6바이트 읽기 */
    msgs[0].addr = data->client->addr;
    msgs[0].flags = 0; /* Write */
    msgs[0].len = 1;
    msgs[0].buf = &reg;

    msgs[1].addr = data->client->addr;
    msgs[1].flags = I2C_M_RD; /* Read */
    msgs[1].len = 6;
    msgs[1].buf = rx_buf;

    /* i2c_transfer는 처리한 메시지 개수를 반환합니다 (정상 시 2) */
    ret = i2c_transfer(data->client->adapter, msgs, 2);
    if (ret != 2) {
        printk(KERN_ERR "MPU-6050 Sensor: i2c_transfer failed (ret=%d)\n", ret);
        return -EIO;
    }

    /* 수신된 상위 바이트(_H)와 하위 바이트(_L) 데이터를 16비트 정수로 결합 */
    accel_x = (s16)((rx_buf[0] << 8) | rx_buf[1]);
    accel_y = (s16)((rx_buf[2] << 8) | rx_buf[3]);
    accel_z = (s16)((rx_buf[4] << 8) | rx_buf[5]);

    /* 사용자 공간으로 전달할 문자열 포맷팅 */
    msg_len = snprintf(msg, sizeof(msg), "Accel_X: %d\nAccel_Y: %d\nAccel_Z: %d\n", accel_x, accel_y, accel_z);

    /* 버퍼 오버플로우 방지를 위해 복사할 길이 계산 */
    to_copy = min((size_t)msg_len, len);

    /* 커널 공간의 msg 배열 데이터를 사용자 공간의 buffer로 복사 */
    if (copy_to_user(buffer, msg, to_copy)) {
        printk(KERN_ERR "MPU-6050 Sensor: Failed to send data to user\n");
        return -EFAULT;
    }
    
    return to_copy;
}

static long mpu6050_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
    struct mpu6050_data *data = filep->private_data;
    int val;
    int ret;

    /* 유효성 검사 및 사용자 공간 데이터 복사 (명령어 처리)*/
    if (_IOC_DIR(cmd) & _IOC_WRITE) {
        if (copy_from_user(&val, (int __user *)arg, sizeof(int))) {
            return -EFAULT;
        }
    }

    /* 명령어 구분 */
    switch (cmd) {
        case MPU6050_IOC_SET_ACCEL_RANGE:
            /* 4가지 가속도 측정 범위 */
            if (val < 0 || val > 3) return -EINVAL;
            /* 4번째 비트부터 설정, 시프트 필요 */
            ret = mpu6050_write_reg(data->client, MPU6050_REG_ACCEL_CONFIG, (u8)(val << 3));
            break;

        case MPU6050_IOC_SET_DLPF:
            /* 0~7 단계 */
            if (val < 0 || val > 7) return -EINVAL;
            /* MPU-6050은 ACCEL_CONFIG2가 없으며, CONFIG 레지스터(0x1A)의 하위 3비트[2:0]를 사용함 */
            ret = mpu6050_write_reg(data->client, MPU6050_REG_CONFIG, (u8)val);
            break;

        case MPU6050_IOC_SET_SMPLRT_DIV:
            /* 측정 주기 8비트 정수 설정 */
            if (val < 0 || val > 255) return -EINVAL;
            /* 시프트 필요 X */
            ret = mpu6050_write_reg(data->client, MPU6050_REG_SMPLRT_DIV, (u8)val);
            break;

        default:
            return -ENOTTY;
    }

    if (ret < 0) {
        dev_err(&data->client->dev, "ioctl command 0x%x failed: %d\n", cmd, ret);
    }
    
    return ret;
}

/* 앱에서 close() 호출 시 실행 */
/* 센서를 다시 sleep mode로 돌려놓거나, open에서 동적 할당했던 커널 메모리(kmalloc)가 있으면 해제(kfree)함 */
static int mpu6050_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "MPU-6050 Sensor: Device successfully closed\n");
    return 0;
}

/* 시스템 콜과 커널 함수를 매핑하는 구조체 */
static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .open = mpu6050_open,
    .read = mpu6050_read,
    .unlocked_ioctl = mpu6050_ioctl,
    .release = mpu6050_release,
};

/* =========================================================
 * probe / remove
 * ========================================================= */



/* 하드웨어가 발견되었을 때 호출되는 초기화 함수 */
static int mpu6050_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct mpu6050_data *data;
    dev_t current_dev_num;
    u8 who_am_i;
    int ret;
    printk(KERN_INFO "MPU-6050 Sensor: Probe called, hardware found\n");

    /* I2C는 SPI와 달리 개별 설정이 없음 */

    /* WHO_AM_I 레지스터 통신 상태 확인 */
    ret = mpu6050_read_reg(client, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read WHO_AM_I register\n");
        return ret;
    }

    /* WHO_AM_I 값이 0x68인지 확인 */
    if (who_am_i != MPU6050_WHO_AM_I_EXPECTED) {
        dev_err(&client->dev, "Device not recognized. WHO_AM_I = 0x%02x (Expected: 0x%02x)\n", who_am_i, MPU6050_WHO_AM_I_EXPECTED);
        return -ENODEV;
    }

    /* 장치 깨우기 (Wake-up) 및 초기화, 레지스터 맵의 설명에 따름 */

    /* 칩 전체 리셋 */
    ret = mpu6050_write_reg(client, MPU6050_REG_PWR_MGMT_1, MPU6050_DEVICE_RESET);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to send DEVICE_RESET\n");
        return ret;
    }

    /* 100ms 대기 */
    msleep(100);

    /* 신호 경로 리셋 (GYRO_RST | ACCEL_RST | TEMP_RST = 1) */
    ret = mpu6050_write_reg(client, MPU6050_REG_SIGNAL_PATH_RESET, MPU6050_SIG_PATH_RESET_ALL);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to send SIGNAL_PATH_RESET\n");
        return ret;
    }

    /* 100ms 대기 */
    msleep(100);

    /* 칩 리셋 이후 sleep 비트 해제 */
    ret = mpu6050_write_reg(client, MPU6050_REG_PWR_MGMT_1, 0x01);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to wake up device and set clock source\n");
        return ret;
    }

    /* 100ms 대기 */
    msleep(100);

    /* 가속도 센서 초기화 */

    /* 가속도계 테스트, 측정범위 4G */
    ret = mpu6050_write_reg(client, MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_4G);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to init ACCEL_CONFIG\n");
        return ret;
    }

    /* 노이즈 필터 사용: MPU-6050은 ACCEL_CONFIG2(0x1D) 대신 CONFIG(0x1A) 레지스터를 사용함 */
    ret = mpu6050_write_reg(client, MPU6050_REG_CONFIG, MPU6050_CONFIG_DLPF_94HZ); /* 필터 강도 2단계 (대역폭 ~94Hz) */
    if (ret < 0) {
        dev_err(&client->dev, "Failed to init CONFIG (DLPF)\n");
        return ret;
    }

    /* 가속도 샘플링 주기 설정 */
    /* 1000Hz / (1 + 7) = 125Hz (7 입력) */
    ret = mpu6050_write_reg(client, MPU6050_REG_SMPLRT_DIV, MPU6050_SMPLRT_DIV_125HZ);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to init SMPLRT_DIV\n");
        return ret;
    }

    /* MPU-6050 초기화 완료 */
    dev_info(&client->dev, "MPU-6050 Initialized successfully!\n");


    /* 이 센서만을 위한 독립된 메모리 구조체 동적 할당 */
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM;

    /* 장치 고유 정보 세팅 */
    data->client = client;
    if (atomic_read(&mpu6050_minor_cnt) >= MAX_DEVICES)
        return -ENODEV;
    data->minor = atomic_fetch_inc(&mpu6050_minor_cnt); /* 0번, 1번 순차 부여 */
    if (data->minor >= MAX_DEVICES)
        return -ENODEV;

    /* 1. I2C 통신을 위한 포인터 확보 */
    i2c_set_clientdata(client, data);

    /* 문자 디바이스 초기화 및 등록 */
    cdev_init(&data->cdev, &fops);
    data->cdev.owner = THIS_MODULE;
    
    current_dev_num = MKDEV(MAJOR(dev_num), data->minor);
    ret = cdev_add(&data->cdev, current_dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "MPU-6050 Sensor: Failed to add cdev in probe\n");
        return ret;
    }

    /* 디바이스 노드(/dev/mpu6050-x) 생성 */
    /* 두 번째 인자로 client->dev를 넘겨 부모(하드웨어) 장치와 연결시킴 */
    data->dev = device_create(mpu6050_class, &client->dev, current_dev_num, data, DEVICE_NAME "-%d", data->minor);
    if (IS_ERR(data->dev)) {
        cdev_del(&data->cdev);
        return PTR_ERR(data->dev);
    }

    /* Probe 완료 */
    printk(KERN_INFO "MPU-6050 Sensor: Probe successful, /dev/%s-%d ready\n", DEVICE_NAME, data->minor);
    return 0;
}

/* 장치가 물리적으로 제거되거나 드라이버 모듈이 언로드될 때 호출 */
static int mpu6050_remove (struct i2c_client *client) /* 최신 커널은 void 반환 */
{
    /* probe에서 저장해둔 개별 장치 구조체를 가져옴 */
    struct mpu6050_data *data = i2c_get_clientdata(client);
    
    /* 해당 장치에 할당된 특정 노드와 cdev만 정확하게 제거함 */
    device_destroy(mpu6050_class, MKDEV(MAJOR(dev_num), data->minor));
    cdev_del(&data->cdev);

    printk(KERN_INFO "MPU-6050 Sensor [%d]: Device removed\n", data->minor);

    return 0;
}

/* =========================================================
 * I2C Driver 구조체 등록
 * ========================================================= */

/* 디바이스 트리(.dts)와 매칭하기 위한 테이블 */
static const struct of_device_id mpu6050_of_match[] = {
    { .compatible = "invensense,mpu6050", },
    { }
};
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

static const struct i2c_device_id mpu6050_id[] = {
    { "mpu6050", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu6050_id);

/* 3. spi_driver를 i2c_driver로 변경 */
static struct i2c_driver mpu6050_i2c_driver = {
    .driver = {
        .name = DEVICE_NAME,
        .of_match_table = mpu6050_of_match, /* 디바이스 트리 매칭 연결 */
    },
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
    .id_table = mpu6050_id, /* I2C 전용 매칭 테이블 연결 */
};

/* =========================================================
 * 모듈 init / exit
 * ========================================================= */

static int __init mpu6050_init(void)
{
    int ret;
    printk(KERN_INFO "MPU-6050 Sensor: Initializing the Char Device\n");

    /* 메이저 및 마이너 디바이스 id 동적 할당 */
    ret = alloc_chrdev_region(&dev_num, 0, MAX_DEVICES, DEVICE_NAME);
    /* 할당 실패 */
    if (ret < 0) {
        printk(KERN_ERR "MPU-6050 Sensor: Failed to allocate chrdev region\n");
        return ret;
    }
    /* 할당 성공*/
    printk(KERN_INFO "MPU-6050 Sensor: Registered correctly with major number %d\n", MAJOR(dev_num));
    
    /* 디바이스 클래스 등록, /sys/class/sensor_class가 생성됨 */
    mpu6050_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(mpu6050_class)) {
        unregister_chrdev_region(dev_num, MAX_DEVICES);
        printk(KERN_ERR "MPU-6050 Sensor: Failed to register device class\n");
        return PTR_ERR(mpu6050_class);
    }

    /* 3. I2C 드라이버 프레임워크에 드라이버 소프트웨어 등록 */
    /* 이 함수가 성공하면 커널이 하드웨어를 찾고 probe를 호출할 준비를 마침 */
    ret = i2c_add_driver(&mpu6050_i2c_driver);
    if (ret < 0) {
        class_destroy(mpu6050_class);
        unregister_chrdev_region(dev_num, MAX_DEVICES);
        printk(KERN_ERR "MPU-6050 Sensor: Failed to register I2C driver\n");
        return ret;
    }

    printk(KERN_INFO "MPU-6050 Sensor: Char Device created successfully\n");
    return 0;
}

static void __exit mpu6050_exit(void)
{
    /* 할당의 역순으로 리소스를 안전하게 해제 */
    i2c_del_driver(&mpu6050_i2c_driver); /* 여기서 내부적으로 remove가 연쇄 호출됨 */
    class_destroy(mpu6050_class);
    unregister_chrdev_region(dev_num, MAX_DEVICES);
    
    printk(KERN_INFO "MPU-6050 Sensor: Driver module exited\n");
}

module_init(mpu6050_init);
module_exit(mpu6050_exit);

