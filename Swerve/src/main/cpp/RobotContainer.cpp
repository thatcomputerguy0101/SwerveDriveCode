// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "Constants.h"
#include "RobotContainer.h"

#include <frc/geometry/Pose2d.h>
#include <frc/geometry/Rotation2d.h>
#include <frc/kinematics/SwerveDriveKinematics.h>
#include <frc/trajectory/Trajectory.h>
#include <frc/trajectory/TrajectoryConfig.h>
#include <frc/trajectory/TrajectoryGenerator.h>
#include <frc2/command/button/JoystickButton.h>
#include <frc2/command/button/POVButton.h>
#include <frc2/command/Command.h>
#include <frc2/command/CommandScheduler.h>
#include <frc2/command/InstantCommand.h>
#include <frc2/command/RunCommand.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <units/acceleration.h>
#include <units/velocity.h>

RobotContainer::RobotContainer() noexcept {}

frc2::CommandPtr RobotContainer::DriveCommandFactory(RobotContainer *container) noexcept
{
  // Set up default drive command; non-owning pointer is passed by value.
  auto driveRequirements = {dynamic_cast<frc2::Subsystem *>(&container->m_driveSubsystem)};

  // Drive, as commanded by operator joystick controls.
  return frc2::CommandPtr{std::make_unique<frc2::RunCommand>(
      [container]() -> void
      {
        if (container->m_lock)
        {
          (void)container->m_driveSubsystem.SetLockWheelsX();

          return;
        }

        const auto controls = container->GetDriveTeleopControls();

        container->m_driveSubsystem.Drive(
            std::get<0>(controls) * physical::kMaxDriveSpeed,
            std::get<1>(controls) * physical::kMaxDriveSpeed,
            std::get<2>(controls) * physical::kMaxTurnRate,
            std::get<3>(controls));
      },
      driveRequirements)};
}

frc2::CommandPtr RobotContainer::PointCommandFactory(RobotContainer *container) noexcept
{
  // Set up default drive command; non-owning pointer is passed by value.
  auto driveRequirements = {dynamic_cast<frc2::Subsystem *>(&container->m_driveSubsystem)};

  // Point swerve modules, but do not actually drive.
  return frc2::CommandPtr{std::make_unique<frc2::RunCommand>(
      [container]() -> void
      {
        const auto controls = container->GetDriveTeleopControls();

        units::angle::radian_t angle{std::atan2(std::get<0>(controls), std::get<1>(controls))};

        // Ingnore return (bool); no need to check that commanded angle has
        // been reached.
        (void)container->m_driveSubsystem.SetTurningPosition(angle);
      },
      driveRequirements)};
}

void RobotContainer::AutonomousInit() noexcept
{
  m_driveSubsystem.ClearFaults();

  m_driveSubsystem.ResetEncoders();

  m_driveSubsystem.SetDefaultCommand(frc2::RunCommand([&]() -> void {},
                                                      {&m_driveSubsystem}));
  m_infrastructureSubsystem.SetDefaultCommand(frc2::RunCommand([&]() -> void {},
                                                               {&m_infrastructureSubsystem}));
}

void RobotContainer::TeleopInit() noexcept
{
  m_driveSubsystem.ClearFaults();

  m_driveSubsystem.ResetEncoders();

  m_driveSubsystem.SetDefaultCommand(DriveCommandFactory(this));
}

std::optional<frc2::CommandPtr> RobotContainer::GetAutonomousCommand() noexcept
{
#if 0
  if (m_buttonBoard.GetRawButton(9))
#endif
  frc::TrajectoryConfig trajectoryConfig{4.0_mps, 2.0_mps_sq};
  frc::SwerveDriveKinematics<4> kinematics{m_driveSubsystem.kDriveKinematics};

  trajectoryConfig.SetKinematics(kinematics);

  frc::Trajectory trajectory = frc::TrajectoryGenerator::GenerateTrajectory(
      {frc::Pose2d{},
       frc::Pose2d{1.0_m, 0.0_m, frc::Rotation2d{}}},
      trajectoryConfig);

  return TrajectoryAuto::TrajectoryAutoCommandFactory(&m_driveSubsystem, "Test Trajectory", trajectory);
}

