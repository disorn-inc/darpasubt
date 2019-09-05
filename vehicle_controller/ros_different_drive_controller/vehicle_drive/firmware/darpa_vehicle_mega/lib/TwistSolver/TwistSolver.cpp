#include <TwistSolver.h>

/**
 * @brief Solves a wheel's steering angle and drive speed given the linear and angular velocities
 * from a Twist message.
 * 
 * @param linear LinearVels_t type representing the body's linear velocity.
 * @param angular AngularVels_t type representing the body's angular velocity.
 * @param platform PlatformDimensions_t type representing the body's dimension parameters.
 * @param wheel WheelParams_t type representing the wheel's paramters.
 * @param drive DriveParams_t type representing the computed drive parameters.
 * return TwistError_t Error enumeration indicating calculation results.
 */
TwistError_t solveTwist(LinearVels_t linear, AngularVels_t angular, PlatformDimensions_t platform, WheelParams_t wheel, DriveParams_t* drive)
{
  if (linear.x == 0 && linear.y == 0 && angular.z == 0)
  {
    drive->steerAngle = 0;
    drive->posAngle = 90 + wheel.servCalib;
    drive->speed = 0;
    return TWIST_OK;
  }
  // Forward or backward movement only.
  else if (linear.x != 0 && linear.y == 0 && angular.z == 0)
  {
    drive->steerAngle = 0;
    drive->posAngle = 90 + wheel.servCalib;
    drive->speed = linear.x / wheel.radius;
    
    // Convert drive speed to degrees/second for the encoder
    drive->speed = ((drive->speed / (2.0 * M_PI)) * 360.0) * SHAFT_TO_ENCODER_FACTOR;

    drive->steerAngle = (drive->steerAngle / (2.0 * M_PI)) * 360.0; // Convert to degrees.
    drive->posAngle = 90 - drive->steerAngle + wheel.servCalib;

    return TWIST_OK;
  }
  // Turning on the spot.
  else if (linear.x == 0 && linear.y == 0 && angular.z != 0)
  {
    return solvSpotTurn(angular, platform, wheel, drive);
  }
  // Steering movement.
  else if (linear.x != 0 && linear.y == 0 && angular.z != 0)
  {
    // TODO: Combine inner outer arc function into one, since they are similar now. Remove pivotDist from struct as well.
    return solvArcTurn(linear, angular, platform, wheel, drive);
  }
  else if (linear.x == 0 && linear.y != 0 && angular.z == 0)
  {
    return solvStrafe(linear, platform, wheel, drive);
  }
  else if (linear.x != 0 && linear.y != 0 && angular.z == 0)
  {
    return solvStrafe(linear, platform, wheel, drive);
  }

  return TWIST_UNKNOWN;
}

/**
 * @brief Computes the turn radius of a body given it's linear and angular velocities.
 * 
 * @param linear LinearVels_t type representing the body's linear velocity.
 * @param angular AngularVels_t type representing the body's angular velocity.
 * @return double The corresponding turn radius of the body.
 */
 static double solvBodyRadius(LinearVels_t linear, AngularVels_t angular)
{
  double turnRadius = linear.x / angular.z;
  turnRadius = fabs(turnRadius);

  return turnRadius;
}

/**
 * @brief Computes the steering angle and drive speed of a single wheel during spot turning, given
 * the body's angular velocities.
 * 
 * @param angular AngularVels_t type representing the body's angular velocity.
 * @param platform PlatformDimensions_t type representing the body's dimension parameters.
 * @param wheel WheelParams_t type representing the wheel's paramters.
 * @param drive DriveParams_t type representing the computed drive parameters.
 * @return TwistError_t Error enumeration indicating calculation results.
 */
static TwistError_t solvSpotTurn(AngularVels_t angular, PlatformDimensions_t platform, WheelParams_t wheel, DriveParams_t* drive)
{
  if (angular.z == 0)
  {
    drive->steerAngle = 0;
    drive->speed = 0;
    return TWIST_ZERO;
  }

  // Compute the rotation speed required.
  double driveSpeed = platform.diagonalHalf * angular.z / wheel.radius;
  /*
   * Depending if angular.z is positive or negative, computed driveSpeed will be the same sign.
   * 
   * If angular.z is positive, body is spot turning to the left.
   * The left wheels must have driveSpeed of opposite sign.
   * 
   * If angular.z is negative, body is spot turning to the right.
   * The left wheel must still have driveSpeed of opposite sign.
   */
  drive->speed = driveSpeed;
  if (wheel.wheelPos == WHEEL_POS_TOP_LEFT || wheel.wheelPos == WHEEL_POS_BOTTOM_LEFT) drive->speed *= -1;
  
  // Compute the steer angle required.
  double steerAngle = asin(platform.breadthHalf / platform.diagonalHalf);
  if (wheel.wheelPos == WHEEL_POS_TOP_LEFT || wheel.wheelPos == WHEEL_POS_BOTTOM_RIGHT) steerAngle *= -1;
  drive->steerAngle = steerAngle;

  // Convert drive speed to degrees/second for the encoder
  drive->speed = ((drive->speed / (2.0 * M_PI)) * 360.0) * SHAFT_TO_ENCODER_FACTOR;

  drive->steerAngle = (drive->steerAngle / (2.0 * M_PI)) * 360.0; // Convert to degrees.
  drive->posAngle = 90 - drive->steerAngle + wheel.servCalib;

  return TWIST_OK;
}

