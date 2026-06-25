/**
 ******************************************************************************
 * @file     LVGL_Arduino.ino
 * @author   Yongqin Ou
 * @version  V1.0
 * @date     2024-10-30
 * @brief    Setup experiment for multiple modules
 * @license  MIT
 * @copyright Copyright (c) 2024, Waveshare
 ******************************************************************************
 * 
 * Experiment Objective: Learn how to set up and use multiple modules including SD card, display, LVGL, wireless, and RGB lamp.
 *
 * Hardware Resources and Pin Assignment: 
 * 1. SD Card Interface --> As configured in SD_Card.h.
 * 2. Display Interface --> As configured in Display_ST7789.h.
 * 3. Wireless Module Interface --> As configured in Wireless.h.
 * 4. RGB Lamp Interface --> As configured in RGB_lamp.h.
 *
 * Experiment Phenomenon:
 * 1. Runs various tests and initializations for different modules.
 * 2. Displays LVGL examples on the display.
 * 3. Continuously runs loops for timer, RGB lamp, and other tasks.
 * 
 * Notes:
 * None
 * 
 ******************************************************************************
 * 
 * Development Platform: ESP32
 * Support Forum: service.waveshare.com
 * Company Website: www.waveshare.com
 *
 ******************************************************************************
 */
#include "SD_Card.h"
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "LVGL_Example.h"
#include "Wireless.h"
#include "RGB_lamp.h"
#include "I2C_Driver.h"
#include "Gyro_QMI8658.h"
#include "Button_Driver.h"
#include "BAT_Driver.h"

void Driver_Loop(void *parameter)
{
  Wireless_Test2();
  while(1)
  {
    QMI8658_Loop();
    BAT_Get_Volts();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
void setup()
{
  Flash_test();
  BAT_Init();
  Button_Init();
  I2C_Init();
  QMI8658_Init(); 
  RGB_Loop(5);
  SD_Init();         
  LCD_Init();
  Set_Backlight(90);
  Lvgl_Init();

  Lvgl_Example1();     
  // lv_demo_widgets();               
  // lv_demo_benchmark();          
  // lv_demo_keypad_encoder();     
  // lv_demo_music();  
  // lv_demo_stress();   

  Simulated_Touch_Init();
  xTaskCreatePinnedToCore(
    Driver_Loop,     
    "Other Driver task",   
    2048,                
    NULL,                 
    3,                    
    NULL,                
    0                    
  );
}

void loop()
{
  Timer_Loop();
  delay(5);
}
