#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include <gphoto2.h>
#include <jpeglib.h>


#define FRAMEWIDTH 640
#define FRAMEHEIGHT 426
#define PIXELSIZE 3

#define gpcheck(cmd) gpcheck_x(cmd, __FILE__, __LINE__, __func__, #cmd)


static void sig_handler(int);
static void videodev_init(int fd);
static void gpcheck_x(int result, const char *file, int line, const char *func, const char *command);
static void process_frame(int fd, struct jpeg_decompress_struct *cinfo);


sig_atomic_t do_continue = 1;


int main(int argc, char **argv)
{
  Camera *camera;
  GPContext *context;
  CameraFile *gp_buf;
  int vidfd = -1;
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  /* Init videodev */
  vidfd = open("/dev/video0", O_RDWR);
  assert(vidfd>=0);
  videodev_init(vidfd);

  /* Init camera */
  context = gp_context_new();
  gpcheck(gp_camera_new(&camera));
  gpcheck(gp_camera_init(camera, context));
  gpcheck(gp_file_new(&gp_buf));

  /* Init jpeg decompressor */
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  /* Camera is OK */
  fprintf(stderr, "Ready.\n");
  signal(SIGINT, sig_handler);
  signal(SIGPIPE, sig_handler);

  /* Main loop */
  while (do_continue)
  {
    const char *gp_data;
    unsigned long gp_size;

    gpcheck(gp_camera_capture_preview(camera, gp_buf, context));
    gpcheck(gp_file_get_data_and_size(gp_buf, &gp_data, &gp_size));

    jpeg_mem_src(&cinfo, (unsigned char *) gp_data, gp_size);
    process_frame(vidfd, &cinfo);
  }

  fprintf(stderr, "Finished.\n");

  /* Finished, free resources */
  jpeg_destroy_decompress(&cinfo);
  close(vidfd); 
  gpcheck(gp_file_free(gp_buf));
  gpcheck(gp_camera_exit(camera, context));
  gpcheck(gp_camera_free(camera));
  free(context);

  return 0;
}

/*clip value between 0 and 255*/
#define CLIP(value) (unsigned char)(((value)>0xFF)?0xff:(((value)<0)?0:(value)))

void rgb2yuyv(unsigned char *pyuv, unsigned char *prgb, int width, int height) 
{

  int i=0;
  for(i=0;i<(width*height*3);i+=6) 
  {
    /* y */ 
    *pyuv++ =CLIP(0.299 * (prgb[i] - 128) + 0.587 * (prgb[i+1] - 128) + 0.114 * (prgb[i+2] - 128) + 128);
    /* u */
    *pyuv++ =CLIP(((- 0.147 * (prgb[i] - 128) - 0.289 * (prgb[i+1] - 128) + 0.436 * (prgb[i+2] - 128) + 128) +
          (- 0.147 * (prgb[i+3] - 128) - 0.289 * (prgb[i+4] - 128) + 0.436 * (prgb[i+5] - 128) + 128))/2);
    /* y1 */ 
    *pyuv++ =CLIP(0.299 * (prgb[i+3] - 128) + 0.587 * (prgb[i+4] - 128) + 0.114 * (prgb[i+5] - 128) + 128); 
    /* v*/
    *pyuv++ =CLIP(((0.615 * (prgb[i] - 128) - 0.515 * (prgb[i+1] - 128) - 0.100 * (prgb[i+2] - 128) + 128) +
          (0.615 * (prgb[i+3] - 128) - 0.515 * (prgb[i+4] - 128) - 0.100 * (prgb[i+5] - 128) + 128))/2);
  }
}

static void process_frame(int fd, struct jpeg_decompress_struct *cinfo)
{
  JSAMPLE rgb[FRAMEWIDTH * FRAMEHEIGHT * 3];
  JSAMPROW rows[FRAMEHEIGHT];
  unsigned char yuv[FRAMEWIDTH * FRAMEHEIGHT * 2];
  int i;
  ssize_t n;
  
  for (i=0; i<FRAMEHEIGHT; i++)
    rows[i] = rgb + 3*i*FRAMEWIDTH;

  jpeg_read_header(cinfo, TRUE);
  jpeg_start_decompress(cinfo);
  while (cinfo->output_scanline < cinfo->output_height)
    jpeg_read_scanlines(cinfo, rows + cinfo->output_scanline, FRAMEHEIGHT);
  jpeg_finish_decompress(cinfo);

  rgb2yuyv(uyvy, rgb, FRAMEWIDTH, FRAMEHEIGHT);
  n = write(fd, yuv, sizeof(yuv));
}

static void sig_handler(int sig)
{
  do_continue = 0;
  signal(sig, sig_handler);
}

static void gpcheck_x(int result, const char *file, int line, const char *func, const char *command)
{
  if (result>0 || result==GP_OK)
    return;

  const char *error_str = NULL;
  char tmp[200];
  switch (result)
  {
    case GP_ERROR_CORRUPTED_DATA:
      error_str = "Corrupted data";
      break;
    case GP_ERROR_FILE_EXISTS:
      error_str = "File exists";
      break;
    case GP_ERROR_MODEL_NOT_FOUND:
      error_str = "Model not found";
      break;
    case GP_ERROR_DIRECTORY_NOT_FOUND:
      error_str = "Directory not found";
      break;
    case GP_ERROR_CAMERA_BUSY:
      error_str = "Busy";
      break;
    case GP_ERROR_CANCEL:
      error_str = "Canceled";
      break;
    case GP_ERROR_CAMERA_ERROR:
      error_str = "Camera error";
      break;
    case GP_ERROR_OS_FAILURE:
      error_str = "OS failure";
      break;
    default:
      sprintf(tmp, "unknown(%d)", result);
      error_str = tmp;
  } 
  if (error_str)
  {
    fprintf(stderr, "ERROR @ %s:%d (%s)\n %s -> %s\n", file, line, func, command, error_str);
    exit(1);
  }
}

static void videodev_init(int fd)
{
  struct v4l2_capability vid_caps;
  struct v4l2_format vid_format;

  int ret_code = ioctl(fd, VIDIOC_QUERYCAP, &vid_caps);
  assert(ret_code != -1);

  memset(&vid_format, 0, sizeof(vid_format));

  vid_format.type                 = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  vid_format.fmt.pix.width        = FRAMEWIDTH;
  vid_format.fmt.pix.height       = FRAMEHEIGHT;
  vid_format.fmt.pix.field        = V4L2_FIELD_NONE;
  vid_format.fmt.pix.colorspace   = V4L2_COLORSPACE_JPEG;
  vid_format.fmt.pix.pixelformat  = V4L2_PIX_FMT_YUYV;
  vid_format.fmt.pix.bytesperline = FRAMEWIDTH * 2;
  vid_format.fmt.pix.sizeimage    = FRAMEWIDTH * FRAMEHEIGHT * 2;

  ret_code = ioctl(fd, VIDIOC_S_FMT, &vid_format);
  assert(ret_code != -1);
}
