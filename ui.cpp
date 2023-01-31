#include <string.h>
#include "ui.h"

////////////////////////////////////////////////////////////////////////////////
// Encoder 
////////////////////////////////////////////////////////////////////////////////

void ui::setup_encoder()
{
    uint offset = pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, offset, PIN_AB, 1000);
    new_position = (quadrature_encoder_get_count(pio, sm) + 2)/4;
    old_position = new_position;
}

int32_t ui::get_encoder_change()
{
    new_position = (quadrature_encoder_get_count(pio, sm) + 2)/4;
    int32_t delta = new_position - old_position;
    old_position = new_position;
    return delta;
}

int32_t ui::encoder_control(int32_t *value, int32_t min, int32_t max)
{
	int32_t position_change = get_encoder_change();
	*value += position_change;
	if(*value > max) *value = min;
	if(*value < min) *value = max;
	return position_change;
}

////////////////////////////////////////////////////////////////////////////////
// Buttons 
////////////////////////////////////////////////////////////////////////////////

void ui::setup_buttons()
{
  gpio_init(PIN_MENU);
  gpio_set_dir(PIN_MENU, GPIO_IN);
  gpio_pull_up(PIN_MENU);
  gpio_init(PIN_BACK);
  gpio_set_dir(PIN_BACK, GPIO_IN);
  gpio_pull_up(PIN_BACK);
}

bool ui::get_button(uint8_t button){
	if(!gpio_get(button)){
		while(!gpio_get(button)){}
		WAIT_10MS
		return 1;
	}
	WAIT_10MS
	return 0;
}

bool ui::check_button(unsigned button){
	return !gpio_get(button);
}

////////////////////////////////////////////////////////////////////////////////
// Display
////////////////////////////////////////////////////////////////////////////////
void ui::setup_display() {
  i2c_init(i2c1, 400000);
  gpio_set_function(PIN_DISPLAY_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_DISPLAY_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_DISPLAY_SDA);
  gpio_pull_up(PIN_DISPLAY_SCL);
  disp.external_vcc=false;
  ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
}

void ui::display_clear()
{
  cursor_x = 0;
  cursor_y = 0;
  ssd1306_clear(&disp);
}

void ui::display_line1()
{
  cursor_y = 0;
  cursor_x = 0;
}

void ui::display_line2()
{
  cursor_y = 1;
  cursor_x = 0;
}

void ui::display_write(char x)
{
  ssd1306_draw_char(&disp, cursor_x*6, cursor_y*8, 1, x);
  cursor_x++;
}

void ui::display_print(const char str[])
{
  ssd1306_draw_string(&disp, cursor_x*6, cursor_y*8, 1, str);
  cursor_x+=strlen(str);
}

void ui::display_print_num(const char format[], int16_t num)
{
  char buff[16];
  snprintf(buff, 16, format, num);
  ssd1306_draw_string(&disp, cursor_x*6, cursor_y*8, 1, buff);
  cursor_x+=strlen(buff);
}

void ui::display_show()
{
  ssd1306_show(&disp);
}

////////////////////////////////////////////////////////////////////////////////
// Generic Menu Options
////////////////////////////////////////////////////////////////////////////////
float ui::calculate_signal_strength(rx_status &status)
{
    const float full_scale_rms_mW = (0.5f * 0.707f * 1000.0f * 3.3f * 3.3f) / 50.0f;
    const float full_scale_dBm = 10.0f * log10(full_scale_rms_mW);
    const float signal_strength_dBFS = 20.0*log10((float)status.signal_amplitude / (8.0f * 2048.0f));//compared to adc_full_scale
    return full_scale_dBm - 60.0f + signal_strength_dBFS;
}

