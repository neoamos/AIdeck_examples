#include "bsp/camera/himax.h"
#include "bsp/camera/mt9v034.h"
#include "bsp/transport/nina_w10.h"
#include "tools/frame_streamer.h"
#include "stdio.h"

#if defined(CONFIG_GAPUINO) || defined(CONFIG_AI_DECK)
#define CAM_WIDTH    324
#define CAM_HEIGHT   244
#else
#define CAM_WIDTH    320
#define CAM_HEIGHT   240
#endif

static pi_task_t task1;
static pi_task_t task2;
static unsigned char *imgBuff0;
static unsigned char *imgBuff1;
static struct pi_device camera;
static struct pi_device wifi;
static frame_streamer_t *streamer1;
static frame_streamer_t *streamer2;
static pi_buffer_t buffer;
static pi_buffer_t buffer2;
static volatile int stream1_done;
static volatile int stream2_done;

static uint8_t counter = 0;

static void streamer_handler(void *arg);


static void cam_handler(void *arg)
{
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);

  stream1_done = 0;
  stream2_done = 0;

  frame_streamer_send_async(streamer1, &buffer, pi_task_callback(&task1, streamer_handler, (void *)&stream1_done));

  return;
}

static void update_exposure(void *arg){
  pi_camera_capture_async(&camera, imgBuff0, CAM_WIDTH*CAM_HEIGHT, pi_task_callback(&task1, cam_handler, NULL));
}

static void streamer_handler(void *arg)
{
  *(int *)arg = 1;
  if (stream1_done) // && stream2_done)
  {
    counter++;
    if(counter % 5 == 0){
      // Read 324x324 from the camera to force it to update the exposure
      pi_camera_capture_async(&camera, imgBuff0, 324*324, pi_task_callback(&task1, update_exposure, NULL));
    }else{
      pi_camera_capture_async(&camera, imgBuff0, CAM_WIDTH*CAM_HEIGHT, pi_task_callback(&task1, cam_handler, NULL));
    }
    pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  }
}



static int open_pi_camera_himax(struct pi_device *device)
{
  struct pi_himax_conf cam_conf;

  pi_himax_conf_init(&cam_conf);

  cam_conf.format = PI_CAMERA_QVGA;

  pi_open_from_conf(device, &cam_conf);
  if (pi_camera_open(device))
    return -1;


    // rotate image
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  uint8_t set_value=3;
  uint8_t reg_value;
  pi_camera_reg_set(&camera, IMG_ORIENTATION, &set_value);
  pi_time_wait_us(1000000);
  pi_camera_reg_get(&camera, IMG_ORIENTATION, &reg_value);
  if (set_value!=reg_value)
  {
    printf("Failed to rotate camera image\n");
    return -1;
  }
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  
  pi_camera_control(device, PI_CAMERA_CMD_AEG_INIT, 0);

  return 0;
}



static int open_pi_camera_mt9v034(struct pi_device *device)
{
  struct pi_mt9v034_conf cam_conf;

  pi_mt9v034_conf_init(&cam_conf);

  cam_conf.format = PI_CAMERA_QVGA;

  pi_open_from_conf(device, &cam_conf);
  if (pi_camera_open(device))
    return -1;


  
  return 0;
}



static int open_camera(struct pi_device *device)
{
#ifdef CONFIG_GAPOC_A
  return open_pi_camera_mt9v034(device);
#endif
#if defined(CONFIG_GAPUINO) || defined(CONFIG_AI_DECK)
  return open_pi_camera_himax(device);
#endif
  return -1;
}


static int open_wifi(struct pi_device *device)
{
  struct pi_nina_w10_conf nina_conf;

  pi_nina_w10_conf_init(&nina_conf);

  nina_conf.ssid = "";
  nina_conf.passwd = "";
  nina_conf.ip_addr = "0.0.0.0";
  nina_conf.port = 5555;
  pi_open_from_conf(device, &nina_conf);
  if (pi_transport_open(device))
    return -1;

  return 0;
}


static frame_streamer_t *open_streamer(char *name)
{
  struct frame_streamer_conf frame_streamer_conf;

  frame_streamer_conf_init(&frame_streamer_conf);

  frame_streamer_conf.transport = &wifi;
  frame_streamer_conf.format = FRAME_STREAMER_FORMAT_JPEG;
  frame_streamer_conf.width = CAM_WIDTH;
  frame_streamer_conf.height = CAM_HEIGHT;
  frame_streamer_conf.depth = 1;
  frame_streamer_conf.name = name;

  return frame_streamer_open(&frame_streamer_conf);
}
static pi_task_t led_task;
static int led_val = 0;
static struct pi_device gpio_device;
static void led_handle(void *arg)
{
  pi_gpio_pin_write(&gpio_device, 2, led_val);
  led_val ^= 1;
  pi_task_push_delayed_us(pi_task_callback(&led_task, led_handle, NULL), 500000);
}

int main()
{
  printf("Entering main controller...\n");

  pi_freq_set(PI_FREQ_DOMAIN_FC, 150000000);

  pi_gpio_pin_configure(&gpio_device, 2, PI_GPIO_OUTPUT);

  pi_task_push_delayed_us(pi_task_callback(&led_task, led_handle, NULL), 500000);

  imgBuff0 = (unsigned char *)pmsis_l2_malloc((324*324)*sizeof(unsigned char));
  if (imgBuff0 == NULL) {
      printf("Failed to allocate Memory for Image \n");
      return 1;
  }
  printf("Allocated Memory for Image\n");

  if (open_camera(&camera))
  {
    printf("Failed to open camera\n");
    return -1;
  }
  printf("Opened Camera\n");



  if (open_wifi(&wifi))
  {
    printf("Failed to open wifi\n");
    return -1;
  }
  printf("Opened WIFI\n");



  streamer1 = open_streamer("camera");
  if (streamer1 == NULL)
    return -1;

  printf("Opened streamer\n");

  pi_buffer_init(&buffer, PI_BUFFER_TYPE_L2, imgBuff0);
  pi_buffer_set_format(&buffer, CAM_WIDTH, CAM_HEIGHT, 1, PI_BUFFER_FORMAT_GRAY);

  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  pi_camera_capture_async(&camera, imgBuff0, CAM_WIDTH*CAM_HEIGHT, pi_task_callback(&task1, cam_handler, NULL));
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  printf("6\n");

  while(1)
  {
    pi_yield();
  }

  return 0;
}
