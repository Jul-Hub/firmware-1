#include <stdint.h>
#include <stdbool.h>

#include <breezystm32/breezystm32.h>

#include "param.h"
#include "rc.h"
#include "mux.h"

#include "mavlink_util.h"
#include "mavlink_log.h"

void init_rc()
{
  _rc_control.x.type = ANGLE;
  _rc_control.y.type = ANGLE;
  _rc_control.z.type = RATE;
  _rc_control.F.type = THROTTLE;

  _rc_control.x.value = 0;
  _rc_control.y.value = 0;
  _rc_control.z.value = 0;
  _rc_control.F.value = 0;

  _offboard_control.x.active = false;
  _offboard_control.y.active = false;
  _offboard_control.z.active = false;
  _offboard_control.F.active = false;
}

static void convertPWMtoRad()
{
  // Get Roll control command out of RC
  if (_rc_control.x.type == ANGLE)
  {
    _rc_control.x.value = (float)((pwmRead(_params.values[PARAM_RC_X_CHANNEL]) - _params.values[PARAM_RC_X_CENTER])
                           *2.0f*get_param_float(PARAM_RC_MAX_ROLL))/(float)_params.values[PARAM_RC_X_RANGE];
  }
  else if (_rc_control.x.type == RATE)
  {
    _rc_control.x.value = (float)((pwmRead(_params.values[PARAM_RC_X_CHANNEL]) - _params.values[PARAM_RC_X_CENTER])
                            *2.0f*get_param_float(PARAM_RC_MAX_ROLLRATE))/(float)_params.values[PARAM_RC_X_RANGE];
  }
  else if (_rc_control.x.type == PASSTHROUGH)
  {
    _rc_control.x.value = pwmRead(_params.values[PARAM_RC_X_CHANNEL]) - _params.values[PARAM_RC_X_CENTER];
  }

  // Get Pitch control command out of RC
  if (_rc_control.y.type == ANGLE)
  {
    _rc_control.y.value = ((pwmRead(_params.values[PARAM_RC_Y_CHANNEL]) - _params.values[PARAM_RC_Y_CENTER])
                            *2.0f*get_param_float(PARAM_RC_MAX_PITCH))/(float)_params.values[PARAM_RC_Y_RANGE];
  }
  else if (_rc_control.y.type == RATE)
  {
    _rc_control.y.value = (float)((pwmRead(_params.values[PARAM_RC_Y_CHANNEL]) - _params.values[PARAM_RC_Y_CENTER])
                            *2.0f*get_param_float(PARAM_RC_MAX_PITCHRATE))/(float)_params.values[PARAM_RC_Y_RANGE];
  }
  else if (_rc_control.y.type == PASSTHROUGH)
  {
    _rc_control.y.value = pwmRead(_params.values[PARAM_RC_Y_CHANNEL]) - _params.values[PARAM_RC_Y_CENTER];
  }

  // Get the Yaw control command type out of RC
  if (_rc_control.z.type == RATE)
  {
    _rc_control.z.value = ((pwmRead(_params.values[PARAM_RC_Z_CHANNEL]) - _params.values[PARAM_RC_Z_CENTER])
                           *2.0f*get_param_float(PARAM_RC_MAX_YAWRATE))/(float)_params.values[PARAM_RC_Z_RANGE];
  }
  else if (_rc_control.z.type == PASSTHROUGH)
  {
    _rc_control.z.value = pwmRead(_params.values[PARAM_RC_Z_CHANNEL]) - _params.values[PARAM_RC_Z_CENTER];
  }

  // Finally, the throttle command
  _rc_control.F.value = (float)((pwmRead(_params.values[PARAM_RC_F_CHANNEL]) - _params.values[PARAM_RC_F_BOTTOM]) * 1000.0f)
                        / (float)_params.values[PARAM_RC_F_RANGE];
}


bool receive_rc(uint32_t now)
{
  // if it has been more than 20ms then look for new RC values and parse them
  static uint32_t last_rc_receive_time = 0;
  static uint32_t time_of_last_stick_deviation = 0;

  if (now - last_rc_receive_time < 20000)
  {
    return false;
  }
  last_rc_receive_time = now;
  // Get timestamp for deadband control lag


  // Figure out the desired control type from the switches and params
  if (_params.values[PARAM_FIXED_WING])
  {
    // for using fixedwings
    _rc_control.x.type = _rc_control.y.type = _rc_control.z.type = PASSTHROUGH;
    _rc_control.F.type = THROTTLE;
  }
  else
  {
    _rc_control.x.type = _rc_control.y.type = (pwmRead(_params.values[PARAM_RC_ATT_CONTROL_TYPE_CHANNEL]) > 1500) ? ANGLE : RATE;
    _rc_control.z.type = RATE;
    _rc_control.F.type = (pwmRead(_params.values[PARAM_RC_F_CONTROL_TYPE_CHANNEL]) > 1500) ? ALTITUDE : THROTTLE;
  }

  // Interpret PWM Values from RC
  convertPWMtoRad();

  // Set flags for attitude channels
  if (pwmRead(_params.values[PARAM_RC_ATTITUDE_OVERRIDE_CHANNEL]) > 1500
      || now - time_of_last_stick_deviation < (uint32_t)(_params.values[PARAM_OVERRIDE_LAG_TIME])*1000)
  {
    // Pilot is in full control
    _rc_control.x.active = true;
    _rc_control.y.active = true;
    _rc_control.z.active = true;
  }
  else
  {
    // Check for stick deviation - if so, then the channel is active
    _rc_control.x.active = _rc_control.y.active  = _rc_control.z.active =
                             abs(pwmRead(_params.values[PARAM_RC_X_CHANNEL]) - _params.values[PARAM_RC_X_CENTER]) >
                             _params.values[PARAM_RC_OVERRIDE_DEVIATION]
                             || abs(pwmRead(_params.values[PARAM_RC_Y_CHANNEL]) - _params.values[PARAM_RC_Y_CENTER]) >
                             _params.values[PARAM_RC_OVERRIDE_DEVIATION]
                             || abs(pwmRead(_params.values[PARAM_RC_Z_CHANNEL]) - _params.values[PARAM_RC_Z_CENTER]) >
                             _params.values[PARAM_RC_OVERRIDE_DEVIATION];
    if (_rc_control.x.active)
    {
      // reset override lag
      time_of_last_stick_deviation = now;
    }
  }


  // Set flags for throttle channel
  if (pwmRead(_params.values[PARAM_RC_THROTTLE_OVERRIDE_CHANNEL]) > 1500)
  {
    // RC Pilot is in full control
    _rc_control.F.active = true;
  }
  else
  {
    // Onboard Control - min throttle Checking will be done in mux and in the controller.
    _rc_control.F.active = false;
  }

  _new_command = true;
  return true;
}

