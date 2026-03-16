/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2011 Sorin P. <sorin@hypermagik.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>

#define LOG_TAG "Sensors"
#define LOG_NDEBUG 0

#include <cutils/log.h>

#include "GtpPSensor.h"
#include "sensors.h"

#define LOGD ALOGE
#define LOGE ALOGE
#define EVENT_TYPE_PROXIMITY   ABS_DISTANCE
#define PS_INPUT_DEV_NAME  "goodix_proximity"

#define SYSFS_PATH_ENABLE  "/sys/goodix/proximity/enable"
#define SYSFS_PATH_STATE   "/sys/goodix/proximity/state"
/*****************************************************************************/

GtpPSensor::GtpPSensor()
  : SensorBase(NULL, PS_INPUT_DEV_NAME),
    mEnabled(0),
    mPendingMask(0),
    mInputReader(32)
{
	LOGD("GTPSensor init.");
	memset((void *)&mPendingEvents, 0, sizeof(mPendingEvents));
	
	mPendingEvents.version = sizeof(sensors_event_t);
	mPendingEvents.sensor = ID_P;
	mPendingEvents.type = SENSOR_TYPE_PROXIMITY;
	
	mHasPendingEvent = false;
	sprintf(mSysFsPath_Enable, "%s", SYSFS_PATH_ENABLE);	
	sprintf(mSysFsPath_State, "%s", SYSFS_PATH_STATE);	
}

GtpPSensor::~GtpPSensor()
{
}

bool GtpPSensor::hasPendingEvents() const
{
	LOGD("GTPSensor: hasPendingEvent=%s", mHasPendingEvent? "True" : "False");
	return mHasPendingEvent;
}

int GtpPSensor::setDelay(int32_t handle, int64_t ns)
{
	LOGD("GTPSensor: setDelay %ld", (long)ns);
	if(handle != ID_P)
	{
		LOGE("GTPSensor: handle not equal ID_P, handle=%d, ID_P=%d", handle, ID_P);
		return -EINVAL;
	}
	return 0;
}

int GtpPSensor::setEnable(int32_t handle, int enabled)
{
	int err = 0;
	
	LOGD("GtpPSensor: setEnable=%s , previous=%s",
		enabled? "Enabled" : "Disabled", mEnabled? "Enabled" : "Disabled");
	
	if(handle != ID_P)
	{
		LOGE("GTPSensor: handle not equal ID_P, handle=%d, ID_P=%d", handle, ID_P);
		return -EINVAL;
	}

	if (!mEnabled && enabled) 
	{
		err = write_sys_attribute(mSysFsPath_Enable, "1\n", 2);
	} 
	else if (mEnabled && !enabled) 
	{
		err = write_sys_attribute(mSysFsPath_Enable, "0\n", 2);
	}
	
	if (err != 0) 
	{
        	LOGE("GtpPSensor: write sysfs failed (%s)", strerror(errno));
        	return err;
	}

	mEnabled = enabled;
	return err;
}

int GtpPSensor::getEnable(int32_t handle)
{
	LOGD("GtpPSensor: getEnable, state=%s", mEnabled? "Enabled" : "Disabled");
	if(handle != ID_P)
	{
		LOGE("GTPSensor: handle not equal ID_P, handle=%d, ID_P=%d", handle, ID_P);
		return -EINVAL;
	}
	return mEnabled;
}

int GtpPSensor::readEvents(sensors_event_t* data, int count)
{
	
	LOGD("GTPSensor: readEvents, count=%d", count);
	if (count < 1)
        	return -EINVAL;

	ssize_t n = mInputReader.fill(data_fd);
	if (n < 0)
		return n;

	int numEventReceived = 0;
	input_event const* event;

	while (count && mInputReader.readEvent(&event))
	{
		int type = event->type;
		if (type == EV_ABS) {
	        	if(event->code == EVENT_TYPE_PROXIMITY)
			{
            			mPendingEvents.distance = event->value;
            			LOGD("GtpPSensor: mPendingEvents.distance = %f", mPendingEvents.distance);
         		}
			else
			{
            			LOGE("GtpPSensor: unknown event (code=%d value=%d)", event->code, event->value);
			}
		} else if (type == EV_SYN) {
			int64_t time = timevalToNano(event->time);
			mPendingEvents.timestamp = time;
			if(mEnabled)
			{
				*data++ = mPendingEvents;
				count--;
				numEventReceived++;
				LOGD("GTPSensor: report event, value = %f", mPendingEvents.distance);
			}
			else
			{
				LOGE("GTPSensor: sensor is disabled, drop event!");
			}
		}
		else 
		{
			LOGE("GTPSensor: unknown event (type=%d, code=%d)", type, event->code);
		}
		mInputReader.next();
	}

	return numEventReceived;
}


