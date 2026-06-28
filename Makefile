CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGET = uc_test
SRC = src/main.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -march=x86-64

clean:
	rm -f $(TARGET)