/**
 * @brief Computes the steering angle and drive speed of wheels from inner arc, given the body's
 * linear and angular velocities.
 * 
 * @param linear LinearVels_t type representing the body's linear velocity.
 * @param angular AngularVels_t type representing the body's angular velocity.
 * @param platform PlatformDimensions_t type representing the body's dimension parameters.
 * @param wheel WheelParams_t type representing the wheel's paramters.
 * @param drive DriveParams_t type representing the computed drive parameters. 
 * @return TwistError_t Error enumeration indicating calculation results.
 */
static TwistError_t solvInArcTurn(LinearVels_t linear, AngularVels_t angular, PlatformDimensions_t platform, WheelParams_t wheel, DriveParams_t* drive)
{
  if (angular.z == 0)
  {
    drive->steerAngle = 0;
    drive->speed = 0;
    return TWIST_ZERO;
  }

  // Compute the steering angle required.
  double bodyRadius = solvBodyRadius(linear, angular);
  if (bodyRadius < PLATFORM_RADIUS_LIM)
  {
    // Body radius is too low, there is no valid steer angle.
    drive->steerAngle = 0;
    drive->speed = 0;
    return TWIST_EX_LIM;
  }
  double arcRadius = sqrt(pow(bodyRadius - platform.lengthHalf, 2) + (platform.breadthHalf * platform.breadthHalf));

  double steerAngle = asin(platform.breadthHalf / arcRadius);
  // Arc turn counter clockwise.
  if (angular.z > 0)
  {
    if (wheel.wheelPos == WHEEL_POS_BOTTOM_LEFT && linear.x > 0) steerAngle *= -1;
    if (wheel.wheelPos == WHEEL_POS_TOP_RIGHT && linear.x < 0) steerAngle *= -1;
  }
  // Arc turn clockwise.
  else if (angular.z < 0)
  {
    if (wheel.wheelPos == WHEEL_POS_TOP_RIGHT && linear.x > 0) steerAngle *= -1;
    if (wheel.wheelPos == WHEEL_POS_BOTTOM_LEFT && linear.x < 0) steerAngle *= -1;
  }
  drive->steerAngle = steerAngle;

  // Compute the rotation speed required.
  // Keep all values positive for easier sign manupulation.
  angular.z = fabs(angular.z);
  steerAngle = fabs(steerAngle);

  double driveSpeed = arcRadius * angular.z / wheel.radius;
  if (linear.x < 0) driveSpeed *= -1; // Movement is backwards.
  drive->speed = driveSpeed;

  // Convert drive speed to degrees/second for the encoder
  drive->speed = ((drive->speed / (2.0 * M_PI)) * 360.0) * SHAFT_TO_ENCODER_FACTOR;

  drive->steerAngle = (drive->steerAngle / (2.0 * M_PI)) * 360.0; // Convert to degrees.
  drive->posAngle = 90 - drive->steerAngle + wheel.servCalib;

  return TWIST_OK;
}

/**
 * @brief Computes the steering angle and drive speed of wheels from outer arc, given the body's
 * linear and angular velocities.
 * 
 * @param linear LinearVels_t type representing the body's linear velocity.
 * @param angular AngularVels_t type representing the body's angular velocity.
 * @param platform PlatformDimensions_t type representing the body's dimension parameters.
 * @param wheel WheelParams_t type representing the wheel's paramters.
 * @param drive DriveParams_t type representing the computed drive parameters. 
 * @return TwistError_t Error enumeration indicating calculation results.
 */
static TwistError_t solvOutArcTurn(LinearVels_t linear, AngularVels_t angular, PlatformDimensions_t platform, WheelParams_t wheel, DriveParams_t* drive)
{
  if (angular.z == 0)
  {
    drive->steerAngle = 0;
    drive->speed = 0;
    return TWIST_ZERO;
  }

  // Compute the steer angle.
  double bodyRadius = solvBodyRadius(linear, angular);
  if (bodyRadius < PLATFORM_RADIUS_LIM)
  {
    // Body radius is too low, there is no valid steer angle.
    drive->steerAngle = 0;
    drive->speed = 0;
    return TWIST_EX_LIM;
  }
  double arcRadius = sqrt((platform.breadthHalf * platform.breadthHalf) + pow(platform.lengthHalf + bodyRadius, 2));
  
  double steerAngle = asin(platform.breadthHalf / arcRadius);
  // Arc turn counter clockwise.
  if (angular.z > 0)
  {
    if (wheel.wheelPos == WHEEL_POS_BOTTOM_RIGHT && linear.x > 0) steerAngle *= -1;
    if (wheel.wheelPos == WHEEL_POS_TOP_LEFT && linear.x < 0) steerAngle *= -1;
  }
  // Arc turn clockwise.
  else
  {
    if (wheel.wheelPos == WHEEL_POS_TOP_LEFT && linear.x > 0) steerAngle *= -1;
    if (wheel.wheelPos == WHEEL_POS_BOTTOM_RIGHT && linear.x < 0) steerAngle *= -1;
  }
  drive->steerAngle = steerAngle;

  // Compute the rotation speed required.
  // Keep all values positive for easier sign manupulation.
  angular.z = fabs(angular.z);
  steerAngle = fabs(steerAngle);

  double driveSpeed = arcRadius * angular.z / wheel.radius;
  if (linear.x < 0) driveSpeed *= -1; // Movement is backwards.
  drive->speed = driveSpeed;

  // Convert drive speed to degrees/second for the encoder
  drive->speed = ((drive->speed / (2.0 * M_PI)) * 360.0) * SHAFT_TO_ENCODER_FACTOR;

  drive->steerAngle = (drive->steerAngle / (2.0 * M_PI)) * 360.0; // Convert to degrees.
  drive->posAngle = 90 - drive->steerAngle + wheel.servCalib;

  return TWIST_OK;
}