void ui::update_display(rx_status & status, rx & receiver)
{
  char buff [16];
  ssd1306_clear(&disp);

  //frequency
  uint32_t remainder, MHz, kHz, Hz;
  MHz = (uint32_t)settings[idx_frequency]/1000000u;
  remainder = (uint32_t)settings[idx_frequency]%1000000u; 
  kHz = remainder/1000u;
  remainder = remainder%1000u; 
  Hz = remainder;
  snprintf(buff, 16, "%2u.%03u", MHz, kHz);
  ssd1306_draw_string(&disp, 0, 0, 2, buff);
  snprintf(buff, 16, ".%03u", Hz);
  ssd1306_draw_string(&disp, 72, 0, 1, buff);

  //signal strength
  const float power = calculate_signal_strength(status);

  //CPU 
  const float block_time = (float)adc_block_size/(float)adc_sample_rate;
  const float busy_time = ((float)status.busy_time*1e-6f);

  //mode
  const char modes[][4]  = {" AM", "LSB", "USB", " FM", " CW"};
  ssd1306_draw_string(&disp, 102, 0, 1, modes[settings[idx_mode]]);
  //step
  const char steps[][8]  = {"   10Hz", "   50Hz", "  100Hz", "   1kHz", "   5kHz", "  10kHz", "12.5kHz", "  25kHz", "  50kHz", " 100kHz"};
  ssd1306_draw_string(&disp, 72, 8, 1, steps[settings[idx_step]]);
  //signal strength/cpu
  snprintf(buff, 16, "%2.0fdBm %2.0f%%", power, (100.0f*busy_time)/block_time);
  ssd1306_draw_string(&disp, 0, 16, 1, buff);

  //Display spectrum capture
  int16_t spectrum[128];
  int16_t offset;
  receiver.get_spectrum(spectrum, offset);
  ssd1306_draw_string(&disp, offset, 63-8, 1, "^");
  for(uint16_t x=0; x<128; x++)
  {
      int16_t y = (90-20.0*log10(spectrum[x]))/3;
      ssd1306_draw_pixel(&disp, x, y+32);
  }

  ssd1306_show(&disp);

}

////////////////////////////////////////////////////////////////////////////////
// Generic Menu Options
////////////////////////////////////////////////////////////////////////////////

void ui::print_option(const char options[], uint8_t option){
    char x;
    uint8_t i, idx=0;

    //find nth substring
    for(i=0; i<option; i++){ 
      while(options[idx++]!='#'){}
    }

    //print substring
    while(1){
      x = options[idx];
      if(x==0 || x=='#') return;
      display_write(x);
      idx++;
    }
}

//choose from an enumerate list of settings
uint32_t ui::enumerate_entry(const char title[], const char options[], uint32_t max, uint32_t *value)
{
  int32_t select=*value;
  bool draw_once = true;
  while(1){
    if(encoder_control(&select, 0, max)!=0 || draw_once)
    {
      //print selected menu item
      draw_once = false;
      display_clear();
      display_print(title);
      display_line2();
      print_option(options, select);
      display_show();
    }

    //select menu item
    if(get_button(PIN_MENU)){
      *value = select;
      return 1;
    }

    //cancel
    if(get_button(PIN_BACK)){
      return 0;
    }

    WAIT_100MS
  }
}

//select a number in a range
int16_t ui::number_entry(const char title[], const char format[], int16_t min, int16_t max, int16_t multiple, uint32_t *value)
{
  int32_t select=*value;

  bool draw_once = true;
  while(1){
    if(encoder_control(&select, min, max)!=0 || draw_once)
    {
      //print selected menu item
      draw_once = false;
      display_clear();
      display_print(title);
      display_line2();
      display_print_num(format, select*multiple);
      display_show();
    }

    //select menu item
    if(get_button(PIN_MENU)){
      *value = select*multiple;
      return 1;
    }

    //cancel
    if(get_button(PIN_BACK)){
      return 0;
    }

    WAIT_100MS
  }
}

