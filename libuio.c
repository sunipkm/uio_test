#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

inline uint64_t get_nsec(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000000000L + ((uint64_t)ts.tv_nsec);
}

sig_atomic_t done = 0;

void sighandler()
{
    done = 1;
}

typedef struct {
    int fd;
    int allocated;
    size_t size;
    void *ptr;
} uio_dev;

uio_dev *uio_init(const char *fname)
{
    uio_dev *dev = (uio_dev *)malloc(sizeof(uio_dev));
    if (dev == NULL)
    {
        perror("can not allocate memory");
        return NULL;
    }
    dev->allocated = 0;
    int fd = open(fname, O_RDWR);
    if (fd < 0)
    {
        perror("failed to open uio device");
        free(dev);
        return NULL;
    }
    dev->fd = fd;
    return dev;
}

void uio_destroy(uio_dev *dev)
{
    if (dev->allocated)
        munmap(dev->ptr, dev->size);
    close(dev->fd);
    free(dev);
}

int uio_mmap(uio_dev *dev, size_t size)
{
    dev->ptr = mmap(NULL,
                    size,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    dev->fd,
                    0);
    
    if (dev->ptr == MAP_FAILED)
    {
        perror("uio_mmap");
        return -1;
    }
    dev->size = size;
    dev->allocated = 1;
    return size;
}

void uio_unmask_irq(uio_dev *dev)
{
    uint32_t unmask = 1;
    ssize_t rv = write(dev->fd, &unmask, sizeof(unmask));
    if (rv != (ssize_t)sizeof(unmask))
    {
        perror("uio_unmask_irq");
    }
}

void uio_wait_irq(uio_dev *dev, int timeout_ms)
{
    struct pollfd pfd = {.fd = dev->fd, .events = POLLIN};
    int rv = poll(&pfd, 1, timeout_ms);

    if (rv >= 1)
    {
        uint32_t info, ret;
        ret = read(dev->fd, &info, sizeof(info));
        printf("%s: received interrupt %d\n", __func__, info);
    }
    else if (rv == 0)
    {
        printf("%s: interrupt timed out\n", __func__);
    }
    else
    {
        perror("uio_wait_irq");
    }
}

uint32_t uio_read_reg(uio_dev *dev, uint32_t offset)
{
    uint32_t *tmp = dev->ptr;
    return tmp[offset >> 2];
}

uint32_t uio_write_reg(uio_dev *dev, uint32_t offset, uint32_t val)
{
    uint32_t *tmp = dev->ptr;
    tmp[offset >> 2] = val;
    printf("%s: wrote 0x%02x to 0x%03x\n", __func__, val, offset);
    return uio_read_reg(dev, offset);
}

#define UIO_RESET    0x0
#define UIO_ENABLE   0x1
#define UIO_TSTAMP   0x8
#define UIO_TRIG_IN  0x100
#define UIO_BOOL_OUT 0x104
#define UIO_CHAR_OUT 0x108

void *wait_func(void *d)
{
    uio_dev *dev = (uio_dev *)d;
    uint64_t start = get_nsec();
    uio_unmask_irq(dev);
    uio_wait_irq(dev, 10000);
    uint64_t stop = get_nsec();
    double time = (stop - start)*1e-9;
    printf("%s: waited for %.9lf seconds\n", __func__, time);
    printf("%s: UIO bool: 0x%01x\n", __func__, uio_read_reg(dev, UIO_BOOL_OUT));
    printf("%s: UIO char: 0x%02x\n", __func__, uio_read_reg(dev, UIO_CHAR_OUT));
    return NULL;
}

int main()
{
    // set up signal handler for sigint
    struct sigaction sa;
    sa.sa_handler = &sighandler;
    sigaction(SIGINT, &sa, NULL);

    // open device and mmap
    uio_dev *dev = uio_init("/dev/uio0");
    if (dev == NULL)
    {
        return -1;
    }
    int status = uio_mmap(dev, 0x1000);
    if (status < 0)
    {
        perror("mmap failed");
        return -1;
    }
    printf("IP UID: %d\n", uio_read_reg(dev, 0x8));

    printf("Resetting ip: 0x%02x\n", uio_write_reg(dev, UIO_RESET, 0x1));
    printf("Enabling ip: 0x%02x\n", uio_write_reg(dev, UIO_ENABLE, 0x1));

    uint32_t timeout = 1000000 * ((rand() % 3) + 2); // 2 - 5 seconds
    printf("IRQ trigger in %d seconds\n", timeout / 1000000);

    printf("Creating interrupt handling thread...\n");
    pthread_t irq_thread;
    int ret = pthread_create(&irq_thread, NULL, &wait_func, (void *)dev);
    if (ret)
    {
        perror(": error pthread_create");
        return -1;
    }
    usleep(timeout);

    printf("Triggering interrupt: 0x%02x\n", uio_write_reg(dev, UIO_TRIG_IN, 0xb)); // write 11

    pthread_join(irq_thread, NULL);

    printf("Enter [q] to quit...\n");
    char b = '\0';
    do 
    {
        status = scanf(" %c", &b);
    }
    while(b != 'q' || b != 'Q'); // waiting for sigint

    printf("Disabling interrupt: 0x%02x\n", uio_write_reg(dev, UIO_TRIG_IN, 0x0)); // write 0

    uio_destroy(dev);
    return 0;
}