#include "systems/controllers/lipm_traj_gen.h"

#include <math.h>

#include <string>

using std::cout;
using std::endl;
using std::string;
using std::vector;

using Eigen::MatrixXd;
using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::VectorXd;

using drake::systems::BasicVector;
using drake::systems::Context;
using drake::systems::DiscreteUpdateEvent;
using drake::systems::DiscreteValues;
using drake::systems::EventStatus;

using drake::multibody::JacobianWrtVariable;
using drake::multibody::MultibodyPlant;
using drake::trajectories::ExponentialPlusPiecewisePolynomial;
using drake::trajectories::PiecewisePolynomial;

namespace dairlib {
namespace systems {

LIPMTrajGenerator::LIPMTrajGenerator(
    const MultibodyPlant<double>& plant, Context<double>* context,
    double desired_com_height, const vector<int>& unordered_fsm_states,
    const vector<double>& unordered_state_durations,
    const vector<vector<std::pair<const Eigen::Vector3d,
                                  const drake::multibody::Frame<double>&>>>&
        contact_points_in_each_state)
    : plant_(plant),
      context_(context),
      desired_com_height_(desired_com_height),
      unordered_fsm_states_(unordered_fsm_states),
      unordered_state_durations_(unordered_state_durations),
      contact_points_in_each_state_(contact_points_in_each_state),
      world_(plant_.world_frame()) {
  this->set_name("lipm_traj");

  // Checking vector dimension
  DRAKE_DEMAND(unordered_fsm_states.size() == unordered_state_durations.size());
  DRAKE_DEMAND(unordered_fsm_states.size() ==
               contact_points_in_each_state.size());

  // Input/Output Setup
  state_port_ =
      this->DeclareVectorInputPort(OutputVector<double>(plant.num_positions(),
                                                        plant.num_velocities(),
                                                        plant.num_actuators()))
          .get_index();
  fsm_port_ = this->DeclareVectorInputPort(BasicVector<double>(1)).get_index();
  // Provide an instance to allocate the memory first (for the output)
  PiecewisePolynomial<double> pp_part(VectorXd(0));
  MatrixXd K = MatrixXd::Ones(0, 0);
  MatrixXd A = MatrixXd::Identity(0, 0);
  MatrixXd alpha = MatrixXd::Ones(0, 0);
  ExponentialPlusPiecewisePolynomial<double> exp(K, A, alpha, pp_part);
  drake::trajectories::Trajectory<double>& traj_inst = exp;
  this->DeclareAbstractOutputPort("lipm_traj", traj_inst,
                                  &LIPMTrajGenerator::CalcTraj);

  // Discrete state event
  DeclarePerStepDiscreteUpdateEvent(&LIPMTrajGenerator::DiscreteVariableUpdate);
  // The time of the last touch down
  prev_td_time_idx_ = this->DeclareDiscreteState(1);
  // The last state of FSM
  prev_fsm_state_idx_ = this->DeclareDiscreteState(-0.1 * VectorXd::Ones(1));
}

EventStatus LIPMTrajGenerator::DiscreteVariableUpdate(
    const Context<double>& context,
    DiscreteValues<double>* discrete_state) const {
  // Read in finite state machine
  const BasicVector<double>* fsm_output =
      (BasicVector<double>*)this->EvalVectorInput(context, fsm_port_);
  VectorXd fsm_state = fsm_output->get_value();

  auto prev_td_time =
      discrete_state->get_mutable_vector(prev_td_time_idx_).get_mutable_value();
  auto prev_fsm_state = discrete_state->get_mutable_vector(prev_fsm_state_idx_)
                            .get_mutable_value();

  if (fsm_state(0) != prev_fsm_state(0)) {  // if at touchdown
    prev_fsm_state(0) = fsm_state(0);

    // Get time
    const OutputVector<double>* robot_output =
        (OutputVector<double>*)this->EvalVectorInput(context, state_port_);
    double timestamp = robot_output->get_timestamp();
    double current_time = static_cast<double>(timestamp);
    prev_td_time(0) = current_time;
  }

  return EventStatus::Succeeded();
}

void LIPMTrajGenerator::CalcTraj(
    const Context<double>& context,
    drake::trajectories::Trajectory<double>* traj) const {
  // Read in current state
  const OutputVector<double>* robot_output =
      (OutputVector<double>*)this->EvalVectorInput(context, state_port_);
  VectorXd v = robot_output->GetVelocities();

  // Read in finite state machine
  const BasicVector<double>* fsm_output =
      (BasicVector<double>*)this->EvalVectorInput(context, fsm_port_);
  VectorXd fsm_state = fsm_output->get_value();

  // Find fsm_state in unordered_fsm_states_
  auto it = find(unordered_fsm_states_.begin(), unordered_fsm_states_.end(),
                 int(fsm_state(0)));
  int mode_index = std::distance(unordered_fsm_states_.begin(), it);
  if (it == unordered_fsm_states_.end()) {
    cout << "WARNING: fsm state number " << fsm_state(0)
         << " doesn't exist in LIPMTrajGenerator\n";
    mode_index = 0;
  }

  // Get discrete states
  const auto prev_td_time =
      context.get_discrete_state(prev_td_time_idx_).get_value();

  // Get time
  double timestamp = robot_output->get_timestamp();
  auto current_time = static_cast<double>(timestamp);

  double end_time_of_this_fsm_state =
      prev_td_time(0) + unordered_state_durations_[mode_index];
  // Ensure "current_time < end_time_of_this_fsm_state" to avoid error in
  // creating trajectory.
  if ((end_time_of_this_fsm_state <= current_time + 0.001)) {
    end_time_of_this_fsm_state = current_time + 0.002;
  }

  VectorXd q = robot_output->GetPositions();
  multibody::SetPositionsIfNew<double>(plant_, q, context_);

  // Get center of mass position and velocity
  Vector3d CoM = plant_.CalcCenterOfMassPosition(*context_);
  MatrixXd J(3, plant_.num_velocities());
  plant_.CalcJacobianCenterOfMassTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, world_, world_, &J);
  Vector3d dCoM = J * v;