/**
 * @brief Determines how to compute the steering angle and drive speed of a single wheel during arc
 * turning, given the body's linear and angular velocities.
 * 
 * @param linear LinearVels_t type representing the body's linear velocity.
 * @param angular AngularVels_t type representing the body's angular velocity.
 * @param platform PlatformDimensions_t type representing the body's dimension parameters.
 * @param wheel WheelParams_t type representing the wheel's paramters.
 * @param drive DriveParams_t type representing the computed drive parameters. 
 * @return TwistError_t Error enumeration indicating calculation results.
 */
static TwistError_t solvArcTurn(LinearVels_t linear, AngularVels_t angular, PlatformDimensions_t platform, WheelParams_t wheel, DriveParams_t* drive)
{
  WheelPosition_t wheelPos = wheel.wheelPos;
  if (angular.z > 0 && (wheelPos == WHEEL_POS_TOP_LEFT || wheelPos == WHEEL_POS_BOTTOM_LEFT))
  {
    if (linear.x > 0)
    {
      return solvInArcTurn(linear, angular, platform, wheel, drive);
    }
    else
    {
      return solvOutArcTurn(linear, angular, platform, wheel, drive);
    }
  }
  else if (angular.z > 0 && (wheelPos == WHEEL_POS_TOP_RIGHT || wheelPos == WHEEL_POS_BOTTOM_RIGHT))
  {
    if (linear.x > 0)
    {
      return solvOutArcTurn(linear, angular, platform, wheel, drive);
    }
    else
    {
      return solvInArcTurn(linear, angular, platform, wheel, drive);
    }
    
  }
  else if (angular.z < 0 && (wheelPos == WHEEL_POS_TOP_RIGHT || wheelPos == WHEEL_POS_BOTTOM_RIGHT))
  {
    if (linear.x > 0)
    {
      return solvInArcTurn(linear, angular, platform, wheel, drive);
    }
    else
    {
      return solvOutArcTurn(linear, angular, platform, wheel, drive);
    }
    
  }
  else
  {
    if (linear.x > 0)
    {
      return solvOutArcTurn(linear, angular, platform, wheel, drive);
    }
    else
    {
      return solvInArcTurn(linear, angular, platform, wheel, drive);
    }
  }
}

/**
 * @brief Determines the drive speed and steer angle for strafing movement.
 * 
 * @param linear LinearVels_t type representing the body's linear velocity.
 * @param platform PlatformDimensions_t type representing the body's dimension parameters.
 * @param wheel WheelParams_t type representing the wheel's paramters.
 * @param drive DriveParams_t type representing the computed drive parameters. 
 * @return TwistError_t Error enumeration indicating calculation results. 
 */
static TwistError_t solvStrafe(LinearVels_t linear, PlatformDimensions_t platform, WheelParams_t wheel, DriveParams_t* drive)
{
  // Get the steer angle first.
   // We input a negative linear.y to follow mathematical convention where positive horizontal axis denotes right.
  double dirAngle = atan2(linear.x, -linear.y);
  double steerAngle = 0;
  if (dirAngle >= 0 && dirAngle <= M_PI_2)
  {
    steerAngle = -dirAngle;
  }
  else if (dirAngle > M_PI_2 && dirAngle <= M_PI)
  {
    steerAngle = dirAngle - M_PI_2;
  }
  else if (dirAngle < 0 && dirAngle >= -M_PI_2)
  {
    steerAngle = -dirAngle;
  }
  else
  {
    steerAngle = dirAngle + M_PI_2;
  }
  drive->steerAngle = (steerAngle / (2.0 * M_PI)) * 360.0; // Convert to degrees.
  drive->posAngle = 90 - drive->steerAngle + wheel.servCalib;

  // Get the drive speed.
  drive->speed = sqrt((linear.x * linear.x) + (linear.y * linear.y));
  if (dirAngle < 0) drive->speed *= -1;
  // Convert drive speed to degrees/second for the encoder
  drive->speed = ((drive->speed / (2.0 * M_PI)) * 360.0) * SHAFT_TO_ENCODER_FACTOR;

  return TWIST_OK;
}
