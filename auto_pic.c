/*1.自动图片读取显示作业*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <linux/input.h>
#include <dirent.h>

#include <jpeglib.h>
#include <jerror.h>

int lcd_fd;
int *memp;
char picture[1920 * 1080 * 4];

typedef struct node
{
    char pic_path[256];       // 图片路径
    int pic_type;             // 图片类型 1---bmp 2---jpg或jpeg
    struct node *prev, *next; // 前置后置指针
} Node, *node_t;

// 创建链表
node_t creatEmptylist()
{
    node_t head = malloc(sizeof(Node));
    if (head)
    {
        head->pic_path[0] = 0;
        head->pic_type = 0;
        head->prev = head->next = head; // 空循环链表指针指向自己
    }
    return head;
}

// 尾部插入
node_t insertList(node_t head, const char *pic_path, int pic_type)
{
    // 创建节点
    node_t newNode = malloc(sizeof(Node));
    if (newNode)
    {
        strcpy(newNode->pic_path, pic_path);
        newNode->pic_type = pic_type;

        newNode->next = head;
        newNode->prev = head->prev;
        head->prev->next = newNode;
        head->prev = newNode;
    }
    return newNode;
}

// 删除节点
void deleteNode(node_t deleteNode)
{
    deleteNode->prev->next = deleteNode->next;
    deleteNode->next->prev = deleteNode->prev;

    free(deleteNode);
}

//释放堆空间
void destroyList(node_t *phead)
{
    node_t tmp = NULL, head = (*phead)->next;

    // 释放头节点以外的所有结点
    while (head != *phead)
    {
        tmp = head;
        head = head->next;
        free(tmp);
    }
    free(head); // 释放头节点

    *phead = NULL;
}

//开启LCD和内存映射
void lcd_init()
{
    lcd_fd = open("/dev/fb0", O_RDWR);
    if (lcd_fd < 0)
    {
        perror("open");
        exit(1);
    }

    memp = mmap(NULL, 800 * 480 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, lcd_fd, 0);
    if (memp == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }
}

//关闭LCD和内存映射
void lcd_free()
{
    munmap(memp, 800 * 480 * 4);
    close(lcd_fd);
}

// 指定位置(x,y)显示指定大小(new_width, new_height)图片
void show_bmp(const char *path, int x, int y, int new_width, int new_height)
{
    char buf[3] = {};
    int offset = 0, width = 0, height = 0;
    short bpp = 0;

    int pic_fd = open(path, O_RDWR);
    if (pic_fd < 0)
    {
        perror("open");
        exit(1);
    }

    // 读取位图文件的参数信息
    int res = read(pic_fd, buf, 2);
    if (res < 0)
    {
        perror("read");
        exit(1);
    }

    // 读取图像数据离文件开头的距离
    lseek(pic_fd, 10, SEEK_SET);
    res = read(pic_fd, &offset, 4);
    if (res < 0)
    {
        perror("read");
        exit(1);
    }

    // 读取图像的宽度和高度
    lseek(pic_fd, 18, SEEK_SET);
    res = read(pic_fd, &width, 4);
    if (res < 0)
    {
        perror("read");
        exit(1);
    }

    res = read(pic_fd, &height, 4);
    if (res < 0)
    {
        perror("read");
        exit(1);
    }

    // 读取图像的位数
    lseek(pic_fd, 28, SEEK_SET);
    res = read(pic_fd, &bpp, 2);
    if (res < 0)
    {
        perror("read");
        exit(1);
    }

    printf("buf = %s,offset = %d, width = %d, height = %d, bpp = %d\n",
           buf, offset, width, height, bpp);

    // 一行补齐的字节数
    int skip = (4 - (width * bpp / 8) % 4) % 4;

    // 读取图像数据
    char pic_buffer[width * height * bpp / 8 + skip * height];
    lseek(pic_fd, offset, SEEK_SET);
    res = read(pic_fd, pic_buffer, width * height * bpp / 8 + skip * height);
    if (res < 0)
    {
        perror("read");
        exit(1);
    }

    if (x + new_width > 800 || y + new_height > 480)
    {
        printf("图片过大\n");
        exit(1);
    }

    // 显示图片
    int i, j, i0, j0; // i0,j0是图片上的位置
    for (i = 0; i < new_height; i++)
    {
        for (j = 0; j < new_width; j++)
        {
            // 图片上第i行第j个像素点也是lcd上第i行第j个像素点
            // A R G B         //B G R
            i0 = i * height / new_height;
            j0 = j * width / new_width;
            *(memp + 800 * (i + y) + j + x) = pic_buffer[(height - 1 - i0) * width * 3 + j0 * 3 + 0 + skip * (height - 1 - i0)] |
                                              pic_buffer[(height - 1 - i0) * width * 3 + j0 * 3 + 1 + skip * (height - 1 - i0)] << 8 |
                                              pic_buffer[(height - 1 - i0) * width * 3 + j0 * 3 + 2 + skip * (height - 1 - i0)] << 16;
        }
    }

    // 关闭文件
    close(pic_fd);
}

// 解析jpeg图片
void show_jpeg(const char *path, int x, int y, int new_width, int new_height)
{
    char *p = picture;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr; // 错误结构

    cinfo.err = jpeg_std_error(&jerr); // 设置解压结构的错误处理
    jpeg_create_decompress(&cinfo);

    // 打开解压文件
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        perror("fopen");
        exit(-1);
    }
    jpeg_stdio_src(&cinfo, fp);

    // 读取文件头
    jpeg_read_header(&cinfo, TRUE);
    printf("%d X %d %d\n", cinfo.image_width, cinfo.image_height, cinfo.num_components);

    // 开始解压
    jpeg_start_decompress(&cinfo);

    // 获取图像数据
    do
    {
        jpeg_read_scanlines(&cinfo, &p, 1);
        p += cinfo.image_width * cinfo.num_components;
    } while (cinfo.output_scanline < cinfo.image_height);

    if (x + new_width > 800 || y + new_height > 480)
    {
        printf("图片过大\n");
        exit(1);
    }

    // 显示
    int i, j, i0, j0;
    for (i = 0; i < new_height; i++)
    {
        for (j = 0; j < new_width; j++)
        {
            i0 = i * cinfo.image_height / new_height;
            j0 = j * cinfo.image_width / new_width;

            *(memp + 800 * (y + i) + x + j) = picture[3 * (i0 * cinfo.image_width + j0)] << 16 |
                                              picture[3 * (i0 * cinfo.image_width + j0) + 1] << 8 |
                                              picture[3 * (i0 * cinfo.image_width + j0) + 2];
        }
    }

    // 结束解压
    jpeg_finish_decompress(&cinfo);
    // 销毁解压结构
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
}

//获取图片的后缀
void get_pic(char *path, node_t picList)
{
    DIR *dir;
    struct dirent *dp;
    char file_path[1024];
    char buf[1024];
    int ret;

    dir = opendir(path);
    if (dir == NULL)
    {
        perror("opendir");
        exit(1);
    }

    while ((dp = readdir(dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
        {
            continue;
        }

        if (path[strlen(path) - 1] == '/')
        {
            sprintf(file_path, "%s%s", path, dp->d_name);
        }
        else
        {
            sprintf(file_path, "%s/%s", path, dp->d_name);
        }

        if (dp->d_type == DT_DIR)
        {
            get_pic(file_path, picList);
        }
        else if (dp->d_type == DT_REG)
        {
            // 判断文件的后缀是否是.bmp
            if (strlen(dp->d_name) > 4 &&
                (strcmp(dp->d_name + strlen(dp->d_name) - 4, ".bmp") == 0 ||
                 strcmp(dp->d_name + strlen(dp->d_name) - 4, ".BMP") == 0))
            {
                printf("%s\n", file_path);
                // 加入链表中
                insertList(picList, file_path, 1);
            }
            else if (strlen(dp->d_name) > 4 &&
                     (strcmp(dp->d_name + strlen(dp->d_name) - 4, ".jpg") == 0 ||
                      strcmp(dp->d_name + strlen(dp->d_name) - 4, ".JPG") == 0 ||
                      strcmp(dp->d_name + strlen(dp->d_name) - 5, ".jpeg") == 0 ||
                      strcmp(dp->d_name + strlen(dp->d_name) - 5, ".JPEG") == 0))
            {
                printf("%s\n", file_path);
                // 加入链表中
                insertList(picList, file_path, 2);
            }
        }
    }

    closedir(dir);
}

//显示黑色
void show_black()
{
    // 操作显存
    int i, j;
    for (int i = 0; i < 480; i++)
    {
        for (j = 0; j < 800; j++)
        {

            *(memp + i * 800 + j) = 0x0;
        }
    }
}

// 显示图片
void show_pic(const char *path, int pic_type)
{
    if (pic_type == 1)
    {
        show_bmp(path, 0, 0, 800, 480);
    }
    else if (pic_type == 2)
    {
        show_jpeg(path, 0, 0, 800, 480);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage:%s <pic dir path>\n", argv[0]);
        exit(-1);
    }

    // 读取目录创建链表
    node_t picList = creatEmptylist();
    get_pic(argv[1], picList);
    // 如果没有图片,退出
    if (picList->next == picList)
    {
        printf("目标目录没有图片!\n");
        exit(-1);
    }

    lcd_init();
    // 显示第一张
    node_t node = picList->next, tmp = NULL;
    show_pic(node->pic_path, node->pic_type);

    int fd = open("/dev/input/event0", O_RDONLY);
    if (fd == -1)
    {
        perror("open");
        exit(1);
    }

    struct input_event ev;
    int x, y, x1, y1;

    while (1)
    {
        read(fd, &ev, sizeof(ev));

        // printf("type = %d, code = %d, value = %d\n", ev.type, ev.code, ev.value);
        if (ev.type == EV_ABS && ev.code == ABS_X)
        {
            x = ev.value * 800 / 1024;
        }
        else if (ev.type == EV_ABS && ev.code == ABS_Y)
        {
            y = ev.value * 480 / 600;
        }
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 1)
        {
            x1 = x;
            y1 = y;
            // printf("按下的坐标(%d, %d)\n", x, y);
        }
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0)
        {
            // printf("松开的坐标(%d, %d)\n", x, y);
            // 左划>50像素
            if (x < x1 - 50)
            {
                node = node->next;
                // 略过头节点
                if (node == picList)
                {
                    node = node->next;
                }

                show_pic(node->pic_path, node->pic_type);
                printf("打开了%s的图片\n",node->pic_path);
            }
            // 右划>50像素
            else if (x > x1 + 50)
            {
                node = node->prev;
                // 略过头节点
                if (node == picList)
                {
                    node = node->prev;
                }

                show_pic(node->pic_path, node->pic_type);
                printf("打开了%s的图片\n",node->pic_path);
            }
            else if (y < y1 - 100)  //上划100个像素
            {
                if (node->next->next == node)
                {
                    show_black();
                    break;
                }
                // 移动到下一张,再删除
                tmp = node;
                printf("删除了%s这张图片\n",node->pic_path);
                node = node->next;
                // 略过头节点
                if (node == picList)
                {
                    node = node->next;
                }
                show_pic(node->pic_path, node->pic_type);
                deleteNode(tmp);

            }
        }
    }

    lcd_free();
    destroyList(&picList);
    return 0;
}