  // Stance foot position (Forward Kinematics)
  // Take the average of all the points
  Vector3d stance_foot_pos = Vector3d::Zero();
  for (unsigned int j = 0; j < contact_points_in_each_state_[mode_index].size();
       j++) {
    Vector3d position;
    plant_.CalcPointsPositions(
        *context_, contact_points_in_each_state_[mode_index][j].second,
        contact_points_in_each_state_[mode_index][j].first, world_, &position);
    stance_foot_pos += position;
  }
  stance_foot_pos /= contact_points_in_each_state_[mode_index].size();

  // Get CoM_wrt_foot for LIPM
  const double CoM_wrt_foot_x = CoM(0) - stance_foot_pos(0);
  const double CoM_wrt_foot_y = CoM(1) - stance_foot_pos(1);
  const double CoM_wrt_foot_z = (CoM(2) - stance_foot_pos(2));
  const double dCoM_wrt_foot_x = dCoM(0);
  const double dCoM_wrt_foot_y = dCoM(1);
  // const double dCoM_wrt_foot_z = dCoM(2);
  DRAKE_DEMAND(CoM_wrt_foot_z > 0);

  // create a 3D one-segment polynomial for ExponentialPlusPiecewisePolynomial
  // Note that the start time in T_waypoint_com is also used by
  // ExponentialPlusPiecewisePolynomial.
  vector<double> T_waypoint_com = {current_time, end_time_of_this_fsm_state};

  vector<MatrixXd> Y(T_waypoint_com.size(), MatrixXd::Zero(3, 1));
  Y[0](0, 0) = stance_foot_pos(0);
  Y[1](0, 0) = stance_foot_pos(0);
  Y[0](1, 0) = stance_foot_pos(1);
  Y[1](1, 0) = stance_foot_pos(1);
  // We add stance_foot_pos(2) to desired COM height to account for state
  // drifting
  Y[0](2, 0) = desired_com_height_ + stance_foot_pos(2);
  Y[1](2, 0) = desired_com_height_ + stance_foot_pos(2);

  MatrixXd Y_dot_start = MatrixXd::Zero(3, 1);
  MatrixXd Y_dot_end = MatrixXd::Zero(3, 1);

  PiecewisePolynomial<double> pp_part =
      PiecewisePolynomial<double>::CubicWithContinuousSecondDerivatives(
          T_waypoint_com, Y, Y_dot_start, Y_dot_end);

  // Dynamics of LIPM
  // ddy = 9.81/CoM_wrt_foot_z*y, which has an analytical solution.
  // Let omega^2 = 9.81/CoM_wrt_foot_z.
  // Let y0 and dy0 be the intial position and velocity. Then the solution is
  //   y = k_1 * exp(w*t) + k_2 * exp(-w*t)
  // where k_1 = (y0 + dy0/w)/2
  //       k_2 = (y0 - dy0/w)/2.
  double omega = sqrt(9.81 / CoM_wrt_foot_z);
  double k1x = 0.5 * (CoM_wrt_foot_x + dCoM_wrt_foot_x / omega);
  double k2x = 0.5 * (CoM_wrt_foot_x - dCoM_wrt_foot_x / omega);
  double k1y = 0.5 * (CoM_wrt_foot_y + dCoM_wrt_foot_y / omega);
  double k2y = 0.5 * (CoM_wrt_foot_y - dCoM_wrt_foot_y / omega);

  // Sum of two exponential + one-segment 3D polynomial
  MatrixXd K = MatrixXd::Zero(3, 2);
  MatrixXd A = MatrixXd::Zero(2, 2);
  MatrixXd alpha = MatrixXd::Zero(2, 1);
  K << k1x, k2x, k1y, k2y, 0, 0;
  A << omega, 0, 0, -omega;
  alpha << 1, 1;

  // Assign traj
  auto exp_pp_traj = (ExponentialPlusPiecewisePolynomial<double>*)dynamic_cast<
      ExponentialPlusPiecewisePolynomial<double>*>(traj);
  *exp_pp_traj =
      ExponentialPlusPiecewisePolynomial<double>(K, A, alpha, pp_part);
}

}  // namespace systems
}  // namespace dairlib
