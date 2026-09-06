/**
 ***************************************************************************************************
 * ESP32P4_BUS_ANALYZER — 总线通信分析仪
 * 平台：慧勤智远 ESP32-P4 CB V3.2
 *
 * 总线：
 *   CAN    TJA1050      TX33 / RX34           ≤1 Mbps
 *   RS485  TP8485E-SR   RX30 / TX31 / DE32    ≤250 kbps
 *   UART2  TTL          TX9  / RX10
 *   I2C1                SDA7 / SCL8
 *   SPI                 SCLK26 MOSI27 MISO6 CS12
 *   PWM                 GPIO11（LEDC 可调频）
 *   ADC                 GPIO20（ADC1_CH4，0~3.3V）
 *
 * 其它：LVGL 3.5″ 触摸屏、SD 录制 CSV、WiFi SoftAP 上位机 BUS1 UDP:9527
 * 已移除：FOC 电机、片内以太网
 *
 ***************************************************************************************************
 * 构建
 *   idf.py set-target esp32p4
 *   idf.py build
 *   idf.py -p COMx flash monitor
 *
 * WiFi AP 默认：ESP32P4-BUS / 12345678 → 192.168.4.1:9527
 * 上位机：python tools/bus1_listen.py 192.168.4.1
 ***************************************************************************************************
 */
