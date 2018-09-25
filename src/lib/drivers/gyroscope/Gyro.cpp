/****************************************************************************
 *
 *   Copyright (c) 2018 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/


#include "Gyro.hpp"

Gyro::Gyro(const char *path, device::Device *interface, uint8_t dev_type) :
	CDev(path),
	_interface(interface)
{
}

Gyro::~Gyro()
{
	if (_topic != nullptr) {
		orb_unadvertise(_topic);
	}
}

int Gyro::init()
{
	CDev::init();

	gyro_report report{};

	if (_topic == nullptr) {
		_topic = orb_advertise_multi(ORB_ID(sensor_gyro), &report, &_orb_class_instance, ORB_PRIO_HIGH - 1);

		if (_topic == nullptr) {
			PX4_ERR("Advertise failed.");
			return PX4_ERROR;
		}
	}

	return PX4_OK;
}

int Gyro::ioctl(struct file *filp, int cmd, unsigned long arg)
{
	switch (cmd) {
	case GYROIOCSSCALE:
		// Copy scale in.
		memcpy(&_scale, (struct gyro_calibration_s *) arg, sizeof(_scale));
		return OK;

	case GYROIOCGSCALE:
		// Copy scale out.
		memcpy((struct gyro_calibration_s *) arg, &_scale, sizeof(_scale));
		return OK;

	case DEVIOCGDEVICEID:
		return (int)_interface->get_device_id();

	default:
		// Give it to the superclass.
		return CDev::ioctl(filp, cmd, arg);
	}
}

void Gyro::configure_filter(float sample_freq, float cutoff_freq)
{
	_filter_x.set_cutoff_frequency(sample_freq, cutoff_freq);
	_filter_y.set_cutoff_frequency(sample_freq, cutoff_freq);
	_filter_z.set_cutoff_frequency(sample_freq, cutoff_freq);
}

// @TODO: Use fixed point math to reclaim CPU usage.
int Gyro::publish(float x, float y, float z, float scale, Rotation rotation)
{
	sensor_gyro_s report{};

	report.device_id   = _interface->get_device_id();
	report.error_count = 0;
	report.scaling 	   = scale;
	report.timestamp   = hrt_absolute_time();

	// Raw values (ADC units 0 - 65535)
	report.x_raw = x;
	report.y_raw = y;
	report.z_raw = z;

	// Apply the rotation.
	rotate_3f(rotation, x, y, z);

	// Apply the scaling
	x *= scale;
	y *= scale;
	z *= scale;

	// Filtered values
	report.x = _filter_x.apply(x);
	report.y = _filter_y.apply(y);
	report.z = _filter_z.apply(z);

	// Integrated values
	matrix::Vector3f values(x, y, z);
	matrix::Vector3f integrated_value;

	bool gyro_notify = _integrator.put(report.timestamp, values, integrated_value, report.integral_dt);

	report.x_integral = integrated_value(0);
	report.y_integral = integrated_value(1);
	report.z_integral = integrated_value(2);

	if (gyro_notify) {
		poll_notify(POLLIN);
		orb_publish(ORB_ID(sensor_gyro), _topic, &report);
	}

	return PX4_OK;
}