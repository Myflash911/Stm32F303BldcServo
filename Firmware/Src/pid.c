/*
 	bldc-drive Cheap and simple brushless DC motor driver designed for CNC applications using STM32 microcontroller.
	Copyright (C) 2015 Pekka Roivainen

	Pid loop is based on F103ServoDrive project of Mihai.
	http://www.cnczone.com/forums/open-source-controller-boards/283428-cnc.html

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "stm32f3xx_hal.h"
#include "stm32f3xx_hal_gpio.h"

#include <stdlib.h>
#include "pid.h"
//#include "pwm.h"
//#include "hall.h"
//#include "encoder.h"
//#include "input.h"
#include "configuration.h"

struct PIDState pidState;

int32_t position_error;
int32_t pid_requested_position;
int32_t pid_last_requested_position;
int32_t pid_last_requested_position_delta;
volatile int32_t pid_integrated_error;
int32_t pid_prev_position_error;
uint32_t max_error; //statistics

int motor_running = 1;
int dir = 0;

volatile uint16_t duty;

void initPid()
{
	pid_requested_position = (int)TIM4->CNT;
	pid_integrated_error = 0;
	pid_prev_position_error =0;

	s.pid_Kp = 100;
	s.pid_Ki = 0;
	s.pid_Kd = 0;
	s.pid_FF1 = 0;
	s.pid_FF2 = 0;
	s.max_error = 600;

/*
	//TIM3 used for pid loop timing.
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_TimeBaseStructure.TIM_Prescaler = 72; //1MHz counter
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period = PID_TIM_PERIOD; //4kHz update interval
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
	TIM_Cmd(TIM3, ENABLE);

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);*/

}

//This is called from TIM3 update interrupt. defined in input.c
bool updatePid()
{
	uint32_t abs_position_error;
	int32_t position_delta;
	int32_t position_delta_delta;
	//getEncoderCount(); //this is dangerous to call from interrupt
	if (!motor_running) {
		pid_integrated_error = 0;
		return false;
	}

	position_error = ((short)(TIM4->CNT)) - pid_requested_position;

	double outp = 0;
	outp = 1.0f * (double)position_error;
	outp *= 15.45f;

	position_delta= pid_requested_position - pid_last_requested_position;
	position_delta_delta = position_delta - pid_last_requested_position_delta;

	pid_last_requested_position = pid_requested_position;
	pid_last_requested_position_delta = position_delta;

	abs_position_error = abs(position_error);
	if (abs_position_error < s.pid_deadband) {
		position_error = 0;
	} else if (position_error > 0) {
		position_error -= s.pid_deadband;
	} else {
		position_error += s.pid_deadband;
	}

	if (abs_position_error > max_error) {
		max_error = abs_position_error;
	}

	if (abs_position_error > s.max_error) {
//	      pwm_motorStop();
//	      ERROR_LED_ON;
		return false;
	}

//P
	int32_t output = position_error * s.pid_Kp;

//I
	pid_integrated_error += position_error * s.pid_Ki;
	if (pid_integrated_error > MAX_INTEGRATION_ERROR) {
		pid_integrated_error = MAX_INTEGRATION_ERROR;
	} else if (pid_integrated_error < -MAX_INTEGRATION_ERROR) {
		pid_integrated_error = -MAX_INTEGRATION_ERROR;
	}
	output += (int)((float)pid_integrated_error * 0.05f);

//D
	output += (position_error - pid_prev_position_error) * s.pid_Kd;
	pid_prev_position_error = position_error;


//FF1
	output += position_delta * s.pid_FF1;
//FF2
	output += position_delta_delta * s.pid_FF2;

	output /= 10; //provide larger dynamic range for pid. (without this, having pid_Ki = 1 was enough for oscillation.

//	output = position_error;

    //limit output power
	if (output > MAX_DUTY) {
		output = MAX_DUTY;
	} else if (output < -MAX_DUTY) {
		output = -MAX_DUTY;
	}

	#ifdef PWM_SIGN_MAGNITUDE_MODE

		if (output > 0) {
			dir = 0;
		} else {
			dir = 1;
		}
		duty = abs(output);

	#else
	#endif

	pidState.dir = dir;
	pidState.duty = duty;

//	pwm_setDutyCycle();

	return true;
}
