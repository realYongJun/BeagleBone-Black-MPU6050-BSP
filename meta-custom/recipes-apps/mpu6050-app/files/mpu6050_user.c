#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define DEVICE "/dev/mpu6050-0"

int main() {
    int fd;
    char buf[128];

    struct timespec start, end;

    fd = open(DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    printf("MPU6050 driver data collecting... (terminate with Ctrl+C)\n");

    while (1) {

        clock_gettime(CLOCK_MONOTONIC, &start);

        int ret = read(fd, buf, sizeof(buf) - 1);

        clock_gettime(CLOCK_MONOTONIC, &end);

        if (ret <= 0) {
            fprintf(stderr, "data reading fail!\n");
            usleep(10000);
            continue;
        }

        buf[ret] = '\0';

        long long diff =
            (end.tv_sec - start.tv_sec) * 1000000000LL +
            (end.tv_nsec - start.tv_nsec);

        int raw_ax, raw_ay, raw_az;

        if (sscanf(buf,
                   "Accel_X: %d\nAccel_Y: %d\nAccel_Z: %d",
                   &raw_ax, &raw_ay, &raw_az) == 3)
        {
            float ax = raw_ax / 8192.0f;
            float ay = raw_ay / 8192.0f;
            float az = raw_az / 8192.0f;

            printf("\rX:%6.3f Y:%6.3f Z:%6.3f, Latency: %8lld ns",
                   ax, ay, az, diff);

            fflush(stdout);
        }

        usleep(100000);
    }

    close(fd);
    return 0;
}

