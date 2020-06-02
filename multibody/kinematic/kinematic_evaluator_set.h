#pragma once

#include "multibody/kinematic/kinematic_evaluator.h"

namespace dairlib {
namespace multibody {

/// Simple class that maintains a vector pointers to KinematicEvaluator
/// objects. Provides a basic API for counting and accumulating evaluations
/// and their Jacobians.
template <typename T>
class KinematicEvaluatorSet {
 public:
  explicit KinematicEvaluatorSet(
      const drake::multibody::MultibodyPlant<T>& plant);

  /// Evaluates phi(q), limited only to active rows
  drake::VectorX<T> EvalActive(
      const drake::systems::Context<T>& context) const;

  /// Evaluates the time-derivative, d/dt phi(q), limited only to active rows
  drake::VectorX<T> EvalActiveTimeDerivative(
      const drake::systems::Context<T>& context) const;

  /// Evaluates the constraint Jacobian w.r.t. velocity v (not qdot)
  ///  limited only to active rows
  drake::MatrixX<T> EvalActiveJacobian(
      const drake::systems::Context<T>& context) const;

  /// Evaluates Jdot * v, useful for computing second derivative,
  ///  which would be d^2 phi/dt^2 = J * vdot + Jdot * v
  ///  limited only to active rows
  drake::VectorX<T> EvalActiveJacobianDotTimesV(
      const drake::systems::Context<T>& context) const;

  /// Evaluates the time-derivative, d/dt phi(q)
  drake::VectorX<T> EvalFullTimeDerivative(
      const drake::systems::Context<T>& context) const;

  /// Evaluates, phi(q), including inactive rows
  drake::VectorX<T> EvalFull(
      const drake::systems::Context<T>& context) const;

  /// Evaluates the Jacobian w.r.t. velocity v (not qdot)
  drake::MatrixX<T> EvalFullJacobian(
      const drake::systems::Context<T>& context) const;

  /// Evaluates Jdot * v, useful for computing constraint second derivative,
  drake::VectorX<T> EvalFullJacobianDotTimesV(
      const drake::systems::Context<T>& context) const;

  /// Determines the list of evaluators contained in the union with another set
  /// Specifically, `index` is in the returned vector if
  /// other.evaluators_.at(index) is an element of other.evaluators, as judged
  /// by a comparison of the KinematicEvaluator<T>* pointers.
  ///
  /// Again, note that this is an index set into the other object, not self.
  std::vector<int> FindUnion(KinematicEvaluatorSet<T> other);

  /// Compute M(q) * d/dt v, given the state, control inputs and constraint
  /// forces. Forces are associated with the full kinematic elements.
  /// @param context
  /// @param lambda constraint forces, applied via
  ///   evaluators.EvalActiveJacobian().transpose() * lambda
  drake::VectorX<T> CalcMassMatrixTimesVDot(
      const drake::systems::Context<T>& context,
      const drake::VectorX<T>& lambda) const;

  /// Computes vdot given the state, control inputs and constraint
  /// forces. Similar to CalcMassMatrixTimesVDot, but uses inv(M)
  /// and includes qdot. Forces are associated with the full kinematic
  /// elements.
  /// @param context
  /// @param lambda constraint forces, applied via
  ///   evaluators.EvalActiveJacobian().transpose() * lambda
  drake::VectorX<T> CalcTimeDerivatives(
      const drake::systems::Context<T>& context,
      const drake::VectorX<T>& lambda) const;

  /// Computes vdot given the state and control inputs, satisfying kinematic
  /// constraints.
  /// Solves for the constraint forces using the full kinematic elements.
  /// Similar to CalcTimeDerivatives(context, evaluators, lambda), but solves
  /// for the forces to satisfy the constraint ddot phi = -kp*phi - kd*phidot
  ///   NOTE: the constraint __only__ includes the active contacts, but the
  ///   force lambda returns the full contact set
  /// @param context
  /// @param alpha Inverse time constant for constraint stabilization.
  ///   Results in kp = alpha^2, kd = 2*alpha. Default = 0
  drake::VectorX<T> CalcTimeDerivatives(
      const drake::systems::Context<T>& context, double alpha = 0) const;

  /// Computes vdot given the state and control inputs, satisfying kinematic
  /// constraints.
  /// See CalcTimeDerivatives(context, kp, kd) for full details. This version
  /// also returns the constraint force lambda via an input pointer.
  drake::VectorX<T> CalcTimeDerivatives(
      const drake::systems::Context<T>& context, drake::VectorX<T>* lambda, 
      double alpha = 0) const;

  /// Gets the starting index into phi_full of the specified evaluator
  int evaluator_full_start(int index) const;

  /// Gets the starting index into phi_active of the specified evaluator
  int evaluator_active_start(int index) const;

  KinematicEvaluator<T>* get_evaluator(int index) {
    return evaluators_.at(index);
  };

  /// Adds an evaluator to the end of the list, returning the associated index
  int add_evaluator(KinematicEvaluator<T>* e);

  /// Count the total number of active rows
  int count_active() const;

  /// Count the total number of rows
  int count_full() const;

  int num_evaluators() const { return evaluators_.size(); };

  const drake::multibody::MultibodyPlant<T>& plant() const { return plant_; };

 private:
  const drake::multibody::MultibodyPlant<T>& plant_;
  std::vector<KinematicEvaluator<T>*> evaluators_;
};

}  // namespace multibody
}  // namespace dairlib