std::tuple<double, double, double, bool> RobotContainer::GetDriveTeleopControls() noexcept
{
  // The robot's frame of reference is the standard unit circle, from
  // trigonometry.  However, the front of the robot is facing along the positve
  // X axis.  This means the poitive Y axis extends outward from the left (or
  // port) side of the robot.  Poitive rotation is counter-clockwise.  On the
  // other hand, as the controller is held, the Y axis is aligned with forward.
  // And, specifically, it is the negative Y axis which extends forward.  So,
  // the robot's X is the controllers inverted Y.  On the controller, the X
  // axis lines up with the robot's Y axis.  And, the controller's positive X
  // extends to the right.  So, the robot's Y is the controller's inverted X.
  // Finally, the other controller joystick is used for commanding rotation and
  // things work out so that this is also an inverted X axis.
  double x = -m_xbox.GetLeftY();
  double y = -m_xbox.GetLeftX();
  double z = -m_xbox.GetRightX();

  // PlayStation controllers seem to do this strange thing with the rotation:
  // double z = -m_xbox.GetLeftTriggerAxis();
  // Note: there is now a PS4Controller class.

  // Add some deadzone, so the robot doesn't drive when the joysticks are
  // released and return to "zero".  These implement a continuous deadband, one
  // in which the full range of outputs may be generated, once joysticks move
  // outside the deadband.

  // Also, cube the result, to provide more opertor control.  Just cubing the
  // raw value does a pretty good job with the deadband, but doing both is easy
  // and guarantees no movement in the deadband.  Cubing makes it easier to
  // command smaller/slower movements, while still being able to command full
  // power.  The 'mixer` parameter is used to shape the `raw` input, some mix
  // between out = in^3.0 and out = in.
  auto shape = [](double raw, double mixer = 0.75) -> double
  {
    // Input deadband around 0.0 (+/- range).
    constexpr double range = 0.05;

    constexpr double slope = 1.0 / (1.0 - range);

    if (raw >= -range && raw <= +range)
    {
      raw = 0.0;
    }
    else if (raw < -range)
    {
      raw += range;
      raw *= slope;
    }
    else if (raw > +range)
    {
      raw -= range;
      raw *= slope;
    }

    return mixer * std::pow(raw, 3.0) + (1.0 - mixer) * raw;
  };

  x = shape(x);
  y = shape(y);
  z = shape(z, 0.0);

  if (m_slow || m_buttonBoard.GetRawButton(9))
  {
    x *= 0.50;
    y *= 0.50;
    z *= 0.40;
  }
  else
  { // XXX Still needed?
    x *= 2.0;
    y *= 2.0;
    z *= 1.6;
  }

  return std::make_tuple(x, y, z, m_fieldOriented);
}

void RobotContainer::TestInit() noexcept
{
  frc2::CommandScheduler::GetInstance().CancelAll();

  m_driveSubsystem.ClearFaults();

  m_driveSubsystem.ResetEncoders();

  m_driveSubsystem.TestInit();

  frc::SendableChooser<std::function<frc2::CommandPtr()>> *chooser{m_driveSubsystem.TestModeChooser()};

  chooser->SetDefaultOption("Zero", std::bind(ZeroCommand::ZeroCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Turning Max", std::bind(MaxVAndATurningCommand::MaxVAndATurningCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Drive Max", std::bind(MaxVAndADriveCommand::MaxVAndADriveCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Xs and Os", std::bind(XsAndOsCommand::XsAndOsCommandFactory, &m_driveSubsystem));
  chooser->AddOption("RotateModules", std::bind(RotateModulesCommand::RotateModulesCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Point", std::bind(PointCommandFactory, this));
  chooser->AddOption("Square", std::bind(SquareCommand::SquareCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Spirograph", std::bind(SpirographCommand::SpirographCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Orbit", std::bind(OrbitCommand::OrbitCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Pirouette", std::bind(PirouetteCommand::PirouetteCommandFactory, &m_driveSubsystem));
  chooser->AddOption("Drive", std::bind(DriveCommandFactory, this));
  chooser->AddOption("Spin", std::bind(SpinCommand::SpinCommandFactory, &m_driveSubsystem));

  frc2::CommandScheduler::GetInstance().Enable();
}

void RobotContainer::TestExit() noexcept
{
  frc2::CommandScheduler::GetInstance().CancelAll();

  m_driveSubsystem.ClearFaults();
  ;

  m_driveSubsystem.ResetEncoders();

  m_driveSubsystem.TestExit();

  m_driveSubsystem.BurnConfig();
}

void RobotContainer::TestPeriodic() noexcept
{
  m_driveSubsystem.TestPeriodic();
}

void RobotContainer::DisabledInit() noexcept
{
  frc2::CommandScheduler::GetInstance().CancelAll();

  m_driveSubsystem.ClearFaults();

  m_driveSubsystem.ResetEncoders();

  // Useful things may be done disabled... (construct, config, dashboard, etc.)
  frc2::CommandScheduler::GetInstance().Enable();

  m_driveSubsystem.DisabledInit();
}

void RobotContainer::DisabledExit() noexcept
{
  m_driveSubsystem.ClearFaults();

  m_driveSubsystem.ResetEncoders();

  m_driveSubsystem.DisabledExit();
}