////////////////////////////////////////////////////////////////////////////////
// Frequency menu item (digit by digit)
////////////////////////////////////////////////////////////////////////////////
bool ui::frequency_entry(){

  int32_t digit=0;
  int32_t digits[8];
  int32_t i, digit_val;
  int32_t edit_mode = 0;
  unsigned frequency;

  //convert to binary representation
  frequency = settings[idx_frequency];
  digit_val = 10000000;
  for(i=0; i<8; i++){
      digits[i] = frequency / digit_val;
      frequency %= digit_val;
      digit_val /= 10;
  }

  bool draw_once = true;
  while(1){

    bool encoder_changed;
    if(edit_mode){
      //change the value of a digit 
      encoder_changed = encoder_control(&digits[digit], 0, 9);
    } 
    else 
    {
      //change between digits
      encoder_changed = encoder_control(&digit, 0, 9);
    }

    //if encoder changes, or screen hasn't been updated
    if(encoder_changed || draw_once)
    {
      draw_once = false;
      display_clear();

      //write frequency to lcd
      display_line1();
      for(i=0; i<8; i++)
      {
        display_write(digits[i] + '0');
        if(i==1||i==4) display_write('.');
      }
      display_print(" Y N");

      //print cursor
      display_line2();
      for(i=0; i<16; i++)
      {
        if(i==digit)
        {
          if(edit_mode)
          {
            display_write('X');
          } 
          else 
          {
            display_write('^');
          }
        } 
        else 
        {
          display_write(' ');
        }
        if(i==1||i==4||i==7|i==8) display_write(' ');
      }
      display_show();
    }

    //select menu item
    if(get_button(PIN_MENU))
    {
      draw_once = true;
	    edit_mode = !edit_mode;
	    if(digit==8) //Yes
      {
	      digit_val = 10000000;

        //convert back to a binary representation
        settings[idx_frequency] = 0;
        for(i=0; i<8; i++)
        {
	        settings[idx_frequency] += (digits[i] * digit_val);
		      digit_val /= 10;
		    }
		    return true;
	    }
	    if(digit==9) return 0; //No
	  }

    //cancel
    if(get_button(PIN_BACK))
    {
      return false;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// This is the main UI loop. Should get called about 100 times/second
////////////////////////////////////////////////////////////////////////////////
bool ui::do_ui(bool rx_settings_changed)
{

    //update frequency if encoder changes
    uint32_t encoder_change = get_encoder_change();
    if(encoder_change != 0)
    {
      rx_settings_changed = true;
      settings[idx_frequency] += encoder_change * step_sizes[settings[idx_step]];
    }

    //if button is pressed enter menu
    else if(check_button(PIN_MENU))
    {
      get_button(PIN_MENU);

      //top level menu
      uint32_t setting = 0;
      if(!enumerate_entry("menu:", "Frequency#Volume#Mode#AGC Speed#Squelch#Frequency Step#CW Sidetone Frequency#USB Programming Mode#", 7, &setting)) return 1;

      switch(setting)
      {
        case 0 : 
          rx_settings_changed = frequency_entry();
          break;

        case 1 : 
          rx_settings_changed = number_entry("Volume", "%i", 0, 9, 1, &settings[idx_volume]);
          break;

        case 2 : 
          rx_settings_changed = enumerate_entry("Mode", "AM#LSB#USB#FM#CW", 4, &settings[idx_mode]);
          break;

        case 3 :
          rx_settings_changed = enumerate_entry("AGC Speed", "fast#normal#slow#very slow#", 3, &settings[idx_agc_speed]);
          break;

        case 4 :
          rx_settings_changed = enumerate_entry("Squelch", "S0#S1#S2#S3#S4#S5#S6#S7#S8#S9#S9+10dB#S9+20dB#S9+30dB", 13, &settings[idx_squelch]);
          break;

        case 5 : 
          rx_settings_changed = enumerate_entry("Frequency Step", "10Hz#50Hz#100Hz#1kHz#5kHz#10kHz#12.5kHz#25kHz#50kHz#100kHz#", 9, &settings[idx_step]);
          settings[idx_frequency] -= settings[idx_frequency]%step_sizes[settings[idx_step]];
          break;

        case 6 : 
          rx_settings_changed = number_entry("CW Sidetone Frequency", "%iHz", 1, 30, 100, &settings[idx_cw_sidetone]);
          break;

        case 7 : 
          uint32_t programming_mode = 0;
          enumerate_entry("USB Programming Mode", "No#Yes#", 1, &programming_mode);
          if(programming_mode)
          {
            reset_usb_boot(0,0);
          }
          break;
      }
    }

    if(rx_settings_changed)
    {
      settings_to_apply.tuned_frequency_Hz = settings[idx_frequency];
      settings_to_apply.agc_speed = settings[idx_agc_speed];
      settings_to_apply.mode = settings[idx_mode];
      settings_to_apply.step_Hz = step_sizes[settings[idx_step]];
      settings_to_apply.cw_sidetone_Hz = settings[idx_cw_sidetone];
    }

    update_display(status, receiver);

    return rx_settings_changed;

}

ui::ui(rx_settings & settings_to_apply, rx_status & status, rx &receiver) : settings_to_apply(settings_to_apply), status(status), receiver(receiver)
{
  setup_display();
  setup_encoder();
  setup_buttons();

  //Apply default settings
  settings[idx_frequency] = 198e3;
  settings[idx_agc_speed] = 3;
  settings[idx_mode] = AM;
  settings[idx_step] = 3;
  settings[idx_cw_sidetone] = 1000;
}