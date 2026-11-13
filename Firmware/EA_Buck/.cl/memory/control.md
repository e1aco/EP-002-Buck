# 电压/电流双环控制参数

CTRL_DUTY_MAX = 0.80  @你 PWM≤80%  #你  2026-11-13
CTRL_CCR_PER_DUTY = 3600  @memory TIM1_ARR=3599  #推导  2026-11-13
CTRL_LOOP_HZ = 10000 = ADC 10kHz/1  @memory TIM4_CC4_FREQ=10kHz  #推导  2026-11-13
CTRL_ADC_STEP_US = 100 = 1/10kHz  @memory TIM4_CC4_FREQ=10kHz  #推导  2026-11-13
CTRL_START_KP = 0.002 duty/V  @memory LC=220uH/220uF  #推导  2026-11-13
CTRL_START_KI = 10.0 duty/(V·s)  @memory CTRL_START_KP=0.002 fc=38Hz  #推导  2026-11-13
CTRL_VOLT_KP = 0.050 A/V  @memory C=220uF fc=50Hz  #推导  2026-11-13
CTRL_VOLT_KI = 15.0 A/(V·s)  @memory CTRL_VOLT_KP=0.050  #推导  2026-11-13
CTRL_CURR_KP = 0.020 duty/A  @memory L=220uH Vin=24V fc=500Hz  #推导  2026-11-13
CTRL_CURR_KI = 64.0 duty/(A·s)  @memory CTRL_CURR_KP=0.020  #推导  2026-11-13
CTRL_ILIMIT_A = 3.0  @你 3A恒流限幅  #你  2026-11-13
CTRL_VREF_MIN_MV = 3300  @你  #你  2026-11-13
CTRL_VREF_MAX_MV = 12000  @你  #你  2026-11-13
CTRL_VREF_DEFAULT_MV = 3300  @你 上电默认3.3V  #你  2026-11-13
CTRL_VREF_SLEW = 240mV/ms  @memory CTRL_VREF_MAX_MV=12000  #推导  2026-11-13
CTRL_OC_TRIP_MA = 3300  @memory CTRL_ILIMIT_A=3.0  #推导  2026-11-13
CTRL_OC_COUNT = 3  @推导 10kHz×3=300us  #推导  2026-11-13
CTRL_OVP_MV = 13500  @推导 12V+12.5%=13.5V  #推导  2026-11-13
CTRL_OVP_COUNT = 3  @memory CTRL_OC_COUNT=3  #推导  2026-11-13
CTRL_CURRENT_VALID_DUTY = 0.10  @实测 8.3%时I=0/V=10  #实测  2026-11-13
ADC_V_SCALE = 4.4233mV/码  @Firmware/Buck bsp_adc_scale.h  #实测  2026-11-04
ADC_I_SCALE = 1.6589mA/码  @Firmware/Buck bsp_adc_scale.h  #实测  2026-11-04
