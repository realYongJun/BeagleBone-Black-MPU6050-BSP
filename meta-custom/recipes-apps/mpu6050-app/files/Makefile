# Yocto 빌드 엔진이 넘겨주는 플래그를 수용하도록 구조화
TARGET = mpu6050_user
OBJS = mpu6050_user.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

