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
    return uio_read_reg(dev, offset);
}

uio_dev *dev;

void *wait_thread(void *id)
{
    uio_unmask_irq(dev);
    uio_wait_irq(dev, 10000);
    return NULL;
}

int main()
{
    dev = uio_init("/dev/uio0");
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
    return 0;
}